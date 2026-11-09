#include "F4TCampGrants.h"

#include "F4TDropLedger.h"
#include "F4TDurableState.h"
#include "F4TInventoryContract.h"
#include "F4TInventorySnapshot.h"
#include "F4TInventoryWire.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace F4T::CampGrants
{
	namespace
	{
		using Json = nlohmann::json;

		std::string OperationName(std::uint64_t a_operationId, std::size_t a_chunk)
		{
			return "camp-" + std::to_string(a_operationId) + "-" + std::to_string(a_chunk);
		}

		bool HasExactKeys(const Json& a_object, std::initializer_list<const char*> a_keys)
		{
			return a_object.is_object() && a_object.size() == a_keys.size() &&
				std::all_of(a_keys.begin(), a_keys.end(), [&a_object](const char* a_key) { return a_object.contains(a_key); });
		}

		Json LinesJson(const std::vector<Trade::Line>& a_lines)
		{
			Json lines = Json::array();
			for (const auto& line : a_lines) lines.push_back(Json{ { "itemKey", line.itemKey }, { "count", line.count } });
			return lines;
		}

		std::optional<std::vector<Trade::Line>> ReadLines(const Json& a_lines)
		{
			if (!a_lines.is_array() || a_lines.empty() || a_lines.size() > F4T::InventoryWire::kMaximumTradeLines) return std::nullopt;
			std::vector<Trade::Line> lines;
			std::set<std::string> keys;
			for (const auto& entry : a_lines) {
				if (!HasExactKeys(entry, { "itemKey", "count" }) || !entry.at("itemKey").is_string() || !entry.at("count").is_number_unsigned()) return std::nullopt;
				const auto count = entry.at("count").get<std::uint64_t>();
				const auto& key = entry.at("itemKey").get_ref<const std::string&>();
				if (count == 0 || count > F4T::Drops::kMaximumDropCount || key.size() > F4T::InventoryContract::kMaximumItemKeyChars ||
					!F4T::InventoryContract::ParseItemKey(key) || !keys.insert(key).second) return std::nullopt;
				lines.push_back(Trade::Line{ key, static_cast<std::uint32_t>(count) });
			}
			return lines;
		}

		Json ChunksJson(const std::vector<Chunk>& a_chunks)
		{
			Json chunks = Json::array();
			for (const auto& chunk : a_chunks) chunks.push_back(Json{ { "revision", chunk.revision }, { "lines", LinesJson(chunk.lines) } });
			return chunks;
		}

		std::optional<std::vector<Chunk>> ReadChunks(const Json& a_chunks)
		{
			if (!a_chunks.is_array() || a_chunks.size() > kMaximumChunksPerGrant) return std::nullopt;
			std::vector<Chunk> chunks;
			for (const auto& entry : a_chunks) {
				if (!HasExactKeys(entry, { "revision", "lines" }) || !entry.at("revision").is_number_unsigned() ||
					entry.at("revision").get<std::uint64_t>() > (std::numeric_limits<std::uint32_t>::max)()) return std::nullopt;
				auto lines = ReadLines(entry.at("lines"));
				if (!lines) return std::nullopt;
				chunks.push_back(Chunk{ entry.at("revision").get<std::uint32_t>(), std::move(*lines) });
			}
			return chunks;
		}

		const Json* FindRecord(const Json& a_profile, std::uint64_t a_operationId)
		{
			const auto records = a_profile.find("campMoves");
			if (records == a_profile.end() || !records->is_array()) return nullptr;
			for (const auto& record : *records) {
				if (record.is_object() && record.contains("operationId") && record.at("operationId") == a_operationId) return &record;
			}
			return nullptr;
		}

		bool HistoryHas(const Json& a_history, const std::string& a_operationId)
		{
			return std::any_of(a_history.begin(), a_history.end(), [&](const Json& a_operation) {
				return a_operation.at("request").at("operationId") == a_operationId;
			});
		}

		bool Credit(Json& a_items, const Trade::Line& a_line)
		{
			const auto parts = F4T::InventoryContract::ParseItemKey(a_line.itemKey);
			if (!parts) return false;
			const auto found = a_items.find(a_line.itemKey);
			if (found == a_items.end()) {
				char form[9]{};
				std::snprintf(form, sizeof(form), "%08X", parts->localId);
				a_items[a_line.itemKey] = Json{ { "plugin", parts->plugin }, { "localFormId", form }, { "count", a_line.count } };
				return true;
			}
			const auto owned = found->at("count").get<std::uint32_t>();
			if (owned > F4T::InventorySnapshot::kMaximumItemCount - a_line.count) return false;
			(*found)["count"] = owned + a_line.count;
			return true;
		}

		bool IsAckedRecord(const Json& a_record)
		{
			return !a_record.is_object() || a_record.value("acked", false);
		}

		bool MakeRoom(Json& a_records)
		{
			const auto pending = static_cast<std::size_t>(std::count_if(a_records.begin(), a_records.end(), [](const Json& a_record) {
				return !IsAckedRecord(a_record);
			}));
			if (pending >= kMaximumRecords) return false;
			while (static_cast<std::size_t>(std::count_if(a_records.begin(), a_records.end(), IsAckedRecord)) >= kMaximumAckedRecords) {
				a_records.erase(std::find_if(a_records.begin(), a_records.end(), IsAckedRecord));
			}
			return true;
		}

		std::string StateName(ReceiptState a_state)
		{
			if (a_state == ReceiptState::Pending) return "pending";
			if (a_state == ReceiptState::Applied) return "applied";
			return "acked";
		}

		std::optional<Receipt> ReadReceipt(const Json& a_record)
		{
			if (!a_record.is_object() || !a_record.contains("operationId") || !a_record.at("operationId").is_number_unsigned() ||
				!a_record.contains("state") || !a_record.at("state").is_string()) return std::nullopt;
			Receipt receipt{ a_record.at("operationId").get<std::uint64_t>(), ReceiptState::Pending, {} };
			const auto& state = a_record.at("state").get_ref<const std::string&>();
			if (state == "acked") {
				receipt.state = ReceiptState::Acked;
				return receipt;
			}
			if (state == "applied") {
				receipt.state = ReceiptState::Applied;
			} else if (state != "pending") {
				return std::nullopt;
			}
			if (!a_record.contains("baseline")) return std::nullopt;
			try {
				receipt.baseline = a_record.at("baseline").get<Counts>();
			} catch (const Json::exception&) {
				return std::nullopt;
			}
			return receipt;
		}

		Counts ExpectedCounts(const Grant& a_grant)
		{
			Counts expected;
			for (const auto& chunk : a_grant.chunks) {
				for (const auto& line : chunk.lines) expected[line.itemKey] += line.count;
			}
			return expected;
		}

		std::vector<Receipt>::iterator FindReceipt(std::vector<Receipt>& a_receipts, std::uint64_t a_operationId)
		{
			return std::find_if(a_receipts.begin(), a_receipts.end(), [a_operationId](const Receipt& a_receipt) { return a_receipt.operationId == a_operationId; });
		}

		void TrimAcked(std::vector<Receipt>& a_receipts)
		{
			const auto isAcked = [](const Receipt& a_receipt) { return a_receipt.state == ReceiptState::Acked; };
			while (static_cast<std::size_t>(std::count_if(a_receipts.begin(), a_receipts.end(), isAcked)) > kMaximumAckedRecords) {
				a_receipts.erase(std::find_if(a_receipts.begin(), a_receipts.end(), isAcked));
			}
		}

		bool AckedReceipt(const Receipt& a_receipt)
		{
			return a_receipt.state == ReceiptState::Acked;
		}

		std::size_t CountAcked(const Worlds& a_worlds)
		{
			std::size_t count = 0;
			for (const auto& entry : a_worlds) {
				count += static_cast<std::size_t>(std::count_if(entry.second.begin(), entry.second.end(), AckedReceipt));
			}
			return count;
		}

		bool EraseFirst(Worlds& a_worlds, const std::string& a_currentWorld, bool a_ackedOnly)
		{
			const auto eraseFrom = [&a_worlds, a_ackedOnly](Worlds::iterator a_world) {
				auto& receipts = a_world->second;
				const auto found = std::find_if(receipts.begin(), receipts.end(), [a_ackedOnly](const Receipt& a_receipt) {
					return !a_ackedOnly || AckedReceipt(a_receipt);
				});
				if (found == receipts.end()) {
					return false;
				}
				receipts.erase(found);
				if (receipts.empty()) {
					a_worlds.erase(a_world);
				}
				return true;
			};
			for (auto world = a_worlds.begin(); world != a_worlds.end(); ++world) {
				if (world->first != a_currentWorld && eraseFrom(world)) {
					return true;
				}
			}
			const auto current = a_worlds.find(a_currentWorld);
			return current != a_worlds.end() && eraseFrom(current);
		}

		bool CountsRose(const Counts& a_before, const Counts& a_after, const std::vector<Trade::Line>& a_added)
		{
			Counts added;
			for (const auto& line : a_added) added[line.itemKey] += line.count;
			return std::all_of(added.begin(), added.end(), [&](const auto& a_entry) {
				const auto before = a_before.find(a_entry.first);
				const auto after = a_after.find(a_entry.first);
				return before != a_before.end() && after != a_after.end() && after->second - before->second >= a_entry.second;
			});
		}

		std::vector<Trade::Line> MissingLines(const Grant& a_grant, const Counts& a_current, const Receipt& a_receipt)
		{
			Counts missing = ExpectedCounts(a_grant);
			for (auto& entry : missing) {
				const auto baseline = a_receipt.baseline.find(entry.first);
				const auto grown = baseline == a_receipt.baseline.end() ? std::int64_t{ 0 } : a_current.at(entry.first) - baseline->second;
				const auto present = std::clamp<std::int64_t>(grown, 0, entry.second);
				entry.second -= present;
			}
			std::vector<Trade::Line> lines;
			for (const auto& chunk : a_grant.chunks) {
				for (const auto& line : chunk.lines) {
					auto& left = missing[line.itemKey];
					const auto take = std::min<std::int64_t>(left, line.count);
					if (take == 0) continue;
					left -= take;
					lines.push_back(Trade::Line{ line.itemKey, static_cast<std::uint32_t>(take) });
				}
			}
			return lines;
		}
	}

	std::vector<Trade::Line> FlattenLines(const std::vector<Chunk>& a_chunks)
	{
		std::vector<Trade::Line> lines;
		for (const auto& chunk : a_chunks) lines.insert(lines.end(), chunk.lines.begin(), chunk.lines.end());
		return lines;
	}

	Commit CommitMove(Json& a_root, std::string_view a_profileId, std::uint64_t a_operationId, const std::vector<Trade::Line>& a_lines)
	{
		Commit result;
		const auto refuse = [&result](const char* a_reason) {
			result.outcome = Outcome::Rejected;
			result.reason = a_reason;
			result.chunks.clear();
			return result;
		};
		if (a_operationId == 0) return refuse("invalidRequest");
		if (a_root.at("inventoryAuthorityBlocked").get<bool>()) return refuse("inventoryAuthorityUnavailable");
		auto& profiles = a_root.at("profiles");
		const auto found = profiles.find(std::string{ a_profileId });
		if (found == profiles.end()) return refuse("inventoryAuthorityUnavailable");
		auto& profile = *found;
		if (const auto* record = FindRecord(profile, a_operationId)) {
			auto chunks = ReadChunks(record->value("chunks", Json::array()));
			if (!chunks) return refuse("operationMismatch");
			result.outcome = Outcome::Replayed;
			result.chunks = std::move(*chunks);
			result.pending = !record->value("acked", false);
			return result;
		}
		Json records = profile.contains("campMoves") && profile.at("campMoves").is_array() ? profile.at("campMoves") : Json::array();
		if (!MakeRoom(records)) return refuse("capacityExceeded");
		if (a_lines.empty()) {
			records.push_back(Json{ { "operationId", a_operationId }, { "chunks", Json::array() }, { "acked", true } });
			profile["campMoves"] = std::move(records);
			result.outcome = Outcome::Committed;
			return result;
		}
		if (a_lines.size() > F4T::InventoryWire::kMaximumTradeLines * kMaximumChunksPerGrant) return refuse("capacityExceeded");
		if (!profile.contains("inventory") || !profile.contains("inventoryOperations")) return refuse("inventoryAuthorityUnavailable");
		if (F4T::DurableState::PendingStartingGrant(a_root, a_profileId)) return refuse("startingInventoryPending");
		const auto baseRevision = profile.at("inventory").at("revision").get<std::uint32_t>();
		const auto chunkCount = (a_lines.size() + F4T::InventoryWire::kMaximumTradeLines - 1) / F4T::InventoryWire::kMaximumTradeLines;
		if (baseRevision > (std::numeric_limits<std::uint32_t>::max)() - chunkCount) return refuse("capacityExceeded");

		auto items = profile.at("inventory").at("items");
		auto history = profile.at("inventoryOperations");
		std::vector<Chunk> chunks;
		for (std::size_t index = 0; index < chunkCount; ++index) {
			const auto first = a_lines.begin() + static_cast<std::ptrdiff_t>(index * F4T::InventoryWire::kMaximumTradeLines);
			const auto last = a_lines.begin() + static_cast<std::ptrdiff_t>((std::min)(a_lines.size(), (index + 1) * F4T::InventoryWire::kMaximumTradeLines));
			Chunk chunk{ baseRevision + static_cast<std::uint32_t>(index) + 1, std::vector<Trade::Line>(first, last) };
			const auto name = OperationName(a_operationId, index);
			if (HistoryHas(history, name)) return refuse("operationMismatch");
			for (const auto& line : chunk.lines) {
				if (!Credit(items, line)) return refuse("capacityExceeded");
			}
			const Json request{ { "type", "tradeCommit" }, { "operationId", name }, { "expectedRevision", chunk.revision - 1 },
				{ "give", Json::array() }, { "receive", LinesJson(chunk.lines) } };
			if (!F4T::Trade::NormalizeCommitRequest(request)) return refuse("invalidRequest");
			if (history.size() >= F4T::Drops::kMaximumOperationHistory) history.erase(history.begin());
			history.push_back(Json{ { "request", request }, { "result", F4T::Trade::CommitResultRecord(request, chunk.revision) } });
			chunks.push_back(std::move(chunk));
		}
		const Json candidate{ { "initialized", true }, { "revision", baseRevision + static_cast<std::uint32_t>(chunkCount) }, { "items", items } };
		try {
			F4T::DurableState::ValidateInventory(candidate);
		} catch (const std::invalid_argument&) {
			return refuse("capacityExceeded");
		}
		profile.at("inventory") = candidate;
		profile.at("inventoryOperations") = std::move(history);
		records.push_back(Json{ { "operationId", a_operationId }, { "chunks", ChunksJson(chunks) }, { "acked", false } });
		profile["campMoves"] = std::move(records);
		result.outcome = Outcome::Committed;
		result.chunks = std::move(chunks);
		result.pending = true;
		return result;
	}

	std::optional<Commit> Replay(const Json& a_root, std::string_view a_profileId, std::uint64_t a_operationId)
	{
		const auto profiles = a_root.find("profiles");
		if (profiles == a_root.end() || !profiles->is_object()) return std::nullopt;
		const auto profile = profiles->find(std::string{ a_profileId });
		if (profile == profiles->end()) return std::nullopt;
		const auto* record = FindRecord(*profile, a_operationId);
		if (record == nullptr) return std::nullopt;
		auto chunks = ReadChunks(record->value("chunks", Json::array()));
		if (!chunks) return std::nullopt;
		return Commit{ Outcome::Replayed, {}, std::move(*chunks), !record->value("acked", false) };
	}

	bool Ack(Json& a_root, std::string_view a_profileId, std::uint64_t a_operationId)
	{
		const auto profiles = a_root.find("profiles");
		if (profiles == a_root.end() || !profiles->is_object()) return false;
		const auto profile = profiles->find(std::string{ a_profileId });
		if (profile == profiles->end()) return false;
		const auto records = profile->find("campMoves");
		if (records == profile->end() || !records->is_array()) return false;
		for (auto& record : *records) {
			if (!record.is_object() || !record.contains("operationId") || record.at("operationId") != a_operationId) continue;
			if (record.value("acked", false)) return false;
			record["acked"] = true;
			return true;
		}
		return false;
	}

	std::vector<Grant> Pending(const Json& a_root, std::string_view a_profileId)
	{
		std::vector<Grant> grants;
		const auto profiles = a_root.find("profiles");
		if (profiles == a_root.end() || !profiles->is_object()) return grants;
		const auto profile = profiles->find(std::string{ a_profileId });
		if (profile == profiles->end()) return grants;
		const auto records = profile->find("campMoves");
		if (records == profile->end() || !records->is_array()) return grants;
		for (const auto& record : *records) {
			if (!record.is_object() || record.value("acked", true) || !record.contains("operationId") || !record.at("operationId").is_number_unsigned()) continue;
			auto chunks = ReadChunks(record.value("chunks", Json::array()));
			if (chunks && !chunks->empty()) grants.push_back(Grant{ record.at("operationId").get<std::uint64_t>(), std::move(*chunks) });
		}
		return grants;
	}

	Json MakeGrantPacket(std::uint64_t a_operationId, const std::vector<Chunk>& a_chunks)
	{
		return Json{ { "type", "campGrant" }, { "operationId", a_operationId }, { "chunks", ChunksJson(a_chunks) } };
	}

	std::optional<Grant> ParseGrantPacket(const Json& a_packet)
	{
		if (!HasExactKeys(a_packet, { "type", "operationId", "chunks" }) || a_packet.at("type") != "campGrant" ||
			!a_packet.at("operationId").is_number_unsigned() || a_packet.at("operationId").get<std::uint64_t>() == 0) return std::nullopt;
		auto chunks = ReadChunks(a_packet.at("chunks"));
		if (!chunks || chunks->empty()) return std::nullopt;
		return Grant{ a_packet.at("operationId").get<std::uint64_t>(), std::move(*chunks) };
	}

	Json MakeGrantAck(std::uint64_t a_operationId)
	{
		return Json{ { "type", "campGrantAck" }, { "operationId", a_operationId } };
	}

	std::optional<std::uint64_t> ParseGrantAck(const Json& a_packet)
	{
		if (!HasExactKeys(a_packet, { "type", "operationId" }) || a_packet.at("type") != "campGrantAck" ||
			!a_packet.at("operationId").is_number_unsigned() || a_packet.at("operationId").get<std::uint64_t>() == 0) return std::nullopt;
		return a_packet.at("operationId").get<std::uint64_t>();
	}

	ApplyOutcome ApplyOnce(const Grant& a_grant, const Counts& a_current, std::vector<Receipt>& a_receipts,
		const std::function<bool(const std::vector<Trade::Line>&)>& a_apply, const std::function<std::optional<Counts>()>& a_recount)
	{
		const auto expected = ExpectedCounts(a_grant);
		for (const auto& entry : expected) {
			if (!a_current.contains(entry.first)) return ApplyOutcome::Retry;
		}
		auto found = FindReceipt(a_receipts, a_grant.operationId);
		if (found == a_receipts.end()) {
			Receipt pending{ a_grant.operationId, ReceiptState::Pending, {} };
			for (const auto& entry : expected) pending.baseline[entry.first] = a_current.at(entry.first);
			a_receipts.push_back(std::move(pending));
			TrimAcked(a_receipts);
			found = FindReceipt(a_receipts, a_grant.operationId);
		}
		if (found->state == ReceiptState::Acked) return ApplyOutcome::Acknowledge;
		if (found->state == ReceiptState::Applied) return ApplyOutcome::Hold;
		const auto missing = MissingLines(a_grant, a_current, *found);
		if (!a_apply(missing)) return ApplyOutcome::Retry;
		const auto after = a_recount();
		if (!after || !CountsRose(a_current, *after, missing)) return ApplyOutcome::Retry;
		found->state = ReceiptState::Applied;
		return ApplyOutcome::Hold;
	}

	std::vector<std::uint64_t> AppliedIds(const std::vector<Receipt>& a_receipts)
	{
		std::vector<std::uint64_t> ids;
		for (const auto& receipt : a_receipts) {
			if (receipt.state == ReceiptState::Applied) ids.push_back(receipt.operationId);
		}
		return ids;
	}

	std::vector<std::uint64_t> MarkLanded(std::vector<Receipt>& a_receipts, const std::vector<std::uint64_t>& a_operationIds)
	{
		std::vector<std::uint64_t> landed;
		for (const auto operationId : a_operationIds) {
			const auto found = FindReceipt(a_receipts, operationId);
			if (found == a_receipts.end() || found->state != ReceiptState::Applied) continue;
			found->state = ReceiptState::Acked;
			found->baseline.clear();
			landed.push_back(operationId);
		}
		TrimAcked(a_receipts);
		return landed;
	}

	void AddAcked(std::vector<Receipt>& a_receipts, const std::vector<std::uint64_t>& a_operationIds)
	{
		for (const auto operationId : a_operationIds) {
			if (FindReceipt(a_receipts, operationId) == a_receipts.end()) a_receipts.push_back(Receipt{ operationId, ReceiptState::Acked, {} });
		}
		TrimAcked(a_receipts);
	}

	std::optional<std::vector<std::uint64_t>> ParseAckedIds(std::string_view a_text)
	{
		std::vector<std::uint64_t> ids;
		std::istringstream input{ std::string{ a_text } };
		std::string token;
		while (input >> token) {
			std::uint64_t operationId{};
			const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), operationId);
			if (error != std::errc{} || end != token.data() + token.size() || operationId == 0) return std::nullopt;
			ids.push_back(operationId);
		}
		return ids;
	}

	std::string FormatWorlds(const Worlds& a_worlds)
	{
		Json worlds = Json::array();
		for (const auto& [worldId, receipts] : a_worlds) {
			Json records = Json::array();
			for (const auto& receipt : receipts) {
				Json record{ { "operationId", receipt.operationId }, { "state", StateName(receipt.state) } };
				if (receipt.state != ReceiptState::Acked) record["baseline"] = receipt.baseline;
				records.push_back(std::move(record));
			}
			worlds.push_back(Json{ { "worldId", worldId }, { "receipts", std::move(records) } });
		}
		return Json{ { "worlds", std::move(worlds) } }.dump();
	}

	std::optional<Worlds> ParseWorlds(std::string_view a_text)
	{
		const auto document = Json::parse(std::string{ a_text }, nullptr, false);
		if (!HasExactKeys(document, { "worlds" }) || !document.at("worlds").is_array()) return std::nullopt;
		Worlds worlds;
		for (const auto& entry : document.at("worlds")) {
			if (!HasExactKeys(entry, { "worldId", "receipts" }) || !entry.at("worldId").is_string() || !entry.at("receipts").is_array()) return std::nullopt;
			const auto worldId = entry.at("worldId").get<std::string>();
			if (worlds.contains(worldId)) return std::nullopt;
			auto& receipts = worlds[worldId];
			for (const auto& record : entry.at("receipts")) {
				auto receipt = ReadReceipt(record);
				if (!receipt) return std::nullopt;
				receipts.push_back(std::move(*receipt));
			}
		}
		BoundAcked(worlds);
		return worlds;
	}

	std::optional<Snapshot> ParseSnapshot(std::string_view a_text)
	{
		const auto document = Json::parse(std::string{ a_text }, nullptr, false);
		if (!HasExactKeys(document, { "worldId", "receipts" }) || !document.at("worldId").is_string() || !document.at("receipts").is_array()) return std::nullopt;
		Snapshot snapshot{ document.at("worldId").get<std::string>(), {} };
		for (const auto& record : document.at("receipts")) {
			auto receipt = ReadReceipt(record);
			if (!receipt) return std::nullopt;
			snapshot.receipts.push_back(std::move(*receipt));
		}
		TrimAcked(snapshot.receipts);
		return snapshot;
	}

	void BoundAcked(Worlds& a_worlds)
	{
		while (CountAcked(a_worlds) > kMaximumAckedRecords) {
			EraseFirst(a_worlds, std::string{}, true);
		}
	}

	bool FitWorlds(Worlds& a_worlds, const std::string& a_currentWorld, std::size_t a_maximumBytes)
	{
		auto truncated = false;
		while (FormatWorlds(a_worlds).size() > a_maximumBytes) {
			if (EraseFirst(a_worlds, a_currentWorld, true)) {
				continue;
			}
			if (!EraseFirst(a_worlds, a_currentWorld, false)) {
				break;
			}
			truncated = true;
		}
		return truncated;
	}
}

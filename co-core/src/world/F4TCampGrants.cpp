#include "F4TCampGrants.h"

#include "F4TDropLedger.h"
#include "F4TDurableState.h"
#include "F4TInventoryContract.h"
#include "F4TInventorySnapshot.h"
#include "F4TInventoryWire.h"

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
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

		void KeepNewest(std::vector<std::uint64_t>& a_ids)
		{
			if (a_ids.size() > kMaximumAckedRecords) a_ids.erase(a_ids.begin(), a_ids.begin() + static_cast<std::ptrdiff_t>(a_ids.size() - kMaximumAckedRecords));
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

	// Constraint: apply, then remember, then ack, as in the settlement charge flow; source:CO-BETA/plugin/src/F4TSettlementAccessClient.cpp
	ApplyOutcome ApplyOnce(std::uint64_t a_operationId, const std::function<bool(std::uint64_t)>& a_wasApplied,
		const std::function<bool()>& a_apply, const std::function<void(std::uint64_t)>& a_remember)
	{
		if (a_wasApplied(a_operationId)) return ApplyOutcome::Acknowledge;
		if (!a_apply()) return ApplyOutcome::Retry;
		a_remember(a_operationId);
		return ApplyOutcome::Acknowledge;
	}

	std::vector<std::uint64_t> ParseAppliedIds(std::string_view a_text)
	{
		std::vector<std::uint64_t> ids;
		std::istringstream input{ std::string{ a_text } };
		std::uint64_t id{};
		while (input >> id) ids.push_back(id);
		KeepNewest(ids);
		return ids;
	}

	std::string FormatAppliedIds(const std::vector<std::uint64_t>& a_ids)
	{
		std::string text;
		for (const auto id : a_ids) text += std::to_string(id) + '\n';
		return text;
	}

	void RememberAppliedId(std::vector<std::uint64_t>& a_ids, std::uint64_t a_operationId)
	{
		a_ids.push_back(a_operationId);
		KeepNewest(a_ids);
	}
}

#include "F4TNpcLootSync.h"

#include "F4TNpcCoopValidation.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::NpcLootSync
{
	namespace
	{
		using Json = nlohmann::json;

		std::optional<std::uint32_t> ReadPlayerId(const Json& a_packet)
		{
			const auto found = a_packet.find("playerId");
			if (found == a_packet.end() || !found->is_number_integer()) {
				return std::nullopt;
			}
			const auto value = found->get<std::int64_t>();
			if (value <= 0 || value > static_cast<std::int64_t>((std::numeric_limits<std::uint32_t>::max)())) {
				return std::nullopt;
			}
			return static_cast<std::uint32_t>(value);
		}

		std::uint32_t ParseHex(std::string_view a_value)
		{
			std::uint32_t value = 0;
			std::from_chars(a_value.data(), a_value.data() + a_value.size(), value, 16);
			return value;
		}

		LootIdentity ParseIdentity(const Json& a_normalized)
		{
			return LootIdentity{
				.npcId = a_normalized.at("npcId").get<std::uint32_t>(),
				.sourceFormId = ParseHex(a_normalized.at("sourceFormId").get<std::string>()),
				.cellId = ParseHex(a_normalized.at("cellId").get<std::string>()),
				.worldspaceId = ParseHex(a_normalized.at("worldspaceId").get<std::string>()),
				.authorityEpoch = a_normalized.at("authorityEpoch").get<std::uint32_t>()
			};
		}

		std::string FormatHex(std::uint32_t a_value)
		{
			char buffer[9]{};
			std::snprintf(buffer, sizeof(buffer), "%08X", a_value);
			return buffer;
		}

		Json IdentityToJson(const LootIdentity& a_identity)
		{
			return Json{
				{ "npcId", a_identity.npcId },
				{ "cellId", FormatHex(a_identity.cellId) },
				{ "worldspaceId", a_identity.worldspaceId == 0 ? std::string{} : FormatHex(a_identity.worldspaceId) },
				{ "sourceFormId", FormatHex(a_identity.sourceFormId) },
				{ "authorityEpoch", a_identity.authorityEpoch }
			};
		}
	}

	std::optional<RemoteLoot> ParseRemoteLoot(const Json& a_packet)
	{
		const auto authorityPlayerId = ReadPlayerId(a_packet);
		const auto normalized = F4T::NpcCoopValidation::NormalizeNpcLoot(a_packet);
		if (!authorityPlayerId || !normalized) {
			return std::nullopt;
		}

		RemoteLoot loot;
		loot.identity = ParseIdentity(*normalized);
		loot.revision = normalized->at("revision").get<std::uint32_t>();
		loot.authorityPlayerId = *authorityPlayerId;
		for (const auto& item : normalized->at("items")) {
			loot.items.push_back(LootItem{
				.formId = ParseHex(item.at("formId").get<std::string>()),
				.count = item.at("count").get<std::uint32_t>()
			});
		}
		return loot;
	}

	std::optional<TakeRequest> ParseTakeRequest(const Json& a_packet)
	{
		const auto takerPlayerId = ReadPlayerId(a_packet);
		const auto normalized = F4T::NpcCoopValidation::NormalizeNpcLootTake(a_packet);
		if (!takerPlayerId || !normalized) {
			return std::nullopt;
		}

		TakeRequest request;
		request.identity = ParseIdentity(*normalized);
		request.takerPlayerId = *takerPlayerId;
		request.operationId = normalized->at("operationId").get<std::string>();
		request.formId = ParseHex(normalized->at("formId").get<std::string>());
		request.count = normalized->at("count").get<std::uint32_t>();
		return request;
	}

	std::optional<TakeResult> ParseTakeResult(const Json& a_packet)
	{
		const auto normalized = F4T::NpcCoopValidation::NormalizeNpcLootResult(a_packet);
		if (!normalized) {
			return std::nullopt;
		}

		TakeResult result;
		result.operationId = normalized->at("operationId").get<std::string>();
		result.accepted = normalized->at("status") == "accepted";
		if (const auto reason = normalized->find("reason"); reason != normalized->end()) {
			result.reason = reason->get<std::string>();
		}
		result.formId = ParseHex(normalized->at("formId").get<std::string>());
		result.count = normalized->at("count").get<std::uint32_t>();
		result.revision = normalized->at("revision").get<std::uint32_t>();
		result.takerPlayerId = normalized->at("takerPlayerId").get<std::uint32_t>();
		return result;
	}

	Json RemoteLootToJson(const RemoteLoot& a_loot)
	{
		Json items = Json::array();
		for (const auto& item : a_loot.items) {
			items.push_back(Json{
				{ "formId", FormatHex(item.formId) }, // source:CO-BETA/docs/protocol/packets.md
				{ "count", item.count }
			});
		}
		Json packet = IdentityToJson(a_loot.identity);
		packet["type"] = "npcLoot";
		packet["revision"] = a_loot.revision;
		packet["items"] = std::move(items);
		packet["playerId"] = a_loot.authorityPlayerId;
		return packet;
	}

	Json TakeRequestToJson(const TakeRequest& a_take)
	{
		Json packet = IdentityToJson(a_take.identity);
		packet["type"] = "npcLootTake";
		packet["operationId"] = a_take.operationId;
		packet["formId"] = FormatHex(a_take.formId); // source:CO-BETA/docs/protocol/packets.md
		packet["count"] = a_take.count;
		packet["playerId"] = a_take.takerPlayerId;
		return packet;
	}

	Json TakeResultToJson(const TakeResult& a_result)
	{
		Json packet{
			{ "type", "npcLootResult" },
			{ "operationId", a_result.operationId },
			{ "status", a_result.accepted ? "accepted" : "rejected" },
			{ "revision", a_result.revision },
			{ "formId", FormatHex(a_result.formId) }, // source:CO-BETA/docs/protocol/packets.md
			{ "count", a_result.count },
			{ "takerPlayerId", a_result.takerPlayerId }
		};
		if (!a_result.reason.empty()) {
			packet["reason"] = a_result.reason;
		}
		return packet;
	}

	PublishList BuildPublishList(std::span<const LootItem> a_raw)
	{
		std::map<std::uint32_t, std::uint32_t> merged;
		for (const auto& item : a_raw) {
			// source:CO-BETA/docs/protocol/packets.md
			if (item.formId == 0 || item.formId >= RuntimeFormIdBase || item.count == 0) {
				continue;
			}
			auto& total = merged[item.formId];
			total = static_cast<std::uint32_t>(std::min<std::uint64_t>(std::uint64_t{ total } + item.count, MaximumLootCount));
		}

		PublishList list;
		if (merged.size() > MaximumPublishTypes) {
			list.truncated = true;
			return list;
		}
		list.items.reserve(merged.size());
		for (const auto& [formId, count] : merged) {
			list.items.push_back(LootItem{ .formId = formId, .count = count });
		}
		return list;
	}

	MirrorDiff ComputeMirrorDiff(std::span<const LootItem> a_current, std::span<const LootItem> a_target)
	{
		std::map<std::uint32_t, std::int64_t> surplus;
		for (const auto& item : a_current) {
			surplus[item.formId] += static_cast<std::int64_t>(item.count);
		}
		for (const auto& item : a_target) {
			surplus[item.formId] -= static_cast<std::int64_t>(item.count);
		}

		MirrorDiff diff;
		for (const auto& [formId, delta] : surplus) {
			if (delta > 0) {
				diff.removals.push_back(LootItem{ .formId = formId, .count = static_cast<std::uint32_t>(delta) });
			} else if (delta < 0) {
				diff.additions.push_back(LootItem{ .formId = formId, .count = static_cast<std::uint32_t>(-delta) });
			}
		}
		return diff;
	}

	std::uint32_t ClampToAvailable(std::uint32_t a_requested, std::uint32_t a_available) noexcept
	{
		return (std::min)(a_requested, a_available);
	}

	std::uint32_t NextRevision(std::uint32_t a_previous) noexcept
	{
		const auto next = a_previous + 1;
		return next == 0 ? 1 : next;
	}

	std::string MakeOperationId(std::uint32_t a_takerPlayerId, std::uint32_t a_counter)
	{
		return "t" + std::to_string(a_takerPlayerId) + "-" + std::to_string(a_counter);
	}

	bool TakeCoalescer::Add(std::uint32_t a_npcId, std::uint32_t a_formId, std::uint32_t a_count)
	{
		if (a_count == 0) {
			return false;
		}
		for (auto& take : _takes) {
			if (take.npcId == a_npcId && take.formId == a_formId) {
				take.count = static_cast<std::uint32_t>(std::min<std::uint64_t>(std::uint64_t{ take.count } + a_count, MaximumLootCount));
				return true;
			}
		}
		if (_takes.size() >= MaximumCoalescedTakes) {
			return false;
		}
		_takes.push_back(CoalescedTake{
			.npcId = a_npcId,
			.formId = a_formId,
			.count = (std::min)(a_count, MaximumLootCount)
		});
		return true;
	}

	std::vector<CoalescedTake> TakeCoalescer::Drain(std::size_t a_maxPerCall)
	{
		const auto taken = (std::min)(a_maxPerCall, _takes.size());
		std::vector<CoalescedTake> drained(_takes.begin(), _takes.begin() + static_cast<std::ptrdiff_t>(taken));
		_takes.erase(_takes.begin(), _takes.begin() + static_cast<std::ptrdiff_t>(taken));
		return drained;
	}

	std::uint32_t RevisionCounter::Next(std::uint32_t a_npcId)
	{
		auto& revision = _last[a_npcId];
		revision = NextRevision(revision);
		return revision;
	}

	void RevisionCounter::Clear() noexcept
	{
		_last.clear();
	}

	void OperationLru::Remember(std::uint32_t a_takerPlayerId, std::string_view a_operationId, const TakeResult& a_result)
	{
		const auto found = std::find_if(_entries.begin(), _entries.end(), [&](const Entry& a_entry) {
			return a_entry.takerPlayerId == a_takerPlayerId && a_entry.operationId == a_operationId;
		});
		if (found != _entries.end()) {
			found->result = a_result;
			std::rotate(found, found + 1, _entries.end());
			return;
		}
		if (_entries.size() >= MaximumOperationResults) {
			_entries.erase(_entries.begin());
		}
		_entries.push_back(Entry{ .takerPlayerId = a_takerPlayerId, .operationId = std::string{ a_operationId }, .result = a_result });
	}

	std::optional<TakeResult> OperationLru::Find(std::uint32_t a_takerPlayerId, std::string_view a_operationId)
	{
		const auto found = std::find_if(_entries.begin(), _entries.end(), [&](const Entry& a_entry) {
			return a_entry.takerPlayerId == a_takerPlayerId && a_entry.operationId == a_operationId;
		});
		if (found == _entries.end()) {
			return std::nullopt;
		}
		auto result = found->result;
		std::rotate(found, found + 1, _entries.end());
		return result;
	}
}

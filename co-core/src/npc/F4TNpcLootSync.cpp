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
		constexpr std::size_t kMaximumAckOperationIdLength = 64;

		std::optional<std::uint32_t> ReadPlayerId(const Json& a_packet, const char* a_key = "playerId")
		{
			const auto found = a_packet.find(a_key);
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

		std::string_view AckStateName(TakeAckState a_state) noexcept
		{
			if (a_state == TakeAckState::Expired) {
				return "expired";
			}
			if (a_state == TakeAckState::TakerGone) {
				return "takerGone";
			}
			return "forwarded";
		}

		std::string FormatHex(std::uint32_t a_value)
		{
			char buffer[9]{};
			std::snprintf(buffer, sizeof(buffer), "%08X", a_value);
			return buffer;
		}

		Json IdentityToJson(const LootIdentity& a_identity)
		{
			Json identity{
				{ "npcId", a_identity.npcId },
				{ "cellId", FormatHex(a_identity.cellId) },
				{ "worldspaceId", a_identity.worldspaceId == 0 ? std::string{} : FormatHex(a_identity.worldspaceId) },
				{ "sourceFormId", FormatHex(a_identity.sourceFormId) },
				{ "authorityEpoch", a_identity.authorityEpoch }
			};
			if (a_identity.sourceFormId == 0) {
				identity["runtimeNpc"] = true;
			}
			return identity;
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
		request.expiresInMs = normalized->contains("expiresInMs") ? normalized->at("expiresInMs").get<std::uint32_t>() : 0;
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
		packet["expiresInMs"] = a_take.expiresInMs;
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

	Json TakeAckToJson(const TakeAck& a_ack)
	{
		return Json{
			{ "type", "npcLootAck" },
			{ "operationId", a_ack.operationId },
			{ "takerPlayerId", a_ack.takerPlayerId },
			{ "state", std::string{ AckStateName(a_ack.state) } }
		};
	}

	std::optional<TakeAck> ParseTakeAck(const Json& a_packet)
	{
		const auto takerPlayerId = ReadPlayerId(a_packet, "takerPlayerId");
		const auto operationIt = a_packet.find("operationId");
		const auto stateIt = a_packet.find("state");
		if (!takerPlayerId || operationIt == a_packet.end() || !operationIt->is_string() || stateIt == a_packet.end() || !stateIt->is_string()) {
			return std::nullopt;
		}
		TakeAck ack;
		ack.takerPlayerId = *takerPlayerId;
		ack.operationId = operationIt->get<std::string>();
		if (ack.operationId.empty() || ack.operationId.size() > kMaximumAckOperationIdLength) {
			return std::nullopt;
		}
		const auto state = stateIt->get<std::string>();
		if (state == "expired") {
			ack.state = TakeAckState::Expired;
		} else if (state == "takerGone") {
			ack.state = TakeAckState::TakerGone;
		} else if (state != "forwarded") {
			return std::nullopt;
		}
		return ack;
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

	std::uint32_t UnmatchedTakeCount(std::uint32_t a_requestedItemId, std::uint32_t a_requestedCount, const TakeResult& a_result) noexcept
	{
		const auto& [operationId, accepted, reason, resultItemId, count, revision, takerPlayerId] = a_result;
		if (!accepted || resultItemId != a_requestedItemId) {
			return a_requestedCount;
		}
		return a_requestedCount - ClampToAvailable(a_requestedCount, count);
	}

	std::uint32_t LateGrantCount(std::uint32_t a_itemId, std::uint32_t a_removed, const TakeResult& a_result) noexcept
	{
		const auto& [operationId, accepted, reason, resultItemId, count, revision, takerPlayerId] = a_result;
		if (!accepted || resultItemId != a_itemId) {
			return 0;
		}
		return ClampToAvailable(a_removed, count);
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

	bool TakeRequestExpired(std::uint64_t a_nowMilliseconds, std::uint64_t a_receivedMilliseconds, std::uint32_t a_expiresInMilliseconds) noexcept
	{
		if (a_expiresInMilliseconds == 0) {
			return true;
		}
		const auto age = a_nowMilliseconds >= a_receivedMilliseconds ? a_nowMilliseconds - a_receivedMilliseconds : std::uint64_t{ 0 };
		return static_cast<std::int64_t>(age) > static_cast<std::int64_t>(a_expiresInMilliseconds) - TakeHostRefusalMarginMilliseconds;
	}

	bool IsLiveTakeContext(const TakeContext& a_current, const TakeContext& a_operation)
	{
		return !a_current.serverKey.empty() && !a_current.characterId.empty() && a_current == a_operation;
	}

	bool TimedOutTakeExpired(bool a_held, std::uint64_t a_createdMilliseconds, std::uint64_t a_nowMilliseconds) noexcept
	{
		return !a_held && a_nowMilliseconds >= a_createdMilliseconds && a_nowMilliseconds - a_createdMilliseconds > TimedOutTakeMilliseconds;
	}

	bool TakeLedger::Open(TakeOp a_op, double a_nowSeconds)
	{
		auto key = std::make_pair(a_op.request.takerPlayerId, a_op.request.operationId);
		if (_ops.contains(key) || PendingCount() >= MaximumPendingTakes) {
			return false;
		}
		if (_ops.size() >= MaximumTakeLedgerEntries) {
			auto oldest = _ops.end();
			for (auto entry = _ops.begin(); entry != _ops.end(); ++entry) {
				if (entry->second.state != TakeState::Pending && (oldest == _ops.end() || entry->second.settledSeconds < oldest->second.settledSeconds)) {
					oldest = entry;
				}
			}
			if (oldest == _ops.end()) {
				return false;
			}
			_ops.erase(oldest);
		}
		a_op.state = TakeState::Pending;
		a_op.openedSeconds = a_nowSeconds;
		a_op.settledSeconds = 0.0;
		_ops.emplace(std::move(key), std::move(a_op));
		return true;
	}

	const TakeOp* TakeLedger::Find(std::uint32_t a_takerPlayerId, std::string_view a_operationId) const
	{
		const auto found = _ops.find(std::make_pair(a_takerPlayerId, std::string{ a_operationId }));
		return found == _ops.end() ? nullptr : &found->second;
	}

	void TakeLedger::Settle(std::uint32_t a_takerPlayerId, std::string_view a_operationId, TakeState a_state, double a_nowSeconds)
	{
		const auto found = _ops.find(std::make_pair(a_takerPlayerId, std::string{ a_operationId }));
		if (found == _ops.end() || found->second.state != TakeState::Pending) {
			return;
		}
		found->second.state = a_state;
		found->second.settledSeconds = a_nowSeconds;
	}

	std::vector<TakeOp> TakeLedger::Sweep(double a_nowSeconds, const std::set<std::uint32_t>& a_livePlayerIds)
	{
		std::erase_if(_ops, [a_nowSeconds](const auto& a_entry) {
			return a_entry.second.state != TakeState::Pending && a_nowSeconds - a_entry.second.settledSeconds > TakeTombstoneSeconds;
		});
		std::vector<TakeOp> transitions;
		for (auto& entry : _ops) {
			auto& op = entry.second;
			if (op.state != TakeState::Pending) {
				continue;
			}
			const auto age = a_nowSeconds - op.openedSeconds;
			if (!a_livePlayerIds.contains(op.request.takerPlayerId)) {
				op.state = TakeState::TakerGone;
			} else if (!(age >= 0.0) || age > TakeLifetimeSeconds) {
				op.state = TakeState::Expired;
			} else {
				continue;
			}
			op.settledSeconds = a_nowSeconds;
			transitions.push_back(op);
		}
		return transitions;
	}

	std::size_t TakeLedger::PendingCount() const noexcept
	{
		return static_cast<std::size_t>(std::count_if(_ops.begin(), _ops.end(), [](const auto& a_entry) {
			return a_entry.second.state == TakeState::Pending;
		}));
	}
}

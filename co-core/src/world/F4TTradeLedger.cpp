#include "F4TTradeLedger.h"

#include "F4TDurableState.h"
#include "F4TInventoryContract.h"
#include "F4TInventoryWire.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace F4T::Trade
{
	namespace
	{
		using Json = nlohmann::json;
		using Outcome = F4T::Drops::Outcome;

		Verdict Accepted()
		{
			return Verdict{ true, {} };
		}

		Verdict Refused(const char* a_reason)
		{
			return Verdict{ false, a_reason };
		}

		bool HasExactKeys(const Json& a_object, std::initializer_list<const char*> a_keys)
		{
			return a_object.is_object() && a_object.size() == a_keys.size() &&
				std::all_of(a_keys.begin(), a_keys.end(), [&a_object](const char* a_key) { return a_object.contains(a_key); });
		}

		bool ValidString(const Json& a_packet, const char* a_field, std::size_t a_maximumChars)
		{
			const auto found = a_packet.find(a_field);
			return found != a_packet.end() && found->is_string() && !found->get_ref<const std::string&>().empty() &&
				found->get_ref<const std::string&>().size() <= a_maximumChars;
		}

		bool ValidTradeId(const Json& a_packet)
		{
			return ValidString(a_packet, "tradeId", kMaximumTradeIdChars);
		}

		bool ValidOperationId(const Json& a_packet)
		{
			return ValidString(a_packet, "operationId", F4T::Drops::kMaximumOperationIdChars);
		}

		std::optional<std::uint32_t> ReadUnsigned(const Json& a_object, const char* a_field)
		{
			const auto found = a_object.find(a_field);
			if (found == a_object.end() || !found->is_number_unsigned()) return std::nullopt;
			const auto value = found->get<std::uint64_t>();
			if (value > (std::numeric_limits<std::uint32_t>::max)()) return std::nullopt;
			return static_cast<std::uint32_t>(value);
		}

		bool ValidItemKey(const std::string& a_key)
		{
			return !a_key.empty() && a_key.size() <= F4T::InventoryContract::kMaximumItemKeyChars && F4T::InventoryContract::ParseItemKey(a_key).has_value();
		}

		bool ValidLines(const std::vector<Line>& a_lines)
		{
			if (a_lines.size() > F4T::InventoryWire::kMaximumTradeLines) return false;
			std::set<std::string> keys;
			return std::all_of(a_lines.begin(), a_lines.end(), [&keys](const Line& a_line) {
				return a_line.count != 0 && a_line.count <= F4T::Drops::kMaximumDropCount && ValidItemKey(a_line.itemKey) &&
					keys.insert(a_line.itemKey).second;
			});
		}

		std::optional<std::vector<Line>> ReadLines(const Json& a_lines)
		{
			if (!a_lines.is_array() || a_lines.size() > F4T::InventoryWire::kMaximumTradeLines) return std::nullopt;
			std::vector<Line> lines;
			for (const auto& entry : a_lines) {
				if (!HasExactKeys(entry, { "itemKey", "count" }) || !entry.at("itemKey").is_string() || !entry.at("count").is_number_unsigned()) return std::nullopt;
				const auto count = entry.at("count").get<std::uint64_t>();
				if (count > F4T::Drops::kMaximumDropCount) return std::nullopt;
				lines.push_back(Line{ entry.at("itemKey").get<std::string>(), static_cast<std::uint32_t>(count) });
			}
			if (!ValidLines(lines)) return std::nullopt;
			return lines;
		}

		Json LinesJson(const std::vector<Line>& a_lines)
		{
			Json lines = Json::array();
			for (const auto& line : a_lines) lines.push_back(Json{ { "itemKey", line.itemKey }, { "count", line.count } });
			return lines;
		}

		Json CommitRecord(const std::string& a_operationId, const CommitParty& a_own, const std::vector<Line>& a_receive)
		{
			return Json{ { "type", "tradeCommit" }, { "operationId", a_operationId }, { "expectedRevision", a_own.expectedRevision },
				{ "give", LinesJson(a_own.give) }, { "receive", LinesJson(a_receive) } };
		}

		bool HasItems(const Json& a_items, const std::vector<Line>& a_lines)
		{
			return std::all_of(a_lines.begin(), a_lines.end(), [&a_items](const Line& a_line) {
				const auto found = a_items.find(a_line.itemKey);
				return found != a_items.end() && found->at("count").get<std::uint32_t>() >= a_line.count;
			});
		}

		std::optional<Json> ApplyLines(const Json& a_items, const std::vector<Line>& a_give, const std::vector<Line>& a_receive, const Json& a_partnerItems)
		{
			Json items = a_items;
			for (const auto& line : a_give) {
				auto& entry = items.at(line.itemKey);
				const auto remaining = entry.at("count").get<std::uint32_t>() - line.count;
				if (remaining == 0) items.erase(line.itemKey);
				else entry["count"] = remaining;
			}
			for (const auto& line : a_receive) {
				const auto found = items.find(line.itemKey);
				if (found == items.end()) {
					items[line.itemKey] = a_partnerItems.at(line.itemKey);
					items[line.itemKey]["count"] = line.count;
					continue;
				}
				const auto owned = found->at("count").get<std::uint32_t>();
				if (owned > F4T::Drops::kMaximumDropCount - line.count) return std::nullopt;
				(*found)["count"] = owned + line.count;
			}
			return items;
		}

		const Json* FindOperation(const Json& a_profile, const std::string& a_operationId)
		{
			for (const auto& operation : a_profile.at("inventoryOperations")) {
				if (operation.at("request").at("operationId") == a_operationId) return &operation;
			}
			return nullptr;
		}

		void AppendOperation(Json& a_profile, const Json& a_record, const Json& a_result)
		{
			auto& history = a_profile.at("inventoryOperations");
			if (history.size() >= F4T::Drops::kMaximumOperationHistory) history.erase(history.begin());
			history.push_back(Json{ { "request", a_record }, { "result", a_result } });
		}

		std::optional<std::size_t> IndexOf(const std::vector<Trade>& a_trades, std::string_view a_tradeId)
		{
			const auto found = std::find_if(a_trades.begin(), a_trades.end(), [a_tradeId](const Trade& a_trade) { return a_trade.tradeId == a_tradeId; });
			if (found == a_trades.end()) return std::nullopt;
			return static_cast<std::size_t>(std::distance(a_trades.begin(), found));
		}

		std::optional<std::size_t> ActiveIndexFor(const std::vector<Trade>& a_trades, std::uint32_t a_playerId)
		{
			for (std::size_t index = 0; index < a_trades.size(); ++index) {
				if (a_trades[index].initiator.playerId == a_playerId || a_trades[index].target.playerId == a_playerId) return index;
			}
			return std::nullopt;
		}

		Side* SideFor(Trade& a_trade, std::uint32_t a_playerId)
		{
			if (a_trade.initiator.playerId == a_playerId) return &a_trade.initiator;
			if (a_trade.target.playerId == a_playerId) return &a_trade.target;
			return nullptr;
		}

		void ClearConfirmations(Trade& a_trade)
		{
			for (Side* side : { &a_trade.initiator, &a_trade.target }) {
				side->confirmed = false;
				side->confirmedRevision = 0;
			}
		}

		std::pair<std::uint32_t, std::uint32_t> PairKey(std::uint32_t a_first, std::uint32_t a_second)
		{
			return { (std::min)(a_first, a_second), (std::max)(a_first, a_second) };
		}

		void EndAt(std::vector<Trade>& a_trades, std::vector<Ended>& a_ended, std::size_t a_index, std::string a_reason)
		{
			const auto& trade = a_trades[a_index];
			a_ended.push_back(Ended{ trade.tradeId, trade.initiator.playerId, trade.target.playerId, std::move(a_reason) });
			a_trades.erase(a_trades.begin() + static_cast<std::ptrdiff_t>(a_index));
		}
	}

	std::optional<Intent> ParseClientIntent(const Json& a_packet)
	{
		if (!a_packet.is_object() || !a_packet.contains("type") || !a_packet.at("type").is_string()) return std::nullopt;
		const auto& type = a_packet.at("type").get_ref<const std::string&>();
		Intent intent;
		if (type == "tradeRequest") {
			if (!HasExactKeys(a_packet, { "type", "targetPlayerId" })) return std::nullopt;
			const auto target = ReadUnsigned(a_packet, "targetPlayerId");
			if (!target || *target == 0) return std::nullopt;
			intent.kind = IntentKind::Request;
			intent.targetPlayerId = *target;
			return intent;
		}
		if (type == "tradeRespond") {
			if (!HasExactKeys(a_packet, { "type", "tradeId", "accept" }) || !ValidTradeId(a_packet) || !a_packet.at("accept").is_boolean()) return std::nullopt;
			intent.kind = IntentKind::Respond;
			intent.tradeId = a_packet.at("tradeId").get<std::string>();
			intent.accept = a_packet.at("accept").get<bool>();
			return intent;
		}
		if (type == "tradeOffer") {
			if (!HasExactKeys(a_packet, { "type", "tradeId", "lines" }) || !ValidTradeId(a_packet)) return std::nullopt;
			auto lines = ReadLines(a_packet.at("lines"));
			if (!lines) return std::nullopt;
			intent.kind = IntentKind::Offer;
			intent.tradeId = a_packet.at("tradeId").get<std::string>();
			intent.lines = std::move(*lines);
			return intent;
		}
		if (type == "tradeConfirm") {
			if (!HasExactKeys(a_packet, { "type", "tradeId", "offerVersion" }) || !ValidTradeId(a_packet)) return std::nullopt;
			const auto offerVersion = ReadUnsigned(a_packet, "offerVersion");
			if (!offerVersion) return std::nullopt;
			intent.kind = IntentKind::Confirm;
			intent.tradeId = a_packet.at("tradeId").get<std::string>();
			intent.offerVersion = *offerVersion;
			return intent;
		}
		if (type == "tradeCancel") {
			if (!HasExactKeys(a_packet, { "type", "tradeId" }) || !ValidTradeId(a_packet)) return std::nullopt;
			intent.kind = IntentKind::Cancel;
			intent.tradeId = a_packet.at("tradeId").get<std::string>();
			return intent;
		}
		return std::nullopt;
	}

	std::optional<Json> NormalizeCommitRequest(const Json& a_record)
	{
		if (!HasExactKeys(a_record, { "type", "operationId", "expectedRevision", "give", "receive" }) || a_record.at("type") != "tradeCommit" ||
			!ValidOperationId(a_record) || !ReadUnsigned(a_record, "expectedRevision") || !ReadLines(a_record.at("give")) ||
			!ReadLines(a_record.at("receive"))) return std::nullopt;
		return a_record;
	}

	Json CommitResultRecord(const Json& a_record, std::uint32_t a_revision)
	{
		return Json{ { "type", "tradeResult" }, { "operationId", a_record.at("operationId") }, { "status", "committed" }, { "revision", a_revision } };
	}

	CommitResult Commit(Json& a_root, const CommitRequest& a_request)
	{
		CommitResult result;
		const auto refuse = [&result](const char* a_reason) {
			result.outcome = Outcome::Rejected;
			result.reason = a_reason;
			return result;
		};
		if (a_root.at("inventoryAuthorityBlocked").get<bool>()) return refuse("inventoryAuthorityUnavailable");
		if (a_request.first.profileId == a_request.second.profileId) return refuse("selfTrade");
		auto& profiles = a_root.at("profiles");
		const auto firstFound = profiles.find(a_request.first.profileId);
		const auto secondFound = profiles.find(a_request.second.profileId);
		if (firstFound == profiles.end() || secondFound == profiles.end() || !firstFound->contains("inventory") ||
			!secondFound->contains("inventory")) return refuse("inventoryAuthorityUnavailable");
		if (F4T::DurableState::PendingStartingGrant(a_root, a_request.first.profileId) ||
			F4T::DurableState::PendingStartingGrant(a_root, a_request.second.profileId)) return refuse("startingInventoryPending");
		auto& firstProfile = *firstFound;
		auto& secondProfile = *secondFound;
		const auto firstRevision = firstProfile.at("inventory").at("revision").get<std::uint32_t>();
		const auto secondRevision = secondProfile.at("inventory").at("revision").get<std::uint32_t>();
		result.firstRevision = firstRevision;
		result.secondRevision = secondRevision;

		const auto firstRecord = NormalizeCommitRequest(CommitRecord(a_request.operationId, a_request.first, a_request.second.give));
		const auto secondRecord = NormalizeCommitRequest(CommitRecord(a_request.operationId, a_request.second, a_request.first.give));
		if (!firstRecord || !secondRecord) return refuse("invalidRequest");

		const auto* firstOperation = FindOperation(firstProfile, a_request.operationId);
		const auto* secondOperation = FindOperation(secondProfile, a_request.operationId);
		if (firstOperation != nullptr || secondOperation != nullptr) {
			if (firstOperation == nullptr || secondOperation == nullptr || firstOperation->at("request") != *firstRecord ||
				secondOperation->at("request") != *secondRecord) return refuse("operationMismatch");
			result.outcome = Outcome::Replayed;
			result.firstRevision = firstOperation->at("result").at("revision").get<std::uint32_t>();
			result.secondRevision = secondOperation->at("result").at("revision").get<std::uint32_t>();
			return result;
		}

		if (a_request.first.expectedRevision != firstRevision || a_request.second.expectedRevision != secondRevision) return refuse("staleRevision");
		constexpr auto maximumRevision = (std::numeric_limits<std::uint32_t>::max)();
		if (firstRevision == maximumRevision || secondRevision == maximumRevision) return refuse("capacityExceeded");

		const auto& firstItems = firstProfile.at("inventory").at("items");
		const auto& secondItems = secondProfile.at("inventory").at("items");
		if (!HasItems(firstItems, a_request.first.give) || !HasItems(secondItems, a_request.second.give)) return refuse("insufficientItems");
		const auto firstNext = ApplyLines(firstItems, a_request.first.give, a_request.second.give, secondItems);
		const auto secondNext = ApplyLines(secondItems, a_request.second.give, a_request.first.give, firstItems);
		if (!firstNext || !secondNext) return refuse("capacityExceeded");
		const Json firstInventory{ { "initialized", true }, { "revision", firstRevision + 1 }, { "items", *firstNext } };
		const Json secondInventory{ { "initialized", true }, { "revision", secondRevision + 1 }, { "items", *secondNext } };
		try {
			F4T::DurableState::ValidateInventory(firstInventory);
			F4T::DurableState::ValidateInventory(secondInventory);
		} catch (const std::invalid_argument&) {
			return refuse("capacityExceeded");
		}

		firstProfile.at("inventory") = firstInventory;
		secondProfile.at("inventory") = secondInventory;
		AppendOperation(firstProfile, *firstRecord, CommitResultRecord(*firstRecord, firstRevision + 1));
		AppendOperation(secondProfile, *secondRecord, CommitResultRecord(*secondRecord, secondRevision + 1));
		result.outcome = Outcome::Committed;
		result.reason.clear();
		result.firstRevision = firstRevision + 1;
		result.secondRevision = secondRevision + 1;
		return result;
	}

	Json StatePacket(const Trade& a_trade, std::uint32_t a_viewerPlayerId)
	{
		const bool initiator = a_trade.initiator.playerId == a_viewerPlayerId;
		const auto& own = initiator ? a_trade.initiator : a_trade.target;
		const auto& partner = initiator ? a_trade.target : a_trade.initiator;
		return Json{ { "type", "tradeState" }, { "tradeId", a_trade.tradeId }, { "phase", a_trade.accepted ? "negotiating" : "requested" },
			{ "role", initiator ? "initiator" : "target" }, { "partnerPlayerId", partner.playerId }, { "offerVersion", a_trade.offerVersion },
			{ "ownOffer", LinesJson(own.offer) }, { "ownConfirmed", own.confirmed },
			{ "partnerOffer", LinesJson(partner.offer) }, { "partnerConfirmed", partner.confirmed } };
	}

	Json EndedPacket(const Ended& a_ended, std::uint32_t a_viewerPlayerId)
	{
		const auto partner = a_ended.initiatorPlayerId == a_viewerPlayerId ? a_ended.targetPlayerId : a_ended.initiatorPlayerId;
		return Json{ { "type", "tradeState" }, { "tradeId", a_ended.tradeId }, { "phase", "ended" }, { "partnerPlayerId", partner },
			{ "reason", a_ended.reason } };
	}

	Json CommitResultPacket(const std::string& a_tradeId, const CommitRequest& a_request, const CommitResult& a_result, bool a_first)
	{
		const auto& own = a_first ? a_request.first : a_request.second;
		const auto& partner = a_first ? a_request.second : a_request.first;
		const auto revision = a_first ? a_result.firstRevision : a_result.secondRevision;
		const bool committed = a_result.outcome != Outcome::Rejected;
		Json packet{ { "type", "tradeResult" }, { "tradeId", a_tradeId }, { "operationId", a_request.operationId },
			{ "status", committed ? "committed" : "rejected" } };
		if (!committed) packet["reason"] = a_result.reason;
		if (revision) packet["revision"] = *revision;
		if (committed) {
			packet["give"] = LinesJson(own.give);
			packet["receive"] = LinesJson(partner.give);
		}
		return packet;
	}

	Json RefusalPacket(std::string_view a_tradeId, std::string_view a_reason)
	{
		Json packet{ { "type", "tradeResult" }, { "status", "rejected" }, { "reason", std::string{ a_reason } } };
		if (!a_tradeId.empty()) packet["tradeId"] = std::string{ a_tradeId };
		return packet;
	}

	Verdict Book::Request(std::uint32_t a_initiatorPlayerId, std::uint32_t a_targetPlayerId, double a_nowMonotonic)
	{
		if (a_initiatorPlayerId == 0 || a_targetPlayerId == 0) return Refused("invalidPlayer");
		if (a_initiatorPlayerId == a_targetPlayerId) return Refused("selfTrade");
		if (ActiveIndexFor(_trades, a_initiatorPlayerId).has_value() || ActiveIndexFor(_trades, a_targetPlayerId).has_value()) return Refused("busy");
		const auto cooldown = _cooldowns.find(PairKey(a_initiatorPlayerId, a_targetPlayerId));
		if (cooldown != _cooldowns.end() && a_nowMonotonic < cooldown->second) return Refused("cooldown");
		Trade trade;
		trade.tradeId = std::to_string(_epoch) + "-" + std::to_string(++_sequence);
		trade.initiator.playerId = a_initiatorPlayerId;
		trade.target.playerId = a_targetPlayerId;
		trade.lastActivityMonotonic = a_nowMonotonic;
		_trades.push_back(std::move(trade));
		return Accepted();
	}

	Verdict Book::Respond(const std::string& a_tradeId, std::uint32_t a_playerId, bool a_accept, double a_nowMonotonic)
	{
		const auto index = IndexOf(_trades, a_tradeId);
		if (!index) return Refused("notFound");
		auto& trade = _trades[*index];
		if (trade.accepted) return Refused("wrongPhase");
		if (trade.target.playerId != a_playerId) return Refused("notTarget");
		if (!a_accept) {
			EndWithCooldown(*index, "declined", a_nowMonotonic);
			return Accepted();
		}
		trade.accepted = true;
		trade.lastActivityMonotonic = a_nowMonotonic;
		return Accepted();
	}

	Verdict Book::SetOffer(const std::string& a_tradeId, std::uint32_t a_playerId, std::vector<Line> a_lines, double a_nowMonotonic)
	{
		const auto index = IndexOf(_trades, a_tradeId);
		if (!index) return Refused("notFound");
		auto& trade = _trades[*index];
		auto* side = SideFor(trade, a_playerId);
		if (side == nullptr) return Refused("notParticipant");
		if (!trade.accepted) return Refused("notNegotiating");
		if (!ValidLines(a_lines)) return Refused("invalidOffer");
		side->offer = std::move(a_lines);
		ClearConfirmations(trade);
		trade.offerVersion += 1;
		trade.lastActivityMonotonic = a_nowMonotonic;
		return Accepted();
	}

	Verdict Book::Confirm(const std::string& a_tradeId, std::uint32_t a_playerId, std::uint32_t a_revision, std::uint32_t a_offerVersion, double a_nowMonotonic)
	{
		const auto index = IndexOf(_trades, a_tradeId);
		if (!index) return Refused("notFound");
		auto& trade = _trades[*index];
		auto* side = SideFor(trade, a_playerId);
		if (side == nullptr) return Refused("notParticipant");
		if (!trade.accepted) return Refused("notNegotiating");
		if (trade.initiator.offer.empty() && trade.target.offer.empty()) return Refused("emptyTrade");
		if (a_offerVersion != trade.offerVersion) return Refused("offerChanged");
		side->confirmed = true;
		side->confirmedRevision = a_revision;
		trade.lastActivityMonotonic = a_nowMonotonic;
		return Accepted();
	}

	Verdict Book::Cancel(const std::string& a_tradeId, std::uint32_t a_playerId, double a_nowMonotonic)
	{
		const auto index = IndexOf(_trades, a_tradeId);
		if (!index) return Refused("notFound");
		if (SideFor(_trades[*index], a_playerId) == nullptr) return Refused("notParticipant");
		EndWithCooldown(*index, "cancelled", a_nowMonotonic);
		return Accepted();
	}

	bool Book::Ready(const std::string& a_tradeId) const
	{
		const auto* trade = Find(a_tradeId);
		return trade != nullptr && trade->accepted && trade->initiator.confirmed && trade->target.confirmed;
	}

	std::optional<CommitRequest> Book::BuildCommit(const std::string& a_tradeId, const std::string& a_initiatorProfileId,
		const std::string& a_targetProfileId) const
	{
		if (!Ready(a_tradeId)) return std::nullopt;
		const auto& trade = *Find(a_tradeId);
		CommitRequest request;
		request.operationId = "trade-" + trade.tradeId;
		request.first = CommitParty{ a_initiatorProfileId, trade.initiator.confirmedRevision, trade.initiator.offer };
		request.second = CommitParty{ a_targetProfileId, trade.target.confirmedRevision, trade.target.offer };
		return request;
	}

	void Book::Unconfirm(const std::string& a_tradeId)
	{
		if (const auto index = IndexOf(_trades, a_tradeId)) ClearConfirmations(_trades[*index]);
	}

	void Book::Finish(const std::string& a_tradeId, const std::string& a_reason)
	{
		if (const auto index = IndexOf(_trades, a_tradeId)) EndAt(_trades, _ended, *index, a_reason);
	}

	void Book::EndFor(std::uint32_t a_playerId, const std::string& a_reason)
	{
		if (const auto index = ActiveIndexFor(_trades, a_playerId)) EndAt(_trades, _ended, *index, a_reason);
	}

	void Book::Expire(double a_nowMonotonic)
	{
		for (std::size_t index = 0; index < _trades.size();) {
			const auto limit = _trades[index].accepted ? kIdleExpirySeconds : kRequestExpirySeconds;
			if (a_nowMonotonic - _trades[index].lastActivityMonotonic > limit) EndWithCooldown(index, "expired", a_nowMonotonic);
			else ++index;
		}
		std::erase_if(_cooldowns, [a_nowMonotonic](const auto& a_entry) { return a_nowMonotonic >= a_entry.second; });
	}

	void Book::EndWithCooldown(std::size_t a_index, const std::string& a_reason, double a_nowMonotonic)
	{
		const auto& trade = _trades[a_index];
		_cooldowns[PairKey(trade.initiator.playerId, trade.target.playerId)] = a_nowMonotonic + kPairCooldownSeconds;
		EndAt(_trades, _ended, a_index, a_reason);
	}

	const Trade* Book::Find(const std::string& a_tradeId) const
	{
		const auto index = IndexOf(_trades, a_tradeId);
		return index ? &_trades[*index] : nullptr;
	}

	const Trade* Book::ActiveFor(std::uint32_t a_playerId) const
	{
		const auto index = ActiveIndexFor(_trades, a_playerId);
		return index ? &_trades[*index] : nullptr;
	}

	const std::vector<Trade>& Book::Trades() const noexcept
	{
		return _trades;
	}

	std::vector<Ended> Book::TakeEnded()
	{
		return std::exchange(_ended, {});
	}
}

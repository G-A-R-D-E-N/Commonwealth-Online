#include "ServerCharacterSlots.h"

#include "DurableStateFileStore.h"
#include "OpenSslTokenService.h"
#include "PersistenceGate.h"
#include "ServerProtocolImpl.h"

#include "F4TCharacterSlots.h"

#include <iostream>

namespace CommonwealthOnline::Server
{
	std::optional<std::vector<OutboundFrame>> ProtocolHost::Impl::DispatchCharacterSlotPacket(
		const std::string& a_type,
		std::uint32_t a_connectionId,
		Session& a_session,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		if (!a_type.starts_with("character")) {
			return std::nullopt;
		}
		CharacterSlotService::SessionMatches matches = [&](std::uint32_t a_other, std::string_view a_account) {
			const auto session = sessions.find(a_other);
			return session != sessions.end() && session->second.profileId == a_account;
			};
		auto frames = characterSlots.Dispatch(
			a_type, a_connectionId, a_session.profileId, a_packet,
			a_monotonicSeconds, a_unixTimeSeconds, matches, a_session.characterSlotRequests);
		if (frames) AppendFrames(*frames, ForgetErasedProfiles(a_unixTimeSeconds));
		return frames;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::ForgetErasedProfiles(double a_unixTimeSeconds)
	{
		std::vector<OutboundFrame> frames;
		const auto erased = characterSlots.TakeErasedProfiles();
		if (!socialGraph.Enabled() || erased.empty()) return frames;
		for (const auto& profileId : erased) {
			// Constraint: online ex-friends must learn the friendship ended before it is erased.
			AppendFrames(frames, friendService.ForgetProfileFrames(profileId, sessions, a_unixTimeSeconds));
			socialGraph.ForgetProfile(profileId);
		}
		(void)socialGraph.SaveNow();
		return frames;
	}

	namespace
	{
		using F4T::CharacterSlotProtocol::MakeDeleteChallenge;
		using F4T::CharacterSlotProtocol::MakeResult;
		using F4T::CharacterSlots::AccountKey;
		using F4T::CharacterSlots::IsValidSlot;
		using F4T::CharacterSlots::IsValidSlotCount;
		using F4T::CharacterSlots::SlotProfileKey;

		constexpr double kDeleteTokenWindowSeconds = 120.0;
		constexpr std::uint32_t kDeleteTokenBytes = 16;
		constexpr double kPendingSelectionSeconds = 60.0;

		[[nodiscard]] std::string_view OpForType(std::string_view a_type) noexcept
		{
			if (a_type == "characterList") return "list";
			if (a_type == "characterCreate") return "create";
			if (a_type == "characterSelect") return "select";
			if (a_type == "characterDelete") return "delete";
			if (a_type == "characterRestore") return "restore";
			return {};
		}

		[[nodiscard]] bool SlotAwareHello(const nlohmann::json& a_hello)
		{
			const auto found = a_hello.find("slotProtocol");
			// Constraint: read wide; a widened value is legacy, never a throw.
			return found != a_hello.end() && found->is_number_unsigned() && found->get<std::uint64_t>() == 1;
		}
	}

	bool CharacterSlotService::Attach(PersistenceGate* a_persistence, const Config& a_config)
	{
		persistence = a_persistence;
		if (persistence == nullptr) {
			return false;
		}
		if (!IsValidSlotCount(a_config.slotCount) || a_config.retentionDays < 1 || a_config.retentionDays > 90) {
			persistence = nullptr;
			return false;
		}
		slotCount = a_config.slotCount;
		retentionSeconds = static_cast<double>(a_config.retentionDays) * 86400.0;
		std::string conflict;
		if (!persistence->Store().MigrateCharacterSlotsToSlotZero(conflict)) {
			std::cerr << "Character slots unavailable: legacy profile migration refused"
				<< (conflict.empty() ? std::string{} : " (conflicting profile '" + conflict + "')") << ".\n";
			persistence = nullptr;
			return false;
		}
		return true;
	}

	CharacterSlotService::HelloOutcome CharacterSlotService::OnHello(
		std::uint32_t a_connectionId,
		std::string_view a_sessionProfileId,
		const Json& a_helloPacket,
		double a_monotonicSeconds,
		double a_unixTimeSeconds,
		const SessionMatches& a_sessionMatches)
	{
		HelloOutcome outcome;
		if (persistence == nullptr) {
			return outcome;
		}
		const auto account = AccountOf(a_sessionProfileId);
		if (!account) {
			return outcome;
		}
		outcome.account = *account;
		PurgeExpired(*account, a_unixTimeSeconds, a_sessionMatches);
		std::erase_if(pendingSelections, [&](const auto& entry) { return entry.second.expiresMonotonic <= a_monotonicSeconds; });
		const bool slotAware = SlotAwareHello(a_helloPacket);
		if (const auto slot = HelloSlot(*account, slotAware)) {
			outcome.persistenceProfile = SlotProfileKey(*account, *slot);
			persistence->RebindConnectionProfile(a_connectionId, outcome.persistenceProfile);
			activeSelections.insert_or_assign(*account, Selection{ a_connectionId, *slot });
		} else {
			activeSelections.erase(*account);
		}
		outcome.readyBlock = ReadyBlock(*account, a_connectionId, slotAware, a_unixTimeSeconds);
		return outcome;
	}

	std::vector<std::string> CharacterSlotService::TakeErasedProfiles()
	{
		std::vector<std::string> erased;
		erased.swap(erasedProfiles);
		return erased;
	}

	void CharacterSlotService::ReleaseConnection(std::uint32_t a_connectionId)
	{
		if (persistence == nullptr) {
			return;
		}
		persistence->ConnectionClosed(a_connectionId);
		std::erase_if(activeSelections, [&](const auto& entry) { return entry.second.connectionId == a_connectionId; });
	}

	CharacterSlotService::Json CharacterSlotService::ReadyBlock(
		std::string_view a_account,
		std::uint32_t a_connectionId,
		bool a_slotAware,
		double a_unixTimeSeconds)
	{
		(void)a_unixTimeSeconds;
		Json block{ { "supported", true }, { "slots", slotCount } };
		if (persistence == nullptr) {
			block["supported"] = false;
			return block;
		}
		const auto stored = activeSelections.find(a_account);
		const bool mine = stored != activeSelections.end() && stored->second.connectionId == a_connectionId;
		block["bound"] = mine;
		if (mine) {
			block["selected"] = stored->second.slot;
		}
		if (!a_slotAware) {
			block["legacy"] = true;
			block["notice"] = mine
				? "Multi-character slots are active: update the mod to create and switch characters."
				: "Multi-character slots are active, but no character is loaded for this client: update the mod to create, select or restore one.";
		}
		return block;
	}

	std::optional<std::vector<OutboundFrame>> CharacterSlotService::Dispatch(
		std::string_view a_type,
		std::uint32_t a_connectionId,
		std::string_view a_sessionProfileId,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds,
		const SessionMatches& a_sessionMatches,
		F4T::PacketRateLimit::TokenBucket& a_bucket)
	{
		const auto op = OpForType(a_type);
		if (op.empty()) {
			return std::nullopt;
		}
		std::erase_if(pendingDeletes, [&](const auto& entry) { return entry.second.expiresMonotonic <= a_monotonicSeconds; });
		const auto account = AccountOf(a_sessionProfileId);
		if (persistence == nullptr || !account) {
			return Refuse(a_connectionId, op, "slotsUnavailable", false, 0);
		}
		if (!F4T::PacketRateLimit::TakeToken(a_bucket, a_monotonicSeconds)) {
			return Refuse(a_connectionId, op, "rateLimited", false, 0);
		}
		PurgeExpired(*account, a_unixTimeSeconds, a_sessionMatches);

		std::uint32_t slot = 0;
		std::string name;
		bool hasToken = false;
		std::string token;
		if (a_type == "characterCreate") {
			const auto create = F4T::CharacterSlotProtocol::NormalizeCreate(a_packet);
			if (!create) {
				return Refuse(a_connectionId, op, "invalidRequest", false, 0);
			}
			slot = create->slot;
			name = create->name;
		} else if (a_type == "characterDelete") {
			const auto parsed = F4T::CharacterSlotProtocol::NormalizeDelete(a_packet);
			if (!parsed) {
				return Refuse(a_connectionId, op, "invalidRequest", false, 0);
			}
			slot = parsed->slot;
			hasToken = parsed->hasConfirmToken;
			token = parsed->confirmToken;
		} else if (a_type != "characterList") {
			const auto selected = F4T::CharacterSlotProtocol::NormalizeSlot(a_packet);
			if (!selected) {
				return Refuse(a_connectionId, op, "invalidRequest", false, 0);
			}
			slot = *selected;
		}

		if (a_type != "characterList" && !IsValidSlot(slot, slotCount)) {
			return Refuse(a_connectionId, op, "badSlot", true, slot);
		}

		auto& store = persistence->Store();
		const auto records = SlotRecords(*account);

		if (a_type == "characterList") {
			Json slots = Json::array();
			for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(slotCount); ++i) {
				const auto found = records.find(i);
				const bool deleted = found != records.end() && found->second.contains("deletedAt");
				bool occupied = found != records.end();
				if (!occupied) {
					try {
						occupied = store.GetPlayerState(SlotProfileKey(*account, i)).has_value();
					} catch (const std::exception&) {
					}
				}
				if (deleted) {
					Json entry{ { "slot", i }, { "empty", true }, { "deleted", true } };
					if (const auto stamp = found->second.find("deletedAt"); stamp != found->second.end() && stamp->is_number()) {
						entry["deletedAt"] = *stamp;
						entry["restorableUntil"] = stamp->get<double>() + retentionSeconds;
					}
					slots.push_back(std::move(entry));
					continue;
				}
				if (!occupied) {
					slots.push_back(Json{ { "slot", i }, { "empty", true } });
					continue;
				}
				const Json& record = (found != records.end()) ? found->second : Json{};
				Json entry{ { "slot", i }, { "name", SlotName(*account, i, record) } };
				if (const auto created = record.find("createdAt"); created != record.end()) {
					entry["createdAt"] = *created;
				}
				if (const auto played = record.find("lastPlayedAt"); played != record.end()) {
					entry["lastPlayedAt"] = *played;
				}
				slots.push_back(std::move(entry));
			}
			Json state{ { "type", "characterListState" }, { "slots", std::move(slots) } };
			if (const auto active = ActiveSelection(*account, a_sessionMatches)) {
				if (active->connectionId == a_connectionId) {
					state["selectedSlot"] = active->slot;
				}
			}
			return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, state) };
		}

		const auto key = SlotProfileKey(*account, slot);
		const auto recordIt = records.find(slot);
		const bool occupied = recordIt != records.end() || store.GetPlayerState(key).has_value();
		const bool deleted = recordIt != records.end() && recordIt->second.contains("deletedAt");

		if (a_type == "characterCreate") {
			std::size_t live = 0;
			for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(slotCount); ++i) {
				if (IsLiveSlot(*account, i)) {
					++live;
				}
			}
			if (live >= slotCount) {
				return Refuse(a_connectionId, op, "slotsFull", true, slot);
			}
			if (deleted) {
				return Refuse(a_connectionId, op, "slotDeleted", true, slot);
			}
			if (occupied) {
				return Refuse(a_connectionId, op, "slotOccupied", true, slot);
			}
			if (NameTaken(*account, name, slot)) {
				return Refuse(a_connectionId, op, "nameTaken", true, slot);
			}
			try {
				// Ordering: record before the player state.
				if (!PersistRecord(*account, slot, Json{ { "name", name }, { "createdAt", a_unixTimeSeconds }, { "lastPlayedAt", a_unixTimeSeconds } })) {
					return Refuse(a_connectionId, op, "persistenceUnavailable", true, slot);
				}
				if (!store.UpdatePlayerState(key, Json{ { "characterName", name } })) {
					return Refuse(a_connectionId, op, "persistenceUnavailable", true, slot);
				}
			} catch (const std::exception&) {
				return Refuse(a_connectionId, op, "persistenceUnavailable", true, slot);
			}
			LogEvent(*account, slot, "created", name);
			return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, MakeResult(op, true, {}, slot, true)) };
		}

		if (a_type == "characterSelect") {
			if (!occupied) {
				return Refuse(a_connectionId, op, "slotEmpty", true, slot);
			}
			if (deleted) {
				return Refuse(a_connectionId, op, "slotDeleted", true, slot);
			}
			const auto active = ActiveSelection(*account, a_sessionMatches);
			if (active && active->connectionId != a_connectionId) {
				return Refuse(a_connectionId, op, "alreadyActive", true, slot);
			}
			if (!active || active->slot != slot) {
				try {
					(void)persistence->FlushPendingPlayerStates(true);
					Json record = (recordIt != records.end()) ? recordIt->second : Json::object();
					record["lastPlayedAt"] = a_unixTimeSeconds;
					if (!PersistRecord(*account, slot, record)) {
						return Refuse(a_connectionId, op, "persistenceUnavailable", true, slot);
					}
				} catch (const std::exception&) {
					return Refuse(a_connectionId, op, "persistenceUnavailable", true, slot);
				}
			}
			pendingSelections.insert_or_assign(*account, PendingSelection{ slot, a_monotonicSeconds + kPendingSelectionSeconds });
			Json selected = MakeResult(op, true, {}, slot, true);
			selected["needsReconnect"] = true;
			return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, selected) };
		}

		if (a_type == "characterRestore") {
			if (!deleted) {
				return Refuse(a_connectionId, op, occupied ? "notDeleted" : "slotEmpty", true, slot);
			}
			try {
				Json record = recordIt->second;
				record.erase("deletedAt");
				record["lastPlayedAt"] = a_unixTimeSeconds;
				if (!PersistRecord(*account, slot, record)) {
					return Refuse(a_connectionId, op, "persistenceUnavailable", true, slot);
				}
			} catch (const std::exception&) {
				return Refuse(a_connectionId, op, "persistenceUnavailable", true, slot);
			}
			LogEvent(*account, slot, "restored", {});
			return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, MakeResult(op, true, {}, slot, true)) };
		}

		const auto connectionProfile = persistence->ProfileForConnection(a_connectionId);
		if (connectionProfile && *connectionProfile == key) {
			return Refuse(a_connectionId, op, "slotActive", true, slot);
		}

		if (!hasToken) {
			if (!occupied) {
				return Refuse(a_connectionId, op, "slotEmpty", true, slot);
			}
			if (deleted) {
				return Refuse(a_connectionId, op, "slotDeleted", true, slot);
			}
			if (const auto active = ActiveSelection(*account, a_sessionMatches)) {
				if (active->slot == slot && active->connectionId == a_connectionId) {
					return Refuse(a_connectionId, op, "slotActive", true, slot);
				}
				if (active->connectionId != a_connectionId) {
					return Refuse(a_connectionId, op, "alreadyActive", true, slot);
				}
			}
			std::erase_if(pendingDeletes, [&](const auto& entry) { return entry.second.account == *account && entry.second.slot == slot; });
			const auto issued = tokens.CreateRandomHex(kDeleteTokenBytes);
			pendingDeletes.insert_or_assign(issued, PendingDelete{ *account, slot, a_connectionId, a_monotonicSeconds + kDeleteTokenWindowSeconds });
			LogEvent(*account, slot, "delete-challenge", {});
			return std::vector<OutboundFrame>{ ReliableFrame(
				a_connectionId, MakeDeleteChallenge(slot, issued, static_cast<std::uint32_t>(kDeleteTokenWindowSeconds))) };
		}
		const auto found = pendingDeletes.find(token);
		if (found == pendingDeletes.end()) {
			return Refuse(a_connectionId, op, "badToken", true, slot);
		}
		const PendingDelete pending = found->second;
		pendingDeletes.erase(found);
		if (pending.account != *account || pending.slot != slot || pending.connectionId != a_connectionId) {
			return Refuse(a_connectionId, op, "badToken", true, slot);
		}
		if (const auto active = ActiveSelection(*account, a_sessionMatches)) {
			if (active->connectionId != a_connectionId) {
				return Refuse(a_connectionId, op, "alreadyActive", true, slot);
			}
			if (active->slot == slot) {
				return Refuse(a_connectionId, op, "slotActive", true, slot);
			}
		}
		const auto fresh = SlotRecords(*account);
		const auto freshIt = fresh.find(slot);
		const bool freshDeleted = freshIt != fresh.end() && freshIt->second.contains("deletedAt");
		if (freshDeleted || (freshIt == fresh.end() && !store.GetPlayerState(key).has_value())) {
			return Refuse(a_connectionId, op, "slotEmpty", true, slot);
		}
		try {
			Json record = (freshIt != fresh.end()) ? freshIt->second : Json::object();
			record["deletedAt"] = a_unixTimeSeconds;
			if (!PersistRecord(*account, slot, record)) {
				return Refuse(a_connectionId, op, "persistenceUnavailable", true, slot);
			}
		} catch (const std::exception&) {
			return Refuse(a_connectionId, op, "persistenceUnavailable", true, slot);
		}
		LogEvent(*account, slot, "deleted", {});
		return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, MakeResult(op, true, {}, slot, true)) };
	}

	std::optional<std::string> CharacterSlotService::AccountOf(std::string_view a_sessionProfileId) const
	{
		if (persistence == nullptr) {
			return std::nullopt;
		}
		return AccountKey(a_sessionProfileId);
	}

	void CharacterSlotService::PurgeExpired(
		std::string_view a_account,
		double a_unixTimeSeconds,
		const SessionMatches& a_sessionMatches)
	{
		if (persistence == nullptr) {
			return;
		}
		auto& store = persistence->Store();
		const auto active = ActiveSelection(a_account, a_sessionMatches);
		for (const auto& [slot, record] : SlotRecords(a_account)) {
			const auto deleted = record.find("deletedAt");
			if (deleted == record.end() || !deleted->is_number()) {
				continue;
			}
			if (!(a_unixTimeSeconds - deleted->get<double>() >= retentionSeconds)) {
				continue;
			}
			if (active && active->slot == slot) {
				continue;
			}
			try {
				const auto key = SlotProfileKey(a_account, slot);
				// Constraint: keep the account's graph entry while another slot still has state.
				const bool keepParent = HasRemainingSlotState(a_account, slot);
				// Ordering: profile before record.
				const bool profileErased = store.EraseProfileByKey(key, !keepParent);
				const bool recordErased = store.EraseCharacterSlotRecord(a_account, slot);
				// Constraint: forget the graph even when the store had nothing to erase.
				if (!keepParent) erasedProfiles.emplace_back(key);
				if (profileErased && recordErased) {
					LogEvent(a_account, slot, "purged", {});
				} else {
					LogEvent(a_account, slot, "purge-retry", {});
				}
			} catch (const std::exception&) {
			}
		}
		for (const auto& [slot, record] : SlotRecords(a_account)) {
			if (record.contains("deletedAt")) {
				continue;
			}
			bool hasState = false;
			try {
				hasState = store.GetPlayerState(SlotProfileKey(a_account, slot)).has_value();
			} catch (const std::exception&) {
				continue;
			}
			if (!hasState) {
				try {
					if (store.EraseCharacterSlotRecord(a_account, slot)) {
						LogEvent(a_account, slot, "orphan-record", {});
					}
				} catch (const std::exception&) {
				}
			}
		}
	}

	std::optional<CharacterSlotService::Selection> CharacterSlotService::ActiveSelection(
		std::string_view a_account,
		const SessionMatches& a_sessionMatches) const
	{
		const auto found = activeSelections.find(a_account);
		if (found == activeSelections.end()) {
			return std::nullopt;
		}
		if (a_sessionMatches && !a_sessionMatches(found->second.connectionId, a_account)) {
			return std::nullopt;
		}
		return found->second;
	}

	bool CharacterSlotService::HasRemainingSlotState(std::string_view a_account, std::uint32_t a_exceptSlot) const
	{
		const auto records = SlotRecords(a_account);
		for (std::uint32_t slot = 0; slot < static_cast<std::uint32_t>(slotCount); ++slot) {
			if (slot == a_exceptSlot) continue;
			if (IsLiveSlot(a_account, slot)) return true;
			// Constraint: a soft-deleted sibling still holds restorable playerState.
			const auto found = records.find(slot);
			if (found == records.end() || !found->second.contains("deletedAt")) continue;
			try {
				if (persistence->Store().GetPlayerState(SlotProfileKey(a_account, slot)).has_value()) return true;
			} catch (const std::exception&) {
			}
		}
		return false;
	}

	bool CharacterSlotService::IsLiveSlot(std::string_view a_account, std::uint32_t a_slot) const
	{
		if (persistence == nullptr || !IsValidSlot(a_slot, slotCount)) {
			return false;
		}
		try {
			const auto records = persistence->Store().GetCharacterSlotRecords(a_account);
			const auto found = records.find(a_slot);
			if (found != records.end()) {
				return !found->second.contains("deletedAt");
			}
			return persistence->Store().GetPlayerState(SlotProfileKey(a_account, a_slot)).has_value();
		} catch (const std::exception&) {
			return false;
		}
	}

	std::optional<std::uint32_t> CharacterSlotService::LastSelectedLiveSlot(std::string_view a_account) const
	{
		const auto stored = activeSelections.find(a_account);
		if (stored != activeSelections.end() && IsLiveSlot(a_account, stored->second.slot)) {
			return stored->second.slot;
		}
		const auto records = SlotRecords(a_account);
		std::optional<std::uint32_t> best;
		double bestPlayed{};
		for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(slotCount); ++i) {
			if (!IsLiveSlot(a_account, i)) {
				continue;
			}
			double played = -1.0;
			const auto found = records.find(i);
			if (found != records.end()) {
				if (const auto stamp = found->second.find("lastPlayedAt"); stamp != found->second.end() && stamp->is_number()) {
					played = stamp->get<double>();
				}
			}
			if (!best || played > bestPlayed) {
				best = i;
				bestPlayed = played;
			}
		}
		return best;
	}

	std::optional<std::uint32_t> CharacterSlotService::HelloSlot(std::string_view a_account, bool a_slotAware)
	{
		if (a_slotAware) {
			const auto pending = pendingSelections.find(a_account);
			if (pending == pendingSelections.end()) {
				return std::nullopt;
			}
			const auto slot = pending->second.slot;
			pendingSelections.erase(pending);
			if (!IsLiveSlot(a_account, slot)) {
				return std::nullopt;
			}
			return slot;
		}
		if (IsLiveSlot(a_account, F4T::CharacterSlots::FirstSlot)) {
			return F4T::CharacterSlots::FirstSlot;
		}
		const auto records = SlotRecords(a_account);
		const auto first = records.find(F4T::CharacterSlots::FirstSlot);
		if (first != records.end() && first->second.contains("deletedAt")) {
			return std::nullopt;
		}
		if (const auto live = LastSelectedLiveSlot(a_account)) {
			return live;
		}
		if (records.empty()) {
			return F4T::CharacterSlots::FirstSlot;
		}
		return std::nullopt;
	}

	std::map<std::uint32_t, CharacterSlotService::Json> CharacterSlotService::SlotRecords(std::string_view a_account) const
	{
		if (persistence == nullptr) {
			return {};
		}
		try {
			return persistence->Store().GetCharacterSlotRecords(a_account);
		} catch (const std::exception&) {
			return {};
		}
	}

	std::vector<OutboundFrame> CharacterSlotService::Refuse(
		std::uint32_t a_connectionId,
		std::string_view a_op,
		std::string_view a_reason,
		bool a_hasSlot,
		std::uint32_t a_slot) const
	{
		return { ReliableFrame(a_connectionId, MakeResult(a_op, false, a_reason, a_slot, a_hasSlot)) };
	}

	std::string CharacterSlotService::SlotName(std::string_view a_account, std::uint32_t a_slot, const Json& a_record) const
	{
		if (persistence != nullptr) {
			try {
				const auto state = persistence->Store().GetPlayerState(SlotProfileKey(a_account, a_slot));
				if (state) {
					const auto name = state->find("characterName");
					if (name != state->end() && name->is_string() && !name->get_ref<const std::string&>().empty()) {
						return name->get<std::string>();
					}
				}
			} catch (const std::exception&) {
			}
		}
		const auto name = a_record.find("name");
		if (name != a_record.end() && name->is_string()) {
			return name->get<std::string>();
		}
		return {};
	}

	bool CharacterSlotService::NameTaken(std::string_view a_account, std::string_view a_name, std::uint32_t a_exceptSlot) const
	{
		for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(slotCount); ++i) {
			if (i == a_exceptSlot) {
				continue;
			}
			const auto key = SlotProfileKey(std::string{ a_account }, i);
			try {
				const auto state = persistence->Store().GetPlayerState(key);
				if (state) {
					const auto name = state->find("characterName");
					if (name != state->end() && name->is_string() &&
						F4T::CharacterSlotProtocol::NamesEqual(name->get_ref<const std::string&>(), a_name)) {
						return true;
					}
				}
			} catch (const std::exception&) {
			}
		}
		for (const auto& [slot, record] : SlotRecords(a_account)) {
			if (slot == a_exceptSlot) {
				continue;
			}
			const auto name = record.find("name");
			if (name != record.end() && name->is_string() && F4T::CharacterSlotProtocol::NamesEqual(name->get_ref<const std::string&>(), a_name)) {
				return true;
			}
		}
		return false;
	}

	bool CharacterSlotService::PersistRecord(std::string_view a_account, std::uint32_t a_slot, const Json& a_record) const
	{
		return persistence->Store().SetCharacterSlotRecord(a_account, a_slot, a_record);
	}

	void CharacterSlotService::LogEvent(
		std::string_view a_account,
		std::uint32_t a_slot,
		std::string_view a_event,
		std::string_view a_detail) const
	{
		std::cerr << "CharacterSlots[" << a_account << "#" << a_slot << "] " << a_event;
		if (!a_detail.empty()) {
			std::cerr << " " << a_detail;
		}
		std::cerr << '\n';
	}
}

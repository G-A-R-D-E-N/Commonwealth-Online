#include "ServerCharacterSlots.h"

#include "DurableStateFileStore.h"
#include "PersistenceGate.h"
#include "ServerProtocolImpl.h"

#include "F4TCharacterSlotProtocol.h"
#include "F4TCharacterSlots.h"
#include "F4TDurableState.h"
#include "F4TSaveProfileLogic.h"

#include <exception>

namespace CommonwealthOnline::Server
{
	std::vector<OutboundFrame> CharacterSlotService::AbandonBound(
		std::uint32_t a_connectionId,
		std::string_view a_sessionProfileId,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds,
		F4T::PacketRateLimit::TokenBucket& a_bucket)
	{
		constexpr std::string_view op = "abandon";
		const auto account = AccountOf(a_sessionProfileId);
		if (persistence == nullptr || !account) {
			return Refuse(a_connectionId, op, "slotsUnavailable", false, 0);
		}
		if (!F4T::PacketRateLimit::TakeToken(a_bucket, a_monotonicSeconds)) {
			return Refuse(a_connectionId, op, "rateLimited", false, 0);
		}
		if (a_packet.find("slot") == a_packet.end()) {
			return AbandonUnstarted(a_connectionId, *account, op, a_unixTimeSeconds);
		}
		const auto requested = F4T::CharacterSlotProtocol::NormalizeSlot(a_packet);
		if (!requested) {
			return Refuse(a_connectionId, op, "invalidRequest", false, 0);
		}
		if (!F4T::CharacterSlots::IsValidSlot(*requested, slotCount)) {
			return Refuse(a_connectionId, op, "badSlot", true, *requested);
		}
		const auto slotKey = F4T::CharacterSlots::SlotProfileKey(*account, *requested);
		bool hasRecord = false;
		std::optional<Json> grant;
		std::optional<Json> inventory;
		std::optional<Json> playerState;
		try {
			auto& store = persistence->Store();
			(void)persistence->FlushPendingPlayerStates(true);
			hasRecord = SlotRecords(*account).contains(*requested);
			grant = store.GetStartingGrant(slotKey);
			inventory = store.GetInventory(slotKey);
			playerState = store.GetPlayerState(slotKey);
		} catch (const std::exception&) {
			return Refuse(a_connectionId, op, "persistenceUnavailable", true, *requested);
		}
		if (!hasRecord && !grant && !inventory && !playerState) {
			return Refuse(a_connectionId, op, "slotEmpty", true, *requested);
		}
		const auto bound = persistence->ProfileForConnection(a_connectionId);
		const auto key = bound ? F4T::CharacterSlots::ParseSlotKey(*bound) : std::nullopt;
		const bool accountBound = key && key->first == *account;
		if (!accountBound || key->second != *requested) {
			if (hasRecord || playerState || F4T::DurableState::HasCommittedFirstJoinState(grant, inventory, playerState)) {
				return accountBound ? Refuse(a_connectionId, op, "slotMismatch", true, *requested)
					: Refuse(a_connectionId, op, "notBound", false, 0);
			}
			if (!EraseSlot(*account, *requested)) {
				return Refuse(a_connectionId, op, "persistenceUnavailable", true, *requested);
			}
			LogEvent(*account, *requested, "abandoned-remnant", {});
			ClearAbandonConfirmation(*account);
			return { ReliableFrame(a_connectionId, F4T::CharacterSlotProtocol::MakeResult(op, true, {}, *requested, true)) };
		}
		const auto slot = key->second;
		if (F4T::DurableState::HasCommittedFirstJoinState(grant, inventory, playerState)) {
			return Refuse(a_connectionId, op, "slotCommitted", true, slot);
		}
		if (!EraseSlot(*account, slot)) {
			return Refuse(a_connectionId, op, "persistenceUnavailable", true, slot);
		}
		ReleaseConnection(a_connectionId);
		abandonedConnection = AbandonedConnection{ a_connectionId, *bound };
		LogEvent(*account, slot, "abandoned-uncommitted", {});
		ClearAbandonConfirmation(*account);
		return {
			ReliableFrame(a_connectionId, F4T::CharacterSlotProtocol::MakeResult(op, true, {}, slot, true)),
			ReliableFrame(a_connectionId, Json{
				{ "type", "sessionEnded" },
				{ "code", "character_abandoned" },
				{ "reason", "The unfinished first character had no saved progress and was discarded. Reconnect to create a character." },
				{ "serverTime", a_unixTimeSeconds } }) };
	}

	std::vector<OutboundFrame> CharacterSlotService::AbandonUnstarted(
		std::uint32_t a_connectionId,
		std::string_view a_account,
		std::string_view a_op,
		double a_unixTimeSeconds)
	{
		(void)a_unixTimeSeconds;
		try {
			auto& store = persistence->Store();
			(void)persistence->FlushPendingPlayerStates(true);
			std::optional<std::uint32_t> confirmed;
			for (const auto& mark : store.GetFirstJoinAbandonedSlots(a_account)) {
				const auto key = F4T::CharacterSlots::SlotProfileKey(a_account, mark.slot);
				const bool emptied = !SlotRecords(a_account).contains(mark.slot) && !store.GetStartingGrant(key) &&
					!store.GetInventory(key) && !store.GetPlayerState(key);
				if (!emptied) {
					(void)store.DropFirstJoinAbandonedSlot(a_account, mark.slot);
					continue;
				}
				if (!confirmed || mark.slot < *confirmed) confirmed = mark.slot;
			}
			const auto records = SlotRecords(a_account);
			std::optional<std::uint32_t> onlySlot;
			std::size_t statedSlots = 0;
			bool onlySlotIsLeftover = false;
			for (std::uint32_t slot = 0; slot < static_cast<std::uint32_t>(slotCount); ++slot) {
				const auto key = F4T::CharacterSlots::SlotProfileKey(a_account, slot);
				const auto grant = store.GetStartingGrant(key);
				const auto inventory = store.GetInventory(key);
				const auto playerState = store.GetPlayerState(key);
				const bool committed = F4T::DurableState::HasCommittedFirstJoinState(grant, inventory, playerState);
				const bool occupied = records.contains(slot) || playerState || committed;
				if (!occupied && !grant && !inventory) continue;
				++statedSlots;
				onlySlot = slot;
				onlySlotIsLeftover = !occupied;
			}
			if (confirmed && statedSlots == 0) {
				LogEvent(a_account, *confirmed, "abandoned-admin-confirmed", {});
				return { ReliableFrame(a_connectionId, F4T::CharacterSlotProtocol::MakeResult(a_op, true, {}, *confirmed, true)) };
			}
			if (!confirmed && statedSlots == 1 && onlySlotIsLeftover) {
				if (persistence->ProfileInUseByOtherConnection(F4T::CharacterSlots::SlotProfileKey(a_account, *onlySlot), a_connectionId)) {
					return Refuse(a_connectionId, a_op, "rateLimited", false, 0);
				}
				if (!EraseSlot(a_account, *onlySlot)) {
					return Refuse(a_connectionId, a_op, "persistenceUnavailable", false, 0);
				}
				LogEvent(a_account, *onlySlot, "abandoned-unstarted", {});
				return { ReliableFrame(a_connectionId, F4T::CharacterSlotProtocol::MakeResult(a_op, true, {}, *onlySlot, true)) };
			}
			return statedSlots == 0 ? Refuse(a_connectionId, a_op, "slotEmpty", false, 0)
				: Refuse(a_connectionId, a_op, "notBound", false, 0);
		} catch (const std::exception&) {
			return Refuse(a_connectionId, a_op, "persistenceUnavailable", false, 0);
		}
	}

	std::optional<CharacterSlotService::AbandonedConnection> CharacterSlotService::TakeAbandonedConnection()
	{
		const auto taken = abandonedConnection;
		abandonedConnection.reset();
		return taken;
	}

	bool CharacterSlotService::AbandonForAdmin(std::string_view a_account, std::uint32_t a_slot, double a_unixTimeSeconds, const SessionMatches& a_sessionMatches, bool a_withoutSave)
	{
		if (persistence == nullptr || !F4T::CharacterSlots::IsValidSlot(a_slot, slotCount)) {
			return false;
		}
		const auto active = ActiveSelection(a_account, a_sessionMatches);
		if (active && active->slot == a_slot) {
			return false;
		}
		const auto key = F4T::CharacterSlots::SlotProfileKey(a_account, a_slot);
		try {
			auto& store = persistence->Store();
			(void)persistence->FlushPendingPlayerStates(true);
			const auto grant = store.GetStartingGrant(key);
			const auto inventory = store.GetInventory(key);
			if (a_withoutSave) {
				const Json* recovery = grant && grant->contains("recovery") ? &grant->at("recovery") : nullptr;
				if (recovery && recovery->is_object() && recovery->contains("characterId") && recovery->at("characterId").is_string()) {
					const auto& characterId = recovery->at("characterId").get_ref<const std::string&>();
					for (const auto& save : store.GetSaveAttestations(a_account)) {
						const auto saved = F4T::SaveProfileLogic::ExtractCharacterId(save.saveFile);
						if (saved && F4T::SaveProfileLogic::CharacterIdsEqual(*saved, characterId)) {
							return false;
						}
					}
				}
			} else {
				if (grant && (!grant->contains("recovery") || grant->at("recovery") != Json{ { "state", "unstarted" } })) {
					return false;
				}
				if (inventory && inventory->at("revision") != 0) {
					return false;
				}
			}
			const bool exists = SlotRecords(a_account).contains(a_slot) || grant || inventory || store.GetPlayerState(key);
			if (!exists) {
				return false;
			}
		} catch (const std::exception&) {
			return false;
		}
		try {
			if (!persistence->Store().SetFirstJoinAbandonedSlot(a_account, a_slot, a_unixTimeSeconds)) {
				return false;
			}
		} catch (const std::exception&) {
			return false;
		}
		if (!EraseSlot(a_account, a_slot, true)) {
			return false;
		}
		LogEvent(a_account, a_slot, "abandoned-admin", {});
		return true;
	}

	std::optional<std::vector<OutboundFrame>> ProtocolHost::Impl::AbandonSlotForAdmin(
		std::string_view a_account,
		std::uint32_t a_slot,
		double a_unixTimeSeconds,
		bool a_withoutSave)
	{
		if (!characterSlots.AbandonForAdmin(a_account, a_slot, a_unixTimeSeconds, [this](std::uint32_t a_other, std::string_view a_profile) {
			const auto session = sessions.find(a_other);
			return session != sessions.end() && session->second.profileId == a_profile;
		}, a_withoutSave)) {
			return std::nullopt;
		}
		return ForgetErasedProfiles(a_unixTimeSeconds);
	}
}

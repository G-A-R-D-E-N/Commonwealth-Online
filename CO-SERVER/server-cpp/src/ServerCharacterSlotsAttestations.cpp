#include "ServerCharacterSlots.h"

#include "DurableStateFileStore.h"
#include "PersistenceGate.h"

#include "F4TCharacterSlots.h"

namespace CommonwealthOnline::Server
{
	std::vector<std::pair<std::uint32_t, std::string>> CharacterSlotService::SlotCharacterIds(std::string_view a_account) const
	{
		std::vector<std::pair<std::uint32_t, std::string>> result;
		for (std::uint32_t slot = 0; slot < static_cast<std::uint32_t>(slotCount); ++slot) {
			const auto grant = persistence->Store().GetStartingGrant(F4T::CharacterSlots::SlotProfileKey(a_account, slot));
			const auto recovery = grant && grant->contains("recovery") ? &grant->at("recovery") : nullptr;
			if (!recovery || !recovery->is_object() || !recovery->contains("characterId") || !recovery->at("characterId").is_string()) continue;
			auto characterId = recovery->at("characterId").get<std::string>();
			if (!characterId.empty()) result.emplace_back(slot, std::move(characterId));
		}
		return result;
	}

	void CharacterSlotService::ScopeLegacyAttestations(std::string_view a_account)
	{
		(void)persistence->Store().ScopeLegacySaveAttestations(a_account, SlotCharacterIds(a_account));
	}

	void CharacterSlotService::DropSlotAttestations(std::string_view a_account, std::uint32_t a_slot)
	{
		(void)persistence->Store().EraseSlotSaveAttestations(a_account, a_slot);
	}
}

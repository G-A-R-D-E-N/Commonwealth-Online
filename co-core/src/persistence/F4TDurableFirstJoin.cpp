#include "F4TDurableState.h"

namespace F4T::DurableState
{
	bool HasCommittedFirstJoinState(const std::optional<nlohmann::json>& a_grant,
		const std::optional<nlohmann::json>& a_inventory, const std::optional<nlohmann::json>& a_playerState,
		bool a_saveAttested)
	{
		if (a_grant && (!a_grant->contains("recovery") || a_grant->at("recovery") != nlohmann::json{ { "state", "unstarted" } })) return true;
		// Constraint: the revision counts only with an attestation that applies to this slot (unscoped applies to all).
		if (a_saveAttested && a_inventory && a_inventory->at("revision") != 0) return true;
		if (a_playerState) {
			for (const auto& entry : a_playerState->items()) {
				const auto& key = entry.key();
				if (key != "type" && key != "characterName" && key != "appearance" && key != "progression" && key != "equippedItems" && key != "powerArmor") return true;
			}
		}
		return false;
	}

	bool ScopeLegacySaveAttestations(nlohmann::json& a_root, std::string_view a_account,
		const SlotCharacterIds& a_slotCharacters, double a_updatedAt)
	{
		const auto attestations = GetSaveAttestations(a_root, a_account);
		if (attestations.empty()) return false;
		auto& profile = a_root.at("profiles").at(std::string{ a_account });
		auto& records = profile.at("saveAttestations");
		bool changed = false;
		for (std::size_t index = 0; index < attestations.size(); ++index) {
			if (attestations[index].slot) continue;
			const auto character = F4T::SaveProfileLogic::ExtractCharacterId(attestations[index].saveFile);
			if (!character) continue;
			std::optional<std::uint32_t> match;
			std::size_t matches = 0;
			for (const auto& [slot, characterId] : a_slotCharacters) {
				if (!F4T::SaveProfileLogic::CharacterIdsEqual(characterId, *character)) continue;
				match = slot;
				++matches;
			}
			if (matches != 1) continue;
			records[index]["slot"] = *match;
			changed = true;
		}
		if (changed) profile["updatedAt"] = a_updatedAt;
		return changed;
	}

	bool EraseSlotSaveAttestations(nlohmann::json& a_root, std::string_view a_account, std::uint32_t a_slot, double a_updatedAt)
	{
		const auto attestations = GetSaveAttestations(a_root, a_account);
		if (attestations.empty()) return false;
		auto& profile = a_root.at("profiles").at(std::string{ a_account });
		auto& records = profile.at("saveAttestations");
		nlohmann::json kept = nlohmann::json::array();
		for (std::size_t index = 0; index < attestations.size(); ++index) {
			if (attestations[index].slot != a_slot) kept.push_back(records[index]);
		}
		if (kept.size() == attestations.size()) return false;
		records = std::move(kept);
		profile["updatedAt"] = a_updatedAt;
		return true;
	}
}

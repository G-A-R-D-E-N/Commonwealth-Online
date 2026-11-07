#include "F4TDurableState.h"

namespace F4T::DurableState
{
	bool HasCommittedFirstJoinState(const std::optional<nlohmann::json>& a_grant,
		const std::optional<nlohmann::json>& a_inventory, const std::optional<nlohmann::json>& a_playerState)
	{
		if (a_grant && (!a_grant->contains("recovery") || a_grant->at("recovery") != nlohmann::json{ { "state", "unstarted" } })) return true;
		if (a_inventory && a_inventory->at("revision") != 0) return true;
		if (a_playerState) {
			for (const auto& entry : a_playerState->items()) {
				const auto& key = entry.key();
				if (key != "type" && key != "characterName" && key != "appearance" && key != "progression" && key != "equippedItems" && key != "powerArmor") return true;
			}
		}
		return false;
	}
}

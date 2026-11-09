#pragma once

#include <nlohmann/json.hpp>

#include <optional>

namespace F4T::PlayerStateValidation
{
	inline constexpr const char* kDurablePlayerStateFields[] = { "characterName", "appearance", "equippedItems", "powerArmor", "progression" };
	inline constexpr const char* kRelayedPlayerStateFields[] = { "characterName", "appearance", "equippedItems", "powerArmor" };
	// Constraint, record: CO-BETA/plugin/src/F4TLocalPlayerState.cpp:190 publishes "rightHand"; CO-BETA/plugin/src/F4TThrowableSync.cpp:73 publishes "throwable".
	inline constexpr const char* kRightHandSlot = "rightHand";
	inline constexpr const char* kThrowableSlot = "throwable";

	[[nodiscard]] std::optional<nlohmann::json> NormalizePlayerState(const nlohmann::json& a_packet);
}

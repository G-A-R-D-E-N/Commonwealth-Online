#pragma once

#include <nlohmann/json.hpp>

#include <string_view>

namespace F4T::Downed
{
	void Tick();

	[[nodiscard]] bool TryHandlePacket(std::string_view a_type, const nlohmann::json& a_packet);

	[[nodiscard]] float ResolveLocalPlayerDamage(float a_healthBefore, float a_maximumHealth, float a_damage);

	[[nodiscard]] bool IsLocalPlayerDowned();
	[[nodiscard]] bool IsLocalPlayerDead();

	// Game thread only.
	void OnWorldSaved();
	void OnWorldLoaded();
	[[nodiscard]] bool HasCheckpoint();

	void NoteWelcome();
}

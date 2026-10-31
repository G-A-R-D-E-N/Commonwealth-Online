#pragma once

#include <chrono>
#include <cstdint>

namespace RE
{
	class Actor;
	class PlayerCharacter;
}

namespace F4T::LocalAnimationGraphDebug
{
	void UpdateLocalPlayerAnimationGraphDebug(
		RE::PlayerCharacter& a_player,
		bool a_isMoving,
		bool a_isSprinting,
		bool a_isSneaking,
		bool a_isJumping,
		bool a_weaponDrawn,
		float a_movementSpeed,
		std::chrono::steady_clock::time_point a_currentTime);

	void UpdateProxyAnimationGraphReadOnlyDebug(
		RE::Actor& a_proxy,
		std::uint32_t a_remotePlayerId);
}

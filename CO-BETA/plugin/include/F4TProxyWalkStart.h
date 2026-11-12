#pragma once

#include <cstdint>
#include <optional>

namespace RE
{
	class Actor;
}

namespace F4T::ProxyWalkStart
{
	struct RetryState
	{
		bool pending{ false };
		std::uint32_t refusedTicks{ 0 };
		std::optional<bool> stanceResentFor{};
	};

	bool SendEvents(
		RE::Actor& a_proxy,
		bool a_isSneaking,
		std::uint32_t a_remotePlayerId,
		std::uint32_t a_attempt);

	void RetryPending(
		RetryState& a_state,
		RE::Actor& a_proxy,
		bool a_isMoving,
		bool a_isJumping,
		bool a_isSneaking,
		std::uint32_t a_remotePlayerId);
}

#pragma once

#include <chrono>
#include <optional>

namespace F4T::ProxyLocomotionStaleness
{
	inline constexpr auto kStaleLocomotionAge = std::chrono::milliseconds{ 500 };

	[[nodiscard]] inline bool IsLocomotionStale(
		std::chrono::steady_clock::time_point a_received,
		std::chrono::steady_clock::time_point a_now) noexcept
	{
		if (a_received == std::chrono::steady_clock::time_point{} || a_now < a_received) {
			return false;
		}

		return a_now - a_received >= kStaleLocomotionAge;
	}

	template <typename TRemote>
	void ZeroStaleLocomotion(TRemote& a_remote)
	{
		a_remote.isMoving = false;
		a_remote.isSprinting = false;
		a_remote.movementSpeed = 0.0F;
		a_remote.animationGraphSpeed = -1.0F;
	}

	template <typename TRemote>
	[[nodiscard]] bool IsLocomotionIdle(const TRemote& a_remote)
	{
		return !a_remote.isMoving && !a_remote.isSprinting && a_remote.movementSpeed == 0.0F &&
		       a_remote.animationGraphSpeed <= 0.0F;
	}

	template <typename TRemote>
	[[nodiscard]] const TRemote& SelectMovementView(
		const TRemote& a_remote,
		std::optional<TRemote>& a_storage,
		std::chrono::steady_clock::time_point a_now)
	{
		if (!IsLocomotionStale(a_remote.lastReceivedLocalTime, a_now) || IsLocomotionIdle(a_remote)) {
			return a_remote;
		}

		a_storage = a_remote;
		ZeroStaleLocomotion(*a_storage);
		return *a_storage;
	}
}

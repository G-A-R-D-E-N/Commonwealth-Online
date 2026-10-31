#pragma once

#include <chrono>
#include <span>

namespace F4T::ReplicationRuntime
{
	struct PlayerLocation;
	struct PlayerMovementState;
	struct PlayerTransform;
}

namespace F4T::RemotePlayerState
{
	struct RemoteActionEvent;
	struct RemoteAppearance;
	struct RemoteEquippedItem;
}

namespace F4T::PowerArmorSync
{
	struct PowerArmorState;
}

namespace F4T::LocalPlayerTransformSend
{
	inline constexpr auto kTransformSendInterval = std::chrono::milliseconds{ 50 };
	inline constexpr auto kMovingTransformSendTolerance = std::chrono::milliseconds{ 8 };

	struct TransformPublishState
	{
		bool bypassesThrottle{};
		bool hasThrottledChange{};
		bool withinSendInterval{};
		bool isMoving{};
		bool movingEdge{};
	};

	[[nodiscard]] inline bool IsWithinSendInterval(std::chrono::steady_clock::duration a_sinceLastSend, bool a_isMoving)
	{
		const auto interval = a_isMoving ? kTransformSendInterval - kMovingTransformSendTolerance : kTransformSendInterval;
		return a_sinceLastSend < interval;
	}

	[[nodiscard]] inline bool ShouldPublishTransform(const TransformPublishState& a_state)
	{
		if (a_state.bypassesThrottle || a_state.movingEdge) {
			return true;
		}

		return (a_state.hasThrottledChange || a_state.isMoving) && !a_state.withinSendInterval;
	}

	bool Send(
		const F4T::ReplicationRuntime::PlayerTransform& a_transform,
		const F4T::ReplicationRuntime::PlayerLocation& a_location,
		const F4T::ReplicationRuntime::PlayerMovementState& a_movement,
		const char* a_movementType,
		float a_animationGraphSpeed,
		std::span<const F4T::RemotePlayerState::RemoteEquippedItem> a_equippedItems,
		const F4T::RemotePlayerState::RemoteAppearance* a_appearance,
		std::span<const F4T::RemotePlayerState::RemoteActionEvent> a_actionEvents,
		const char* a_characterName,
		const F4T::PowerArmorSync::PowerArmorState* a_powerArmor);
}

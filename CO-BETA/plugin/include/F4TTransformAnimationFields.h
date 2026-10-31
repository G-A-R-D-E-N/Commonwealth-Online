#pragma once

#include "F4TProxyAnimationFieldClamps.h"

#include <nlohmann/json.hpp>

namespace F4T::TransformAnimationFields
{
	inline void WriteAnimationFields(
		nlohmann::json& a_packet,
		float a_animationDirectionDegrees,
		float a_aimPitchDegrees,
		float a_turnDeltaDegreesPerSecond)
	{
		a_packet["animationDirection"] =
			F4T::ProxyRemoteAnimationState::ClampAnimationDirectionDegrees(a_animationDirectionDegrees);
		a_packet["aimPitch"] = F4T::ProxyRemoteAnimationState::ClampAimPitchDegrees(a_aimPitchDegrees);
		a_packet["turnDelta"] =
			F4T::ProxyRemoteAnimationState::ClampTurnDeltaDegreesPerSecond(a_turnDeltaDegreesPerSecond);
	}
}

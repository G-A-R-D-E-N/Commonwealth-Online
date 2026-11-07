#pragma once

namespace F4T::ProxyPuppet
{
	struct ProxyLocomotionState
	{
		float speed{ -1.0F };
		float direction{ 0.0F };
		float velocityX{ 0.0F };
		float velocityY{ 0.0F };
		float velocityZ{ 0.0F };
		float aimPitchDegrees{ 0.0F };
		float turnDelta{ 0.0F };
		bool hasAimState{ false };
	};

	inline void ApplyLocomotionMotionFields(
		ProxyLocomotionState& a_state,
		float a_speed,
		float a_direction,
		float a_velocityX,
		float a_velocityY,
		float a_velocityZ)
	{
		a_state.speed = a_speed;
		a_state.direction = a_direction;
		a_state.velocityX = a_velocityX;
		a_state.velocityY = a_velocityY;
		a_state.velocityZ = a_velocityZ;
	}
}

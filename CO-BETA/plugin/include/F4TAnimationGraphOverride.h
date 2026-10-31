// License: GPL-3.0 with additional terms (FatherLamb); ported from FathersFallout client-ng/src/sync/GraphOverride.{h,cpp}.
#pragma once

namespace RE
{
	class Actor;
}

namespace F4T::AnimationGraphOverride
{
	bool Install(RE::Actor* a_anyBody);
	bool Installed();

	void DetachMovementChannels(RE::Actor* a_body);

	void SetProxySpeed(RE::Actor* a_body, float a_speed, float a_directionRadians);
	void SetProxyPitch(RE::Actor* a_body, float a_pitchDegrees);
	void ClearProxy(RE::Actor* a_body);

	bool SendGraphEvent(RE::Actor* a_body, const char* a_event);
	bool TakeProxyResync(RE::Actor* a_body);
}

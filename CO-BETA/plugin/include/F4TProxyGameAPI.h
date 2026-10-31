#pragma once

#include "RE/N/NiPoint3.h"

namespace RE
{
	class Actor;
}

namespace F4T::ProxyGameAPI
{
	void PositionRemoteActor(
		RE::Actor& a_proxy,
		const RE::NiPoint3& a_pos,
		float a_headingZ,
		bool a_force = false);

	void PositionRemoteActor(
		RE::Actor& a_proxy,
		const RE::NiPoint3& a_pos,
		const RE::NiPoint3& a_rot,
		bool a_force = false);
}

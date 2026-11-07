// License: GPL-3.0 with additional terms (FatherLamb); ported from FathersFallout client-ng/src/sync/GraphOverride.h (InstallNoTalk).
#pragma once

#include <cstdint>
#include <vector>

namespace RE
{
	class Actor;
	class TESBoundObject;
}

namespace F4T::ProxyNoTalk
{
	bool Install(RE::Actor* a_proxy);
	bool InstallOnBase(RE::TESBoundObject* a_base);
	bool Installed();
	void SetPolicyBlock(std::vector<std::uint32_t> a_baseFormIds);
	bool PolicyBlockActive();
}

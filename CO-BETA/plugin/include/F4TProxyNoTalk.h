// License: GPL-3.0 with additional terms (FatherLamb); ported from FathersFallout client-ng/src/sync/GraphOverride.h (InstallNoTalk).
#pragma once

namespace RE
{
	class Actor;
}

namespace F4T::ProxyNoTalk
{
	bool Install(RE::Actor* a_proxy);
	bool Installed();
}

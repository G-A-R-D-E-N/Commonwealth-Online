#include "CommonwealthOnlineUI_API.h"

#include <cassert>
#include <cstdint>

namespace
{
	int g_refusedCalls = 0;

	void CountRefusal(COMMONWEALTH_ONLINE_UI_API::InterfaceVersion, std::uint32_t)
	{
		++g_refusedCalls;
	}

	template <typename T>
	void CheckLayoutGate()
	{
		using Map = COMMONWEALTH_ONLINE_UI_API::InterfaceVersionMap<T>;
		T table{};
		assert(COMMONWEALTH_ONLINE_UI_API::AcceptInterface(&table, Map::layout) == &table);
		assert(COMMONWEALTH_ONLINE_UI_API::AcceptInterface(&table, Map::layout ^ 1u) == nullptr);
		assert(COMMONWEALTH_ONLINE_UI_API::AcceptInterface(&table, 0u) == nullptr);
		assert(COMMONWEALTH_ONLINE_UI_API::AcceptInterface(&table, ~Map::layout) == nullptr);
	}
}

int main()
{
	using namespace COMMONWEALTH_ONLINE_UI_API;

	CheckLayoutGate<IVCommonwealthOnlineUI1>();
	CheckLayoutGate<IVCommonwealthOnlineUI2>();
	CheckLayoutGate<IVCommonwealthOnlineUI3>();
	CheckLayoutGate<IVCommonwealthOnlineUI4>();
	CheckLayoutGate<IVCommonwealthOnlineUI5>();
	CheckLayoutGate<IVCommonwealthOnlineUI6>();
	CheckLayoutGate<IVCommonwealthOnlineUI7>();
	CheckLayoutGate<IVCommonwealthOnlineUI8>();
	CheckLayoutGate<IVCommonwealthOnlineUI10>();
	CheckLayoutGate<IVCommonwealthOnlineUI11>();
	CheckLayoutGate<IVCommonwealthOnlineUI13>();

	assert(GetModuleHandleW(L"CommonwealthOnlineUI.dll") == nullptr);
	OnInterfaceRefused = CountRefusal;
	assert(RequestPluginAPI<IVCommonwealthOnlineUI1>() == nullptr);
	assert(RequestPluginAPI<IVCommonwealthOnlineUI7>() == nullptr);
	assert(g_refusedCalls == 0);
	OnInterfaceRefused = nullptr;
	return 0;
}

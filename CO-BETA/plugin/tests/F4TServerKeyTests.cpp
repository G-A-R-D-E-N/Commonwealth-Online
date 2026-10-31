#include "F4TServerKey.h"

#include <cassert>
#include <cstdint>
#include <string>

int main()
{
	using F4T::ServerProfileKey::IsSteamGuestJoinId;
	using F4T::ServerProfileKey::SteamGuestProfileHost;
	using F4T::ServerProfileKey::TryParseServerKey;

	assert(IsSteamGuestJoinId("steam-76561198000000001"));
	assert(IsSteamGuestJoinId("steam-1"));
	assert(!IsSteamGuestJoinId("steam-host"));
	assert(!IsSteamGuestJoinId("steam-guest"));
	assert(!IsSteamGuestJoinId("steam-0"));
	assert(!IsSteamGuestJoinId("steam-"));
	assert(!IsSteamGuestJoinId("steam"));
	assert(!IsSteamGuestJoinId(""));
	assert(!IsSteamGuestJoinId("steam-12a"));
	assert(!IsSteamGuestJoinId("steam-+12"));
	assert(!IsSteamGuestJoinId("steam- 12"));
	assert(!IsSteamGuestJoinId("steam-99999999999999999999999"));
	assert(!IsSteamGuestJoinId("STEAM-76561198000000001"));
	assert(!IsSteamGuestJoinId("lan-76561198000000001"));

	std::string host;
	std::uint16_t port = 0;
	assert(TryParseServerKey("steam-76561198000000001:7777", host, port));
	assert(host == "steam-76561198000000001");
	assert(port == 7777);
	assert(TryParseServerKey("127.0.0.1:7777", host, port));
	assert(host == "127.0.0.1" && port == 7777);

	assert(SteamGuestProfileHost("steam-76561198000000002") == "steam:76561198000000002");
	assert(TryParseServerKey(SteamGuestProfileHost("steam-76561198000000002") + ":7777", host, port));
	assert(host == "steam:76561198000000002" && port == 7777);

	return 0;
}

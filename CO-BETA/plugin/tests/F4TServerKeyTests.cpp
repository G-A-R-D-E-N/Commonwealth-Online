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

	using F4T::ServerProfileKey::NormalizeHost;
	assert(NormalizeHost("play.example.com:7797") == "play.example.com");
	assert(NormalizeHost("Play.Example.com") == "play.example.com");
	assert(NormalizeHost("203.0.113.5:7797") == "203.0.113.5");
	assert(NormalizeHost("localhost:7797") == "127.0.0.1");
	assert(NormalizeHost("[::1]:7797") == "127.0.0.1");
	assert(NormalizeHost("2001:db8::1") == "2001:db8::1");
	assert(NormalizeHost("[2001:db8::1]:7797") == "2001:db8::1");
	assert(NormalizeHost("steam:76561198000000002") == "steam:76561198000000002");
	assert(NormalizeHost("play.example.com:0") == "play.example.com:0");
	assert(NormalizeHost("play.example.com:99999") == "play.example.com:99999");
	assert(NormalizeHost("play.example.com:abc") == "play.example.com:abc");
	assert(NormalizeHost("play.example.com:7797") + ":7797" == "play.example.com:7797");

	assert(SteamGuestProfileHost("steam-76561198000000002") == "steam:76561198000000002");
	assert(TryParseServerKey(SteamGuestProfileHost("steam-76561198000000002") + ":7777", host, port));
	assert(host == "steam:76561198000000002" && port == 7777);

	return 0;
}

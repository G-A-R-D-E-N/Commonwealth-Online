#include "F4TConnectionPolicy.h"

#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

int main()
{
	using namespace F4T::ConnectionPolicy;

	ConnectAttemptLimiter limiter;
	for (std::uint32_t attempt = 0; attempt < 8; ++attempt) {
		assert(limiter.Allow(0x7F000001, 100.0 + attempt));
	}
	assert(!limiter.Allow(0x7F000001, 108.0));
	assert(limiter.TrackedPeers() == 1);

	limiter.Clear();
	assert(limiter.TrackedPeers() == 0);
	for (std::uint32_t attempt = 0; attempt < 8; ++attempt) {
		assert(limiter.Allow(1, 200.0));
	}
	assert(!limiter.Allow(1, 210.0));
	assert(limiter.Allow(1, 210.0001));

	ConnectAttemptConfig small{ .maximumAttempts = 2, .windowSeconds = 5.0 };
	assert(limiter.Allow(2, 1.0, small));
	assert(limiter.Allow(2, 2.0, small));
	assert(!limiter.Allow(2, 3.0, small));
	assert(limiter.Allow(3, 3.0, small));

	assert(MaximumPendingConnections(0) == 16);
	assert(MaximumPendingConnections(4) == 16);
	assert(MaximumPendingConnections(8) == 16);
	assert(MaximumPendingConnections(9) == 18);
	assert(MaximumPendingConnections((std::numeric_limits<std::size_t>::max)()) == (std::numeric_limits<std::size_t>::max)());

	assert(CanActivate(0, 1));
	assert(!CanActivate(1, 1));
	assert(!CanActivate(0, 0));

	assert(!HandshakeExpired(100.0, 110.0, false));
	assert(HandshakeExpired(100.0, 110.0001, false));
	assert(!HandshakeExpired(100.0, 1000.0, true));

	assert(!IdleExpired(std::nullopt, 1000.0, true));
	assert(!IdleExpired(100.0, 160.0, true));
	assert(IdleExpired(100.0, 160.0001, true));
	assert(!IdleExpired(100.0, 1000.0, false));

	ConnectAttemptLimiter churn;
	for (std::uint64_t peer = 0; peer < MaximumTrackedConnectPeers * 4; ++peer) {
		(void)churn.Allow(peer, static_cast<double>(peer));
	}
	assert(churn.TrackedPeers() <= MaximumTrackedConnectPeers);

	ConnectAttemptLimiter burst;
	for (std::uint64_t peer = 0; peer < MaximumTrackedConnectPeers * 4; ++peer) {
		(void)burst.Allow(peer, 1000.0);
		assert(burst.TrackedPeers() <= MaximumTrackedConnectPeers);
	}
	assert(burst.TrackedPeers() <= MaximumTrackedConnectPeers);

	ConnectAttemptLimiter retention;
	constexpr std::uint64_t persistentPeer = 999999;
	for (std::uint64_t round = 0; round < MaximumTrackedConnectPeers * 2; ++round) {
		(void)retention.Allow(persistentPeer, 2000.0);
		(void)retention.Allow(round, 2000.0);
	}
	assert(retention.TrackedPeers() <= MaximumTrackedConnectPeers);
	assert(!retention.Allow(persistentPeer, 2000.0));

	assert(Ipv6Prefix64("2606:4700:10::1") == std::optional<std::uint64_t>{ 0x2606470000100000ULL });
	assert(Ipv6Prefix64("2606:4700:10:0:ffff::9") == Ipv6Prefix64("2606:4700:10::1"));
	assert(Ipv6Prefix64("[2606:4700:10::1]") == Ipv6Prefix64("2606:4700:10::1"));
	assert(Ipv6Prefix64("2606:4700:0010:0000:0000:0000:0000:0001") == Ipv6Prefix64("2606:4700:10::1"));
	assert(Ipv6Prefix64("::1") == std::optional<std::uint64_t>{ 0 });
	assert(Ipv6Prefix64("::") == std::optional<std::uint64_t>{ 0 });
	assert(Ipv6Prefix64("1::") == std::optional<std::uint64_t>{ 0x0001000000000000ULL });
	assert(Ipv6Prefix64("ABCD:EF01::") == std::optional<std::uint64_t>{ 0xABCDEF0100000000ULL });
	assert(!Ipv6Prefix64("127.0.0.1"));
	assert(!Ipv6Prefix64("::ffff:1.2.3.4"));
	assert(!Ipv6Prefix64("1::2::3"));
	assert(!Ipv6Prefix64("1:2:3:4:5:6:7"));
	assert(!Ipv6Prefix64("1:2:3:4:5:6:7:8:9"));
	assert(!Ipv6Prefix64("1:2:3:4:5:6:7::8"));
	assert(!Ipv6Prefix64("12345::"));
	assert(!Ipv6Prefix64("g::1"));
	assert(!Ipv6Prefix64(":1"));
	assert(!Ipv6Prefix64("1:"));
	assert(!Ipv6Prefix64(""));
	assert(!Ipv6Prefix64("example.com"));
	assert(Ipv6Prefix64Text(0x2606470000100000ULL) == "2606:4700:10:0::/64");
	assert(Ipv6Prefix64Text(0) == "0:0:0:0::/64");
	const auto prefixText = Ipv6Prefix64Text(0x2606470000100000ULL);
	assert(Ipv6Prefix64(std::string_view{ prefixText }.substr(0, prefixText.size() - 3)) == std::optional<std::uint64_t>{ 0x2606470000100000ULL });
	assert(Ipv6PeerKey(0) > (std::numeric_limits<std::uint32_t>::max)());
	assert(Ipv6PeerKey(0x2606470000100000ULL) != Ipv6PeerKey(0x2606470000110000ULL));

	const auto normalizes = [](std::string a_input, std::string_view a_host, std::uint16_t a_port) {
		std::uint16_t port = 7777;
		return NormalizeConnectEndpoint(a_input, port) && a_input == a_host && port == a_port;
	};
	const auto rejects = [](std::string a_input) {
		std::uint16_t port = 7777;
		return !NormalizeConnectEndpoint(a_input, port);
	};
	assert(normalizes("  127.0.0.1 ", "127.0.0.1", 7777));
	assert(normalizes("server.example.com:7780", "server.example.com", 7780));
	assert(normalizes("server.example.com:0007780", "server.example.com", 7780));
	assert(normalizes("server.example.com:99999", "server.example.com:99999", 7777));
	assert(normalizes("2606:4700:10::1", "2606:4700:10::1", 7777));
	assert(normalizes("[2606:4700:10::1]", "2606:4700:10::1", 7777));
	assert(normalizes("[2606:4700:10::1]:7781", "2606:4700:10::1", 7781));
	assert(normalizes("[example.com]", "example.com", 7777));
	assert(rejects("[2606:4700:10::1]:abc"));
	assert(rejects("[2606:4700:10::1]x"));
	assert(rejects("[2606:4700:10::1"));
	assert(rejects("[]:7777"));
	assert(normalizes(":7777", ":7777", 7777));
	assert(rejects("   "));

	return 0;
}

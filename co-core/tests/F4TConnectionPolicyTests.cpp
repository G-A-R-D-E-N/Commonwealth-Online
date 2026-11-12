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

	assert(MessageWithinTypeLimit("chatSend", SmallMessageMaximumBytes));
	assert(!MessageWithinTypeLimit("chatSend", SmallMessageMaximumBytes + 1));
	assert(!MessageWithinTypeLimit("keepAlive", 256 * 1024));
	assert(MessageWithinTypeLimit("hello", 256 * 1024));
	assert(MessageWithinTypeLimit("saveAttested", 256 * 1024));
	assert(MessageWithinTypeLimit("", 256 * 1024));

	assert(ServerMessageWithinTypeLimit("combatHit", SmallMessageMaximumBytes));
	assert(!ServerMessageWithinTypeLimit("combatHit", SmallMessageMaximumBytes + 1));
	for (const std::string_view type : { "keepAlive", "combatHit", "projectileAim", "playerAnimEvent", "explosionSpawn", "npcHit", "npcDamage", "npcLootTake" }) {
		assert(ServerMessageWithinTypeLimit(type, SmallMessageMaximumBytes));
		assert(!ServerMessageWithinTypeLimit(type, SmallMessageMaximumBytes + 1));
	}
	assert(ServerMessageWithinTypeLimit("", 60 * 1024));
	assert(ServerMessageWithinTypeLimit("factionList", 60 * 1024));
	assert(ServerMessageWithinTypeLimit("worldState", 60 * 1024));

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

	{
		constexpr std::uint64_t kGuestA = 76561198000000042ULL;
		constexpr std::uint64_t kGuestB = 76561198000000099ULL;
		const auto tunnelKey = SourcePeerKey(0x7F000001u, {});
		assert(tunnelKey == 0);
		assert(SteamPeerKey(kGuestA) == kGuestA);
		assert(SteamPeerKey(kGuestA) != SteamPeerKey(kGuestB));
		assert(SteamPeerKey(0) == 0);
		assert(SteamPeerKey(1) == 0);
		assert(SteamPeerKey(0x7F000001u) == 0);
		assert(SteamPeerKey(Ipv6PeerKey(0x2001000000000000ULL)) == 0);
		assert(SteamPeerKey(kGuestA) > 0xFFFFFFFFULL && (SteamPeerKey(kGuestA) >> 63) == 0);

		ConnectAttemptLimiter tunnel;
		for (std::uint32_t attempt = 0; attempt < 8; ++attempt) {
			assert(tunnel.Allow(SteamPeerKey(kGuestA), 500.0));
		}
		assert(!tunnel.Allow(SteamPeerKey(kGuestA), 501.0));
		assert(tunnel.Allow(SteamPeerKey(kGuestB), 501.0));
		assert(tunnel.TrackedPeers() == 2);
	}

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

	assert(!AfkExpired(std::nullopt, 1000.0, true));
	assert(!AfkExpired(100.0, 1000.0, true));
	assert(!AfkExpired(100.0, 999.9, true));
	assert(AfkExpired(100.0, 1000.0001, true));
	assert(!AfkExpired(100.0, 1000.0, false));
	assert(!AfkExpired(100.0, 1000.0, true, 1200.0));

	assert(LimitsForHost("127.0.0.1").connectSeconds == DefaultLoopbackConnectSeconds);
	assert(LimitsForHost("127.0.0.1").welcomeWaitSeconds == DefaultWelcomeWaitSeconds);
	assert(LimitsForHost("127.0.15.9").connectSeconds == DefaultLoopbackConnectSeconds);
	assert(LimitsForHost("::1").connectSeconds == DefaultLoopbackConnectSeconds);
	assert(LimitsForHost("[::1]").connectSeconds == DefaultLoopbackConnectSeconds);
	assert(LimitsForHost("localhost").welcomeWaitSeconds == DefaultWelcomeWaitSeconds);
	assert(LimitsForHost("203.0.113.7").connectSeconds == DefaultInternetConnectSeconds);
	assert(LimitsForHost("203.0.113.7").welcomeWaitSeconds == DefaultInternetWelcomeWaitSeconds);
	assert(LimitsForHost("192.168.1.20").connectSeconds == DefaultInternetConnectSeconds);
	assert(LimitsForHost("server.example.com").welcomeWaitSeconds == DefaultInternetWelcomeWaitSeconds);
	assert(LimitsForHost("2606:4700:10::1").connectSeconds == DefaultInternetConnectSeconds);
	assert(DefaultInternetConnectSeconds > DefaultLoopbackConnectSeconds);
	assert(DefaultInternetWelcomeWaitSeconds > DefaultWelcomeWaitSeconds);
	assert(DefaultAfkKickSeconds == 900.0);
	assert(DefaultAfkExemptionFreshnessSeconds == 45.0);
	assert(DefaultReconnectRetrySeconds == 20.0);
	assert(DefaultReconnectRetryIntervalSeconds == 3.0);

	assert(!AfkExemptFromClientState(false, false, false));
	assert(AfkExemptFromClientState(true, false, false));
	assert(AfkExemptFromClientState(false, true, false));
	assert(AfkExemptFromClientState(false, false, true));

	assert(DecideJoinRetry("duplicate_identity", 0, 0.0) == JoinRetryDecision::retryAfterInterval);
	assert(DecideJoinRetry("duplicate_identity", 4, 19.9) == JoinRetryDecision::retryAfterInterval);
	assert(DecideJoinRetry("duplicate_identity", 5, 1.0) == JoinRetryDecision::showFailure);
	assert(DecideJoinRetry("duplicate_identity", 0, DefaultReconnectRetrySeconds) == JoinRetryDecision::showFailure);
	assert(DecideJoinRetry("duplicate_identity", 0, 20.5) == JoinRetryDecision::showFailure);
	for (const std::string code : { "banned", "server_full", "wrong_password", "hello_timeout", "", "modpack_mismatch" }) {
		assert(DecideJoinRetry(code, 0, 0.0) == JoinRetryDecision::showFailure);
	}
	assert(DecideJoinRetry("duplicate_identity", 0, std::numeric_limits<double>::quiet_NaN()) == JoinRetryDecision::showFailure);

	// a decision at 19.9 s must not start an attempt at 22.9 s; one at 16.9 s may start one at 19.9 s
	assert(JoinRetryAttemptFits(16.9, DefaultReconnectRetryIntervalSeconds));
	assert(DecideJoinRetry("duplicate_identity", 4, 16.9 + DefaultReconnectRetryIntervalSeconds) == JoinRetryDecision::retryAfterInterval);
	assert(!JoinRetryAttemptFits(19.9, DefaultReconnectRetryIntervalSeconds));
	assert(JoinRetryAttemptFits(16.9, DefaultReconnectRetryIntervalSeconds, 19.9 + DefaultReconnectRetryIntervalSeconds));
	assert(DecideJoinRetry("duplicate_identity", 4, 22.9) == JoinRetryDecision::showFailure);
	assert(JoinRetryAttemptFits(0.0, 0.0));
	assert(!JoinRetryAttemptFits(DefaultReconnectRetrySeconds, 0.0));
	assert(!JoinRetryAttemptFits(16.9, -1.0));
	assert(!JoinRetryAttemptFits(std::numeric_limits<double>::quiet_NaN(), DefaultReconnectRetryIntervalSeconds));
	assert(!JoinRetryAttemptFits(0.0, std::numeric_limits<double>::infinity()));

	assert(DecideNextJoinRetry("duplicate_identity", "", "", 0, 0.0) == JoinRetryStep::retryAfterInterval);
	assert(DecideNextJoinRetry("duplicate_identity", "fp-old", "", 4, 19.9) == JoinRetryStep::retryAfterInterval);
	assert(DecideNextJoinRetry("duplicate_identity", "fp-old", "", 5, 1.0) == JoinRetryStep::stop);
	assert(DecideNextJoinRetry("duplicate_identity", "fp-old", "", 0, DefaultReconnectRetrySeconds) == JoinRetryStep::stop);
	assert(DecideNextJoinRetry("duplicate_identity", "", "", 0, std::numeric_limits<double>::quiet_NaN()) == JoinRetryStep::stop);

	assert(DecideNextJoinRetry("modpack_mismatch", "fp-old", "fp-new", 0, 0.0) == JoinRetryStep::retryImmediately);
	assert(DecideNextJoinRetry("modpack_mismatch", "", "fp-new", 0, 0.0) == JoinRetryStep::retryImmediately);
	assert(DecideNextJoinRetry("modpack_mismatch", "fp-old", "fp-new", 4, 19.9) == JoinRetryStep::retryImmediately);
	assert(DecideNextJoinRetry("modpack_mismatch", "fp-new", "fp-new", 0, 0.0) == JoinRetryStep::stop);
	assert(DecideNextJoinRetry("modpack_mismatch", "fp-old", "", 0, 0.0) == JoinRetryStep::stop);
	assert(DecideNextJoinRetry("modpack_mismatch", "fp-old", "fp-new", 5, 0.0) == JoinRetryStep::stop);
	assert(DecideNextJoinRetry("modpack_mismatch", "fp-old", "fp-new", 0, DefaultReconnectRetrySeconds) == JoinRetryStep::stop);
	assert(DecideNextJoinRetry("modpack_mismatch", "fp-old", "fp-new", 0, 20.5) == JoinRetryStep::stop);
	assert(DecideNextJoinRetry("modpack_mismatch", "fp-old", "fp-new", 0, std::numeric_limits<double>::quiet_NaN()) == JoinRetryStep::stop);
	assert(DecideNextJoinRetry("modpack_mismatch", "fp-old", "fp-new", 0, std::numeric_limits<double>::infinity()) == JoinRetryStep::stop);
	assert(DecideNextJoinRetry("banned", "fp-old", "fp-new", 0, 0.0) == JoinRetryStep::stop);
	assert(DecideNextJoinRetry("banned", "fp-old", "", 0, 0.0) == JoinRetryStep::stop);
	assert(DecideNextJoinRetry("", "", "", 0, 0.0) == JoinRetryStep::stop);
	assert(DecideNextJoinRetry("", "fp-old", "fp-new", 0, 0.0) == JoinRetryStep::stop);
	for (const std::string_view code : { "banned", "server_full", "wrong_password", "hello_timeout", "" }) {
		assert(DecideNextJoinRetry(code, "fp-old", "", 0, 0.0) == JoinRetryStep::stop);
		assert(DecideNextJoinRetry(code, "fp-old", "fp-new", 0, 0.0) == JoinRetryStep::stop);
		assert(DecideNextJoinRetry(code, "", "fp-new", 0, 0.0) == JoinRetryStep::stop);
	}

	// constraint: this failure sequence must give interval, immediate, stop in that order.
	assert(DecideNextJoinRetry("duplicate_identity", "fp-old", "", 0, 0.0) == JoinRetryStep::retryAfterInterval);
	assert(DecideNextJoinRetry("modpack_mismatch", "fp-old", "fp-new", 1, 3.5) == JoinRetryStep::retryImmediately);
	assert(DecideNextJoinRetry("modpack_mismatch", "fp-new", "fp-new", 2, 3.5) == JoinRetryStep::stop);

	assert(DecideResumeRecovery("unknown_resume", true));
	assert(!DecideResumeRecovery("unknown_resume", false));
	for (const std::string_view code : { "banned", "profile_banned", "kicked", "duplicate_identity", "resume_expired",
		"resume_required", "identity_capacity", "invalid_resume", "invalid_resume_ack", "client_version_mismatch", "modpack_mismatch", "server_full", "" }) {
		assert(!DecideResumeRecovery(code, true));
		assert(!DecideResumeRecovery(code, false));
	}
	assert(DecideNextJoinRetry("unknown_resume", "", "", 0, 0.0) == JoinRetryStep::stop);
	assert(DecideNextJoinRetry("unknown_resume", "fp-old", "fp-new", 0, 0.0) == JoinRetryStep::stop);
	assert(DecideNextJoinRetry("invalid_resume", "", "", 0, 0.0) == JoinRetryStep::stop);
	assert(DecideNextJoinRetry("invalid_resume", "fp-old", "fp-new", 0, 0.0) == JoinRetryStep::stop);

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

	ConnectAttemptLimiter flooded;
	constexpr std::uint64_t floodedPeer = 0xF100D;
	const ConnectAttemptConfig floodConfig{ .maximumAttempts = 8, .windowSeconds = 10.0 };
	for (std::uint32_t attempt = 0; attempt < 8; ++attempt) {
		assert(flooded.Allow(floodedPeer, 5000.0, floodConfig));
	}
	for (std::uint32_t attempt = 0; attempt < 100'000; ++attempt) {
		assert(!flooded.Allow(floodedPeer, 5000.0, floodConfig));
	}
	assert(flooded.TrackedPeers() == 1);
	assert(!flooded.Allow(floodedPeer, 5009.999, floodConfig));
	assert(flooded.Allow(floodedPeer, 5010.001, floodConfig));

	ConnectAttemptLimiter oversized;
	constexpr std::uint64_t oversizedPeer = 0xF100E;
	const ConnectAttemptConfig oversizedConfig{ .maximumAttempts = 100'000, .windowSeconds = 10.0 };
	std::uint32_t oversizedAccepted = 0;
	for (std::uint32_t attempt = 0; attempt < 100'000; ++attempt) {
		if (oversized.Allow(oversizedPeer, 6000.0, oversizedConfig)) {
			++oversizedAccepted;
		}
	}
	assert(oversizedAccepted == MaximumConnectAttemptHistory);
	assert(oversized.TrackedPeers() == 1);
	assert(!oversized.Allow(oversizedPeer, 6000.0, oversizedConfig));
	assert(!oversized.Allow(oversizedPeer, 6005.0, oversizedConfig));
	assert(oversized.Allow(oversizedPeer, 6010.001, oversizedConfig));

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
	assert(!Ipv6Prefix64("::ffff:7f00:1"));
	assert(!Ipv6Prefix64("0:0:0:0:0:ffff:7f00:1"));
	assert(Ipv6Prefix64("::fffe:7f00:1") == std::optional<std::uint64_t>{ 0 });
	assert(SourcePeerKey(0, "::ffff:7f00:1") == 0);
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

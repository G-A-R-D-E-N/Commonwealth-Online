#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace F4T::ConnectionPolicy
{
	inline constexpr std::uint32_t DefaultMaximumConnectAttempts = 8;
	inline constexpr double DefaultConnectAttemptWindowSeconds = 10.0;
	inline constexpr double DefaultHandshakeTimeoutSeconds = 10.0;
	inline constexpr double DefaultSteamLinkWaitSeconds = 20.0;
	inline constexpr double DefaultLoopbackConnectSeconds = 3.0;
	inline constexpr double DefaultWelcomeWaitSeconds = 3.0;
	inline constexpr double DefaultIdleTimeoutSeconds = 60.0;
	inline constexpr std::size_t MinimumPendingConnections = 16;
	inline constexpr std::size_t MaximumTrackedConnectPeers = 4096;

	struct ConnectAttemptConfig
	{
		std::uint32_t maximumAttempts{ DefaultMaximumConnectAttempts };
		double windowSeconds{ DefaultConnectAttemptWindowSeconds };
	};

	class ConnectAttemptLimiter
	{
	public:
		[[nodiscard]] bool Allow(
			std::uint64_t a_peerKey,
			double a_nowMonotonicSeconds,
			ConnectAttemptConfig a_config = {});
		void Clear() noexcept;
		[[nodiscard]] std::size_t TrackedPeers() const noexcept;

	private:
		void EnforceBound(double a_nowMonotonicSeconds, double a_windowSeconds);

		std::unordered_map<std::uint64_t, std::deque<double>> _attempts;
	};

	// The first 64 bits of an IPv6 address in text form (brackets allowed, no embedded IPv4).
	[[nodiscard]] std::optional<std::uint64_t> Ipv6Prefix64(std::string_view a_text) noexcept;
	[[nodiscard]] std::string Ipv6Prefix64Text(std::uint64_t a_prefix);
	// Limiter key for an IPv6 /64; never collides with an IPv4 key, which is below 2^32.
	[[nodiscard]] std::uint64_t Ipv6PeerKey(std::uint64_t a_prefix) noexcept;

	// Trims a typed server address and splits "host:port" or "[ipv6]:port"; a bare IPv6 literal is kept whole.
	[[nodiscard]] bool NormalizeConnectEndpoint(std::string& a_host, std::uint16_t& a_port);

	[[nodiscard]] std::size_t MaximumPendingConnections(std::size_t a_maximumPlayers) noexcept;
	[[nodiscard]] bool CanActivate(std::size_t a_activePlayers, std::size_t a_maximumPlayers) noexcept;
	[[nodiscard]] bool HandshakeExpired(
		double a_connectedAtUnixSeconds,
		double a_nowUnixSeconds,
		bool a_gameplayActive,
		double a_timeoutSeconds = DefaultHandshakeTimeoutSeconds) noexcept;
	[[nodiscard]] bool IdleExpired(
		const std::optional<double>& a_lastPacketAtUnixSeconds,
		double a_nowUnixSeconds,
		bool a_gameplayActive,
		double a_timeoutSeconds = DefaultIdleTimeoutSeconds) noexcept;
}

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace F4T::Transport
{
	inline constexpr std::uint16_t kMaximumPort = 65535;
	inline constexpr std::size_t kMaximumSteamIdDigits = 20;
	inline constexpr std::string_view kSteamScheme = "steam:";

	enum class EndpointKind
	{
		DirectIpv4,
		SteamId
	};

	enum class ProviderKind
	{
		DirectUdp,
		SteamP2p
	};

	struct TransportEndpoint
	{
		EndpointKind kind{ EndpointKind::DirectIpv4 };
		std::string host;
		std::uint16_t port{};
		std::uint64_t steamId64{};

		[[nodiscard]] bool operator==(const TransportEndpoint&) const = default;
	};

	[[nodiscard]] std::optional<TransportEndpoint> ParseEndpoint(std::string_view a_text);
	[[nodiscard]] std::string FormatEndpoint(const TransportEndpoint& a_endpoint);
	[[nodiscard]] std::optional<ProviderKind> SelectProvider(
		EndpointKind a_kind,
		std::span<const ProviderKind> a_available);
}

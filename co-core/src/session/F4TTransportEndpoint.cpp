#include "F4TTransportEndpoint.h"

#include <algorithm>
#include <charconv>
#include <cstddef>

namespace F4T::Transport
{
	namespace
	{
		std::optional<std::uint64_t> ParseDecimal(std::string_view a_digits, std::size_t a_maximumDigits)
		{
			if (a_digits.empty() || a_digits.size() > a_maximumDigits) return std::nullopt;
			if (a_digits.size() > 1 && a_digits.front() == '0') return std::nullopt;

			std::uint64_t value{};
			const auto [end, error] = std::from_chars(a_digits.data(), a_digits.data() + a_digits.size(), value);
			if (error != std::errc{} || end != a_digits.data() + a_digits.size()) return std::nullopt;
			return value;
		}

		bool IsDottedQuad(std::string_view a_host)
		{
			if (std::count(a_host.begin(), a_host.end(), '.') != 3) return false;

			std::size_t start = 0;
			for (std::size_t octet = 0; octet < 4; ++octet) {
				const auto dot = a_host.find('.', start);
				const auto end = dot == std::string_view::npos ? a_host.size() : dot;
				const auto value = ParseDecimal(a_host.substr(start, end - start), 3);
				if (!value || *value > 255) return false;
				if (dot == std::string_view::npos) break;
				start = dot + 1;
			}
			return true;
		}
	}

	std::optional<TransportEndpoint> ParseEndpoint(std::string_view a_text)
	{
		if (a_text.empty()) return std::nullopt;

		if (a_text.starts_with(kSteamScheme)) {
			const auto steamId = ParseDecimal(a_text.substr(kSteamScheme.size()), kMaximumSteamIdDigits);
			if (!steamId || *steamId == 0) return std::nullopt;

			TransportEndpoint endpoint;
			endpoint.kind = EndpointKind::SteamId;
			endpoint.steamId64 = *steamId;
			return endpoint;
		}

		const auto separator = a_text.rfind(':');
		if (separator == std::string_view::npos) return std::nullopt;

		const auto host = a_text.substr(0, separator);
		const auto port = ParseDecimal(a_text.substr(separator + 1), 5);
		if (!port || *port == 0 || *port > kMaximumPort) return std::nullopt;
		if (!IsDottedQuad(host)) return std::nullopt;

		TransportEndpoint endpoint;
		endpoint.kind = EndpointKind::DirectIpv4;
		endpoint.host = std::string{ host };
		endpoint.port = static_cast<std::uint16_t>(*port);
		return endpoint;
	}

	std::string FormatEndpoint(const TransportEndpoint& a_endpoint)
	{
		if (a_endpoint.kind == EndpointKind::SteamId) {
			return std::string{ kSteamScheme } + std::to_string(a_endpoint.steamId64);
		}
		return a_endpoint.host + ":" + std::to_string(a_endpoint.port);
	}

	std::optional<ProviderKind> SelectProvider(EndpointKind a_kind, std::span<const ProviderKind> a_available)
	{
		const auto required = a_kind == EndpointKind::SteamId ? ProviderKind::SteamP2p : ProviderKind::DirectUdp;
		return std::find(a_available.begin(), a_available.end(), required) == a_available.end()
			? std::nullopt
			: std::optional{ required };
	}
}

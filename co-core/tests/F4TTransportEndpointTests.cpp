#include "F4TTransportEndpoint.h"

#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace
{
	using F4T::Transport::EndpointKind;
	using F4T::Transport::FormatEndpoint;
	using F4T::Transport::ParseEndpoint;
	using F4T::Transport::ProviderKind;
	using F4T::Transport::SelectProvider;
	using F4T::Transport::TransportEndpoint;

	void ExpectDirect(std::string_view a_text, std::string_view a_host, std::uint16_t a_port)
	{
		const auto parsed = ParseEndpoint(a_text);
		assert(parsed.has_value());
		assert(parsed->kind == EndpointKind::DirectIpv4);
		assert(parsed->host == a_host);
		assert(parsed->port == a_port);
		assert(FormatEndpoint(*parsed) == a_text);
	}

	void ExpectSteam(std::string_view a_text, std::uint64_t a_steamId)
	{
		const auto parsed = ParseEndpoint(a_text);
		assert(parsed.has_value());
		assert(parsed->kind == EndpointKind::SteamId);
		assert(parsed->steamId64 == a_steamId);
		assert(FormatEndpoint(*parsed) == a_text);
	}

	void ExpectRejected(std::string_view a_text)
	{
		assert(!ParseEndpoint(a_text).has_value());
	}

	void ExpectSelection(EndpointKind a_kind, std::span<const ProviderKind> a_available, std::optional<ProviderKind> a_expected)
	{
		assert(SelectProvider(a_kind, a_available) == a_expected);
	}
}

int main()
{
	ExpectDirect("0.0.0.0:1", "0.0.0.0", 1);
	ExpectDirect("127.0.0.1:7777", "127.0.0.1", 7777);
	ExpectDirect("192.168.1.9:7800", "192.168.1.9", 7800);
	ExpectDirect("10.20.30.40:8080", "10.20.30.40", 8080);
	ExpectDirect("255.255.255.255:65535", "255.255.255.255", 65535);

	ExpectSteam("steam:1", 1);
	ExpectSteam("steam:76561197960265728", 76561197960265728ull);
	ExpectSteam("steam:18446744073709551615", std::numeric_limits<std::uint64_t>::max());

	ExpectRejected("");
	ExpectRejected(" ");
	ExpectRejected("   ");
	ExpectRejected("\t");
	ExpectRejected("1.2.3.4");
	ExpectRejected("1.2.3.4:");
	ExpectRejected(":7777");
	ExpectRejected("1.2.3:7777");
	ExpectRejected("1.2.3.4.5:7777");
	ExpectRejected("1..3.4:7777");
	ExpectRejected(".1.2.3:7777");
	ExpectRejected("1.2.3.:7777");
	ExpectRejected("1.2.3.04:7777");
	ExpectRejected("01.2.3.4:7777");
	ExpectRejected("1.2.3.256:7777");
	ExpectRejected("1.2.3.999:7777");
	ExpectRejected("1.2.3.1000:7777");
	ExpectRejected("1.2.3.4:0");
	ExpectRejected("1.2.3.4:00");
	ExpectRejected("1.2.3.4:07777");
	ExpectRejected(std::string_view{ "1.2.3.4:77\0", 11 });
	ExpectRejected("1.2.3.4:77\xC3\xA9");
	ExpectRejected("1.2.3.4:65536");
	ExpectRejected("1.2.3.4:999999");
	ExpectRejected("1.2.3.4:-1");
	ExpectRejected("1.2.3.4:+1");
	ExpectRejected("1.2.3.4:0x10");
	ExpectRejected("1.2.3.4:7777 ");
	ExpectRejected(" 1.2.3.4:7777");
	ExpectRejected("1.2.3.4 :7777");
	ExpectRejected("1.2.3.4: 7777");
	ExpectRejected("1.2.3.4:77a77");
	ExpectRejected("1.2.3.4:7777abc");
	ExpectRejected("1.2.3.4:7777:8888");
	ExpectRejected("1.2.3.4:7777\n");
	ExpectRejected("steam:");
	ExpectRejected("steam:0");
	ExpectRejected("steam:00");
	ExpectRejected("steam:01");
	ExpectRejected("steam: 1");
	ExpectRejected("steam:1 ");
	ExpectRejected("steam:1.5");
	ExpectRejected("steam:-1");
	ExpectRejected("steam:+1");
	ExpectRejected("steam:0x10");
	ExpectRejected("steam:1a");
	ExpectRejected("steam:18446744073709551616");
	ExpectRejected("steam:99999999999999999999");
	ExpectRejected("steam:000000000000000000001");
	ExpectRejected("Steam:1");
	ExpectRejected("STEAM:1");
	ExpectRejected("steam :1");
	ExpectRejected("udp:1");
	ExpectRejected("ip:1.2.3.4:7777");
	ExpectRejected("steam:1.2.3.4:7777");

	const TransportEndpoint roundTrips[] = {
		TransportEndpoint{ EndpointKind::DirectIpv4, "0.0.0.0", 1, 0 },
		TransportEndpoint{ EndpointKind::DirectIpv4, "192.168.1.9", 7800, 0 },
		TransportEndpoint{ EndpointKind::DirectIpv4, "255.255.255.255", 65535, 0 },
		TransportEndpoint{ EndpointKind::SteamId, {}, 0, 1 },
		TransportEndpoint{ EndpointKind::SteamId, {}, 0, 76561197960265728ull },
		TransportEndpoint{ EndpointKind::SteamId, {}, 0, std::numeric_limits<std::uint64_t>::max() },
	};
	for (const auto& endpoint : roundTrips) {
		const auto text = FormatEndpoint(endpoint);
		const auto reparsed = ParseEndpoint(text);
		assert(reparsed.has_value());
		assert(*reparsed == endpoint);
		assert(FormatEndpoint(*reparsed) == text);
	}

	constexpr ProviderKind kBoth[]{ ProviderKind::DirectUdp, ProviderKind::SteamP2p };
	constexpr ProviderKind kDirectOnly[]{ ProviderKind::DirectUdp };
	constexpr ProviderKind kSteamOnly[]{ ProviderKind::SteamP2p };

	ExpectSelection(EndpointKind::DirectIpv4, kBoth, ProviderKind::DirectUdp);
	ExpectSelection(EndpointKind::SteamId, kBoth, ProviderKind::SteamP2p);
	ExpectSelection(EndpointKind::DirectIpv4, kDirectOnly, ProviderKind::DirectUdp);
	ExpectSelection(EndpointKind::SteamId, kSteamOnly, ProviderKind::SteamP2p);
	ExpectSelection(EndpointKind::DirectIpv4, kSteamOnly, std::nullopt);
	ExpectSelection(EndpointKind::SteamId, kDirectOnly, std::nullopt);
	ExpectSelection(EndpointKind::DirectIpv4, std::span<const ProviderKind>{}, std::nullopt);
	ExpectSelection(EndpointKind::SteamId, std::span<const ProviderKind>{}, std::nullopt);

	return 0;
}

#include "F4TServerKey.h"

#include <cctype>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

namespace
{
	constexpr std::string_view kSteamGuestHostPrefix{ "steam:" };
	constexpr std::string_view kSteamGuestJoinIdPrefix{ "steam-" };

	std::string TrimAscii(std::string_view a_text)
	{
		std::size_t start = 0;
		while (start < a_text.size() && std::isspace(static_cast<unsigned char>(a_text[start])) != 0) {
			++start;
		}

		std::size_t end = a_text.size();
		while (end > start && std::isspace(static_cast<unsigned char>(a_text[end - 1])) != 0) {
			--end;
		}

		return std::string{ a_text.substr(start, end - start) };
	}

	std::string ToLowerAscii(std::string a_text)
	{
		for (auto& ch : a_text) {
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		}
		return a_text;
	}
}

namespace F4T::ServerProfileKey
{
	std::string NormalizeHost(std::string_view a_host)
	{
		auto host = ToLowerAscii(TrimAscii(a_host));
		if (host.empty()) {
			return "127.0.0.1";
		}

		if (host.size() > 2 && host.front() == '[' && host.back() == ']') {
			host = host.substr(1, host.size() - 2);
		}

		if (host == "localhost" || host == "::1" || host == "0:0:0:0:0:0:0:1" || host == "127.0.0.1") {
			return "127.0.0.1";
		}

		if (host.rfind("::ffff:", 0) == 0) {
			const auto mappedHost = host.substr(7);
			if (mappedHost == "127.0.0.1") {
				return "127.0.0.1";
			}
		}

		return host;
	}

	bool TryParseServerKey(std::string_view a_serverKey, std::string& a_host, std::uint16_t& a_port)
	{
		const auto separator = a_serverKey.rfind(':');
		if (separator == std::string_view::npos || separator == 0 || separator + 1 >= a_serverKey.size()) {
			return false;
		}

		const auto hostPart = a_serverKey.substr(0, separator);
		const auto portPart = a_serverKey.substr(separator + 1);

		std::uint32_t parsedPort = 0;
		const auto* begin = portPart.data();
		const auto* end = begin + portPart.size();
		const auto result = std::from_chars(begin, end, parsedPort);
		if (result.ec != std::errc{} || result.ptr != end || parsedPort == 0 || parsedPort > 65535) {
			return false;
		}

		a_host = NormalizeHost(hostPart);
		a_port = static_cast<std::uint16_t>(parsedPort);
		return true;
	}

	bool ServerKeyMatches(const std::string& a_existingKey, const std::string& a_host, std::uint16_t a_port)
	{
		std::string existingHost;
		std::uint16_t existingPort = 0;
		if (!TryParseServerKey(a_existingKey, existingHost, existingPort)) {
			return false;
		}

		return existingPort == a_port && existingHost == NormalizeHost(a_host);
	}

	std::string SteamGuestProfileHost(std::string_view a_joinId)
	{
		const auto steamId = a_joinId.substr(0, kSteamGuestJoinIdPrefix.size()) == kSteamGuestJoinIdPrefix
			? a_joinId.substr(kSteamGuestJoinIdPrefix.size())
			: a_joinId;
		return std::string{ kSteamGuestHostPrefix } + std::string{ steamId };
	}

	bool IsSteamGuestJoinId(std::string_view a_joinId)
	{
		if (a_joinId.substr(0, kSteamGuestJoinIdPrefix.size()) != kSteamGuestJoinIdPrefix) {
			return false;
		}
		const auto digits = a_joinId.substr(kSteamGuestJoinIdPrefix.size());
		std::uint64_t steamId = 0;
		const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), steamId);
		return result.ec == std::errc{} && result.ptr == digits.data() + digits.size() && steamId != 0;
	}
}

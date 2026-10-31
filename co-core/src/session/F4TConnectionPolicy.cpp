#include "F4TConnectionPolicy.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>
#include <vector>

namespace F4T::ConnectionPolicy
{
	bool ConnectAttemptLimiter::Allow(
		std::uint64_t a_peerKey,
		double a_nowMonotonicSeconds,
		ConnectAttemptConfig a_config)
	{
		if (a_config.maximumAttempts == 0) {
			a_config.maximumAttempts = 1;
		}
		if (!std::isfinite(a_config.windowSeconds) || a_config.windowSeconds <= 0.0) {
			a_config.windowSeconds = DefaultConnectAttemptWindowSeconds;
		}
		const auto now = std::isfinite(a_nowMonotonicSeconds) ? a_nowMonotonicSeconds : 0.0;
		const auto cutoff = now - a_config.windowSeconds;
		EnforceBound(now, a_config.windowSeconds);

		auto& attempts = _attempts[a_peerKey];
		while (!attempts.empty() && attempts.front() < cutoff) {
			attempts.pop_front();
		}
		attempts.push_back(now);
		return attempts.size() <= a_config.maximumAttempts;
	}

	void ConnectAttemptLimiter::EnforceBound(double a_nowMonotonicSeconds, double a_windowSeconds)
	{
		if (_attempts.size() < MaximumTrackedConnectPeers) {
			return;
		}

		const auto cutoff = a_nowMonotonicSeconds - a_windowSeconds;
		for (auto entry = _attempts.begin(); entry != _attempts.end();) {
			auto& attempts = entry->second;
			while (!attempts.empty() && attempts.front() < cutoff) {
				attempts.pop_front();
			}
			entry = attempts.empty() ? _attempts.erase(entry) : std::next(entry);
		}
		if (_attempts.size() < MaximumTrackedConnectPeers) {
			return;
		}

		const auto target = MaximumTrackedConnectPeers - (MaximumTrackedConnectPeers / 8);
		const auto excess = _attempts.size() - target;
		std::vector<std::pair<double, std::uint64_t>> byLastSeen;
		byLastSeen.reserve(_attempts.size());
		for (const auto& [peerKey, attempts] : _attempts) {
			byLastSeen.emplace_back(attempts.empty() ? 0.0 : attempts.back(), peerKey);
		}
		std::nth_element(
			byLastSeen.begin(),
			byLastSeen.begin() + static_cast<std::ptrdiff_t>(excess),
			byLastSeen.end());
		for (std::size_t index = 0; index < excess; ++index) {
			_attempts.erase(byLastSeen[index].second);
		}
	}

	void ConnectAttemptLimiter::Clear() noexcept
	{
		_attempts.clear();
	}

	std::size_t ConnectAttemptLimiter::TrackedPeers() const noexcept
	{
		return _attempts.size();
	}

	namespace
	{
		bool ParseGroups(std::string_view a_text, std::uint16_t* a_out, std::size_t a_capacity, std::size_t& a_count) noexcept
		{
			a_count = 0;
			if (a_text.empty()) {
				return true;
			}
			std::size_t begin = 0;
			while (true) {
				const auto end = a_text.find(':', begin);
				const auto group = a_text.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin);
				if (group.empty() || group.size() > 4 || a_count == a_capacity) {
					return false;
				}
				std::uint16_t value = 0;
				for (const char ch : group) {
					const int digit = ch >= '0' && ch <= '9' ? ch - '0'
						: ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
						: ch >= 'A' && ch <= 'F' ? ch - 'A' + 10
						: -1;
					if (digit < 0) {
						return false;
					}
					value = static_cast<std::uint16_t>((value << 4) | digit);
				}
				a_out[a_count++] = value;
				if (end == std::string_view::npos) {
					return true;
				}
				begin = end + 1;
			}
		}
	}

	std::optional<std::uint64_t> Ipv6Prefix64(std::string_view a_text) noexcept
	{
		if (a_text.size() > 2 && a_text.front() == '[' && a_text.back() == ']') {
			a_text = a_text.substr(1, a_text.size() - 2);
		}
		if (a_text.size() < 2 || a_text.size() > 39 || a_text.find(':') == std::string_view::npos) {
			return std::nullopt;
		}
		std::uint16_t head[8]{};
		std::uint16_t tail[8]{};
		std::size_t headCount = 0;
		std::size_t tailCount = 0;
		const auto gap = a_text.find("::");
		if (gap == std::string_view::npos) {
			if (!ParseGroups(a_text, head, 8, headCount) || headCount != 8) {
				return std::nullopt;
			}
		} else {
			if (a_text.find("::", gap + 1) != std::string_view::npos ||
				!ParseGroups(a_text.substr(0, gap), head, 8, headCount) ||
				!ParseGroups(a_text.substr(gap + 2), tail, 8, tailCount) ||
				headCount + tailCount > 7) {
				return std::nullopt;
			}
		}
		std::uint16_t groups[8]{};
		for (std::size_t index = 0; index < headCount; ++index) {
			groups[index] = head[index];
		}
		for (std::size_t index = 0; index < tailCount; ++index) {
			groups[8 - tailCount + index] = tail[index];
		}
		std::uint64_t prefix = 0;
		for (std::size_t index = 0; index < 4; ++index) {
			prefix = (prefix << 16) | groups[index];
		}
		return prefix;
	}

	std::string Ipv6Prefix64Text(std::uint64_t a_prefix)
	{
		constexpr char kHex[] = "0123456789abcdef";
		std::string text;
		for (int index = 0; index < 4; ++index) {
			const auto group = static_cast<std::uint16_t>(a_prefix >> (48 - index * 16));
			bool leading = true;
			for (int shift = 12; shift >= 0; shift -= 4) {
				const auto digit = (group >> shift) & 0xF;
				if (leading && digit == 0 && shift != 0) {
					continue;
				}
				leading = false;
				text.push_back(kHex[digit]);
			}
			text.push_back(':');
		}
		text += ":/64";
		return text;
	}

	std::uint64_t Ipv6PeerKey(std::uint64_t a_prefix) noexcept
	{
		return a_prefix | (std::uint64_t{ 1 } << 63);
	}

	namespace
	{
		bool ParsePort(std::string_view a_text, std::uint16_t& a_port) noexcept
		{
			if (a_text.empty()) {
				return false;
			}
			std::uint32_t value = 0;
			for (const char ch : a_text) {
				if (ch < '0' || ch > '9') {
					return false;
				}
				value = value * 10 + static_cast<std::uint32_t>(ch - '0');
				if (value > 65535) {
					return false;
				}
			}
			if (value < 1 || value > 65535) {
				return false;
			}
			a_port = static_cast<std::uint16_t>(value);
			return true;
		}

		bool IsIpv4Literal(std::string_view a_text) noexcept
		{
			std::size_t begin = 0;
			for (int part = 0; part < 4; ++part) {
				const auto end = part == 3 ? a_text.size() : a_text.find('.', begin);
				if (end == std::string_view::npos || end == begin || end - begin > 3) {
					return false;
				}
				std::uint32_t value = 0;
				for (std::size_t index = begin; index < end; ++index) {
					if (a_text[index] < '0' || a_text[index] > '9') {
						return false;
					}
					value = value * 10 + static_cast<std::uint32_t>(a_text[index] - '0');
				}
				if (value > 255) {
					return false;
				}
				begin = end + 1;
			}
			return true;
		}
	}

	bool NormalizeConnectEndpoint(std::string& a_host, std::uint16_t& a_port)
	{
		const auto first = a_host.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) {
			a_host.clear();
			return false;
		}
		a_host = a_host.substr(first, a_host.find_last_not_of(" \t\r\n") - first + 1);

		if (a_host.front() == '[') {
			const auto close = a_host.find(']');
			if (close == std::string::npos || close == 1) {
				return false;
			}
			const std::string_view rest = std::string_view{ a_host }.substr(close + 1);
			std::uint16_t embeddedPort = 0;
			if (!rest.empty() && (rest.front() != ':' || !ParsePort(rest.substr(1), embeddedPort))) {
				return false;
			}
			a_host = a_host.substr(1, close - 1);
			if (!rest.empty()) {
				a_port = embeddedPort;
			}
			return true;
		}

		if (IsIpv4Literal(a_host)) {
			return true;
		}

		const auto colon = a_host.rfind(':');
		if (colon == std::string::npos || colon == 0 || colon + 1 >= a_host.size() || a_host.find(':') != colon) {
			return true;
		}

		std::uint16_t embeddedPort = 0;
		if (!ParsePort(std::string_view{ a_host }.substr(colon + 1), embeddedPort)) {
			return true;
		}
		a_host = a_host.substr(0, colon);
		a_port = embeddedPort;
		return true;
	}

	std::size_t MaximumPendingConnections(std::size_t a_maximumPlayers) noexcept
	{
		const auto doubled = a_maximumPlayers > (std::numeric_limits<std::size_t>::max)() / 2
			? (std::numeric_limits<std::size_t>::max)()
			: a_maximumPlayers * 2;
		return (std::max)(MinimumPendingConnections, doubled);
	}

	bool CanActivate(std::size_t a_activePlayers, std::size_t a_maximumPlayers) noexcept
	{
		return a_activePlayers < a_maximumPlayers;
	}

	bool HandshakeExpired(
		double a_connectedAtUnixSeconds,
		double a_nowUnixSeconds,
		bool a_gameplayActive,
		double a_timeoutSeconds) noexcept
	{
		if (a_gameplayActive || !std::isfinite(a_connectedAtUnixSeconds) || !std::isfinite(a_nowUnixSeconds) ||
			!std::isfinite(a_timeoutSeconds) || a_timeoutSeconds < 0.0) {
			return false;
		}
		return a_nowUnixSeconds - a_connectedAtUnixSeconds > a_timeoutSeconds;
	}

	bool IdleExpired(
		const std::optional<double>& a_lastPacketAtUnixSeconds,
		double a_nowUnixSeconds,
		bool a_gameplayActive,
		double a_timeoutSeconds) noexcept
	{
		if (!a_gameplayActive || !a_lastPacketAtUnixSeconds || !std::isfinite(*a_lastPacketAtUnixSeconds) ||
			!std::isfinite(a_nowUnixSeconds) || !std::isfinite(a_timeoutSeconds) || a_timeoutSeconds < 0.0) {
			return false;
		}
		return a_nowUnixSeconds - *a_lastPacketAtUnixSeconds > a_timeoutSeconds;
	}
}

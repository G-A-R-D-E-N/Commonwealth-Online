#pragma once

#include "F4TPacketRateLimit.h"
#include "F4TVoiceBudget.h"
#include "F4TVoiceWire.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CommonwealthOnline::Server
{
	inline constexpr double kJoinAllowanceSeconds = 60.0;
	inline constexpr std::uint64_t kJoinAllowanceByteMultiplier = 4;

	inline F4T::PacketRateLimit::Config InboundGameplayLimits(
		double a_nowMonotonicSeconds,
		double a_joinedAtMonotonicSeconds,
		F4T::PacketRateLimit::Config a_budget = {})
	{
		auto limits = a_budget;
		const auto sinceJoin = a_nowMonotonicSeconds - a_joinedAtMonotonicSeconds;
		if (std::isfinite(sinceJoin) && sinceJoin >= 0.0 && sinceJoin < kJoinAllowanceSeconds) {
			limits.maximumBytesPerWindow *= kJoinAllowanceByteMultiplier;
		}
		return limits;
	}

	inline F4T::PacketRateLimit::Result ObserveInboundWire(
		F4T::PacketRateLimit::State& a_state,
		double a_nowMonotonicSeconds,
		std::size_t a_bytes)
	{
		constexpr F4T::PacketRateLimit::Config limits{ F4T::PacketRateLimit::MaximumInboundWirePacketsPerSecond, 8ULL * 1024ULL * 1024ULL, 3, 1.0 };
		return F4T::PacketRateLimit::Observe(a_state, a_nowMonotonicSeconds, limits, static_cast<std::uint64_t>(a_bytes));
	}

	inline F4T::PacketRateLimit::Result ObserveInboundDatagram(
		F4T::PacketRateLimit::State& a_globalState,
		F4T::PacketRateLimit::State& a_voiceState,
		double a_nowMonotonicSeconds,
		std::span<const std::byte> a_datagram,
		F4T::PacketRateLimit::Config a_budget)
	{
		const auto bytes = static_cast<std::uint64_t>(a_datagram.size());
		if (F4T::Voice::HasVoicePrefix(a_datagram)) {
			return F4T::Voice::ObserveVoiceHard(a_voiceState, a_nowMonotonicSeconds, bytes);
		}
		return F4T::PacketRateLimit::Observe(a_globalState, a_nowMonotonicSeconds, a_budget, bytes);
	}

	inline constexpr std::size_t kMaximumInboundWindowTypes = 16;
	inline constexpr std::size_t kMaximumInboundWindowTypeChars = 32;

	inline void NoteInboundWindowBytes(
		std::map<std::string, std::uint64_t>& a_bytesByType,
		bool a_windowReset,
		std::string_view a_type,
		std::uint64_t a_bytes)
	{
		if (a_windowReset) {
			a_bytesByType.clear();
		}
		std::string key{ a_type.substr(0, kMaximumInboundWindowTypeChars) };
		if (!a_bytesByType.contains(key) && a_bytesByType.size() >= kMaximumInboundWindowTypes) {
			key = "other";
		}
		a_bytesByType[key] += a_bytes;
	}

	inline std::string DescribeInboundWindowBytes(const std::map<std::string, std::uint64_t>& a_bytesByType)
	{
		std::vector<std::pair<std::string, std::uint64_t>> entries(a_bytesByType.begin(), a_bytesByType.end());
		std::sort(entries.begin(), entries.end(), [](const auto& a_left, const auto& a_right) {
			return a_left.second != a_right.second ? a_left.second > a_right.second : a_left.first < a_right.first;
		});
		std::string text;
		for (std::size_t index = 0; index < entries.size() && index < 4; ++index) {
			if (!text.empty()) {
				text += ", ";
			}
			text += entries[index].first + "=" + std::to_string(entries[index].second);
		}
		return text.empty() ? std::string{ "none" } : text;
	}

	inline std::string_view RateLimitReason(const F4T::PacketRateLimit::Result& a_result)
	{
		return a_result.byteBudgetExceeded ?
			"Sent more data than the server allows for several seconds." :
			"Sustained packet-rate limit exceeded.";
	}
}

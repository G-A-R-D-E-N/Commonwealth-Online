#pragma once

#include "F4TPacketRateLimit.h"

#include <cstdint>
#include <limits>

namespace F4T::Voice
{
	inline constexpr PacketRateLimit::Config kVoiceHardConfig{
		.maximumPacketsPerWindow = 150,
		.maximumBytesPerWindow = 32ULL * 1024ULL,
		.maximumViolationWindows = 3,
		.windowSeconds = 1.0
	};
	inline constexpr double kVoiceSoftCapacity = 10.0;
	inline constexpr double kVoiceSoftRefillPerSecond = 55.0;
	inline constexpr double kVoiceControlCapacity = 2.0;
	inline constexpr double kVoiceControlRefillPerSecond = 1.0;
	inline constexpr double kVoiceAutoMuteDropRatio = 0.25;
	inline constexpr std::uint32_t kVoiceAutoMuteStreak = 5;
	inline constexpr double kVoiceAutoMuteSeconds = 30.0;
	inline constexpr double kVoiceEgressBytesPerSecond = 192000.0;
	inline constexpr double kVoiceEgressBurstBytes = 16384.0;

	struct SoftDropMonitor
	{
		double windowStart{};
		std::uint32_t accepted{};
		std::uint32_t dropped{};
		std::uint32_t streak{};
		double mutedUntil{ -std::numeric_limits<double>::infinity() };
	};

	struct ByteBucket
	{
		double bytes{};
		double lastSeconds{};
	};

	[[nodiscard]] PacketRateLimit::Result ObserveVoiceHard(
		PacketRateLimit::State& a_state,
		double a_nowMonotonicSeconds,
		std::uint64_t a_fullDatagramBytes) noexcept;

	[[nodiscard]] PacketRateLimit::TokenBucket MakeVoiceSoftBucket(double a_nowMonotonicSeconds) noexcept;
	[[nodiscard]] bool TakeVoiceSoftToken(PacketRateLimit::TokenBucket& a_bucket, double a_nowMonotonicSeconds) noexcept;

	[[nodiscard]] PacketRateLimit::TokenBucket MakeVoiceControlBucket(double a_nowMonotonicSeconds) noexcept;
	[[nodiscard]] bool TakeVoiceControlToken(PacketRateLimit::TokenBucket& a_bucket, double a_nowMonotonicSeconds) noexcept;

	[[nodiscard]] SoftDropMonitor MakeSoftDropMonitor(double a_nowMonotonicSeconds) noexcept;
	[[nodiscard]] bool Record(SoftDropMonitor& a_monitor, double a_nowMonotonicSeconds, bool a_softDropped) noexcept;
	[[nodiscard]] bool IsMuted(const SoftDropMonitor& a_monitor, double a_nowMonotonicSeconds) noexcept;

	[[nodiscard]] ByteBucket MakeVoiceEgressBucket(double a_nowMonotonicSeconds) noexcept;
	[[nodiscard]] bool TakeVoiceEgressBytes(ByteBucket& a_bucket, double a_nowMonotonicSeconds, std::uint64_t a_bytes) noexcept;
}

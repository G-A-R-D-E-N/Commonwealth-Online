#include "F4TVoiceBudget.h"

#include <algorithm>
#include <cmath>

namespace F4T::Voice
{
	PacketRateLimit::Result ObserveVoiceHard(
		PacketRateLimit::State& a_state,
		double a_nowMonotonicSeconds,
		std::uint64_t a_fullDatagramBytes) noexcept
	{
		return PacketRateLimit::Observe(a_state, a_nowMonotonicSeconds, kVoiceHardConfig, a_fullDatagramBytes);
	}

	PacketRateLimit::TokenBucket MakeVoiceSoftBucket(double a_nowMonotonicSeconds) noexcept
	{
		return PacketRateLimit::TokenBucket{ .tokens = kVoiceSoftCapacity, .lastSeconds = std::isfinite(a_nowMonotonicSeconds) ? a_nowMonotonicSeconds : 0.0 };
	}

	bool TakeVoiceSoftToken(PacketRateLimit::TokenBucket& a_bucket, double a_nowMonotonicSeconds) noexcept
	{
		return PacketRateLimit::TakeToken(a_bucket, a_nowMonotonicSeconds, kVoiceSoftRefillPerSecond, kVoiceSoftCapacity);
	}

	PacketRateLimit::TokenBucket MakeVoiceControlBucket(double a_nowMonotonicSeconds) noexcept
	{
		return PacketRateLimit::TokenBucket{ .tokens = kVoiceControlCapacity, .lastSeconds = std::isfinite(a_nowMonotonicSeconds) ? a_nowMonotonicSeconds : 0.0 };
	}

	bool TakeVoiceControlToken(PacketRateLimit::TokenBucket& a_bucket, double a_nowMonotonicSeconds) noexcept
	{
		return PacketRateLimit::TakeToken(a_bucket, a_nowMonotonicSeconds, kVoiceControlRefillPerSecond, kVoiceControlCapacity);
	}

	SoftDropMonitor MakeSoftDropMonitor(double a_nowMonotonicSeconds) noexcept
	{
		return SoftDropMonitor{ .windowStart = PacketRateLimit::Initialize(a_nowMonotonicSeconds).windowStartSeconds };
	}

	bool Record(SoftDropMonitor& a_monitor, double a_nowMonotonicSeconds, bool a_softDropped) noexcept
	{
		const double elapsedWindows = std::floor(a_nowMonotonicSeconds - a_monitor.windowStart);
		if (std::isfinite(a_nowMonotonicSeconds) && elapsedWindows >= 1.0) {
			const double attempts = static_cast<double>(a_monitor.accepted) + a_monitor.dropped;
			const bool overThreshold = a_monitor.dropped > kVoiceAutoMuteDropRatio * attempts;
			a_monitor.streak = overThreshold ? a_monitor.streak + 1U : 0U;
			a_monitor.windowStart += elapsedWindows;
			a_monitor.accepted = 0U;
			a_monitor.dropped = 0U;
			if (a_monitor.streak >= kVoiceAutoMuteStreak) {
				a_monitor.streak = 0U;
				a_monitor.mutedUntil = a_nowMonotonicSeconds + kVoiceAutoMuteSeconds;
				return true;
			}
			if (elapsedWindows > 1.0) {
				a_monitor.streak = 0U;
			}
		}
		if (a_softDropped) {
			++a_monitor.dropped;
		} else {
			++a_monitor.accepted;
		}
		return false;
	}

	bool IsMuted(const SoftDropMonitor& a_monitor, double a_nowMonotonicSeconds) noexcept
	{
		return std::isfinite(a_nowMonotonicSeconds) ? a_nowMonotonicSeconds < a_monitor.mutedUntil : std::isfinite(a_monitor.mutedUntil);
	}

	ByteBucket MakeVoiceEgressBucket(double a_nowMonotonicSeconds) noexcept
	{
		return ByteBucket{ .bytes = kVoiceEgressBurstBytes, .lastSeconds = std::isfinite(a_nowMonotonicSeconds) ? a_nowMonotonicSeconds : 0.0 };
	}

	bool TakeVoiceEgressBytes(ByteBucket& a_bucket, double a_nowMonotonicSeconds, std::uint64_t a_bytes) noexcept
	{
		if (std::isfinite(a_nowMonotonicSeconds) && a_nowMonotonicSeconds > a_bucket.lastSeconds) {
			a_bucket.bytes = (std::min)(kVoiceEgressBurstBytes, a_bucket.bytes + (a_nowMonotonicSeconds - a_bucket.lastSeconds) * kVoiceEgressBytesPerSecond);
			a_bucket.lastSeconds = a_nowMonotonicSeconds;
		}
		const auto requested = static_cast<double>(a_bytes);
		if (a_bucket.bytes < requested) {
			return false;
		}
		a_bucket.bytes -= requested;
		return true;
	}
}

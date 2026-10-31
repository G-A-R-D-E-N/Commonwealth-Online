#pragma once

#include <cstdint>

namespace F4T::PacketRateLimit
{
	inline constexpr std::uint32_t DefaultMaximumPacketsPerSecond = 120;
	inline constexpr std::uint64_t DefaultMaximumBytesPerSecond = 256ULL * 1024ULL;
	inline constexpr std::uint32_t DefaultMaximumViolationWindows = 3;
	inline constexpr double DefaultWindowSeconds = 1.0;
	inline constexpr double DefaultTokenBucketCapacity = 4.0;
	inline constexpr double DefaultTokenBucketRefillPerSecond = 4.0;

	enum class Verdict : std::uint8_t
	{
		Allowed,
		Limited,
		Disconnect
	};

	struct Config
	{
		std::uint32_t maximumPacketsPerWindow{ DefaultMaximumPacketsPerSecond };
		std::uint64_t maximumBytesPerWindow{ DefaultMaximumBytesPerSecond };
		std::uint32_t maximumViolationWindows{ DefaultMaximumViolationWindows };
		double windowSeconds{ DefaultWindowSeconds };
	};

	struct State
	{
		double windowStartSeconds{};
		std::uint32_t windowCount{};
		std::uint64_t windowBytes{};
		std::uint32_t violationWindows{};
		bool windowBlocked{};
	};

	struct Result
	{
		Verdict verdict{ Verdict::Allowed };
		bool windowReset{};
		bool newViolationWindow{};
		bool byteBudgetExceeded{};
	};

	struct TokenBucket
	{
		double tokens{ DefaultTokenBucketCapacity };
		double lastSeconds{};
	};

	[[nodiscard]] State Initialize(double a_nowMonotonicSeconds) noexcept;
	[[nodiscard]] Result Observe(
		State& a_state,
		double a_nowMonotonicSeconds,
		Config a_config = {},
		std::uint64_t a_payloadBytes = 0) noexcept;
	[[nodiscard]] bool TakeToken(
		TokenBucket& a_bucket,
		double a_nowMonotonicSeconds,
		double a_refillPerSecond = DefaultTokenBucketRefillPerSecond,
		double a_capacity = DefaultTokenBucketCapacity) noexcept;
}

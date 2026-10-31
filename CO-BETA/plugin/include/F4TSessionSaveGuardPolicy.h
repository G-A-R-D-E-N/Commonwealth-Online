#pragma once

#include <cstdint>
#include <string_view>

namespace F4T::SessionSaveGuard
{
	inline constexpr std::string_view kSaveBlockedNotice{ "Saving is disabled in multiplayer" };
	inline constexpr std::uint32_t kStablePollsBeforeQueue{ 3 };
	inline constexpr std::uint32_t kMaxQueueAttempts{ 3 };

	enum class Decision
	{
		kAllow,
		kBlock
	};

	[[nodiscard]] constexpr Decision DecidePlayerSave(
		bool a_sessionActive,
		bool a_internalSaveAllowed) noexcept
	{
		if (!a_sessionActive || a_internalSaveAllowed) {
			return Decision::kAllow;
		}
		return Decision::kBlock;
	}

	[[nodiscard]] constexpr bool AcceptsSessionExitRequest(bool a_exitAlreadyPending) noexcept
	{
		return !a_exitAlreadyPending;
	}

	[[nodiscard]] constexpr std::uint32_t NextStablePollCount(std::uint32_t a_previous, bool a_allowed) noexcept
	{
		return a_allowed ? a_previous + 1 : 0;
	}

	[[nodiscard]] constexpr bool QueueReady(std::uint32_t a_stablePolls, std::uint32_t a_attemptsMade) noexcept
	{
		return a_stablePolls >= kStablePollsBeforeQueue && a_attemptsMade < kMaxQueueAttempts;
	}
}

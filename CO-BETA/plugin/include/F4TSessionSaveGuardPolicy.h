#pragma once

#include "F4TSessionSaveGuard.h"

#include <cstdint>

namespace F4T::SessionSaveGuard
{
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

	[[nodiscard]] constexpr bool ShouldRetryQuitSave(bool a_noPollEverAllowed, bool a_firstJoinOnboarding) noexcept
	{
		return !(a_noPollEverAllowed && a_firstJoinOnboarding);
	}

	[[nodiscard]] constexpr bool ShouldAbandonUnsavedFirstJoin(
		bool a_saveLanded,
		bool a_firstJoinOnboarding,
		bool a_holdIsSaveLess,
		bool a_firstJoinSaveInFlight,
		bool a_lossSaveHeld) noexcept
	{
		return !a_saveLanded && a_firstJoinOnboarding && a_holdIsSaveLess && !a_firstJoinSaveInFlight && !a_lossSaveHeld;
	}

	[[nodiscard]] constexpr bool RetryQueuedSave(ArmOwner a_owner, std::uint32_t a_attemptsMade) noexcept
	{
		return a_owner == ArmOwner::kQuit && a_attemptsMade < kMaxQueueAttempts;
	}
}

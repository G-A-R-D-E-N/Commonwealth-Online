#include "F4TFlyOverRestore.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace
{
	using F4T::FlyOver::ExitReason;
	using F4T::FlyOver::IsWorldReset;
	using F4T::FlyOver::RestoreGuard;

	constexpr ExitReason kEngineRestoreReasons[] = { ExitReason::Ready,
		ExitReason::Failed,
		ExitReason::Cancelled,
		ExitReason::KickBan,
		ExitReason::Disconnect,
		ExitReason::SessionTeardown,
		ExitReason::WatchdogTimeout };

	constexpr ExitReason kWorldResetReasons[] = { ExitReason::LoadGame, ExitReason::NewGame, ExitReason::Quit };

	constexpr ExitReason kAllReasons[] = { ExitReason::Ready,
		ExitReason::Failed,
		ExitReason::Cancelled,
		ExitReason::KickBan,
		ExitReason::Disconnect,
		ExitReason::LoadGame,
		ExitReason::NewGame,
		ExitReason::Quit,
		ExitReason::SessionTeardown,
		ExitReason::WatchdogTimeout };

	constexpr std::size_t kEngineRestoreCount = sizeof(kEngineRestoreReasons) / sizeof(kEngineRestoreReasons[0]);
	constexpr std::size_t kWorldResetCount = sizeof(kWorldResetReasons) / sizeof(kWorldResetReasons[0]);
	constexpr std::size_t kAllReasonCount = sizeof(kAllReasons) / sizeof(kAllReasons[0]);
}

int main()
{
	for (std::size_t index = 0; index < kAllReasonCount; ++index) {
		const auto reason = kAllReasons[index];
		const bool worldReset = reason == ExitReason::LoadGame || reason == ExitReason::NewGame || reason == ExitReason::Quit;
		assert(IsWorldReset(reason) == worldReset);
	}

	{
		RestoreGuard guard;
		assert(!guard.IsInstalled());
		assert(!guard.Restore(ExitReason::Ready));
		assert(!guard.IsInstalled());
		assert(guard.RestoreRequests() == 1U);
		assert(guard.EngineRestores() == 0U);
		guard.MarkTakeoverInstalled();
		assert(guard.IsInstalled());
		assert(guard.Restore(ExitReason::Ready));
		assert(!guard.IsInstalled());
		assert(guard.RestoreRequests() == 2U);
		assert(guard.EngineRestores() == 1U);
	}

	for (std::size_t index = 0; index < kEngineRestoreCount; ++index) {
		const auto  reason = kEngineRestoreReasons[index];
		RestoreGuard guard;
		guard.MarkTakeoverInstalled();
		assert(guard.IsInstalled());
		assert(guard.Restore(reason));
		assert(!guard.IsInstalled());
		assert(guard.RestoreRequests() == 1U);
		assert(guard.EngineRestores() == 1U);
		assert(!guard.Restore(reason));
		assert(guard.RestoreRequests() == 2U);
		assert(guard.EngineRestores() == 1U);
	}

	{
		RestoreGuard guard;
		guard.MarkTakeoverInstalled();
		guard.MarkTakeoverInstalled();
		assert(guard.IsInstalled());
		assert(guard.Restore(ExitReason::WatchdogTimeout));
		assert(guard.EngineRestores() == 1U);
		std::uint32_t expectedRequests = 1U;
		for (std::size_t index = 0; index < kAllReasonCount; ++index) {
			++expectedRequests;
			assert(!guard.Restore(kAllReasons[index]));
			assert(!guard.IsInstalled());
			assert(guard.EngineRestores() == 1U);
			assert(guard.RestoreRequests() == expectedRequests);
		}
	}

	for (std::size_t index = 0; index < kWorldResetCount; ++index) {
		const auto  reason = kWorldResetReasons[index];
		RestoreGuard guard;
		guard.MarkTakeoverInstalled();
		assert(guard.IsInstalled());
		assert(!guard.Restore(reason));
		assert(!guard.IsInstalled());
		assert(guard.RestoreRequests() == 1U);
		assert(guard.EngineRestores() == 0U);
		assert(!guard.Restore(ExitReason::Ready));
		assert(!guard.IsInstalled());
		assert(guard.RestoreRequests() == 2U);
		assert(guard.EngineRestores() == 0U);
		guard.MarkTakeoverInstalled();
		assert(guard.IsInstalled());
		assert(guard.Restore(ExitReason::Ready));
		assert(!guard.IsInstalled());
		assert(guard.RestoreRequests() == 3U);
		assert(guard.EngineRestores() == 1U);
	}

	{
		RestoreGuard guard;
		guard.MarkTakeoverInstalled();
		assert(guard.IsInstalled());
		assert(!guard.Restore(ExitReason::NewGame));
		assert(guard.RestoreRequests() == 1U);
		assert(guard.EngineRestores() == 0U);
		guard.MarkTakeoverInstalled();
		assert(guard.IsInstalled());
		guard.AbandonAfterWorldReset();
		assert(!guard.IsInstalled());
		assert(guard.Abandons() == 1U);
		assert(guard.RestoreRequests() == 1U);
		assert(guard.EngineRestores() == 0U);
		assert(!guard.Restore(ExitReason::Ready));
		assert(!guard.IsInstalled());
		assert(guard.RestoreRequests() == 2U);
		assert(guard.EngineRestores() == 0U);
		guard.MarkTakeoverInstalled();
		assert(guard.IsInstalled());
		assert(guard.Restore(ExitReason::Ready));
		assert(!guard.IsInstalled());
		assert(guard.RestoreRequests() == 3U);
		assert(guard.EngineRestores() == 1U);
		assert(!guard.Restore(ExitReason::Ready));
		assert(guard.RestoreRequests() == 4U);
		assert(guard.EngineRestores() == 1U);
		assert(guard.Abandons() == 1U);
	}

	{
		RestoreGuard guard;
		guard.AbandonAfterWorldReset();
		assert(!guard.IsInstalled());
		assert(guard.Abandons() == 1U);
		assert(guard.RestoreRequests() == 0U);
		assert(guard.EngineRestores() == 0U);
	}

	return 0;
}

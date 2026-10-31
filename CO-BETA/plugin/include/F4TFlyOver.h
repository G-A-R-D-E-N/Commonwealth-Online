#pragma once

#include "F4TFlyOverPath.h"
#include "F4TFlyOverPathData.h"
#include "F4TFlyOverRestore.h"

#include <cstdint>

namespace F4T::FlyOver
{
	[[nodiscard]] bool Start();
	[[nodiscard]] bool IsRunning();
	[[nodiscard]] bool Tick();
	void RequestExit();
	[[nodiscard]] bool ExitHoldElapsed(double a_holdSeconds);
	void Restore(ExitReason a_reason);
	void Shutdown();
	[[nodiscard]] double ElapsedSeconds();
	[[nodiscard]] std::uint32_t RestoreCount();
	[[nodiscard]] bool HasPathForCurrentWorld();
}

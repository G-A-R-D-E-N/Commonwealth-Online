#pragma once

namespace F4T::WorkshopLocks
{
	[[nodiscard]] bool CanOpen(bool a_authorized, bool a_locked) noexcept;
	[[nodiscard]] bool CanSetLock(bool a_authorized) noexcept;
}

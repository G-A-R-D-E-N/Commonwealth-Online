#include "F4TWorkshopLocks.h"

namespace F4T::WorkshopLocks
{
	bool CanOpen(bool a_authorized, bool a_locked) noexcept
	{
		return a_authorized || !a_locked;
	}

	bool CanSetLock(bool a_authorized) noexcept
	{
		return a_authorized;
	}
}

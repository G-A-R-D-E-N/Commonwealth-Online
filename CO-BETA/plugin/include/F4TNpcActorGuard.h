#pragma once

#include "F4TNpcSync.h"

namespace F4T::NpcActorGuard
{
	bool Install();

	bool IsInstalled();

	bool IsCoWriteActive();

	bool IsCoKillActive();

	class ScopedCoWrite
	{
	public:
		ScopedCoWrite();
		~ScopedCoWrite();
		ScopedCoWrite(const ScopedCoWrite&) = delete;
		ScopedCoWrite& operator=(const ScopedCoWrite&) = delete;
	};

	class ScopedCoKill
	{
	public:
		ScopedCoKill();
		~ScopedCoKill();
		ScopedCoKill(const ScopedCoKill&) = delete;
		ScopedCoKill& operator=(const ScopedCoKill&) = delete;
	};
}

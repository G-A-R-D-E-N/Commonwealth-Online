#pragma once

#include "F4TRemotePlayerCapacity.h"

#include <cstddef>

namespace F4T::FactionCapacity
{
	inline constexpr std::size_t MaximumFactions = 4096;
	inline constexpr std::size_t MaximumMembers = 64;
	inline constexpr std::size_t MaximumPendingInvitesPerTarget = 256;
	inline constexpr std::size_t MaximumRosterEntries = F4T::RemotePlayerCapacity::MaximumSessionPlayers;
	inline constexpr std::size_t ListPageEntries = 64;
}

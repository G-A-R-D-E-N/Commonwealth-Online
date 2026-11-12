#pragma once

#include <cstdint>
#include <string_view>

namespace F4T::Steam
{
	void SetJoinHeld(bool a_held);
	void HoldJoinForNewHost();
	void AnnounceHostWhenOpen(std::string_view a_name, std::uint64_t a_friendCode);
}

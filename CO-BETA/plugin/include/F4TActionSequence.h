#pragma once

#include "F4TGnsSnapshotEnvelope.h"

#include <cstdint>

namespace F4T::ActionSequence
{
	inline std::uint32_t AllocateNonZero(std::uint32_t& a_next) noexcept
	{
		if (a_next == 0) {
			a_next = 1;
		}
		const auto sequence = a_next;
		a_next = F4T::GnsSnapshotEnvelope::NextSequence(sequence);
		return sequence;
	}
}

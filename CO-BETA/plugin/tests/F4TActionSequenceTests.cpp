#include "F4TActionSequence.h"

#include <cassert>
#include <cstdint>
#include <limits>

namespace
{
	void VerifyWraparound()
	{
		std::uint32_t next = (std::numeric_limits<std::uint32_t>::max)();
		assert(F4T::ActionSequence::AllocateNonZero(next) == (std::numeric_limits<std::uint32_t>::max)());
		assert(next == 1U);
		assert(F4T::ActionSequence::AllocateNonZero(next) == 1U);
		assert(next == 2U);
	}

	void VerifyWrapOrdering()
	{
		std::uint32_t next = (std::numeric_limits<std::uint32_t>::max)();
		const auto last = F4T::ActionSequence::AllocateNonZero(next);
		const auto wrapped = F4T::ActionSequence::AllocateNonZero(next);
		assert(last == (std::numeric_limits<std::uint32_t>::max)());
		assert(wrapped == 1U);
		assert(F4T::GnsSnapshotEnvelope::IsNewer(wrapped, last));
		assert(!F4T::GnsSnapshotEnvelope::IsNewer(last, wrapped));
		assert(!F4T::GnsSnapshotEnvelope::IsNewer(0U, last));
	}

	void VerifyZeroRecovery()
	{
		std::uint32_t next = 0;
		assert(F4T::ActionSequence::AllocateNonZero(next) == 1U);
		assert(next == 2U);
	}
}

int main()
{
	VerifyWraparound();
	VerifyWraparound();
	VerifyWraparound();
	VerifyZeroRecovery();
	VerifyWrapOrdering();
	return 0;
}

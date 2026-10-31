#include "F4TRemotePlayerCapacity.h"

namespace F4T::RemotePlayerCapacity
{
	Admission Decide(std::size_t a_trackedCount, bool a_alreadyTracked) noexcept
	{
		if (a_alreadyTracked) {
			return Admission::UpdateExisting;
		}
		if (a_trackedCount >= MaximumTrackedRemotePlayers) {
			return Admission::RejectAtCapacity;
		}
		return Admission::InsertNew;
	}
}

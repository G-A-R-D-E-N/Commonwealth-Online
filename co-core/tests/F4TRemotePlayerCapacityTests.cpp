#include "F4TRemotePlayerCapacity.h"

#include <cassert>

int main()
{
	using namespace F4T::RemotePlayerCapacity;

	assert(MaximumTrackedRemotePlayers >= MaximumSessionPlayers);

	// Unknown IDs are admitted while there is room.
	assert(Decide(0, false) == Admission::InsertNew);
	assert(Decide(1, false) == Admission::InsertNew);
	assert(Decide(MaximumTrackedRemotePlayers - 1, false) == Admission::InsertNew);

	// Unknown IDs at or beyond capacity are rejected.
	assert(Decide(MaximumTrackedRemotePlayers, false) == Admission::RejectAtCapacity);
	assert(Decide(MaximumTrackedRemotePlayers + 1000, false) == Admission::RejectAtCapacity);

	// Tracked IDs keep updating at capacity; nothing evicts a live player.
	assert(Decide(MaximumTrackedRemotePlayers, true) == Admission::UpdateExisting);
	assert(Decide(MaximumTrackedRemotePlayers + 1000, true) == Admission::UpdateExisting);

	return 0;
}

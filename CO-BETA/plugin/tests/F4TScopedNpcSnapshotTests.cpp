#include "F4TRemoteNpcState.h"

#include <cassert>
#include <chrono>

int main()
{
	using namespace F4T::RemoteNpcState;
	using namespace std::chrono_literals;
	SnapshotCache cache;
	const auto now = std::chrono::steady_clock::time_point{} + 10s;
	NpcState a{}; a.npcId = 1; a.sourceFormId = 100;
	NpcState b{}; b.npcId = 1; b.sourceFormId = 200;
	cache.Replace({"A", "W"}, 10, 1, {a}, now);
	cache.Replace({"B", "W"}, 20, 1, {b}, now);
	const auto both = cache.Snapshot(now, 2s);
	assert(both.size() == 2);
	std::map<NpcKey, NpcState> puppets;
	for (const auto& scoped : both) {
		puppets.emplace(NpcKey{scoped.scope, scoped.hostPlayerId, scoped.epoch, scoped.state.npcId}, scoped.state);
	}
	assert(puppets.size() == 2);
	cache.Replace({"B", "W"}, 20, 1, {}, now + 1s);
	const auto remaining = cache.Snapshot(now + 1s, 2s);
	assert(remaining.size() == 1 && remaining[0].state.sourceFormId == 100);
	cache.Replace({"A", "W"}, 30, 2, {b}, now + 1s);
	const auto changed = cache.Snapshot(now + 1s, 2s);
	assert(changed.size() == 1 && changed[0].hostPlayerId == 30);
	cache.Replace({"A", "W"}, 10, 1, {}, now + 1s);
	assert(cache.Snapshot(now + 1s, 2s).size() == 1);
	cache.Replace({"A", "W"}, 40, 2, {}, now + 1s);
	assert(cache.Snapshot(now + 1s, 2s).size() == 1);
	cache.Replace({"B", "W"}, 20, 1, {b}, now + 1s);
	cache.Replace({"B", "W"}, 30, 2, {}, now + 1s);
	cache.Replace({"B", "W"}, 20, 1, {b}, now + 1s);
	assert(cache.Snapshot(now + 1s, 2s).size() == 1);
	assert(cache.Snapshot(now + 4s, 2s).empty());
	cache.Replace({"A", "W"}, 30, 2, {a}, now + 4s);
	cache.Clear();
	assert(cache.Snapshot(now + 4s, 2s).empty());
}

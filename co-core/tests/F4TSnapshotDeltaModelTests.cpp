#include "F4TSnapshotDeltaModel.h"

#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

namespace
{
	using namespace F4T::Replication;

	void Put(SnapshotState& a_state, std::uint32_t a_entityId, Family a_family, std::vector<std::uint8_t> a_data)
	{
		a_state[ComponentKey{ a_entityId, a_family }] = std::move(a_data);
	}
}

int main()
{
	{
		SnapshotState state;
		Put(state, 1, Family::Transform, { 1, 2 });
		Put(state, 1, Family::NpcState, { 9 });
		assert(ComputeDelta(state, state).Empty());
		assert(SnapshotDelta{}.Empty());
	}

	{
		SnapshotState next;
		Put(next, 7, Family::Transform, { 4, 5, 6 });
		const auto delta = ComputeDelta(SnapshotState{}, next);
		assert(!delta.Empty());
		assert(delta.removedEntities.empty());
		assert(delta.changedEntities.size() == 1);
		assert(delta.changedEntities[0].entityId == 7);
		assert(delta.changedEntities[0].changed.size() == 1);
		assert(delta.changedEntities[0].changed[0].family == Family::Transform);
		assert(delta.changedEntities[0].changed[0].data == (std::vector<std::uint8_t>{ 4, 5, 6 }));
		assert(delta.changedEntities[0].removedComponents.empty());
		assert(ApplyDelta(SnapshotState{}, delta) == next);
	}

	{
		SnapshotState base;
		SnapshotState next;
		Put(base, 3, Family::Transform, { 1 });
		Put(next, 3, Family::Transform, { 2 });
		const auto delta = ComputeDelta(base, next);
		assert(delta.changedEntities.size() == 1);
		assert(delta.changedEntities[0].changed.size() == 1);
		assert(delta.changedEntities[0].changed[0].data == (std::vector<std::uint8_t>{ 2 }));
		assert(delta.changedEntities[0].removedComponents.empty());
		assert(ApplyDelta(base, delta) == next);
	}

	{
		SnapshotState base;
		SnapshotState next;
		Put(base, 2, Family::Transform, { 1 });
		Put(base, 2, Family::NpcState, { 5 });
		Put(next, 2, Family::NpcState, { 5 });
		const auto delta = ComputeDelta(base, next);
		assert(delta.removedEntities.empty());
		assert(delta.changedEntities.size() == 1);
		assert(delta.changedEntities[0].changed.empty());
		assert(delta.changedEntities[0].removedComponents == (std::vector<Family>{ Family::Transform }));
		assert(ApplyDelta(base, delta) == next);
	}

	{
		SnapshotState base;
		Put(base, 4, Family::Transform, { 1 });
		Put(base, 5, Family::Transform, { 2 });
		SnapshotState next;
		Put(next, 5, Family::Transform, { 2 });
		const auto delta = ComputeDelta(base, next);
		assert(delta.removedEntities == (std::vector<std::uint32_t>{ 4 }));
		assert(delta.changedEntities.empty());
		assert(ApplyDelta(base, delta) == next);
	}

	{
		SnapshotState base;
		Put(base, 1, Family::Transform, { 1 });
		Put(base, 2, Family::NpcState, { 2 });
		Put(base, 3, Family::Transform, { 3 });
		SnapshotState next;
		Put(next, 1, Family::Transform, { 1 });
		Put(next, 1, Family::NpcState, { 8 });
		Put(next, 3, Family::NpcState, { 3, 3 });
		Put(next, 4, Family::Transform, { 4 });
		const auto delta = ComputeDelta(base, next);
		assert(ApplyDelta(base, delta) == next);
		assert(ApplyDelta(next, ComputeDelta(next, base)) == base);
	}

	return 0;
}

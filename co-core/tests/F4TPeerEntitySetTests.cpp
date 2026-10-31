#include "F4TPeerEntitySet.h"

#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

namespace
{
	using namespace F4T::Replication;

	SnapshotState MakeState()
	{
		SnapshotState state;
		state[ComponentKey{ 1U, Family::Transform }] = { 0x11U };
		state[ComponentKey{ 1U, Family::NpcState }] = { 0x22U };
		state[ComponentKey{ 2U, Family::Transform }] = { 0x33U };
		state[ComponentKey{ 3U, Family::Transform }] = { 0x44U };
		return state;
	}

	bool AllRegistered(Family) { return true; }
}

int main()
{
	PeerEntitySet sets;
	assert(sets.AddPeer(10U));
	assert(sets.AddPeer(20U));
	assert(!sets.AddPeer(10U));
	assert(sets.RegisterEntity(1U));
	assert(sets.RegisterEntity(2U));
	assert(!sets.RegisterEntity(1U));
	assert(sets.Show(10U, 1U));
	assert(sets.Show(20U, 2U));
	assert((sets.VisibleEntities(10U) == std::vector<EntityId>{ 1U }));
	assert((sets.VisibleEntities(20U) == std::vector<EntityId>{ 2U }));
	assert(sets.IsVisible(10U, 1U) && !sets.IsVisible(10U, 2U));
	assert(sets.IsVisible(20U, 2U) && !sets.IsVisible(20U, 1U));

	assert(sets.RegisterEntity(5U));
	assert(sets.RegisterEntity(3U));
	assert(sets.RegisterEntity(4U));
	assert(sets.Show(10U, 5U));
	assert(sets.Show(10U, 3U));
	assert(sets.Show(10U, 4U));
	assert((sets.VisibleEntities(10U) == std::vector<EntityId>{ 1U, 3U, 4U, 5U }));
	assert(!sets.Show(10U, 3U));
	assert(sets.Hide(10U, 3U));
	assert(!sets.Hide(10U, 3U));
	assert((sets.VisibleEntities(10U) == std::vector<EntityId>{ 1U, 4U, 5U }));

	std::vector<EntityId> iterated;
	sets.ForEachVisibleEntity(10U, [&](EntityId a_entity) { iterated.push_back(a_entity); });
	assert((iterated == std::vector<EntityId>{ 1U, 4U, 5U }));

	{
		const EntityId desired[] = { 5U, 1U, 2U };
		assert(sets.SetVisibleSet(10U, std::span<const EntityId>(desired, 3)));
		assert((sets.VisibleEntities(10U) == std::vector<EntityId>{ 1U, 2U, 5U }));
	}

	{
		const EntityId duplicated[] = { 1U, 2U, 2U };
		assert(!sets.SetVisibleSet(10U, std::span<const EntityId>(duplicated, 3)));
		assert((sets.VisibleEntities(10U) == std::vector<EntityId>{ 1U, 2U, 5U }));
	}
	{
		const EntityId unregistered[] = { 1U, 999U };
		assert(!sets.SetVisibleSet(10U, std::span<const EntityId>(unregistered, 2)));
		assert((sets.VisibleEntities(10U) == std::vector<EntityId>{ 1U, 2U, 5U }));
	}
	{
		const EntityId any[] = { 1U };
		assert(!sets.SetVisibleSet(999U, std::span<const EntityId>(any, 1)));
	}

	assert(!sets.Show(999U, 1U));
	assert(!sets.Show(10U, 12345U));
	assert(!sets.Hide(999U, 1U));
	assert(!sets.DespawnEntity(12345U));
	assert(!sets.RemovePeer(999U));

	assert(sets.Show(20U, 1U));
	assert(sets.RemovePeer(10U));
	assert(!sets.HasPeer(10U));
	assert(sets.VisibleEntities(10U).empty());
	assert(sets.HasPeer(20U));
	assert(sets.IsVisible(20U, 2U));

	assert(sets.AddPeer(10U));
	assert(sets.Show(10U, 2U));
	assert(sets.IsVisible(10U, 2U) && sets.IsVisible(20U, 2U));
	assert(sets.DespawnEntity(2U));
	assert(!sets.IsRegistered(2U));
	assert(!sets.IsVisible(10U, 2U) && !sets.IsVisible(20U, 2U));

	assert((sets.Peers() == std::vector<PeerId>{ 10U, 20U }));

	{
		PeerEntitySet view;
		assert(view.AddPeer(7U));
		assert(view.RegisterEntity(1U));
		assert(view.RegisterEntity(2U));
		assert(view.RegisterEntity(3U));
		assert(view.Show(7U, 1U));
		assert(view.Show(7U, 3U));

		const SnapshotState global = MakeState();
		const SnapshotState selected = SelectPeerSnapshot(view, 7U, global);
		assert(selected.size() == 3);
		assert(selected.count(ComponentKey{ 1U, Family::Transform }) == 1);
		assert(selected.count(ComponentKey{ 1U, Family::NpcState }) == 1);
		assert(selected.count(ComponentKey{ 3U, Family::Transform }) == 1);
		assert(selected.count(ComponentKey{ 2U, Family::Transform }) == 0);

		const auto encoded = EncodeBaseline(selected, 1U);
		assert(encoded.has_value());
		DecodedFrame frame;
		assert(DecodeFrame(*encoded, frame, AllRegistered) == DecodeStatus::Valid);
		assert(frame.baseline == selected);

		assert(SelectPeerSnapshot(view, 999U, global).empty());
	}

	return 0;
}

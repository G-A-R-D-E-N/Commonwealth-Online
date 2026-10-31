#include "F4TDropMirrorTable.h"
#include "F4TSnapshotDeltaWire.h"

#include <cassert>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using namespace F4T::Drops;
	using namespace F4T::Replication;

	const EntityId first = kFirstDroppedEntityId;
	const EntityId second = kFirstDroppedEntityId + 1U;
	const EntityId third = kFirstDroppedEntityId + 2U;

	std::vector<std::uint8_t> Payload(std::uint32_t a_count, std::string a_plugin = "test.esm")
	{
		StableDroppedObject object{
			Json{ { "plugin", std::move(a_plugin) }, { "localFormId", "00000123" }, { "count", a_count } },
			InventoryPosition{ "CAFE", "F00D", 1.0F, 2.0F, 3.0F }
		};
		const auto bytes = EncodeStableDroppedObject(object);
		assert(bytes.has_value());
		return *bytes;
	}

	SnapshotState Baseline(std::initializer_list<std::pair<EntityId, std::uint32_t>> a_entities)
	{
		SnapshotState state;
		for (const auto& [entityId, count] : a_entities) {
			state[ComponentKey{ entityId, Family::StableDroppedItem }] = Payload(count);
		}
		return state;
	}

	DecodedFrame BaselineFrame(std::uint32_t a_sequence, SnapshotState a_state)
	{
		DecodedFrame frame;
		frame.kind = FrameKind::Baseline;
		frame.channel = Channel::Reliable;
		frame.snapshotSequence = a_sequence;
		frame.baseline = std::move(a_state);
		return frame;
	}

	DecodedFrame DeltaFrame(std::uint32_t a_sequence, std::uint32_t a_baseSequence, SnapshotDelta a_delta)
	{
		DecodedFrame frame;
		frame.kind = FrameKind::Delta;
		frame.channel = Channel::Reliable;
		frame.snapshotSequence = a_sequence;
		frame.baselineSequence = a_baseSequence;
		frame.delta = std::move(a_delta);
		return frame;
	}

	std::optional<DecodedFrame> Wire(const std::vector<std::uint8_t>& a_bytes)
	{
		DecodedFrame frame;
		if (DecodeFrame(a_bytes, frame, [](Family) { return true; }) != DecodeStatus::Valid) {
			return std::nullopt;
		}
		return frame;
	}

	void TestBaselineApplies()
	{
		DropMirrorTable table;
		const auto baseline = EncodeBaseline(Baseline({ { first, 1U }, { second, 7U } }), 1, Channel::Reliable);
		assert(baseline.has_value());
		const auto frame = Wire(*baseline);
		assert(frame.has_value());
		assert(table.ApplyFrame(*frame) == MirrorApply::Applied);
		assert(table.Size() == 2);
		assert(table.HasBaseline() && table.BaselineSequence() == 1 && table.LastSequence() == 1);
		const auto found = table.Find(first);
		assert(found.has_value() && found->entityId == first);
		const Json expectedItem{ { "plugin", "test.esm" }, { "localFormId", "00000123" }, { "count", 1U } };
		assert(found->object.item == expectedItem);
		assert(found->object.position.cellId == "0000CAFE" && found->object.position.worldspaceId == "0000F00D");
		assert(found->object.position.x == 1.0F && found->object.position.y == 2.0F && found->object.position.z == 3.0F);
		const auto diff = table.TakeDiff();
		assert(diff.added.size() == 2 && diff.changed.empty() && diff.removed.empty());
		assert(diff.added[0].entityId == first && diff.added[1].entityId == second);
		assert(diff.added[0].materialize && diff.added[1].materialize);
		assert(table.TakeDiff().Empty());
		assert(table.TakeDiff().Size() == 0);
	}

	void TestDeltaChangesAndRemovals()
	{
		DropMirrorTable table;
		const SnapshotState baselineState = Baseline({ { first, 1U }, { second, 7U } });
		const auto baseline = EncodeBaseline(baselineState, 1, Channel::Reliable);
		assert(table.ApplyFrame(*Wire(*baseline)) == MirrorApply::Applied);
		assert(table.TakeDiff().added.size() == 2);

		const SnapshotState next = Baseline({ { first, 3U } });
		const auto delta = ComputeDelta(baselineState, next);
		assert(delta.changedEntities.size() == 1 && delta.removedEntities.size() == 1);
		const auto encoded = EncodeDelta(delta, 2, 1, Channel::Reliable);
		assert(encoded.has_value());
		assert(table.ApplyFrame(*Wire(*encoded)) == MirrorApply::Applied);
		assert(table.Size() == 1 && table.LastSequence() == 2);
		assert(table.Find(first)->object.item.at("count").get<std::uint32_t>() == 3U);
		assert(!table.Find(second).has_value());
		const auto diff = table.TakeDiff();
		assert(diff.changed.size() == 1 && diff.changed[0].entityId == first && diff.changed[0].materialize);
		assert(diff.removed.size() == 1 && diff.removed[0] == second && diff.added.empty());

		const auto removal = ComputeDelta(baselineState, SnapshotState{});
		const auto removalBytes = EncodeDelta(removal, 3, 1, Channel::Reliable);
		assert(removalBytes.has_value());
		assert(table.ApplyFrame(*Wire(*removalBytes)) == MirrorApply::Applied);
		assert(table.Size() == 0 && !table.Find(first).has_value());
		const auto removalDiff = table.TakeDiff();
		assert(removalDiff.removed.size() == 1 && removalDiff.removed[0] == first && removalDiff.changed.empty());

		SnapshotState withTransform = baselineState;
		withTransform[ComponentKey{ first, Family::Transform }] = std::vector<std::uint8_t>{ 1U };
		SnapshotState withoutDrop;
		withoutDrop[ComponentKey{ first, Family::Transform }] = std::vector<std::uint8_t>{ 1U };
		withoutDrop[ComponentKey{ second, Family::StableDroppedItem }] = Payload(7U);
		const auto droppedComponent = ComputeDelta(withTransform, withoutDrop);
		assert(droppedComponent.removedEntities.empty() && droppedComponent.changedEntities.size() == 1);
		assert(droppedComponent.changedEntities[0].changed.empty() &&
			droppedComponent.changedEntities[0].removedComponents.size() == 1 &&
			droppedComponent.changedEntities[0].removedComponents[0] == Family::StableDroppedItem);

		DropMirrorTable componentTable;
		assert(componentTable.ApplyFrame(*Wire(*EncodeBaseline(withTransform, 1, Channel::Reliable))) == MirrorApply::Applied);
		assert(componentTable.TakeDiff().added.size() == 2);
		const auto droppedBytes = EncodeDelta(droppedComponent, 2, 1, Channel::Reliable);
		assert(droppedBytes.has_value());
		assert(componentTable.ApplyFrame(*Wire(*droppedBytes)) == MirrorApply::Applied);
		assert(componentTable.Size() == 1 && !componentTable.Find(first).has_value());
		assert(componentTable.TakeDiff().removed.size() == 1);
	}

	void TestStaleSequencesAreIgnored()
	{
		DropMirrorTable table;
		const auto baseline = EncodeBaseline(Baseline({ { first, 1U } }), 5, Channel::Reliable);
		assert(table.ApplyFrame(*Wire(*baseline)) == MirrorApply::Applied);
		assert(table.TakeDiff().added.size() == 1);

		assert(table.ApplyFrame(*Wire(*baseline)) == MirrorApply::Stale);
		const auto older = EncodeBaseline(Baseline({ { first, 2U } }), 4, Channel::Reliable);
		assert(table.ApplyFrame(*Wire(*older)) == MirrorApply::Stale);
		assert(table.Find(first)->object.item.at("count").get<std::uint32_t>() == 1U);
		assert(table.TakeDiff().Empty());

		SnapshotDelta delta;
		ChangedEntity changed;
		changed.entityId = first;
		changed.changed.push_back(ChangedComponent{ Family::StableDroppedItem, Payload(4U) });
		delta.changedEntities.push_back(changed);
		assert(table.ApplyFrame(*Wire(*EncodeDelta(delta, 6, 4, Channel::Reliable))) == MirrorApply::Stale);
		assert(table.ApplyFrame(*Wire(*EncodeDelta(delta, 5, 5, Channel::Reliable))) == MirrorApply::Stale);
		const auto next = EncodeDelta(delta, 6, 5, Channel::Reliable);
		assert(table.ApplyFrame(*Wire(*next)) == MirrorApply::Applied);
		assert(table.LastSequence() == 6 && table.BaselineSequence() == 5);
		assert(table.TakeDiff().changed.size() == 1);

		const auto identical = EncodeBaseline(Baseline({ { first, 4U } }), 7, Channel::Reliable);
		assert(table.ApplyFrame(*Wire(*identical)) == MirrorApply::Unchanged);
		assert(table.LastSequence() == 7 && table.TakeDiff().Empty());
	}

	void TestRemovalBeforeTakeIsCompressed()
	{
		DropMirrorTable table;
		assert(table.ApplyFrame(BaselineFrame(1, Baseline({ { first, 1U } }))) == MirrorApply::Applied);

		SnapshotDelta removal;
		removal.removedEntities.push_back(first);
		assert(table.ApplyFrame(DeltaFrame(2, 1, removal)) == MirrorApply::Applied);
		assert(table.TakeDiff().Empty());

		assert(table.ApplyFrame(BaselineFrame(3, Baseline({ { first, 1U } }))) == MirrorApply::Applied);
		assert(table.TakeDiff().added.size() == 1);

		DropMirrorTable readded;
		assert(readded.ApplyFrame(BaselineFrame(1, Baseline({ { first, 1U } }))) == MirrorApply::Applied);
		assert(readded.TakeDiff().added.size() == 1);
		assert(readded.ApplyFrame(DeltaFrame(2, 1, removal)) == MirrorApply::Applied);
		assert(readded.ApplyFrame(BaselineFrame(3, Baseline({ { first, 1U } }))) == MirrorApply::Applied);
		const auto diff = readded.TakeDiff();
		assert(diff.changed.size() == 1 && diff.changed[0].entityId == first && diff.removed.empty());
	}

	void TestOversizeAndMalformedAreRejected()
	{
		DropMirrorTable table;
		assert(table.ApplyFrame(BaselineFrame(1, Baseline({ { first, 1U }, { second, 7U } }))) == MirrorApply::Applied);
		assert(table.TakeDiff().added.size() == 2);

		SnapshotState oversize;
		oversize[ComponentKey{ third, Family::StableDroppedItem }] = std::vector<std::uint8_t>(kMaximumMirroredObjectBytes + 1U, 0);
		assert(table.ApplyFrame(BaselineFrame(2, oversize)) == MirrorApply::Rejected);

		SnapshotState malformed;
		malformed[ComponentKey{ third, Family::StableDroppedItem }] = std::vector<std::uint8_t>(29U, 0);
		assert(table.ApplyFrame(BaselineFrame(3, malformed)) == MirrorApply::Rejected);

		SnapshotState belowRange;
		belowRange[ComponentKey{ kFirstDroppedEntityId - 1U, Family::StableDroppedItem }] = Payload(1U);
		assert(table.ApplyFrame(BaselineFrame(4, belowRange)) == MirrorApply::Rejected);

		assert(table.Size() == 2 && table.LastSequence() == 1 && !table.Find(third).has_value());
		assert(table.TakeDiff().Empty());
	}

	void TestEntityCap()
	{
		DropMirrorTable table;
		SnapshotState full;
		for (std::size_t index = 0; index < kMaximumMirroredDrops; ++index) {
			full[ComponentKey{ kFirstDroppedEntityId + static_cast<EntityId>(index), Family::StableDroppedItem }] = Payload(1U);
		}
		assert(table.ApplyFrame(BaselineFrame(1, full)) == MirrorApply::Applied);
		assert(table.Size() == kMaximumMirroredDrops);
		assert(table.TakeDiff().added.size() == kMaximumMirroredDrops);

		full[ComponentKey{ kFirstDroppedEntityId + static_cast<EntityId>(kMaximumMirroredDrops), Family::StableDroppedItem }] = Payload(1U);
		assert(table.ApplyFrame(BaselineFrame(2, std::move(full))) == MirrorApply::Rejected);
		assert(table.Size() == kMaximumMirroredDrops && table.LastSequence() == 1 && table.TakeDiff().Empty());
	}

	void TestUnknownFamiliesAreIgnored()
	{
		DropMirrorTable table;
		SnapshotState unknown;
		unknown[ComponentKey{ first, Family::Transform }] = std::vector<std::uint8_t>{ 1U, 2U, 3U };
		assert(table.ApplyFrame(BaselineFrame(1, unknown)) == MirrorApply::Unchanged);
		assert(table.Size() == 0 && table.HasBaseline() && table.TakeDiff().Empty());

		SnapshotState mixed;
		mixed[ComponentKey{ second, Family::Transform }] = std::vector<std::uint8_t>{ 4U };
		mixed[ComponentKey{ third, Family::StableDroppedItem }] = Payload(9U);
		assert(table.ApplyFrame(BaselineFrame(2, mixed)) == MirrorApply::Applied);
		assert(table.Size() == 1 && table.Find(third).has_value());
		const auto diff = table.TakeDiff();
		assert(diff.added.size() == 1 && diff.added[0].entityId == third);
	}

	void TestPendingDiffCoalescesPerEntity()
	{
		DropMirrorTable table;
		assert(table.ApplyFrame(BaselineFrame(1, Baseline({ { first, 1U } }))) == MirrorApply::Applied);
		assert(table.TakeDiff().added.size() == 1);

		const std::uint32_t rounds = 4096U;
		SnapshotState state = Baseline({ { first, 1U } });
		for (std::uint32_t round = 0; round < rounds; ++round) {
			SnapshotState next = Baseline({ { first, round + 2U } });
			assert(table.ApplyFrame(DeltaFrame(round + 2U, 1U, ComputeDelta(state, next))) == MirrorApply::Applied);
			state = std::move(next);
		}

		const auto diff = table.TakeDiff();
		assert(diff.Size() == 1 && diff.added.empty() && diff.removed.empty());
		assert(diff.changed.size() == 1 && diff.changed[0].entityId == first && diff.changed[0].materialize);
		assert(diff.changed[0].object.item.at("count").get<std::uint32_t>() == rounds + 1U);
	}

	void TestPendingDiffIsBoundedByEntityCap()
	{
		DropMirrorTable table;
		SnapshotState full;
		for (std::size_t index = 0; index < kMaximumMirroredDrops; ++index) {
			full[ComponentKey{ kFirstDroppedEntityId + static_cast<EntityId>(index), Family::StableDroppedItem }] = Payload(1U);
		}
		assert(table.ApplyFrame(BaselineFrame(1, full)) == MirrorApply::Applied);

		for (std::uint32_t round = 0; round < 8U; ++round) {
			SnapshotState next = full;
			for (auto& [key, bytes] : next) {
				if (key.family == Family::StableDroppedItem) {
					bytes = Payload(round + 2U);
				}
			}
			assert(table.ApplyFrame(DeltaFrame(round + 2U, 1U, ComputeDelta(full, next))) == MirrorApply::Applied);
			full = std::move(next);
		}

		const auto diff = table.TakeDiff();
		assert(diff.Size() <= kMaximumMirroredDrops);
		assert(diff.added.size() == kMaximumMirroredDrops && diff.changed.empty() && diff.removed.empty());
	}

	void TestClearResets()
	{
		DropMirrorTable table;
		assert(table.ApplyFrame(BaselineFrame(1, Baseline({ { first, 1U } }))) == MirrorApply::Applied);
		assert(table.TakeDiff().added.size() == 1);
		table.Clear();
		assert(table.Size() == 0 && !table.HasBaseline() && table.BaselineSequence() == 0 && table.LastSequence() == 0);
		assert(table.TakeDiff().Empty() && table.Entries().empty());
		assert(table.ApplyFrame(DeltaFrame(2, 1, SnapshotDelta{})) == MirrorApply::Stale);
		assert(table.Entries().empty());
	}
}

int main()
{
	TestBaselineApplies();
	TestDeltaChangesAndRemovals();
	TestStaleSequencesAreIgnored();
	TestRemovalBeforeTakeIsCompressed();
	TestOversizeAndMalformedAreRejected();
	TestEntityCap();
	TestUnknownFamiliesAreIgnored();
	TestPendingDiffCoalescesPerEntity();
	TestPendingDiffIsBoundedByEntityCap();
	TestClearResets();
	return 0;
}

#include "F4TDropMirrorTable.h"

#include <utility>

namespace F4T::Drops
{
	namespace
	{
		bool SameObject(const StableDroppedObject& a_left, const StableDroppedObject& a_right)
		{
			return a_left.item == a_right.item &&
			       a_left.position.cellId == a_right.position.cellId &&
			       a_left.position.worldspaceId == a_right.position.worldspaceId &&
			       a_left.position.x == a_right.position.x &&
			       a_left.position.y == a_right.position.y &&
			       a_left.position.z == a_right.position.z;
		}
	}

	bool MirrorDiff::Empty() const noexcept
	{
		return added.empty() && changed.empty() && removed.empty();
	}

	std::size_t MirrorDiff::Size() const noexcept
	{
		return added.size() + changed.size() + removed.size();
	}

	MirrorApply DropMirrorTable::ApplyFrame(const F4T::Replication::DecodedFrame& a_frame)
	{
		const auto channel = a_frame.channel;
		if (a_frame.snapshotSequence == 0 || !F4T::Replication::IsValidChannel(channel)) {
			return MirrorApply::Rejected;
		}

		F4T::Replication::SnapshotState candidate;
		if (a_frame.kind == F4T::Replication::FrameKind::Baseline) {
			if (receiver.HasState(channel) &&
				!F4T::GnsSnapshotEnvelope::IsNewer(a_frame.snapshotSequence, receiver.LastSnapshotSequence(channel))) {
				return MirrorApply::Stale;
			}
			candidate = a_frame.baseline;
		} else {
			if (a_frame.baselineSequence == 0 ||
				!receiver.HasState(channel) ||
				a_frame.baselineSequence != receiver.BaselineSequence(channel) ||
				!F4T::GnsSnapshotEnvelope::IsNewer(a_frame.snapshotSequence, receiver.LastSnapshotSequence(channel))) {
				return MirrorApply::Stale;
			}
			candidate = F4T::Replication::ApplyDelta(receiver.Baseline(channel), a_frame.delta);
		}

		const auto decoded = DecodeView(candidate);
		if (!decoded) {
			return MirrorApply::Rejected;
		}

		const bool accepted = a_frame.kind == F4T::Replication::FrameKind::Baseline ?
			receiver.AcceptBaseline(std::move(candidate), a_frame.snapshotSequence, channel) :
			receiver.AcceptDelta(a_frame.delta, a_frame.snapshotSequence, a_frame.baselineSequence, channel);
		if (!accepted) {
			return MirrorApply::Stale;
		}

		bool changed = drops.size() != decoded->size();
		if (!changed) {
			for (const auto& entry : *decoded) {
				const auto found = drops.find(entry.first);
				if (found == drops.end() || !SameObject(found->second, entry.second)) {
					changed = true;
					break;
				}
			}
		}
		RecordDiff(drops, *decoded);
		drops = std::move(*decoded);
		return changed ? MirrorApply::Applied : MirrorApply::Unchanged;
	}

	std::optional<MirroredDrop> DropMirrorTable::Find(EntityId a_entityId) const
	{
		const auto found = drops.find(a_entityId);
		if (found == drops.end()) {
			return std::nullopt;
		}
		return MirroredDrop{ found->first, found->second };
	}

	std::vector<MirroredDrop> DropMirrorTable::Entries() const
	{
		std::vector<MirroredDrop> entries;
		entries.reserve(drops.size());
		for (const auto& [entityId, object] : drops) {
			entries.push_back(MirroredDrop{ entityId, object });
		}
		return entries;
	}

	std::size_t DropMirrorTable::Size() const noexcept
	{
		return drops.size();
	}

	bool DropMirrorTable::HasBaseline() const noexcept
	{
		return receiver.HasState(kMirrorChannel);
	}

	std::uint32_t DropMirrorTable::BaselineSequence() const noexcept
	{
		return receiver.BaselineSequence(kMirrorChannel);
	}

	std::uint32_t DropMirrorTable::LastSequence() const noexcept
	{
		return receiver.LastSnapshotSequence(kMirrorChannel);
	}

	MirrorDiff DropMirrorTable::TakeDiff()
	{
		MirrorDiff diff;
		diff.added.reserve(pending.added.size());
		diff.changed.reserve(pending.changed.size());
		diff.removed.reserve(pending.removed.size());
		for (const auto& entry : pending.added) {
			diff.added.push_back(entry.second);
		}
		for (const auto& entry : pending.changed) {
			diff.changed.push_back(entry.second);
		}
		for (const auto entityId : pending.removed) {
			diff.removed.push_back(entityId);
		}
		pending = PendingMirrorDiff{};
		return diff;
	}

	void DropMirrorTable::Clear() noexcept
	{
		receiver = F4T::Replication::SnapshotReceiver{};
		drops.clear();
		pending = PendingMirrorDiff{};
	}

	std::optional<std::map<EntityId, StableDroppedObject>> DropMirrorTable::DecodeView(
		const F4T::Replication::SnapshotState& a_view) const
	{
		std::map<EntityId, StableDroppedObject> decoded;
		for (const auto& [key, bytes] : a_view) {
			if (key.family != F4T::Replication::Family::StableDroppedItem) {
				continue;
			}
			if (key.entityId < kFirstDroppedEntityId || bytes.size() > kMaximumMirroredObjectBytes ||
				decoded.size() >= kMaximumMirroredDrops) {
				return std::nullopt;
			}
			const auto object = DecodeStableDroppedObject(bytes);
			if (!object) {
				return std::nullopt;
			}
			decoded.emplace(key.entityId, *object);
		}
		return decoded;
	}

	void DropMirrorTable::RecordDiff(
		const std::map<EntityId, StableDroppedObject>& a_before,
		const std::map<EntityId, StableDroppedObject>& a_after)
	{
		for (const auto& [entityId, object] : a_after) {
			const auto before = a_before.find(entityId);
			if (before != a_before.end() && SameObject(before->second, object)) {
				continue;
			}

			if (pending.removed.erase(entityId) != 0) {
				pending.changed.insert_or_assign(entityId, MirrorDiffEntry{ entityId, object, true });
				continue;
			}
			if (pending.added.contains(entityId) || before == a_before.end()) {
				pending.added.insert_or_assign(entityId, MirrorDiffEntry{ entityId, object, true });
				continue;
			}
			pending.changed.insert_or_assign(entityId, MirrorDiffEntry{ entityId, object, true });
		}

		for (const auto& tracked : a_before) {
			const auto entityId = tracked.first;
			if (a_after.contains(entityId)) {
				continue;
			}
			if (pending.added.erase(entityId) != 0) {
				continue;
			}
			pending.changed.erase(entityId);
			pending.removed.insert(entityId);
		}
	}
}

#pragma once

#include "F4TDropLedger.h"
#include "F4TSnapshotReceiver.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace F4T::Drops
{
	inline constexpr std::size_t kMaximumMirroredDrops = kMaximumLiveDrops;
	inline constexpr std::size_t kMaximumMirroredObjectBytes = kMaximumDropBaselineBytes;
	inline constexpr F4T::Replication::Channel kMirrorChannel = F4T::Replication::Channel::Reliable;

	struct MirroredDrop
	{
		EntityId entityId{};
		StableDroppedObject object;
	};

	struct MirrorDiffEntry
	{
		EntityId entityId{};
		StableDroppedObject object;
		bool materialize{ true };
	};

	struct MirrorDiff
	{
		std::vector<MirrorDiffEntry> added;
		std::vector<MirrorDiffEntry> changed;
		std::vector<EntityId> removed;

		[[nodiscard]] bool Empty() const noexcept;
		[[nodiscard]] std::size_t Size() const noexcept;
	};

	struct PendingMirrorDiff
	{
		std::map<EntityId, MirrorDiffEntry> added;
		std::map<EntityId, MirrorDiffEntry> changed;
		std::set<EntityId> removed;
	};

	enum class MirrorApply : std::uint8_t
	{
		Applied,
		Unchanged,
		Stale,
		Rejected
	};

	class DropMirrorTable
	{
	public:
		[[nodiscard]] MirrorApply ApplyFrame(const F4T::Replication::DecodedFrame& a_frame);
		[[nodiscard]] std::optional<MirroredDrop> Find(EntityId a_entityId) const;
		[[nodiscard]] std::vector<MirroredDrop> Entries() const;
		[[nodiscard]] std::size_t Size() const noexcept;
		[[nodiscard]] bool HasBaseline() const noexcept;
		[[nodiscard]] std::uint32_t BaselineSequence() const noexcept;
		[[nodiscard]] std::uint32_t LastSequence() const noexcept;
		[[nodiscard]] MirrorDiff TakeDiff();
		void Clear() noexcept;

	private:
		[[nodiscard]] std::optional<std::map<EntityId, StableDroppedObject>> DecodeView(
			const F4T::Replication::SnapshotState& a_view) const;
		void RecordDiff(const std::map<EntityId, StableDroppedObject>& a_before, const std::map<EntityId, StableDroppedObject>& a_after);

		F4T::Replication::SnapshotReceiver receiver;
		std::map<EntityId, StableDroppedObject> drops;
		PendingMirrorDiff pending;
	};
}

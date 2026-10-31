#include "F4TSnapshotDelta.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace
{
	using namespace F4T::Replication;

	struct TransformTestComponent : ISnapshotComponent
	{
		std::uint32_t x{};
		std::uint16_t yaw{};

		[[nodiscard]] Family ComponentFamily() const noexcept override { return Family::Transform; }
		[[nodiscard]] AuthorityClass Authority() const noexcept override { return AuthorityClass::Client; }
		[[nodiscard]] Channel PreferredChannel() const noexcept override { return Channel::Unreliable; }
		void Serialize(SnapshotStream& a_stream) noexcept override
		{
			a_stream.U32(x);
			a_stream.U16(yaw);
		}
	};

	struct NpcStateTestComponent : ISnapshotComponent
	{
		std::uint32_t health{};

		[[nodiscard]] Family ComponentFamily() const noexcept override { return Family::NpcState; }
		[[nodiscard]] AuthorityClass Authority() const noexcept override { return AuthorityClass::Server; }
		[[nodiscard]] Channel PreferredChannel() const noexcept override { return Channel::Reliable; }
		void Serialize(SnapshotStream& a_stream) noexcept override { a_stream.U32(health); }
	};

	std::unique_ptr<TransformTestComponent> MakeTransform(std::uint32_t a_x, std::uint16_t a_yaw)
	{
		auto component = std::make_unique<TransformTestComponent>();
		component->x = a_x;
		component->yaw = a_yaw;
		return component;
	}

	std::unique_ptr<NpcStateTestComponent> MakeNpc(std::uint32_t a_health)
	{
		auto component = std::make_unique<NpcStateTestComponent>();
		component->health = a_health;
		return component;
	}

	SnapshotState StateOf(const std::vector<EntityComponentSet>& a_entities)
	{
		return BuildSnapshotState(std::span<const EntityComponentSet>(a_entities.data(), a_entities.size()));
	}

	bool AllRegistered(Family) { return true; }

	std::vector<std::uint8_t> Encoded(std::optional<std::vector<std::uint8_t>> a_bytes)
	{
		assert(a_bytes.has_value());
		return std::move(*a_bytes);
	}

	SnapshotState DecodeBaselineState(std::span<const std::uint8_t> a_bytes)
	{
		DecodedFrame frame;
		const auto status = DecodeFrame(a_bytes, frame, AllRegistered);
		assert(status == DecodeStatus::Valid);
		assert(frame.kind == FrameKind::Baseline);
		return frame.baseline;
	}

	SnapshotDelta DecodeDeltaValue(std::span<const std::uint8_t> a_bytes, std::uint32_t& a_snapSeq, std::uint32_t& a_baseSeq)
	{
		DecodedFrame frame;
		const auto status = DecodeFrame(a_bytes, frame, AllRegistered);
		assert(status == DecodeStatus::Valid);
		assert(frame.kind == FrameKind::Delta);
		a_snapSeq = frame.snapshotSequence;
		a_baseSeq = frame.baselineSequence;
		return frame.delta;
	}

	DecodeStatus StatusOf(const std::vector<std::uint8_t>& a_bytes)
	{
		DecodedFrame frame;
		return DecodeFrame(a_bytes, frame, AllRegistered);
	}

	// Raw wire assembler so malformed frames (duplicates, conflicts, bad headers) can be built exactly.
	struct FrameBuf
	{
		std::vector<std::uint8_t> b;
		void u8(std::uint32_t v) { b.push_back(static_cast<std::uint8_t>(v & 0xFFU)); }
		void u16(std::uint32_t v) { u8(v >> 8U); u8(v); }
		void u32(std::uint32_t v) { u8(v >> 24U); u8(v >> 16U); u8(v >> 8U); u8(v); }
		void header(std::uint8_t a_kind, std::uint32_t a_snap, std::uint32_t a_base, std::uint8_t a_flags = 0, std::uint8_t a_reserved = 0)
		{
			u8(SnapDeltaVersion);
			u8(a_kind);
			u8(a_flags);
			u8(a_reserved);
			u32(a_snap);
			u32(a_base);
		}
		void component(std::uint8_t a_family, const std::vector<std::uint8_t>& a_data)
		{
			u8(a_family);
			u16(static_cast<std::uint16_t>(a_data.size()));
			for (const std::uint8_t x : a_data) {
				u8(x);
			}
		}
	};

	SnapshotState OneEntityWithComponents(int a_count)
	{
		SnapshotState state;
		for (int f = 0; f < a_count; ++f) {
			state[ComponentKey{ 1U, static_cast<Family>(f) }] = std::vector<std::uint8_t>(1, 0xABU);
		}
		return state;
	}

	SnapshotState ManyEntities(std::size_t a_count)
	{
		SnapshotState state;
		for (std::size_t e = 0; e < a_count; ++e) {
			state[ComponentKey{ static_cast<std::uint32_t>(e + 1), Family::Transform }] = std::vector<std::uint8_t>(1, 0xABU);
		}
		return state;
	}
}

	void TestChannelFlagsAndPartition()
	{
		SnapshotState state;
		state[ComponentKey{ 7U, Family::Transform }] = { 1U };
		const auto reliableBytes = EncodeBaseline(state, 7U, Channel::Reliable);
		const auto unreliableBytes = EncodeBaseline(state, 8U, Channel::Unreliable);
		assert(reliableBytes.has_value() && unreliableBytes.has_value());
		DecodedFrame reliableFrame;
		DecodedFrame unreliableFrame;
		assert(DecodeFrame(*reliableBytes, reliableFrame, AllRegistered) == DecodeStatus::Valid);
		assert(DecodeFrame(*unreliableBytes, unreliableFrame, AllRegistered) == DecodeStatus::Valid);
		assert(reliableFrame.channel == Channel::Reliable);
		assert(unreliableFrame.channel == Channel::Unreliable);
		auto unknownFlags = *unreliableBytes;
		unknownFlags[2] = 0x02U;
		assert(DecodeFrame(unknownFlags, unreliableFrame, AllRegistered) == DecodeStatus::ReservedNotZero);

		ComponentRegistry registry;
		assert(registry.RegisterDefinition(Family::Transform, AuthorityClass::Server, Channel::Reliable));
		assert(registry.RegisterDefinition(Family::NpcState, AuthorityClass::Server, Channel::Unreliable));
		const auto definitions = registry.Freeze();
		assert(definitions.has_value());
		SnapshotState mixed = state;
		mixed[ComponentKey{ 7U, Family::NpcState }] = { 2U };
		const auto partitioned = PartitionSnapshotByChannel(mixed, *definitions);
		assert(partitioned.has_value());
		assert(partitioned->reliableState.size() == 1U);
		assert(partitioned->unreliableState.size() == 1U);
		SnapshotReceiver receiver;
		assert(receiver.AcceptBaseline(partitioned->reliableState, 1U, Channel::Reliable));
		assert(receiver.AcceptBaseline(partitioned->unreliableState, 1U, Channel::Unreliable));
		assert(receiver.Baseline(Channel::Reliable) == partitioned->reliableState);
		assert(receiver.Baseline(Channel::Unreliable) == partitioned->unreliableState);
	}

int main()
{
	TestChannelFlagsAndPartition();
	// ---- Functional round-trip + no-chaining (the DoD) ----
	std::vector<EntityComponentSet> baselineEntities;
	{
		EntityComponentSet e100;
		e100.entityId = 100U;
		e100.components.push_back(MakeTransform(0x11223344U, 0xABCDU));
		e100.components.push_back(MakeNpc(250U));
		baselineEntities.push_back(std::move(e100));

		EntityComponentSet e200;
		e200.entityId = 200U;
		e200.components.push_back(MakeTransform(7U, 9U));
		baselineEntities.push_back(std::move(e200));
	}
	const SnapshotState baseline = StateOf(baselineEntities);
	assert(baseline.size() == 3);

	std::vector<EntityComponentSet> nextEntities;
	{
		EntityComponentSet e100;
		e100.entityId = 100U;
		e100.components.push_back(MakeTransform(0x11223344U, 0xABCDU));
		e100.components.push_back(MakeNpc(100U));
		nextEntities.push_back(std::move(e100));

		EntityComponentSet e300;
		e300.entityId = 300U;
		e300.components.push_back(MakeTransform(5U, 6U));
		nextEntities.push_back(std::move(e300));
	}
	const SnapshotState next = StateOf(nextEntities);

	const SnapshotDelta delta = ComputeDelta(baseline, next);
	assert(delta.removedEntities.size() == 1);
	assert(delta.removedEntities[0] == 200U);
	assert(delta.changedEntities.size() == 2);
	assert(ApplyDelta(baseline, delta) == next);

	const auto baselineBytes = Encoded(EncodeBaseline(baseline, 10U));
	const auto deltaBytes = Encoded(EncodeDelta(delta, 11U, 10U));
	assert(DecodeBaselineState(baselineBytes) == baseline);
	std::uint32_t decodedSnap = 0;
	std::uint32_t decodedBase = 0;
	const SnapshotDelta decodedDelta = DecodeDeltaValue(deltaBytes, decodedSnap, decodedBase);
	assert(decodedSnap == 11U);
	assert(decodedBase == 10U);

	SnapshotReceiver receiver;
	assert(!receiver.AcceptDelta(decodedDelta, 11U, 10U));
	assert(receiver.AcceptBaseline(DecodeBaselineState(baselineBytes), 10U));
	assert(receiver.CurrentView() == baseline);
	assert(receiver.AcceptDelta(decodedDelta, 11U, 10U));
	assert(receiver.CurrentView() == next);
	assert(receiver.BaselineSequence() == 10U);
	assert(receiver.LastSnapshotSequence() == 11U);

	std::vector<EntityComponentSet> otherEntities;
	{
		EntityComponentSet e100;
		e100.entityId = 100U;
		e100.components.push_back(MakeTransform(0xFFFFFFFFU, 1U));
		e100.components.push_back(MakeNpc(250U));
		otherEntities.push_back(std::move(e100));

		EntityComponentSet e200;
		e200.entityId = 200U;
		e200.components.push_back(MakeTransform(7U, 9U));
		otherEntities.push_back(std::move(e200));
	}
	const SnapshotState other = StateOf(otherEntities);
	const SnapshotDelta otherDelta = ComputeDelta(baseline, other);
	assert(receiver.AcceptDelta(otherDelta, 12U, 10U));
	assert(receiver.CurrentView() == other);
	assert(receiver.BaselineSequence() == 10U);
	assert(!receiver.AcceptDelta(otherDelta, 13U, 999U));
	assert(receiver.CurrentView() == other);
	assert(!receiver.AcceptDelta(decodedDelta, 11U, 10U));
	assert(receiver.CurrentView() == other);

	SnapshotReceiver ordered;
	assert(ordered.AcceptBaseline(baseline, 20U));
	assert(!ordered.AcceptBaseline(next, 5U));
	assert(ordered.CurrentView() == baseline);

	const SnapshotDelta emptyDelta = ComputeDelta(baseline, baseline);
	assert(emptyDelta.Empty());
	assert(ApplyDelta(baseline, emptyDelta) == baseline);

	// ---- Component removed from an entity (not the whole entity) ----
	std::vector<EntityComponentSet> droppedComponentEntities;
	{
		EntityComponentSet e100;
		e100.entityId = 100U;
		e100.components.push_back(MakeTransform(0x11223344U, 0xABCDU));
		droppedComponentEntities.push_back(std::move(e100));

		EntityComponentSet e200;
		e200.entityId = 200U;
		e200.components.push_back(MakeTransform(7U, 9U));
		droppedComponentEntities.push_back(std::move(e200));
	}
	const SnapshotState droppedComponent = StateOf(droppedComponentEntities);
	const SnapshotDelta dropDelta = ComputeDelta(baseline, droppedComponent);
	assert(dropDelta.removedEntities.empty());
	assert(dropDelta.changedEntities.size() == 1);
	assert(dropDelta.changedEntities[0].entityId == 100U);
	assert(dropDelta.changedEntities[0].changed.empty());
	assert(dropDelta.changedEntities[0].removedComponents.size() == 1);
	assert(dropDelta.changedEntities[0].removedComponents[0] == Family::NpcState);
	assert(ApplyDelta(baseline, dropDelta) == droppedComponent);

	const auto dropBytes = Encoded(EncodeDelta(dropDelta, 30U, 20U));
	std::uint32_t dropSnap = 0;
	std::uint32_t dropBase = 0;
	const SnapshotDelta decodedDrop = DecodeDeltaValue(dropBytes, dropSnap, dropBase);
	assert(ApplyDelta(baseline, decodedDrop) == droppedComponent);

	const SnapshotDelta fromEmpty = ComputeDelta(SnapshotState{}, baseline);
	assert(fromEmpty.removedEntities.empty());
	assert(ApplyDelta(SnapshotState{}, fromEmpty) == baseline);

	// ---- Encode: fallible bounds and exact boundaries ----
	assert(!EncodeBaseline(baseline, 0U).has_value());          // zero snapshot sequence
	assert(!EncodeDelta(delta, 0U, 10U).has_value());           // zero snapshot sequence
	assert(!EncodeDelta(delta, 11U, 0U).has_value());           // zero baseline sequence

	assert(EncodeBaseline(ManyEntities(SnapMaxEntities), 1U).has_value());        // exactly 4096 entities
	assert(!EncodeBaseline(ManyEntities(SnapMaxEntities + 1U), 1U).has_value());  // 4097 entities

	assert(EncodeBaseline(OneEntityWithComponents(SnapMaxComponentsPerEntity), 1U).has_value());       // 255 components
	assert(!EncodeBaseline(OneEntityWithComponents(SnapMaxComponentsPerEntity + 1), 1U).has_value());  // 256 components

	{
		SnapshotState atMax;
		atMax[ComponentKey{ 1U, Family::Transform }] = std::vector<std::uint8_t>(SnapMaxComponentBytes, 0U);
		assert(EncodeBaseline(atMax, 1U).has_value());          // 65535-byte payload
		SnapshotState overMax;
		overMax[ComponentKey{ 1U, Family::Transform }] = std::vector<std::uint8_t>(std::size_t{ SnapMaxComponentBytes } + 1U, 0U);
		assert(!EncodeBaseline(overMax, 1U).has_value());       // 65536-byte payload
	}

	{
		SnapshotState huge;  // 17 * 65535 payload bytes exceeds the 1 MiB frame ceiling
		for (int f = 0; f < 17; ++f) {
			huge[ComponentKey{ 1U, static_cast<Family>(f) }] = std::vector<std::uint8_t>(SnapMaxComponentBytes, 0U);
		}
		assert(!EncodeBaseline(huge, 1U).has_value());          // final frame > 1 MiB
	}

	// Frame-size ceiling is enforced from the precomputed size, exactly at the boundary.
	{
		SnapshotState atLimit;  // 15 * (8 + 65535) + (8 + 65409) + 14 == 1048576 == SnapMaxFrameBytes
		for (int e = 0; e < 15; ++e) {
			atLimit[ComponentKey{ static_cast<std::uint32_t>(e + 1), Family::Transform }] = std::vector<std::uint8_t>(SnapMaxComponentBytes, 0U);
		}
		atLimit[ComponentKey{ 16U, Family::Transform }] = std::vector<std::uint8_t>(65409U, 0U);
		auto atLimitBytes = EncodeBaseline(atLimit, 1U);
		assert(atLimitBytes.has_value());
		assert(atLimitBytes->size() == SnapMaxFrameBytes);
		assert(StatusOf(*atLimitBytes) == DecodeStatus::Valid);
		atLimit[ComponentKey{ 16U, Family::Transform }] = std::vector<std::uint8_t>(65410U, 0U);  // one byte over
		assert(!EncodeBaseline(atLimit, 1U).has_value());
	}

	// Delta frame-size ceiling with an empty removedEntities list, exactly at 1 MiB and one byte over.
	{
		SnapshotDelta atLimit;
		for (int e = 0; e < 15; ++e) {
			atLimit.changedEntities.push_back(ChangedEntity{ static_cast<std::uint32_t>(e + 1),
				{ ChangedComponent{ Family::Transform, std::vector<std::uint8_t>(SnapMaxComponentBytes, 0U) } }, {} });
		}
		atLimit.changedEntities.push_back(ChangedEntity{ 16U,
			{ ChangedComponent{ Family::Transform, std::vector<std::uint8_t>(65391U, 0U) } }, {} });
		auto atLimitBytes = EncodeDelta(atLimit, 11U, 10U);
		assert(atLimitBytes.has_value());
		assert(atLimitBytes->size() == SnapMaxFrameBytes);
		assert(StatusOf(*atLimitBytes) == DecodeStatus::Valid);
		atLimit.changedEntities.back().changed[0].data = std::vector<std::uint8_t>(65392U, 0U);
		assert(!EncodeDelta(atLimit, 11U, 10U).has_value());
	}

	{
		SnapshotDelta emptyRecord;  // a changed-entity record with neither changed nor removed components
		emptyRecord.changedEntities.push_back(ChangedEntity{ 42U, {}, {} });
		assert(!EncodeDelta(emptyRecord, 11U, 10U).has_value());
	}

	// ---- Encode: the encoder never emits a frame its own decoder rejects ----
	{
		SnapshotDelta d;  // duplicate changed entity
		d.changedEntities.push_back(ChangedEntity{ 100U, { ChangedComponent{ Family::Transform, { 0U } } }, {} });
		d.changedEntities.push_back(ChangedEntity{ 100U, { ChangedComponent{ Family::NpcState, { 0U } } }, {} });
		assert(!EncodeDelta(d, 11U, 10U).has_value());
	}
	{
		SnapshotDelta d;  // duplicate changed family on one entity
		d.changedEntities.push_back(ChangedEntity{ 100U, { ChangedComponent{ Family::Transform, { 0U } }, ChangedComponent{ Family::Transform, { 1U } } }, {} });
		assert(!EncodeDelta(d, 11U, 10U).has_value());
	}
	{
		SnapshotDelta d;  // duplicate removed family on one entity
		d.changedEntities.push_back(ChangedEntity{ 100U, {}, { Family::NpcState, Family::NpcState } });
		assert(!EncodeDelta(d, 11U, 10U).has_value());
	}
	{
		SnapshotDelta d;  // a family both changed and removed on one entity
		d.changedEntities.push_back(ChangedEntity{ 100U, { ChangedComponent{ Family::Transform, { 0U } } }, { Family::Transform } });
		assert(!EncodeDelta(d, 11U, 10U).has_value());
	}
	{
		SnapshotDelta d;  // an entity both changed and removed
		d.changedEntities.push_back(ChangedEntity{ 100U, { ChangedComponent{ Family::Transform, { 0U } } }, {} });
		d.removedEntities.push_back(100U);
		assert(!EncodeDelta(d, 11U, 10U).has_value());
	}
	{
		SnapshotDelta d;  // duplicate removed entity
		d.removedEntities.push_back(100U);
		d.removedEntities.push_back(100U);
		assert(!EncodeDelta(d, 11U, 10U).has_value());
	}

	// ---- SnapshotReceiver: sequence-zero hardening ----
	{
		SnapshotReceiver r;
		assert(!r.AcceptBaseline(baseline, 0U));  // a zero-sequence baseline is never accepted, even as the first
		assert(!r.HasState());
		assert(r.AcceptBaseline(baseline, 10U));
		assert(!r.AcceptDelta(delta, 0U, 10U));   // zero snapshot sequence
		assert(!r.AcceptDelta(delta, 11U, 0U));   // zero baseline sequence
		assert(r.CurrentView() == baseline);
		assert(r.AcceptDelta(delta, 11U, 10U));
		assert(r.CurrentView() == next);
	}

	{
		SnapshotReceiver r;
		assert(r.AcceptBaseline(baseline, 10U, Channel::Reliable));
		assert(r.AcceptBaseline(next, 20U, Channel::Unreliable));

		assert(r.HasState(Channel::Reliable));
		assert(r.CurrentView(Channel::Reliable) == baseline);
		assert(r.Baseline(Channel::Reliable) == baseline);
		assert(r.BaselineSequence(Channel::Reliable) == 10U);
		assert(r.LastSnapshotSequence(Channel::Reliable) == 10U);

		assert(r.HasState(Channel::Unreliable));
		assert(r.CurrentView(Channel::Unreliable) == next);
		assert(r.Baseline(Channel::Unreliable) == next);
		assert(r.BaselineSequence(Channel::Unreliable) == 20U);
		assert(r.LastSnapshotSequence(Channel::Unreliable) == 20U);

		const auto invalidChannel = static_cast<Channel>(255);
		assert(!r.HasState(invalidChannel));
		assert(r.CurrentView(invalidChannel).empty());
		assert(r.Baseline(invalidChannel).empty());
		assert(r.BaselineSequence(invalidChannel) == 0U);
		assert(r.LastSnapshotSequence(invalidChannel) == 0U);
		assert(r.CurrentView(invalidChannel) != r.CurrentView(Channel::Unreliable));
		assert(r.Baseline(invalidChannel) != r.Baseline(Channel::Unreliable));
		assert(&r.CurrentView(invalidChannel) != &r.CurrentView(Channel::Unreliable));
		assert(&r.Baseline(invalidChannel) != &r.Baseline(Channel::Unreliable));
		assert(!r.AcceptBaseline(next, 21U, invalidChannel));
		assert(!r.AcceptDelta(delta, 21U, 20U, invalidChannel));
		assert(r.HasState(Channel::Unreliable));
		assert(r.CurrentView(Channel::Unreliable) == next);
		assert(r.Baseline(Channel::Unreliable) == next);
		assert(r.BaselineSequence(Channel::Unreliable) == 20U);
		assert(r.LastSnapshotSequence(Channel::Unreliable) == 20U);
	}

	// ---- Decode: header field rejections ----
	{
		DecodedFrame frame;
		assert(DecodeFrame(baselineBytes, frame, IsRegisteredFn{}) == DecodeStatus::NoRegistry);  // empty registry never means allow-all
	}
	{
		FrameBuf f;
		f.header(static_cast<std::uint8_t>(FrameKind::Baseline), 0U, 0U);
		f.u16(1U);
		f.u32(1U);
		f.u8(1U);
		f.component(1U, { 0U });
		assert(StatusOf(f.b) == DecodeStatus::ZeroSnapshotSequence);
	}
	{
		FrameBuf f;
		f.header(static_cast<std::uint8_t>(FrameKind::Delta), 5U, 0U);
		f.u16(0U);
		f.u16(0U);
		assert(StatusOf(f.b) == DecodeStatus::ZeroBaselineSequence);
	}
	{
		FrameBuf f;
		f.header(static_cast<std::uint8_t>(FrameKind::Baseline), 5U, 7U);  // baseline must carry base sequence 0
		f.u16(0U);
		assert(StatusOf(f.b) == DecodeStatus::BaselineHasBaseSequence);
	}
	{
		FrameBuf flagsFrame;
		flagsFrame.header(static_cast<std::uint8_t>(FrameKind::Baseline), 5U, 0U, 1U, 0U);
		flagsFrame.u16(0U);
		assert(StatusOf(flagsFrame.b) == DecodeStatus::Valid);
		FrameBuf reservedFrame;
		reservedFrame.header(static_cast<std::uint8_t>(FrameKind::Baseline), 5U, 0U, 0U, 1U);
		reservedFrame.u16(0U);
		assert(StatusOf(reservedFrame.b) == DecodeStatus::ReservedNotZero);
	}
	{
		auto corruptVersion = baselineBytes;
		corruptVersion[0] = 0x02U;
		assert(StatusOf(corruptVersion) == DecodeStatus::BadVersion);
		auto corruptKind = baselineBytes;
		corruptKind[1] = 0x05U;
		assert(StatusOf(corruptKind) == DecodeStatus::BadKind);
	}

	// ---- Decode: duplicate / conflicting wire identity ----
	{
		FrameBuf f;  // duplicate baseline entity
		f.header(static_cast<std::uint8_t>(FrameKind::Baseline), 10U, 0U);
		f.u16(2U);
		f.u32(100U);
		f.u8(1U);
		f.component(1U, { 0xAAU });
		f.u32(100U);
		f.u8(1U);
		f.component(1U, { 0xBBU });
		assert(StatusOf(f.b) == DecodeStatus::DuplicateEntity);
	}
	{
		FrameBuf f;  // duplicate baseline component (same family twice on one entity)
		f.header(static_cast<std::uint8_t>(FrameKind::Baseline), 10U, 0U);
		f.u16(1U);
		f.u32(100U);
		f.u8(2U);
		f.component(1U, { 0xAAU });
		f.component(1U, { 0xBBU });
		assert(StatusOf(f.b) == DecodeStatus::DuplicateComponent);
	}
	{
		FrameBuf f;  // zero-component baseline entity
		f.header(static_cast<std::uint8_t>(FrameKind::Baseline), 10U, 0U);
		f.u16(1U);
		f.u32(100U);
		f.u8(0U);
		assert(StatusOf(f.b) == DecodeStatus::EmptyEntity);
	}
	{
		FrameBuf f;  // duplicate changed entity
		f.header(static_cast<std::uint8_t>(FrameKind::Delta), 11U, 10U);
		f.u16(2U);
		f.u32(100U);
		f.u8(1U);
		f.component(1U, { 0xAAU });
		f.u8(0U);
		f.u32(100U);
		f.u8(1U);
		f.component(2U, { 0xBBU });
		f.u8(0U);
		f.u16(0U);
		assert(StatusOf(f.b) == DecodeStatus::DuplicateEntity);
	}
	{
		FrameBuf f;  // duplicate changed family within an entity
		f.header(static_cast<std::uint8_t>(FrameKind::Delta), 11U, 10U);
		f.u16(1U);
		f.u32(100U);
		f.u8(2U);
		f.component(1U, { 0xAAU });
		f.component(1U, { 0xBBU });
		f.u8(0U);
		f.u16(0U);
		assert(StatusOf(f.b) == DecodeStatus::DuplicateComponent);
	}
	{
		FrameBuf f;  // duplicate removed family within an entity
		f.header(static_cast<std::uint8_t>(FrameKind::Delta), 11U, 10U);
		f.u16(1U);
		f.u32(100U);
		f.u8(1U);
		f.component(5U, { 0xAAU });
		f.u8(2U);
		f.u8(2U);
		f.u8(2U);
		f.u16(0U);
		assert(StatusOf(f.b) == DecodeStatus::DuplicateComponent);
	}
	{
		FrameBuf f;  // a family both changed and removed on one entity
		f.header(static_cast<std::uint8_t>(FrameKind::Delta), 11U, 10U);
		f.u16(1U);
		f.u32(100U);
		f.u8(1U);
		f.component(1U, { 0xAAU });
		f.u8(1U);
		f.u8(1U);
		f.u16(0U);
		assert(StatusOf(f.b) == DecodeStatus::ConflictingFamily);
	}
	{
		FrameBuf f;  // completely empty delta changed-entity record
		f.header(static_cast<std::uint8_t>(FrameKind::Delta), 11U, 10U);
		f.u16(1U);
		f.u32(100U);
		f.u8(0U);
		f.u8(0U);
		f.u16(0U);
		assert(StatusOf(f.b) == DecodeStatus::EmptyEntity);
	}
	{
		FrameBuf f;  // duplicate removed entity
		f.header(static_cast<std::uint8_t>(FrameKind::Delta), 11U, 10U);
		f.u16(0U);
		f.u16(2U);
		f.u32(100U);
		f.u32(100U);
		assert(StatusOf(f.b) == DecodeStatus::DuplicateEntity);
	}
	{
		FrameBuf f;  // an entity both changed and removed
		f.header(static_cast<std::uint8_t>(FrameKind::Delta), 11U, 10U);
		f.u16(1U);
		f.u32(100U);
		f.u8(1U);
		f.component(1U, { 0xAAU });
		f.u8(0U);
		f.u16(1U);
		f.u32(100U);
		assert(StatusOf(f.b) == DecodeStatus::ConflictingEntity);
	}

	// ---- Decode: unknown family (fail-closed), truncation, trailing bytes ----
	{
		auto onlyTransform = [](Family a_family) { return a_family == Family::Transform; };
		DecodedFrame frame;
		assert(DecodeFrame(baselineBytes, frame, onlyTransform) == DecodeStatus::UnknownFamily);
	}
	{
		DecodedFrame frame;
		assert(DecodeFrame(std::span<const std::uint8_t>(baselineBytes.data(), 4), frame, AllRegistered) == DecodeStatus::HeaderTruncated);
		const std::size_t bodyCut = baselineBytes.size() - 2;
		DecodedFrame bodyFrame;
		assert(DecodeFrame(std::span<const std::uint8_t>(baselineBytes.data(), bodyCut), bodyFrame, AllRegistered) == DecodeStatus::Truncated);
	}
	{
		auto trailing = baselineBytes;
		trailing.push_back(0x00U);
		assert(StatusOf(trailing) == DecodeStatus::TrailingBytes);
	}
	{
		DecodedFrame frame;
		assert(DecodeFrame(baselineBytes, frame, AllRegistered) == DecodeStatus::Valid);
		assert(!frame.baseline.empty());
		auto badVersion = baselineBytes;
		badVersion[0] = 0xFFU;
		assert(DecodeFrame(badVersion, frame, AllRegistered) == DecodeStatus::BadVersion);
		assert(frame.baseline.empty() && frame.delta.changedEntities.empty() && frame.delta.removedEntities.empty());
	}

	return 0;
}

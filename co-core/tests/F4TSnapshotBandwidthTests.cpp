#include "F4TSnapshotDelta.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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

	struct ExpectedBytes
	{
		std::uint32_t entities;
		std::size_t baseline;
		std::size_t oneFieldDelta;
		std::size_t emptyDelta;
	};

	constexpr ExpectedBytes kExpectedBytes[] = {
		{ 16U, 350U, 31U, 16U },
		{ 32U, 686U, 31U, 16U },
		{ 64U, 1358U, 31U, 16U },
	};

	bool AllRegistered(Family) { return true; }

	std::vector<std::uint8_t> Encoded(std::optional<std::vector<std::uint8_t>> a_bytes)
	{
		assert(a_bytes.has_value());
		return std::move(*a_bytes);
	}

	SnapshotState MakeWorld(std::uint32_t a_entityCount, bool a_bumpSecondEntity)
	{
		std::vector<EntityComponentSet> entities;
		for (std::uint32_t e = 0; e < a_entityCount; ++e) {
			EntityComponentSet entity;
			entity.entityId = e + 1U;
			auto transform = std::make_unique<TransformTestComponent>();
			transform->x = e * 16U;
			transform->yaw = static_cast<std::uint16_t>(a_bumpSecondEntity && e == 1U ? 1U : 0U);
			auto npc = std::make_unique<NpcStateTestComponent>();
			npc->health = 100U + e;
			entity.components.push_back(std::move(transform));
			entity.components.push_back(std::move(npc));
			entities.push_back(std::move(entity));
		}
		return BuildSnapshotState(entities);
	}
}

int main()
{
	std::printf("snapshot bytes baseline/delta/empty:");
	for (const auto& budget : kExpectedBytes) {
		const auto baseline = MakeWorld(budget.entities, false);
		const auto next = MakeWorld(budget.entities, true);
		const auto baselineBytes = Encoded(EncodeBaseline(baseline, 1U));
		const auto deltaBytes = Encoded(EncodeDelta(ComputeDelta(baseline, next), 2U, 1U));
		const auto emptyBytes = Encoded(EncodeDelta(ComputeDelta(baseline, baseline), 2U, 1U));
		std::printf(" %u:%zu/%zu/%zu", budget.entities, baselineBytes.size(), deltaBytes.size(), emptyBytes.size());

		DecodedFrame baselineFrame;
		assert(DecodeFrame(baselineBytes, baselineFrame, AllRegistered) == DecodeStatus::Valid);
		assert(baselineFrame.kind == FrameKind::Baseline);
		assert(baselineFrame.baseline == baseline);

		DecodedFrame deltaFrame;
		assert(DecodeFrame(deltaBytes, deltaFrame, AllRegistered) == DecodeStatus::Valid);
		assert(deltaFrame.kind == FrameKind::Delta);
		assert(ApplyDelta(baseline, deltaFrame.delta) == next);

		DecodedFrame emptyFrame;
		assert(DecodeFrame(emptyBytes, emptyFrame, AllRegistered) == DecodeStatus::Valid);
		assert(emptyFrame.kind == FrameKind::Delta);
		assert(emptyFrame.delta.Empty());

		assert(baselineBytes.size() == budget.baseline);
		assert(deltaBytes.size() == budget.oneFieldDelta);
		assert(emptyBytes.size() == budget.emptyDelta);
	}
	std::printf("\n");
	return 0;
}

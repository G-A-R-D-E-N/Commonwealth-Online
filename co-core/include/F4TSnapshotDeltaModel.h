%:pragma once

%:include <cstddef>
%:include <cstdint>
%:include <functional>
%:include <map>
%:include <optional>
%:include <set>
%:include <span>
%:include <utility>
%:include <vector>

%:include "F4TSnapshotComponent.h"

namespace F4T::Replication
{
	inline constexpr std::uint8_t SnapDeltaVersion = 1;
	inline constexpr std::size_t SnapFrameHeaderSize = 12;
	inline constexpr std::uint16_t SnapMaxEntities = 4096;
	inline constexpr std::uint8_t SnapMaxComponentsPerEntity = 255;
	inline constexpr std::uint16_t SnapMaxComponentBytes = 0xFFFF;
	inline constexpr std::size_t SnapMaxFrameBytes = std::size_t{ 1 } << 20;

	enum class FrameKind : std::uint8_t
	{
		Baseline = 0,
		Delta = 1
	};

	enum class DecodeStatus : std::uint8_t
	{
		Valid = 0,
		BadVersion,
		BadKind,
		ReservedNotZero,
		NoRegistry,
		ZeroSnapshotSequence,
		ZeroBaselineSequence,
		BaselineHasBaseSequence,
		HeaderTruncated,
		Truncated,
		SizeExceedsMax,
		CountExceedsMax,
		UnknownFamily,
		EmptyEntity,
		DuplicateEntity,
		DuplicateComponent,
		ConflictingFamily,
		ConflictingEntity,
		TrailingBytes
	};

	struct ComponentKey
	{
		EntityId entityId{};
		Family family{};

		[[nodiscard]] friend bool operator<(const ComponentKey& a_lhs, const ComponentKey& a_rhs) noexcept
		{
			if (a_lhs.entityId != a_rhs.entityId) {
				return a_lhs.entityId < a_rhs.entityId;
			}
			return static_cast<std::uint8_t>(a_lhs.family) < static_cast<std::uint8_t>(a_rhs.family);
		}

		[[nodiscard]] bool operator==(const ComponentKey&) const noexcept = default;
	};

	using SnapshotState = std::map<ComponentKey, std::vector<std::uint8_t>>;
	using IsRegisteredFn = std::function<bool(Family)>;

	struct ChangedComponent
	{
		Family family{};
		std::vector<std::uint8_t> data;

		[[nodiscard]] bool operator==(const ChangedComponent&) const = default;
	};

	struct ChangedEntity
	{
		std::uint32_t entityId{};
		std::vector<ChangedComponent> changed;
		std::vector<Family> removedComponents;
	};

	struct SnapshotDelta
	{
		std::vector<ChangedEntity> changedEntities;
		std::vector<std::uint32_t> removedEntities;

		[[nodiscard]] bool Empty() const noexcept
		{
			return changedEntities.empty() && removedEntities.empty();
		}
	};

	struct DecodedFrame
	{
		FrameKind kind{ FrameKind::Baseline };
		Channel channel{ Channel::Unreliable };
		std::uint32_t snapshotSequence{};
		std::uint32_t baselineSequence{};
		SnapshotState baseline;
		SnapshotDelta delta;
	};

	struct PartitionedSnapshotState
	{
		SnapshotState reliableState;
		SnapshotState unreliableState;
	};

	template <class MapT>
	[[nodiscard]] inline auto EntityComponentRange(MapT& a_state, std::uint32_t a_entityId)
	{
		auto begin = a_state.lower_bound(ComponentKey{ a_entityId, static_cast<Family>(0) });
		auto end = begin;
		while (end != a_state.end() && end->first.entityId == a_entityId) {
			++end;
		}
		return std::pair{ begin, end };
	}

	[[nodiscard]] inline SnapshotState BuildSnapshotState(std::span<const EntityComponentSet> a_entities)
	{
		SnapshotState state;
		for (const auto& entity : a_entities) {
			for (const auto& component : entity.components) {
				auto writer = SnapshotStream::Writer();
				component->Serialize(writer);
				const auto written = writer.Written();
				state[ComponentKey{ entity.entityId, component->ComponentFamily() }] =
					std::vector<std::uint8_t>(written.begin(), written.end());
			}
		}
		return state;
	}

	[[nodiscard]] inline std::optional<PartitionedSnapshotState> PartitionSnapshotByChannel(
		const SnapshotState& a_selected,
		const FrozenComponentDefinitionTable& a_definitions)
	{
		if (!a_definitions.IsValid()) {
			return std::nullopt;
		}
		PartitionedSnapshotState result;
		for (const auto& [key, value] : a_selected) {
			const auto channel = a_definitions.Find(key.family);
			if (!channel.has_value() || !IsValidChannel(*channel)) {
				return std::nullopt;
			}
			if (*channel == Channel::Reliable) {
				result.reliableState.emplace(key, value);
			} else {
				result.unreliableState.emplace(key, value);
			}
		}
		return result;
	}

	[[nodiscard]] inline SnapshotDelta ComputeDelta(const SnapshotState& a_baseline, const SnapshotState& a_next)
	{
		SnapshotDelta delta;
		std::set<std::uint32_t> entityIds;
		for (const auto& [key, value] : a_baseline) {
			entityIds.insert(key.entityId);
		}
		for (const auto& [key, value] : a_next) {
			entityIds.insert(key.entityId);
		}

		for (const std::uint32_t entityId : entityIds) {
			auto [baseBegin, baseEnd] = EntityComponentRange(a_baseline, entityId);
			auto [nextBegin, nextEnd] = EntityComponentRange(a_next, entityId);
			const bool inBaseline = baseBegin != baseEnd;
			const bool inNext = nextBegin != nextEnd;
			if (inBaseline && !inNext) {
				delta.removedEntities.push_back(entityId);
				continue;
			}

			ChangedEntity entity;
			entity.entityId = entityId;
			for (auto it = nextBegin; it != nextEnd; ++it) {
				const Family family = it->first.family;
				const auto baseIt = a_baseline.find(ComponentKey{ entityId, family });
				if (baseIt == a_baseline.end() || baseIt->second != it->second) {
					entity.changed.push_back(ChangedComponent{ family, it->second });
				}
			}
			if (inBaseline) {
				for (auto it = baseBegin; it != baseEnd; ++it) {
					const Family family = it->first.family;
					if (a_next.find(ComponentKey{ entityId, family }) == a_next.end()) {
						entity.removedComponents.push_back(family);
					}
				}
			}
			if (!entity.changed.empty() || !entity.removedComponents.empty()) {
				delta.changedEntities.push_back(std::move(entity));
			}
		}
		return delta;
	}

	[[nodiscard]] inline SnapshotState ApplyDelta(const SnapshotState& a_baseline, const SnapshotDelta& a_delta)
	{
		SnapshotState result = a_baseline;
		for (const auto& entity : a_delta.changedEntities) {
			for (const auto& component : entity.changed) {
				result[ComponentKey{ entity.entityId, component.family }] = component.data;
			}
			for (const Family family : entity.removedComponents) {
				result.erase(ComponentKey{ entity.entityId, family });
			}
		}
		for (const std::uint32_t entityId : a_delta.removedEntities) {
			auto [begin, end] = EntityComponentRange(result, entityId);
			result.erase(begin, end);
		}
		return result;
	}
}

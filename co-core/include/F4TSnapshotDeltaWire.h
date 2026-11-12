%:pragma once

%:include <cstdint>
%:include <iterator>
%:include <optional>
%:include <set>
%:include <span>
%:include <utility>
%:include <vector>

%:include "F4TSnapshotDeltaModel.h"

namespace F4T::Replication
{
	inline constexpr std::uint8_t SnapFlagReliableStream = 0x01;
	inline constexpr std::uint8_t SnapKnownFlags = SnapFlagReliableStream;

	namespace detail
	{
		inline void WriteFrameHeader(
			SnapshotStream& a_stream,
			FrameKind a_kind,
			std::uint32_t a_snapSeq,
			std::uint32_t a_baseSeq,
			Channel a_channel)
		{
			std::uint8_t version = SnapDeltaVersion;
			std::uint8_t kind = static_cast<std::uint8_t>(a_kind);
			std::uint8_t flags = a_channel == Channel::Reliable ? SnapFlagReliableStream : 0U;
			std::uint8_t reserved = 0;
			a_stream.U8(version);
			a_stream.U8(kind);
			a_stream.U8(flags);
			a_stream.U8(reserved);
			a_stream.U32(a_snapSeq);
			a_stream.U32(a_baseSeq);
		}

		[[nodiscard]] inline constexpr std::size_t ComponentWireSize(std::size_t a_payload) noexcept
		{
			return std::size_t{ 1 } + std::size_t{ 2 } + a_payload;
		}

		inline void WriteComponent(SnapshotStream& a_stream, Family a_family, std::span<const std::uint8_t> a_data)
		{
			std::uint8_t tag = static_cast<std::uint8_t>(a_family);
			std::uint16_t length = static_cast<std::uint16_t>(a_data.size());
			a_stream.U8(tag);
			a_stream.U16(length);
			for (const std::uint8_t byte : a_data) {
				std::uint8_t value = byte;
				a_stream.U8(value);
			}
		}

		[[nodiscard]] inline std::vector<std::uint8_t> Finish(SnapshotStream& a_stream)
		{
			const auto written = a_stream.Written();
			return std::vector<std::uint8_t>(written.begin(), written.end());
		}

		[[nodiscard]] inline bool ReadComponent(
			SnapshotStream& a_stream,
			const IsRegisteredFn& a_isRegistered,
			Family& a_family,
			std::vector<std::uint8_t>& a_data,
			DecodeStatus& a_status)
		{
			std::uint8_t tag = 0;
			std::uint16_t length = 0;
			a_stream.U8(tag);
			a_stream.U16(length);
			if (!a_stream.Ok()) {
				a_status = DecodeStatus::Truncated;
				return false;
			}
			a_family = static_cast<Family>(tag);
			if (!a_isRegistered(a_family)) {
				a_status = DecodeStatus::UnknownFamily;
				return false;
			}
			a_data.resize(length);
			for (std::uint16_t i = 0; i < length; ++i) {
				std::uint8_t byte = 0;
				a_stream.U8(byte);
				a_data[i] = byte;
			}
			if (!a_stream.Ok()) {
				a_status = DecodeStatus::Truncated;
				return false;
			}
			return true;
		}
	}

	[[nodiscard]] inline std::optional<std::vector<std::uint8_t>> EncodeBaseline(
		const SnapshotState& a_state,
		std::uint32_t a_snapSeq,
		Channel a_channel = Channel::Unreliable)
	{
		if (a_snapSeq == 0 || !IsValidChannel(a_channel)) {
			return std::nullopt;
		}
		std::set<std::uint32_t> entityIds;
		for (const auto& [key, value] : a_state) {
			entityIds.insert(key.entityId);
		}
		if (entityIds.size() > SnapMaxEntities) {
			return std::nullopt;
		}
		std::size_t total = SnapFrameHeaderSize + 2U;
		for (const std::uint32_t entityId : entityIds) {
			auto [begin, end] = EntityComponentRange(a_state, entityId);
			const auto componentCount = static_cast<std::size_t>(std::distance(begin, end));
			if (componentCount == 0 || componentCount > SnapMaxComponentsPerEntity) {
				return std::nullopt;
			}
			total += 4U + 1U;
			for (auto it = begin; it != end; ++it) {
				if (it->second.size() > SnapMaxComponentBytes) {
					return std::nullopt;
				}
				total += detail::ComponentWireSize(it->second.size());
				if (total > SnapMaxFrameBytes) {
					return std::nullopt;
				}
			}
		}

		auto stream = SnapshotStream::Writer();
		detail::WriteFrameHeader(stream, FrameKind::Baseline, a_snapSeq, 0, a_channel);
		std::uint16_t entityCount = static_cast<std::uint16_t>(entityIds.size());
		stream.U16(entityCount);
		for (const std::uint32_t entityId : entityIds) {
			std::uint32_t id = entityId;
			stream.U32(id);
			auto [begin, end] = EntityComponentRange(a_state, entityId);
			std::uint8_t componentCount = static_cast<std::uint8_t>(std::distance(begin, end));
			stream.U8(componentCount);
			for (auto it = begin; it != end; ++it) {
				detail::WriteComponent(stream, it->first.family, it->second);
			}
		}
		return detail::Finish(stream);
	}

	[[nodiscard]] inline std::optional<std::vector<std::uint8_t>> EncodeDelta(
		const SnapshotDelta& a_delta,
		std::uint32_t a_snapSeq,
		std::uint32_t a_baseSeq,
		Channel a_channel = Channel::Unreliable)
	{
		if (a_snapSeq == 0 || a_baseSeq == 0 || !IsValidChannel(a_channel)) {
			return std::nullopt;
		}
		if (a_delta.changedEntities.size() > SnapMaxEntities || a_delta.removedEntities.size() > SnapMaxEntities) {
			return std::nullopt;
		}
		std::set<std::uint32_t> changedEntityIds;
		std::size_t total = SnapFrameHeaderSize + 2U;
		for (const auto& entity : a_delta.changedEntities) {
			if (entity.changed.empty() && entity.removedComponents.empty()) {
				return std::nullopt;
			}
			if (entity.changed.size() > SnapMaxComponentsPerEntity || entity.removedComponents.size() > SnapMaxComponentsPerEntity) {
				return std::nullopt;
			}
			if (!changedEntityIds.insert(entity.entityId).second) {
				return std::nullopt;
			}
			total += 4U + 1U;
			std::set<std::uint8_t> changedFamilies;
			for (const auto& component : entity.changed) {
				if (component.data.size() > SnapMaxComponentBytes) {
					return std::nullopt;
				}
				if (!changedFamilies.insert(static_cast<std::uint8_t>(component.family)).second) {
					return std::nullopt;
				}
				total += detail::ComponentWireSize(component.data.size());
				if (total > SnapMaxFrameBytes) {
					return std::nullopt;
				}
			}
			total += 1U;
			std::set<std::uint8_t> removedFamilies;
			for (const Family family : entity.removedComponents) {
				const auto tag = static_cast<std::uint8_t>(family);
				if (!removedFamilies.insert(tag).second || changedFamilies.count(tag) != 0) {
					return std::nullopt;
				}
				total += 1U;
			}
			if (total > SnapMaxFrameBytes) {
				return std::nullopt;
			}
		}
		std::set<std::uint32_t> removedEntityIds;
		total += 2U;
		if (total > SnapMaxFrameBytes) {
			return std::nullopt;
		}
		for (const std::uint32_t entityId : a_delta.removedEntities) {
			if (!removedEntityIds.insert(entityId).second || changedEntityIds.count(entityId) != 0) {
				return std::nullopt;
			}
			total += 4U;
			if (total > SnapMaxFrameBytes) {
				return std::nullopt;
			}
		}

		auto stream = SnapshotStream::Writer();
		detail::WriteFrameHeader(stream, FrameKind::Delta, a_snapSeq, a_baseSeq, a_channel);
		std::uint16_t changedCount = static_cast<std::uint16_t>(a_delta.changedEntities.size());
		stream.U16(changedCount);
		for (const auto& entity : a_delta.changedEntities) {
			std::uint32_t id = entity.entityId;
			stream.U32(id);
			std::uint8_t changedComponentCount = static_cast<std::uint8_t>(entity.changed.size());
			stream.U8(changedComponentCount);
			for (const auto& component : entity.changed) {
				detail::WriteComponent(stream, component.family, component.data);
			}
			std::uint8_t removedComponentCount = static_cast<std::uint8_t>(entity.removedComponents.size());
			stream.U8(removedComponentCount);
			for (const Family family : entity.removedComponents) {
				std::uint8_t tag = static_cast<std::uint8_t>(family);
				stream.U8(tag);
			}
		}
		std::uint16_t removedCount = static_cast<std::uint16_t>(a_delta.removedEntities.size());
		stream.U16(removedCount);
		for (const std::uint32_t entityId : a_delta.removedEntities) {
			std::uint32_t id = entityId;
			stream.U32(id);
		}
		return detail::Finish(stream);
	}

	[[nodiscard]] inline DecodeStatus DecodeFrameInto(
		std::span<const std::uint8_t> a_bytes,
		DecodedFrame& a_out,
		const IsRegisteredFn& a_isRegistered)
	{
		if (!a_isRegistered) {
			return DecodeStatus::NoRegistry;
		}
		if (a_bytes.size() > SnapMaxFrameBytes) {
			return DecodeStatus::SizeExceedsMax;
		}
		if (a_bytes.size() < SnapFrameHeaderSize) {
			return DecodeStatus::HeaderTruncated;
		}

		auto stream = SnapshotStream::Reader(a_bytes);
		std::uint8_t version = 0;
		std::uint8_t kind = 0;
		std::uint8_t flags = 0;
		std::uint8_t reserved = 0;
		stream.U8(version);
		stream.U8(kind);
		stream.U8(flags);
		stream.U8(reserved);
		if (version != SnapDeltaVersion) {
			return DecodeStatus::BadVersion;
		}
		if (kind != static_cast<std::uint8_t>(FrameKind::Baseline) && kind != static_cast<std::uint8_t>(FrameKind::Delta)) {
			return DecodeStatus::BadKind;
		}
		if ((flags & static_cast<std::uint8_t>(~SnapKnownFlags)) != 0 || reserved != 0) {
			return DecodeStatus::ReservedNotZero;
		}

		a_out = DecodedFrame{};
		a_out.kind = static_cast<FrameKind>(kind);
		a_out.channel = (flags & SnapFlagReliableStream) != 0 ? Channel::Reliable : Channel::Unreliable;
		stream.U32(a_out.snapshotSequence);
		stream.U32(a_out.baselineSequence);
		if (a_out.snapshotSequence == 0) {
			return DecodeStatus::ZeroSnapshotSequence;
		}
		if (a_out.kind == FrameKind::Baseline) {
			if (a_out.baselineSequence != 0) {
				return DecodeStatus::BaselineHasBaseSequence;
			}
		} else if (a_out.baselineSequence == 0) {
			return DecodeStatus::ZeroBaselineSequence;
		}

		if (a_out.kind == FrameKind::Baseline) {
			std::uint16_t entityCount = 0;
			stream.U16(entityCount);
			if (!stream.Ok()) {
				return DecodeStatus::Truncated;
			}
			if (entityCount > SnapMaxEntities) {
				return DecodeStatus::CountExceedsMax;
			}
			std::set<std::uint32_t> seenEntities;
			for (std::uint16_t e = 0; e < entityCount; ++e) {
				std::uint32_t entityId = 0;
				std::uint8_t componentCount = 0;
				stream.U32(entityId);
				stream.U8(componentCount);
				if (!stream.Ok()) {
					return DecodeStatus::Truncated;
				}
				if (componentCount == 0) {
					return DecodeStatus::EmptyEntity;
				}
				if (!seenEntities.insert(entityId).second) {
					return DecodeStatus::DuplicateEntity;
				}
				std::set<std::uint8_t> seenFamilies;
				for (std::uint8_t c = 0; c < componentCount; ++c) {
					Family family{};
					std::vector<std::uint8_t> data;
					DecodeStatus status = DecodeStatus::Valid;
					if (!detail::ReadComponent(stream, a_isRegistered, family, data, status)) {
						return status;
					}
					if (!seenFamilies.insert(static_cast<std::uint8_t>(family)).second) {
						return DecodeStatus::DuplicateComponent;
					}
					a_out.baseline[ComponentKey{ entityId, family }] = std::move(data);
				}
			}
		} else {
			std::uint16_t changedCount = 0;
			stream.U16(changedCount);
			if (!stream.Ok()) {
				return DecodeStatus::Truncated;
			}
			if (changedCount > SnapMaxEntities) {
				return DecodeStatus::CountExceedsMax;
			}
			std::set<std::uint32_t> seenChangedEntities;
			for (std::uint16_t e = 0; e < changedCount; ++e) {
				ChangedEntity entity;
				std::uint8_t changedComponentCount = 0;
				stream.U32(entity.entityId);
				stream.U8(changedComponentCount);
				if (!stream.Ok()) {
					return DecodeStatus::Truncated;
				}
				if (!seenChangedEntities.insert(entity.entityId).second) {
					return DecodeStatus::DuplicateEntity;
				}
				std::set<std::uint8_t> seenChangedFamilies;
				for (std::uint8_t c = 0; c < changedComponentCount; ++c) {
					Family family{};
					std::vector<std::uint8_t> data;
					DecodeStatus status = DecodeStatus::Valid;
					if (!detail::ReadComponent(stream, a_isRegistered, family, data, status)) {
						return status;
					}
					if (!seenChangedFamilies.insert(static_cast<std::uint8_t>(family)).second) {
						return DecodeStatus::DuplicateComponent;
					}
					entity.changed.push_back(ChangedComponent{ family, std::move(data) });
				}
				std::uint8_t removedComponentCount = 0;
				stream.U8(removedComponentCount);
				if (!stream.Ok()) {
					return DecodeStatus::Truncated;
				}
				if (changedComponentCount == 0 && removedComponentCount == 0) {
					return DecodeStatus::EmptyEntity;
				}
				std::set<std::uint8_t> seenRemovedFamilies;
				for (std::uint8_t c = 0; c < removedComponentCount; ++c) {
					std::uint8_t tag = 0;
					stream.U8(tag);
					if (!stream.Ok()) {
						return DecodeStatus::Truncated;
					}
					const Family family = static_cast<Family>(tag);
					if (!a_isRegistered(family)) {
						return DecodeStatus::UnknownFamily;
					}
					if (!seenRemovedFamilies.insert(tag).second) {
						return DecodeStatus::DuplicateComponent;
					}
					if (seenChangedFamilies.count(tag) != 0) {
						return DecodeStatus::ConflictingFamily;
					}
					entity.removedComponents.push_back(family);
				}
				a_out.delta.changedEntities.push_back(std::move(entity));
			}
			std::uint16_t removedCount = 0;
			stream.U16(removedCount);
			if (!stream.Ok()) {
				return DecodeStatus::Truncated;
			}
			if (removedCount > SnapMaxEntities) {
				return DecodeStatus::CountExceedsMax;
			}
			std::set<std::uint32_t> seenRemovedEntities;
			for (std::uint16_t e = 0; e < removedCount; ++e) {
				std::uint32_t entityId = 0;
				stream.U32(entityId);
				if (!stream.Ok()) {
					return DecodeStatus::Truncated;
				}
				if (!seenRemovedEntities.insert(entityId).second) {
					return DecodeStatus::DuplicateEntity;
				}
				if (seenChangedEntities.count(entityId) != 0) {
					return DecodeStatus::ConflictingEntity;
				}
				a_out.delta.removedEntities.push_back(entityId);
			}
		}

		if (stream.BytesRead() != a_bytes.size()) {
			return DecodeStatus::TrailingBytes;
		}
		return DecodeStatus::Valid;
	}

	[[nodiscard]] inline DecodeStatus DecodeFrame(
		std::span<const std::uint8_t> a_bytes,
		DecodedFrame& a_out,
		const IsRegisteredFn& a_isRegistered)
	{
		const auto status = DecodeFrameInto(a_bytes, a_out, a_isRegistered);
		if (status != DecodeStatus::Valid) {
			a_out = DecodedFrame{};
		}
		return status;
	}
}

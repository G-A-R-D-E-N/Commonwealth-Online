#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <vector>

#include "F4TGnsSnapshotEnvelope.h"

namespace F4T::Replication
{
	using Family = F4T::GnsSnapshotEnvelope::Family;
	using EntityId = std::uint32_t;

	enum class AuthorityClass : std::uint8_t
	{
		Server = 0,
		Host = 1,
		Client = 2
	};

	enum class Channel : std::uint8_t
	{
		Reliable = 0,
		Unreliable = 1
	};

	[[nodiscard]] inline constexpr bool IsValidChannel(Channel a_channel) noexcept
	{
		return a_channel == Channel::Reliable || a_channel == Channel::Unreliable;
	}

	class ComponentRegistry;

	class FrozenComponentDefinitionTable
	{
	public:
		[[nodiscard]] std::optional<Channel> Find(Family a_family) const noexcept
		{
			const auto index = static_cast<std::size_t>(a_family);
			if (index >= m_channels.size() || !m_channels[index].has_value()) {
				return std::nullopt;
			}
			return m_channels[index];
		}

		[[nodiscard]] bool Contains(Family a_family) const noexcept { return Find(a_family).has_value(); }
		[[nodiscard]] bool IsValid() const noexcept { return m_valid; }

	private:
		std::array<std::optional<Channel>, 256> m_channels{};
		bool m_valid{ true };

		friend class ComponentRegistry;
	};

	class SnapshotStream
	{
	public:
		[[nodiscard]] static SnapshotStream Writer()
		{
			SnapshotStream stream;
			stream.m_writing = true;
			return stream;
		}

		[[nodiscard]] static SnapshotStream Reader(std::span<const std::uint8_t> a_bytes) noexcept
		{
			SnapshotStream stream;
			stream.m_writing = false;
			stream.m_read = a_bytes;
			return stream;
		}

		[[nodiscard]] bool IsWriting() const noexcept { return m_writing; }
		[[nodiscard]] bool IsReading() const noexcept { return !m_writing; }
		[[nodiscard]] bool Ok() const noexcept { return m_ok; }
		[[nodiscard]] std::size_t BytesRead() const noexcept { return m_cursor; }
		[[nodiscard]] std::span<const std::uint8_t> Written() const noexcept { return m_write; }

		void U8(std::uint8_t& a_value) noexcept
		{
			if (!m_ok) {
				return;
			}
			if (m_writing) {
				m_write.push_back(a_value);
			} else if (m_cursor < m_read.size()) {
				a_value = m_read[m_cursor++];
			} else {
				m_ok = false;
			}
		}

		void Bool(bool& a_value) noexcept
		{
			std::uint8_t byte = a_value ? 1U : 0U;
			U8(byte);
			if (IsReading()) {
				a_value = byte != 0U;
			}
		}

		void U16(std::uint16_t& a_value) noexcept
		{
			std::uint8_t hi = static_cast<std::uint8_t>((a_value >> 8U) & 0xFFU);
			std::uint8_t lo = static_cast<std::uint8_t>(a_value & 0xFFU);
			U8(hi);
			U8(lo);
			if (IsReading()) {
				a_value = static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8U) | lo);
			}
		}

		void U32(std::uint32_t& a_value) noexcept
		{
			std::uint8_t b0 = static_cast<std::uint8_t>((a_value >> 24U) & 0xFFU);
			std::uint8_t b1 = static_cast<std::uint8_t>((a_value >> 16U) & 0xFFU);
			std::uint8_t b2 = static_cast<std::uint8_t>((a_value >> 8U) & 0xFFU);
			std::uint8_t b3 = static_cast<std::uint8_t>(a_value & 0xFFU);
			U8(b0);
			U8(b1);
			U8(b2);
			U8(b3);
			if (IsReading()) {
				a_value = (static_cast<std::uint32_t>(b0) << 24U) |
				          (static_cast<std::uint32_t>(b1) << 16U) |
				          (static_cast<std::uint32_t>(b2) << 8U) |
				          static_cast<std::uint32_t>(b3);
			}
		}

		void F32(float& a_value) noexcept
		{
			static_assert(sizeof(float) == sizeof(std::uint32_t));
			std::uint32_t bits = 0;
			if (m_writing) {
				std::memcpy(&bits, &a_value, sizeof(bits));
			}
			U32(bits);
			if (IsReading()) {
				std::memcpy(&a_value, &bits, sizeof(a_value));
			}
		}

	private:
		bool m_writing{ true };
		bool m_ok{ true };
		std::vector<std::uint8_t> m_write;
		std::span<const std::uint8_t> m_read;
		std::size_t m_cursor{ 0 };
	};

	class ISnapshotComponent
	{
	public:
		ISnapshotComponent() = default;
		ISnapshotComponent(const ISnapshotComponent&) = default;
		ISnapshotComponent(ISnapshotComponent&&) = default;
		ISnapshotComponent& operator=(const ISnapshotComponent&) = default;
		ISnapshotComponent& operator=(ISnapshotComponent&&) = default;
		virtual ~ISnapshotComponent() = default;

		[[nodiscard]] virtual Family ComponentFamily() const noexcept = 0;
		[[nodiscard]] virtual AuthorityClass Authority() const noexcept = 0;
		[[nodiscard]] virtual Channel PreferredChannel() const noexcept = 0;

		virtual void Serialize(SnapshotStream& a_stream) noexcept = 0;
		virtual void PostSerializeRead() noexcept {}
	};

	class ComponentRegistry
	{
	public:
		using Factory = std::function<std::unique_ptr<ISnapshotComponent>()>;

		struct Entry
		{
			Family family{};
			AuthorityClass authority{};
			Channel channel{};
			Factory factory;
		};

		template <typename T>
		bool Register()
		{
			const T probe{};
			Entry entry{
				probe.ComponentFamily(),
				probe.Authority(),
				probe.PreferredChannel(),
				[]() -> std::unique_ptr<ISnapshotComponent> { return std::make_unique<T>(); }
			};
			return Insert(std::move(entry));
		}

		bool RegisterDefinition(Family a_family, AuthorityClass a_authority, Channel a_channel)
		{
			return Insert(Entry{ a_family, a_authority, a_channel, {} });
		}

		[[nodiscard]] const Entry* Find(Family a_family) const noexcept
		{
			const auto index = static_cast<std::size_t>(a_family);
			if (index < m_entries.size() && m_entries[index].has_value()) {
				return &m_entries[index].value();
			}
			return nullptr;
		}

		[[nodiscard]] bool Contains(Family a_family) const noexcept { return Find(a_family) != nullptr; }

		[[nodiscard]] std::unique_ptr<ISnapshotComponent> Create(Family a_family) const
		{
			const auto* entry = Find(a_family);
			return entry && entry->factory ? entry->factory() : nullptr;
		}

		[[nodiscard]] std::optional<FrozenComponentDefinitionTable> Freeze() const noexcept
		{
			FrozenComponentDefinitionTable result;
			for (std::size_t index = 0; index < m_entries.size(); ++index) {
				if (!m_entries[index].has_value()) {
					continue;
				}
				const auto& entry = m_entries[index].value();
				if (index == 0 || !IsValidChannel(entry.channel)) {
					result.m_valid = false;
					return std::nullopt;
				}
				result.m_channels[index] = entry.channel;
			}
			return result;
		}

	private:
		bool Insert(Entry a_entry)
		{
			const auto index = static_cast<std::size_t>(a_entry.family);
			if (index == 0 || index >= m_entries.size() || m_entries[index].has_value() || !IsValidChannel(a_entry.channel)) {
				return false;
			}
			m_entries[index] = std::move(a_entry);
			return true;
		}

		std::array<std::optional<Entry>, 256> m_entries{};
	};

	struct EntityComponentSet
	{
		EntityId entityId{};
		std::vector<std::unique_ptr<ISnapshotComponent>> components;
	};

	[[nodiscard]] inline bool WriteEntityComponents(SnapshotStream& a_stream, std::span<const EntityComponentSet> a_entities)
	{
		if (a_entities.size() > 0xFFFFU) {
			return false;
		}
		std::set<std::uint32_t> seenEntities;
		for (const auto& entity : a_entities) {
			if (entity.components.size() > 0xFFU) {
				return false;
			}
			if (!seenEntities.insert(entity.entityId).second) {
				return false;
			}
			std::set<std::uint8_t> seenFamilies;
			for (const auto& component : entity.components) {
				if (!seenFamilies.insert(static_cast<std::uint8_t>(component->ComponentFamily())).second) {
					return false;
				}
			}
		}

		auto entityCount = static_cast<std::uint16_t>(a_entities.size());
		a_stream.U16(entityCount);
		for (const auto& entity : a_entities) {
			auto entityId = entity.entityId;
			a_stream.U32(entityId);
			auto componentCount = static_cast<std::uint8_t>(entity.components.size());
			a_stream.U8(componentCount);
			for (const auto& component : entity.components) {
				auto tag = static_cast<std::uint8_t>(component->ComponentFamily());
				a_stream.U8(tag);
				component->Serialize(a_stream);
			}
		}
		return true;
	}

	// An unknown family tag has no self-length to skip, so it fails the whole read.
	[[nodiscard]] inline std::optional<std::vector<EntityComponentSet>> ReadEntityComponents(
		SnapshotStream& a_stream,
		const ComponentRegistry& a_registry)
	{
		std::uint16_t entityCount = 0;
		a_stream.U16(entityCount);
		if (!a_stream.Ok()) {
			return std::nullopt;
		}
		std::vector<EntityComponentSet> entities;
		entities.reserve(entityCount);
		std::set<std::uint32_t> seenEntities;
		for (std::uint16_t entityIndex = 0; entityIndex < entityCount; ++entityIndex) {
			EntityComponentSet entity;
			a_stream.U32(entity.entityId);
			std::uint8_t componentCount = 0;
			a_stream.U8(componentCount);
			if (!a_stream.Ok()) {
				return std::nullopt;
			}
			if (!seenEntities.insert(entity.entityId).second) {
				return std::nullopt;
			}
			std::set<std::uint8_t> seenFamilies;
			for (std::uint8_t componentIndex = 0; componentIndex < componentCount; ++componentIndex) {
				std::uint8_t tag = 0;
				a_stream.U8(tag);
				if (!a_stream.Ok()) {
					return std::nullopt;
				}
				if (!seenFamilies.insert(tag).second) {
					return std::nullopt;
				}
				auto component = a_registry.Create(static_cast<Family>(tag));
				if (!component) {
					return std::nullopt;
				}
				component->Serialize(a_stream);
				if (!a_stream.Ok()) {
					return std::nullopt;
				}
				component->PostSerializeRead();
				entity.components.push_back(std::move(component));
			}
			entities.push_back(std::move(entity));
		}
		return entities;
	}
}

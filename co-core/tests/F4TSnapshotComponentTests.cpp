#include "F4TSnapshotComponent.h"

#include <cassert>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace
{
	using namespace F4T::Replication;

	struct TransformTestComponent : ISnapshotComponent
	{
		std::uint32_t x{};
		std::uint16_t yaw{};
		bool finalized{ false };

		[[nodiscard]] Family ComponentFamily() const noexcept override { return Family::Transform; }
		[[nodiscard]] AuthorityClass Authority() const noexcept override { return AuthorityClass::Client; }
		[[nodiscard]] Channel PreferredChannel() const noexcept override { return Channel::Unreliable; }
		void Serialize(SnapshotStream& a_stream) noexcept override
		{
			a_stream.U32(x);
			a_stream.U16(yaw);
		}
		void PostSerializeRead() noexcept override { finalized = true; }
	};

	struct NpcStateTestComponent : ISnapshotComponent
	{
		std::uint32_t health{};

		[[nodiscard]] Family ComponentFamily() const noexcept override { return Family::NpcState; }
		[[nodiscard]] AuthorityClass Authority() const noexcept override { return AuthorityClass::Server; }
		[[nodiscard]] Channel PreferredChannel() const noexcept override { return Channel::Reliable; }
		void Serialize(SnapshotStream& a_stream) noexcept override { a_stream.U32(health); }
	};

	struct TaggedTestComponent : ISnapshotComponent
	{
		Family family{ Family::Transform };

		[[nodiscard]] Family ComponentFamily() const noexcept override { return family; }
		[[nodiscard]] AuthorityClass Authority() const noexcept override { return AuthorityClass::Client; }
		[[nodiscard]] Channel PreferredChannel() const noexcept override { return Channel::Unreliable; }
		void Serialize(SnapshotStream& a_stream) noexcept override
		{
			std::uint8_t byte = 0;
			a_stream.U8(byte);
		}
	};

	struct Raw
	{
		std::vector<std::uint8_t> b;
		void u8(std::uint32_t v) { b.push_back(static_cast<std::uint8_t>(v & 0xFFU)); }
		void u16(std::uint32_t v) { u8(v >> 8U); u8(v); }
		void u32(std::uint32_t v) { u8(v >> 24U); u8(v >> 16U); u8(v >> 8U); u8(v); }
	};
}

int main()
{
	using F4T::Replication::SnapshotStream;

	auto writer = SnapshotStream::Writer();
	std::uint8_t u8 = 0xABU;
	std::uint16_t u16 = 0x1234U;
	std::uint32_t u32 = 0xDEADBEEFU;
	float f32 = 3.5F;
	bool flag = true;
	writer.U8(u8);
	writer.U16(u16);
	writer.U32(u32);
	writer.F32(f32);
	writer.Bool(flag);
	assert(writer.Ok());

	const auto bytes = writer.Written();
	assert(bytes.size() == 12);
	const std::uint8_t expected[7] = { 0xABU, 0x12U, 0x34U, 0xDEU, 0xADU, 0xBEU, 0xEFU };
	for (std::size_t i = 0; i < 7; ++i) {
		assert(bytes[i] == expected[i]);
	}

	auto reader = SnapshotStream::Reader(bytes);
	std::uint8_t r8 = 0;
	std::uint16_t r16 = 0;
	std::uint32_t r32 = 0;
	float rf = 0.0F;
	bool rflag = false;
	reader.U8(r8);
	reader.U16(r16);
	reader.U32(r32);
	reader.F32(rf);
	reader.Bool(rflag);
	assert(reader.Ok());
	assert(reader.BytesRead() == bytes.size());
	assert(r8 == u8);
	assert(r16 == u16);
	assert(r32 == u32);
	assert(rf == f32);
	assert(rflag == flag);

	const std::uint8_t tiny[1] = { 0x01U };
	auto underflow = SnapshotStream::Reader(std::span<const std::uint8_t>(tiny, 1));
	std::uint32_t drained = 0;
	underflow.U32(drained);
	assert(!underflow.Ok());

	ComponentRegistry registry;
	assert(registry.Register<TransformTestComponent>());
	assert(registry.Register<NpcStateTestComponent>());
	assert(!registry.Register<TransformTestComponent>());
	assert(registry.Contains(Family::Transform));
	assert(registry.Contains(Family::NpcState));
	const auto* transformEntry = registry.Find(Family::Transform);
	assert(transformEntry != nullptr);
	assert(transformEntry->authority == AuthorityClass::Client);
	assert(transformEntry->channel == Channel::Unreliable);
	const auto frozenDefinitions = registry.Freeze();
	assert(frozenDefinitions.has_value());
	assert(frozenDefinitions->Find(Family::Transform) == Channel::Unreliable);
	assert(frozenDefinitions->Find(Family::NpcState) == Channel::Reliable);
	assert(!frozenDefinitions->Find(static_cast<Family>(250)).has_value());

	std::vector<EntityComponentSet> outgoing;
	{
		EntityComponentSet first;
		first.entityId = 100U;
		auto transform = std::make_unique<TransformTestComponent>();
		transform->x = 0x11223344U;
		transform->yaw = 0xABCDU;
		auto npc = std::make_unique<NpcStateTestComponent>();
		npc->health = 250U;
		first.components.push_back(std::move(transform));
		first.components.push_back(std::move(npc));
		outgoing.push_back(std::move(first));

		EntityComponentSet second;
		second.entityId = 200U;
		auto onlyTransform = std::make_unique<TransformTestComponent>();
		onlyTransform->x = 7U;
		onlyTransform->yaw = 9U;
		second.components.push_back(std::move(onlyTransform));
		outgoing.push_back(std::move(second));
	}

	auto setWriter = SnapshotStream::Writer();
	assert(WriteEntityComponents(setWriter, outgoing));
	assert(setWriter.Ok());
	const auto setBytes = setWriter.Written();

	auto setReader = SnapshotStream::Reader(setBytes);
	auto parsed = ReadEntityComponents(setReader, registry);
	assert(parsed.has_value());
	assert(setReader.BytesRead() == setBytes.size());
	assert(parsed->size() == 2);
	assert((*parsed)[0].entityId == 100U);
	assert((*parsed)[0].components.size() == 2);
	assert((*parsed)[0].components[0]->ComponentFamily() == Family::Transform);
	const auto* readTransform = static_cast<const TransformTestComponent*>((*parsed)[0].components[0].get());
	assert(readTransform->x == 0x11223344U);
	assert(readTransform->yaw == 0xABCDU);
	assert(readTransform->finalized);
	const auto* readNpc = static_cast<const NpcStateTestComponent*>((*parsed)[0].components[1].get());
	assert(readNpc->health == 250U);
	assert((*parsed)[1].entityId == 200U);
	assert((*parsed)[1].components.size() == 1);

	ComponentRegistry partialRegistry;
	assert(partialRegistry.Register<TransformTestComponent>());
	assert(!partialRegistry.Contains(Family::NpcState));
	auto rejectingReader = SnapshotStream::Reader(setBytes);
	assert(!ReadEntityComponents(rejectingReader, partialRegistry).has_value());

	{
		std::vector<EntityComponentSet> dupEntity;
		EntityComponentSet a;
		a.entityId = 5U;
		a.components.push_back(std::make_unique<TransformTestComponent>());
		EntityComponentSet b;
		b.entityId = 5U;
		b.components.push_back(std::make_unique<NpcStateTestComponent>());
		dupEntity.push_back(std::move(a));
		dupEntity.push_back(std::move(b));
		auto dupEntityWriter = SnapshotStream::Writer();
		assert(!WriteEntityComponents(dupEntityWriter, dupEntity));  // duplicate entity id
	}
	{
		std::vector<EntityComponentSet> dupFamily;
		EntityComponentSet a;
		a.entityId = 5U;
		a.components.push_back(std::make_unique<TransformTestComponent>());
		a.components.push_back(std::make_unique<TransformTestComponent>());
		dupFamily.push_back(std::move(a));
		auto dupFamilyWriter = SnapshotStream::Writer();
		assert(!WriteEntityComponents(dupFamilyWriter, dupFamily));  // duplicate family on one entity
	}
	{
		std::vector<EntityComponentSet> tooManyComponents;  // 256 distinct families overflows the U8 count
		EntityComponentSet a;
		a.entityId = 5U;
		for (int f = 0; f < 256; ++f) {
			auto component = std::make_unique<TaggedTestComponent>();
			component->family = static_cast<Family>(f);
			a.components.push_back(std::move(component));
		}
		tooManyComponents.push_back(std::move(a));
		auto componentCountWriter = SnapshotStream::Writer();
		assert(!WriteEntityComponents(componentCountWriter, tooManyComponents));
	}
	{
		std::vector<EntityComponentSet> tooManyEntities;  // 65536 overflows the U16 count
		tooManyEntities.reserve(65536);
		for (std::uint32_t e = 0; e < 65536U; ++e) {
			EntityComponentSet entity;
			entity.entityId = e;
			tooManyEntities.push_back(std::move(entity));
		}
		auto entityCountWriter = SnapshotStream::Writer();
		assert(!WriteEntityComponents(entityCountWriter, tooManyEntities));
	}

	{
		Raw r;  // two entities with the same id
		r.u16(2U);
		r.u32(100U);
		r.u8(1U);
		r.u8(1U);  // Transform tag
		r.u32(0x11223344U);
		r.u16(0xABCDU);
		r.u32(100U);
		r.u8(1U);
		r.u8(2U);  // NpcState tag
		r.u32(250U);
		auto dupEntityReader = SnapshotStream::Reader(r.b);
		assert(!ReadEntityComponents(dupEntityReader, registry).has_value());
	}
	{
		Raw r;  // one entity carrying the Transform family twice
		r.u16(1U);
		r.u32(100U);
		r.u8(2U);
		r.u8(1U);
		r.u32(1U);
		r.u16(2U);
		r.u8(1U);
		r.u32(3U);
		r.u16(4U);
		auto dupFamilyReader = SnapshotStream::Reader(r.b);
		assert(!ReadEntityComponents(dupFamilyReader, registry).has_value());
	}

	return 0;
}

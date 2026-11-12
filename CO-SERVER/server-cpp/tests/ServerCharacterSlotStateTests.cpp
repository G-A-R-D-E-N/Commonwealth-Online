#include "ServerFactionTestSupport.h"
#include "F4TCharacterSlots.h"
#include "F4TPacketRateLimit.h"
#include "ServerCharacterSlots.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace FactionTestSupport;
using CommonwealthOnline::Server::CharacterSlotService;
using CommonwealthOnline::Server::DurableStateFileStore;
using CommonwealthOnline::Server::OutboundFrame;
using CommonwealthOnline::Server::PersistenceGate;

namespace
{
	constexpr const char* kAccount = "co-state-acct";
	constexpr double kNow = 200000.0;

	std::string ProfileOf(std::uint32_t a_slot)
	{
		return F4T::CharacterSlots::SlotProfileKey(kAccount, a_slot);
	}

	Json RecordFor(std::string_view a_name, double a_createdAt, std::string_view a_quest)
	{
		return Json{ { "name", std::string{ a_name } }, { "createdAt", a_createdAt }, { "lastPlayedAt", a_createdAt }, { "questProfile", std::string{ a_quest } } };
	}

	Json CreatePacket(std::uint32_t a_slot, std::string_view a_name)
	{
		return Json{ { "type", "characterCreate" }, { "slot", a_slot }, { "name", std::string{ a_name } } };
	}

	void GiveInventory(DurableStateFileStore& a_store, std::uint32_t a_slot)
	{
		(void)a_store.InitializeInventory(ProfileOf(a_slot), Json::array({ SettlerMaterial("000731A3", 5) }));
	}

	std::vector<OutboundFrame> Run(CharacterSlotService& a_service, const Json& a_packet, double a_unixTimeSeconds)
	{
		F4T::PacketRateLimit::TokenBucket bucket;
		const auto type = a_packet.at("type").get<std::string>();
		const auto frames = a_service.Dispatch(type, 10, kAccount, a_packet, 1.0, a_unixTimeSeconds, {}, bucket);
		assert(frames.has_value());
		return *frames;
	}

	struct SlotStateFixture
	{
		explicit SlotStateFixture(std::string_view a_tag) :
			directory(TempDirectory(a_tag)),
			store(directory / "durable-state.json", [] { return 1000.0; }, [] { return std::string{ "0123456789abcdef0123456789abcdef" }; }),
			gate(store)
		{
			AdmitProfile(store, kAccount);
			assert(service.Attach(&gate, CharacterSlotService::Config{}));
		}

		~SlotStateFixture()
		{
			store.WaitForCommits();
			std::filesystem::remove_all(directory);
		}

		SlotStateFixture(const SlotStateFixture&) = delete;
		SlotStateFixture& operator=(const SlotStateFixture&) = delete;

		std::filesystem::path directory;
		DurableStateFileStore store;
		PersistenceGate gate;
		CharacterSlotService service;
	};

	void TestStoreCreateIsAllOrNothing()
	{
		SlotStateFixture fixture{ "slot-state-create" };
		auto& store = fixture.store;
		assert(store.CreateCharacterSlot(kAccount, 0, RecordFor("First", kNow, "quest-first"), Json{ { "characterName", "First" } }));
		assert(store.GetCharacterSlotRecords(kAccount).at(0).at("name") == "First");
		assert(store.GetPlayerState(ProfileOf(0)).has_value());
		assert(!store.CreateCharacterSlot(kAccount, 0, RecordFor("Second", kNow, "quest-second"), Json{ { "characterName", "Second" } }));
		assert(store.GetCharacterSlotRecords(kAccount).at(0).at("name") == "First");
		GiveInventory(store, 1);
		assert(!store.CreateCharacterSlot(kAccount, 1, RecordFor("Third", kNow, "quest-third"), Json{ { "characterName", "Third" } }));
		assert(!store.GetCharacterSlotRecords(kAccount).contains(1));
	}

	void TestLeftoverInventoryHoldsSlot()
	{
		SlotStateFixture fixture{ "slot-state-leftover" };
		GiveInventory(fixture.store, 0);
		const auto listed = RequirePacket(Run(fixture.service, Json{ { "type", "characterList" } }, kNow), 10, "characterListState");
		assert(!listed.at("slots").at(0).value("empty", false));
		const auto refused = RequirePacket(Run(fixture.service, CreatePacket(0, "Ghost"), kNow), 10, "characterResult");
		assert(refused.at("ok") == false);
		assert(refused.at("reason") == "slotOccupied");
		assert(!fixture.store.GetCharacterSlotRecords(kAccount).contains(0));
	}

	void TestOrphanPurgeKeepsSlotWithInventory()
	{
		SlotStateFixture fixture{ "slot-state-orphan" };
		const double aged = kNow - 90000.0;
		assert(fixture.store.SetCharacterSlotRecord(kAccount, 1, RecordFor("Kept", aged, "quest-kept")));
		assert(fixture.store.SetCharacterSlotRecord(kAccount, 2, RecordFor("Stale", aged, "quest-stale")));
		GiveInventory(fixture.store, 1);
		const auto slots = RequirePacket(Run(fixture.service, Json{ { "type", "characterList" } }, kNow), 10, "characterListState").at("slots");
		assert(!slots.at(1).value("empty", false));
		assert(slots.at(2).value("empty", false));
		assert(fixture.store.GetCharacterSlotRecords(kAccount).contains(1));
		assert(!fixture.store.GetCharacterSlotRecords(kAccount).contains(2));
		assert(fixture.store.PendingQuestErase() == std::vector<std::string>{ "quest-stale" });
	}

	void TestEraseCharacterSlotStateRemovesEverything()
	{
		SlotStateFixture fixture{ "slot-state-erase" };
		auto& store = fixture.store;
		assert(store.CreateCharacterSlot(kAccount, 0, RecordFor("First", kNow, "quest-erase"), Json{ { "characterName", "First" } }));
		GiveInventory(store, 0);
		assert(store.EraseCharacterSlotState(kAccount, 0, false, true));
		assert(!store.GetCharacterSlotRecords(kAccount).contains(0));
		assert(!store.GetPlayerState(ProfileOf(0)).has_value());
		assert(!store.GetInventory(ProfileOf(0)).has_value());
		assert(store.PendingQuestErase() == std::vector<std::string>{ "quest-erase" });
	}
}

int main()
{
	TestStoreCreateIsAllOrNothing();
	TestLeftoverInventoryHoldsSlot();
	TestOrphanPurgeKeepsSlotWithInventory();
	TestEraseCharacterSlotStateRemovesEverything();
	std::cout << "All character slot state tests passed.\n";
	return 0;
}

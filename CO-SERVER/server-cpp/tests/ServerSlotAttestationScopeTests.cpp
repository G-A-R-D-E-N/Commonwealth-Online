#include "ServerFactionTestSupport.h"
#include "F4TInventorySnapshot.h"
#include "F4TSaveProfileLogic.h"

#include <cassert>
#include <fstream>
#include <functional>
#include <memory>

using namespace FactionTestSupport;
using CommonwealthOnline::Server::DurableStateFileStore;
using CommonwealthOnline::Server::PersistenceGate;
using CommonwealthOnline::Server::ProtocolHost;

namespace
{
	constexpr const char* kAccount = "co-attest-scope-acct";

	struct Fixture
	{
		std::filesystem::path directory;
		std::unique_ptr<DurableStateFileStore> store;
		std::unique_ptr<PersistenceGate> gate;
		std::unique_ptr<ProtocolHost> host;
	};

	Json Catalog()
	{
		return Json::array({ Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", 5 } } });
	}

	std::string SaveName(std::string_view a_character, std::size_t a_index)
	{
		return "Save" + std::to_string(a_index) + "_" + std::string{ a_character } + "_Nomad_Commonwealth_20260814000101_1_2";
	}

	Fixture OpenFixture(const std::filesystem::path& a_directory)
	{
		Fixture fixture;
		fixture.directory = a_directory;
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		fixture.store = std::make_unique<DurableStateFileStore>(a_directory / "durable-state.json", clock, instanceId);
		fixture.gate = std::make_unique<PersistenceGate>(*fixture.store, Catalog());
		fixture.host = std::make_unique<ProtocolHost>("Slot Attestation Scope");
		assert(fixture.host->EnableCharacterSlots(fixture.gate.get(), 3));
		F4T::Identity::TokenHash hash{};
		hash[0] = 1;
		fixture.store->SaveResumeCredential(kAccount, hash, std::nullopt, true, 2000.0);
		return fixture;
	}

	void EditRoot(const std::filesystem::path& a_directory, const std::function<void(Json&)>& a_edit)
	{
		const auto path = a_directory / "durable-state.json";
		Json root;
		{
			std::ifstream input(path);
			root = Json::parse(input);
		}
		a_edit(root);
		std::ofstream output(path, std::ios::trunc);
		output << root.dump(2);
	}

	Json SlotPacket(std::string_view a_type, std::uint32_t a_slot)
	{
		return Json{ { "type", std::string{ a_type } }, { "slot", a_slot } };
	}

	std::vector<OutboundFrame> Reconnect(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		a_host.BindSessionProfile(a_connectionId, kAccount);
		return a_host.HandleMessage(a_connectionId, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "slotProtocol", 1 } }), a_monotonicSeconds, a_unixTimeSeconds);
	}

	void AssertSlotResult(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_op, bool a_ok, std::string_view a_reason = {})
	{
		const auto result = RequirePacket(a_frames, a_connectionId, "characterResult");
		assert(result.at("op") == std::string{ a_op });
		assert(result.at("ok") == a_ok);
		assert(result.value("reason", std::string{}) == std::string{ a_reason });
	}

	std::string SlotKey(std::uint32_t a_slot)
	{
		return std::string{ kAccount } + "#" + std::to_string(a_slot);
	}

	void CreateSlot(Fixture& a_fixture, std::uint32_t a_connection, std::uint32_t a_slot, double a_time)
	{
		AssertSlotResult(Send(*a_fixture.host, a_connection, Json{ { "type", "characterCreate" }, { "slot", a_slot }, { "name", "Hero" + std::to_string(a_slot) } }, a_time, a_time + 100.0), a_connection, "create", true);
	}

	void SeedBoundSlotOne(std::string_view a_tag, std::filesystem::path& a_directory, bool a_staleSlotOneAttestation = false)
	{
		{
			auto fixture = OpenFixture(TempDirectory(a_tag));
			a_directory = fixture.directory;
			(void)RequirePacket(Reconnect(*fixture.host, 10, 1.0, 101.0), 10, "sessionReady");
			if (a_staleSlotOneAttestation) {
				const F4T::SaveProfileLogic::AttestedSave stale{ SaveName("OLDC0000", 9), std::string(64, 'a'), 0, 1u };
				(void)fixture.store->RecordSaveAttestation(kAccount, stale);
			}
			CreateSlot(fixture, 10, 1, 2.0);
			AssertSlotResult(Send(*fixture.host, 10, SlotPacket("characterSelect", 1), 3.0, 103.0), 10, "select", true);
			fixture.host->HandleDisconnected(10, 103.5);
			assert(fixture.store->InitializeInventory(SlotKey(1), Catalog()).at("initialized") == true);
			fixture.store->WaitForCommits();
		}
		EditRoot(a_directory, [](Json& a_root) {
			const auto key = SlotKey(1);
			a_root["profiles"][key].erase("startingGrant");
			for (std::uint32_t revision = 0; revision < 2; ++revision) {
				const auto items = revision % 2 == 0 ? Json::array() : Catalog();
				const auto request = F4T::InventorySnapshot::NormalizeRequest("revision-" + std::to_string(revision), revision, items);
				assert(request && F4T::InventorySnapshot::Reconcile(a_root, key, *request, {}).changed);
			}
		});
	}

	void BindSlotOne(Fixture& a_fixture, std::uint32_t a_connection)
	{
		(void)RequirePacket(Reconnect(*a_fixture.host, 99, 1.0, 100.0), 99, "sessionReady");
		AssertSlotResult(Send(*a_fixture.host, 99, SlotPacket("characterSelect", 1), 2.0, 101.0), 99, "select", true);
		a_fixture.host->HandleDisconnected(99, 102.0);
		const auto ready = RequirePacket(Reconnect(*a_fixture.host, a_connection, 4.0, 103.0), a_connection, "sessionReady");
		assert(ready.at("persistenceProfile") == SlotKey(1));
	}

	F4T::SaveProfileLogic::AttestedSave Attest(Fixture& a_fixture, std::string_view a_character, std::size_t a_index, std::optional<std::uint32_t> a_slot = std::nullopt)
	{
		const F4T::SaveProfileLogic::AttestedSave save{ SaveName(a_character, a_index), std::string(64, 'a'), 0, a_slot };
		(void)a_fixture.store->RecordSaveAttestation(kAccount, save);
		return save;
	}

	void BeginGrant(Fixture& a_fixture, std::uint32_t a_slot, std::string_view a_character, std::size_t a_saveIndex)
	{
		assert(a_fixture.store->InitializeInventory(SlotKey(a_slot), Catalog()).at("initialized") == true);
		const auto baseline = Attest(a_fixture, a_character, a_saveIndex);
		assert(a_fixture.store->BeginStartingGrant(SlotKey(a_slot), std::string{ a_character }, baseline));
	}

	std::size_t CountSlot(const Fixture& a_fixture, std::optional<std::uint32_t> a_slot)
	{
		std::size_t count = 0;
		for (const auto& save : a_fixture.store->GetSaveAttestations(kAccount)) count += save.slot == a_slot;
		return count;
	}

	void TestLegacyAttestationScopedSoSecondSlotAbandons()
	{
		std::filesystem::path directory;
		SeedBoundSlotOne("scope-abandon", directory);
		{
			auto fixture = OpenFixture(directory);
			BindSlotOne(fixture, 12);
			BeginGrant(fixture, 0, "AAAA1111", 1);
			assert(CountSlot(fixture, std::nullopt) == 1 && fixture.store->GetSaveAttestations(kAccount).size() == 1u);
			const auto result = Send(*fixture.host, 12, Json{ { "type", "characterAbandon" }, { "slot", 1 } }, 5.0, 104.0);
			AssertSlotResult(result, 12, "abandon", true);
			assert(RequirePacket(result, 12, "sessionEnded").at("code") == "character_abandoned");
			assert(!fixture.store->GetInventory(SlotKey(1)).has_value());
			assert(fixture.store->GetInventory(SlotKey(0)).has_value());
			assert(fixture.store->GetStartingGrant(SlotKey(0))->at("recovery").at("state") == "intent");
			assert(CountSlot(fixture, 0u) == 1 && fixture.store->GetSaveAttestations(kAccount).size() == 1u);
			fixture.store->WaitForCommits();
		}
		const auto reopened = OpenFixture(directory);
		assert(CountSlot(reopened, 0u) == 1 && reopened.store->GetSaveAttestations(kAccount).size() == 1u);
		std::filesystem::remove_all(directory);
	}

	void TestAmbiguousCharacterStaysUnscopedAndBlocks()
	{
		std::filesystem::path directory;
		SeedBoundSlotOne("scope-ambiguous", directory);
		auto fixture = OpenFixture(directory);
		BindSlotOne(fixture, 12);
		BeginGrant(fixture, 0, "AAAA1111", 1);
		BeginGrant(fixture, 2, "AAAA1111", 1);
		AssertSlotResult(Send(*fixture.host, 12, Json{ { "type", "characterAbandon" }, { "slot", 1 } }, 5.0, 104.0), 12, "abandon", false, "slotCommitted");
		assert(fixture.store->GetInventory(SlotKey(1))->at("revision") == 2);
		assert(CountSlot(fixture, std::nullopt) == 1 && fixture.store->GetSaveAttestations(kAccount).size() == 1u);
		std::filesystem::remove_all(directory);
	}

	void TestUnmatchedCharacterStaysUnscopedAndBlocks()
	{
		std::filesystem::path directory;
		SeedBoundSlotOne("scope-unmatched", directory);
		auto fixture = OpenFixture(directory);
		BindSlotOne(fixture, 12);
		BeginGrant(fixture, 0, "AAAA1111", 1);
		(void)Attest(fixture, "CCCC3333", 3);
		AssertSlotResult(Send(*fixture.host, 12, Json{ { "type", "characterAbandon" }, { "slot", 1 } }, 5.0, 104.0), 12, "abandon", false, "slotCommitted");
		assert(fixture.store->GetInventory(SlotKey(1))->at("revision") == 2);
		assert(CountSlot(fixture, 0u) == 1 && CountSlot(fixture, std::nullopt) == 1);
		std::filesystem::remove_all(directory);
	}

	void TestDeleteDropsOnlyThatSlotsAttestationsAndReuseStartsClean()
	{
		auto fixture = OpenFixture(TempDirectory("scope-delete"));
		(void)RequirePacket(Reconnect(*fixture.host, 10, 1.0, 101.0), 10, "sessionReady");
		for (std::uint32_t slot = 0; slot < 3; ++slot) CreateSlot(fixture, 10, slot, 2.0 + static_cast<double>(slot));
		BeginGrant(fixture, 0, "AAAA1111", 1);
		BeginGrant(fixture, 1, "BBBB2222", 2);
		(void)Attest(fixture, "CCCC3333", 3);
		(void)Attest(fixture, "BBBB2222", 4, 1u);
		(void)Attest(fixture, "DDDD4444", 5, 2u);
		assert(fixture.store->GetSaveAttestations(kAccount).size() == 5u);

		const auto challenge = RequirePacket(Send(*fixture.host, 10, SlotPacket("characterDelete", 1), 8.0, 108.0), 10, "characterDeleteChallenge");
		AssertSlotResult(Send(*fixture.host, 10, Json{ { "type", "characterDelete" }, { "slot", 1 }, { "confirmToken", challenge.at("confirmToken").get<std::string>() } }, 9.0, 109.0), 10, "delete", true);
		assert(!fixture.store->GetInventory(SlotKey(1)).has_value());
		assert(fixture.store->GetSaveAttestations(kAccount).size() == 3u);
		assert(CountSlot(fixture, 0u) == 1 && CountSlot(fixture, 2u) == 1 && CountSlot(fixture, std::nullopt) == 1 && CountSlot(fixture, 1u) == 0);

		CreateSlot(fixture, 10, 1, 10.0);
		assert(CountSlot(fixture, 1u) == 0 && fixture.store->GetSaveAttestations(kAccount).size() == 3u);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestLeftoverSlotAttestationDoesNotBlockNewCharacterAbandon()
	{
		std::filesystem::path directory;
		SeedBoundSlotOne("scope-leftover", directory, true);
		auto fixture = OpenFixture(directory);
		assert(fixture.store->GetSaveAttestations(kAccount).empty());
		BindSlotOne(fixture, 12);
		const auto result = Send(*fixture.host, 12, Json{ { "type", "characterAbandon" }, { "slot", 1 } }, 5.0, 104.0);
		AssertSlotResult(result, 12, "abandon", true);
		assert(!fixture.store->GetInventory(SlotKey(1)).has_value());
		std::filesystem::remove_all(directory);
	}

	void TestGrantOnlyLeftoverKeepsItsAttestationOnCreate()
	{
		auto fixture = OpenFixture(TempDirectory("scope-grant-leftover"));
		(void)RequirePacket(Reconnect(*fixture.host, 10, 1.0, 101.0), 10, "sessionReady");
		BeginGrant(fixture, 1, "BBBB2222", 2);
		(void)Attest(fixture, "BBBB2222", 4, 1u);
		assert(CountSlot(fixture, 1u) == 1);
		AssertSlotResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "Hero1" } }, 3.0, 103.0), 10, "create", false, "slotOccupied");
		assert(CountSlot(fixture, 1u) == 1 && fixture.store->GetInventory(SlotKey(1)).has_value() && fixture.store->GetStartingGrant(SlotKey(1)).has_value());
		assert(!fixture.store->GetCharacterSlotRecords(kAccount).contains(1u));
		std::filesystem::remove_all(fixture.directory);
	}

	void TestScopeAndEraseWritesAreDurableUnderDeferredCommits()
	{
		auto fixture = OpenFixture(TempDirectory("scope-deferred"));
		fixture.store->SetDeferCommits(true);
		(void)Attest(fixture, "AAAA1111", 1);
		const std::vector<std::pair<std::uint32_t, std::string>> slots{ { 0u, "AAAA1111" } };
		assert(fixture.store->ScopeLegacySaveAttestations(kAccount, slots));
		assert(fixture.store->AllPostedDurable() && CountSlot(fixture, 0u) == 1);
		assert(!fixture.store->ScopeLegacySaveAttestations(kAccount, slots));
		assert(!fixture.store->EraseSlotSaveAttestations(kAccount, 2u));
		assert(fixture.store->EraseSlotSaveAttestations(kAccount, 0u));
		assert(fixture.store->AllPostedDurable() && fixture.store->GetSaveAttestations(kAccount).empty());
		assert(!fixture.store->EraseSlotSaveAttestations(kAccount, 0u));
		fixture.store->WaitForCommits();
		std::filesystem::remove_all(fixture.directory);
	}
}

int main()
{
	TestLegacyAttestationScopedSoSecondSlotAbandons();
	TestAmbiguousCharacterStaysUnscopedAndBlocks();
	TestUnmatchedCharacterStaysUnscopedAndBlocks();
	TestDeleteDropsOnlyThatSlotsAttestationsAndReuseStartsClean();
	TestLeftoverSlotAttestationDoesNotBlockNewCharacterAbandon();
	TestGrantOnlyLeftoverKeepsItsAttestationOnCreate();
	TestScopeAndEraseWritesAreDurableUnderDeferredCommits();
	return 0;
}

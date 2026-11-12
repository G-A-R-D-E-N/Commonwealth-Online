#include "ServerFactionTestSupport.h"
#include "AdminCommandDispatcher.h"
#include "F4TSaveProfileLogic.h"
#include "F4TInventorySnapshot.h"
#include "SaveRecoveryAdmin.h"

#include <cassert>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>

using namespace FactionTestSupport;
using CommonwealthOnline::Server::AdminCommandDispatcher;
using CommonwealthOnline::Server::AdminCommandHooks;
using CommonwealthOnline::Server::DurableStateFileStore;
using CommonwealthOnline::Server::PersistenceGate;
using CommonwealthOnline::Server::ProtocolHost;

namespace
{
	constexpr const char* kAccount = "co-abandon-acct";
	constexpr const char* kSave = "Save9_AAAA1111_Nomad_Commonwealth_20260814000101_1_2";

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

	Fixture OpenFixture(const std::filesystem::path& a_directory, std::optional<Json> a_catalog = Catalog())
	{
		Fixture fixture;
		fixture.directory = a_directory;
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		fixture.store = std::make_unique<DurableStateFileStore>(a_directory / "durable-state.json", clock, instanceId);
		fixture.gate = std::make_unique<PersistenceGate>(*fixture.store, std::move(a_catalog));
		fixture.host = std::make_unique<ProtocolHost>("Character Slots Abandon");
		assert(fixture.host->EnableCharacterSlots(fixture.gate.get(), 3));
		F4T::Identity::TokenHash hash{};
		hash[0] = 1;
		fixture.store->SaveResumeCredential(kAccount, hash, std::nullopt, true, 2000.0);
		return fixture;
	}

	Fixture MakeFixture(std::string_view a_tag, std::optional<Json> a_catalog = Catalog())
	{
		return OpenFixture(TempDirectory(a_tag), std::move(a_catalog));
	}

	void AttestAccountSave(Fixture& a_fixture)
	{
		(void)a_fixture.store->RecordSaveAttestation(kAccount, F4T::SaveProfileLogic::AttestedSave{ kSave, std::string(64, 'a'), 0 });
	}

	void AttestSlotSave(Fixture& a_fixture, std::uint32_t a_slot)
	{
		(void)a_fixture.store->RecordSaveAttestation(kAccount, F4T::SaveProfileLogic::AttestedSave{ kSave, std::string(64, 'a'), 0, a_slot });
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

	Json AbandonPacket(std::uint32_t a_slot)
	{
		return Json{ { "type", "characterAbandon" }, { "slot", a_slot } };
	}

	std::vector<OutboundFrame> Reconnect(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		a_host.BindSessionProfile(a_connectionId, kAccount);
		return a_host.HandleMessage(a_connectionId, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "slotProtocol", 1 } }), a_monotonicSeconds, a_unixTimeSeconds);
	}

	void JoinSlot(ProtocolHost& a_host, std::uint32_t a_connectionId, const std::string& a_account, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		assert(a_account == kAccount);
		(void)RequirePacket(Reconnect(a_host, a_connectionId, a_monotonicSeconds, a_unixTimeSeconds), a_connectionId, "sessionReady");
	}

	void AssertSlotResult(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_op, bool a_ok, std::string_view a_reason = {})
	{
		const auto result = RequirePacket(a_frames, a_connectionId, "characterResult");
		assert(result.at("op") == std::string{ a_op });
		if (result.value("ok", false) != a_ok || result.value("reason", std::string{}) != a_reason) std::cerr << "Expected " << a_op << " ok=" << a_ok << " reason=" << a_reason << ", got " << result.dump() << '\n';
		assert(result.at("ok") == a_ok);
		assert(result.value("reason", std::string{}) == std::string{ a_reason });
	}

	void CreateSelectAndBind(Fixture& a_fixture, std::uint32_t a_firstConnection, std::uint32_t a_boundConnection, std::uint32_t a_slot, double a_time)
	{
		JoinSlot(*a_fixture.host, a_firstConnection, kAccount, a_time, a_time + 100.0);
		AssertSlotResult(Send(*a_fixture.host, a_firstConnection, Json{ { "type", "characterCreate" }, { "slot", a_slot }, { "name", "Hero" } }, a_time + 1.0, a_time + 101.0), a_firstConnection, "create", true);
		AssertSlotResult(Send(*a_fixture.host, a_firstConnection, SlotPacket("characterSelect", a_slot), a_time + 2.0, a_time + 102.0), a_firstConnection, "select", true);
		a_fixture.host->HandleDisconnected(a_firstConnection, a_time + 102.5);
		const auto ready = RequirePacket(Reconnect(*a_fixture.host, a_boundConnection, a_time + 3.0, a_time + 103.0), a_boundConnection, "sessionReady");
		assert(ready.at("persistenceProfile") == std::string{ std::string{ kAccount } + "#" + std::to_string(a_slot) });
	}

	void SelectAndRebind(Fixture& a_fixture, std::uint32_t a_boundConnection, std::uint32_t a_slot)
	{
		JoinSlot(*a_fixture.host, 99, kAccount, 1.0, 100.0);
		AssertSlotResult(Send(*a_fixture.host, 99, SlotPacket("characterSelect", a_slot), 2.0, 101.0), 99, "select", true);
		a_fixture.host->HandleDisconnected(99, 102.0);
		const auto ready = RequirePacket(Reconnect(*a_fixture.host, a_boundConnection, 4.0, 103.0), a_boundConnection, "sessionReady");
		assert(ready.at("persistenceProfile") == std::string{ std::string{ kAccount } + "#" + std::to_string(a_slot) });
	}

	void CommitSlot(Fixture& a_fixture, std::uint32_t a_slot, std::optional<std::uint32_t> a_attestedSlot = std::nullopt)
	{
		const auto key = std::string{ kAccount } + "#" + std::to_string(a_slot);
		assert(a_fixture.store->InitializeInventory(key, Catalog()).at("initialized") == true);
		const F4T::SaveProfileLogic::AttestedSave baseline{ kSave, std::string(64, 'a'), 0, a_attestedSlot };
		(void)a_fixture.store->RecordSaveAttestation(kAccount, baseline);
		assert(a_fixture.store->BeginStartingGrant(key, "AAAA1111", baseline));
	}

	std::filesystem::path SeedBoundCharacter(std::string_view a_tag, const std::function<void(Json&)>& a_edit)
	{
		std::filesystem::path directory;
		{
			auto fixture = MakeFixture(a_tag);
			directory = fixture.directory;
			CreateSelectAndBind(fixture, 10, 11, 1, 1.0);
			assert(fixture.store->InitializeInventory(std::string{ kAccount } + "#1", Catalog()).at("initialized") == true);
			fixture.store->WaitForCommits();
		}
		EditRoot(directory, a_edit);
		return directory;
	}

	void SetInventoryRevision(Json& a_root, std::uint32_t a_revision, std::uint32_t a_slot = 1)
	{
		const auto key = std::string{ kAccount } + "#" + std::to_string(a_slot);
		a_root["profiles"][key].erase("startingGrant");
		for (std::uint32_t revision = 0; revision < a_revision; ++revision) {
			const auto items = revision % 2 == 0 ? Json::array() : Catalog();
			const auto request = F4T::InventorySnapshot::NormalizeRequest("revision-" + std::to_string(revision), revision, items);
			assert(request && F4T::InventorySnapshot::Reconcile(a_root, key, *request, {}).changed);
		}
	}

	void TestAbandonRefusedWhileStoreUnavailable()
	{
		auto fixture = MakeFixture("abandon-store-unavailable");
		const auto key = std::string{ kAccount } + "#1";
		CreateSelectAndBind(fixture, 10, 11, 1, 1.0);
		assert(fixture.store->InitializeInventory(key, Catalog()).at("initialized") == true);
		(void)fixture.gate->FlushPendingPlayerStates(true);
		const auto backup = std::filesystem::path{ (fixture.directory / "durable-state.json").string() + ".bak" };
		std::filesystem::remove(backup);
		std::filesystem::create_directories(backup / "occupied");
		AssertSlotResult(Send(*fixture.host, 11, AbandonPacket(1), 5.0, 104.0), 11, "abandon", false, "persistenceUnavailable");
		assert(fixture.gate->ProfileForConnection(11).has_value());
		assert(fixture.store->GetCharacterSlotRecords(kAccount).contains(1));
		std::filesystem::remove_all(backup);
		const auto result = Send(*fixture.host, 11, AbandonPacket(1), 5.1, 104.1);
		AssertSlotResult(result, 11, "abandon", true);
		assert(RequirePacket(result, 11, "sessionEnded").at("code") == "character_abandoned");
		assert(!fixture.gate->ProfileForConnection(11).has_value());
		assert(!fixture.store->GetInventory(key).has_value());
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonDiscardsBoundUncommittedSlot()
	{
		auto fixture = MakeFixture("abandon-discard");
		const auto key = std::string{ kAccount } + "#1";
		CreateSelectAndBind(fixture, 10, 11, 1, 1.0);
		assert(fixture.store->InitializeInventory(key, Catalog()).at("initialized") == true);
		assert(fixture.host->ActiveCount() == 1);

		const auto result = Send(*fixture.host, 11, AbandonPacket(1), 5.0, 104.0);
		AssertSlotResult(result, 11, "abandon", true);
		assert(RequirePacket(result, 11, "sessionEnded").at("code") == "character_abandoned");
		assert(!fixture.gate->ProfileForConnection(11).has_value());
		assert(!fixture.store->GetInventory(key).has_value());
		assert(!fixture.store->GetStartingGrant(key).has_value());
		assert(!fixture.store->GetPlayerState(key).has_value());
		assert(fixture.host->ActiveCount() == 0);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonRefusesCommittedSlot()
	{
		auto fixture = MakeFixture("abandon-committed");
		const auto key = std::string{ kAccount } + "#1";
		CreateSelectAndBind(fixture, 10, 11, 1, 1.0);
		assert(fixture.store->InitializeInventory(key, Catalog()).at("initialized") == true);
		const F4T::SaveProfileLogic::AttestedSave baseline{ kSave, std::string(64, 'a'), 0 };
		(void)fixture.store->RecordSaveAttestation(kAccount, baseline);
		assert(fixture.store->BeginStartingGrant(key, "AAAA1111", baseline));

		AssertSlotResult(Send(*fixture.host, 11, AbandonPacket(1), 5.0, 104.0), 11, "abandon", false, "slotCommitted");
		assert(fixture.gate->ProfileForConnection(11) == key);
		assert(fixture.store->GetInventory(key).has_value());
		assert(fixture.store->GetStartingGrant(key)->at("recovery").at("state") == "intent");
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonRefusesRevisionAboveZeroWithAttestedSave()
	{
		const auto directory = SeedBoundCharacter("abandon-revision", [](Json& a_root) {
			SetInventoryRevision(a_root, 2);
		});
		auto fixture = OpenFixture(directory);
		AttestAccountSave(fixture);
		SelectAndRebind(fixture, 12, 1);
		AssertSlotResult(Send(*fixture.host, 12, AbandonPacket(1), 5.0, 104.0), 12, "abandon", false, "slotCommitted");
		assert(fixture.store->GetInventory(std::string{ kAccount } + "#1").has_value());
		std::filesystem::remove_all(directory);
	}

	void TestAbandonIgnoresOtherSlotsAttestation()
	{
		const auto directory = SeedBoundCharacter("abandon-other-slot-attested", [](Json& a_root) {
			SetInventoryRevision(a_root, 2);
		});
		auto fixture = OpenFixture(directory);
		const auto key = std::string{ kAccount } + "#1";
		const auto saved = std::string{ kAccount } + "#0";
		SelectAndRebind(fixture, 12, 1);
		CommitSlot(fixture, 0, 0u);
		const auto result = Send(*fixture.host, 12, AbandonPacket(1), 5.0, 104.0);
		AssertSlotResult(result, 12, "abandon", true);
		assert(RequirePacket(result, 12, "sessionEnded").at("code") == "character_abandoned");
		assert(!fixture.store->GetInventory(key).has_value());
		assert(!fixture.store->GetPlayerState(key).has_value());
		assert(fixture.store->GetInventory(saved).has_value());
		assert(fixture.store->GetStartingGrant(saved)->at("recovery").at("state") == "intent");
		const auto attestations = fixture.store->GetSaveAttestations(kAccount);
		assert(attestations.size() == 1u && attestations.front().slot == 0u);
		std::filesystem::remove_all(directory);
	}

	void TestAbandonRefusesOwnSlotAttestation()
	{
		const auto directory = SeedBoundCharacter("abandon-own-slot-attested", [](Json& a_root) {
			SetInventoryRevision(a_root, 2);
		});
		auto fixture = OpenFixture(directory);
		AttestSlotSave(fixture, 1);
		SelectAndRebind(fixture, 12, 1);
		AssertSlotResult(Send(*fixture.host, 12, AbandonPacket(1), 5.0, 104.0), 12, "abandon", false, "slotCommitted");
		assert(fixture.store->GetInventory(std::string{ kAccount } + "#1")->at("revision") == 2);
		assert(fixture.store->GetSaveAttestations(kAccount).size() == 1u);
		std::filesystem::remove_all(directory);
	}

	void TestAbandonRefusesSaveAttestedUnderTwoSlots()
	{
		const auto directory = SeedBoundCharacter("abandon-two-slot-attested", [](Json& a_root) {
			SetInventoryRevision(a_root, 2);
		});
		auto fixture = OpenFixture(directory);
		AttestSlotSave(fixture, 0);
		AttestSlotSave(fixture, 1);
		const auto attestations = fixture.store->GetSaveAttestations(kAccount);
		assert(attestations.size() == 1u && !attestations.front().slot.has_value());
		SelectAndRebind(fixture, 12, 1);
		AssertSlotResult(Send(*fixture.host, 12, AbandonPacket(1), 5.0, 104.0), 12, "abandon", false, "slotCommitted");
		assert(fixture.store->GetInventory(std::string{ kAccount } + "#1")->at("revision") == 2);
		std::filesystem::remove_all(directory);
	}

	void TestAbandonRefusesLegacyUnscopedAttestationForAnySlot()
	{
		const auto directory = SeedBoundCharacter("abandon-legacy-attested", [](Json& a_root) {
			SetInventoryRevision(a_root, 2);
			a_root["profiles"][kAccount]["saveAttestations"] = Json::array({
				Json{ { "saveFile", kSave }, { "sha256", std::string(64, 'a') }, { "attestedAt", 5.0 }, { "slot", 0 } },
				Json{ { "saveFile", std::string{ kSave } + "b" }, { "sha256", std::string(64, 'b') }, { "attestedAt", 4.0 } } });
		});
		auto fixture = OpenFixture(directory);
		const auto attestations = fixture.store->GetSaveAttestations(kAccount);
		assert(attestations.size() == 2u && attestations.front().slot == 0u && !attestations.back().slot.has_value());
		SelectAndRebind(fixture, 12, 1);
		AssertSlotResult(Send(*fixture.host, 12, AbandonPacket(1), 5.0, 104.0), 12, "abandon", false, "slotCommitted");
		assert(fixture.store->GetInventory(std::string{ kAccount } + "#1")->at("revision") == 2);
		std::filesystem::remove_all(directory);
	}

	void TestAbandonDiscardsRevisionAboveZeroWithoutAttestedSave()
	{
		const auto directory = SeedBoundCharacter("abandon-revision-unsaved", [](Json& a_root) {
			SetInventoryRevision(a_root, 8);
		});
		auto fixture = OpenFixture(directory);
		const auto key = std::string{ kAccount } + "#1";
		SelectAndRebind(fixture, 12, 1);
		const auto result = Send(*fixture.host, 12, AbandonPacket(1), 5.0, 104.0);
		AssertSlotResult(result, 12, "abandon", true);
		assert(RequirePacket(result, 12, "sessionEnded").at("code") == "character_abandoned");
		assert(!fixture.store->GetInventory(key).has_value());
		assert(!fixture.store->GetPlayerState(key).has_value());
		assert(!fixture.store->GetStartingGrant(key).has_value());
		std::filesystem::remove_all(directory);
	}

	void TestAbandonRefusesBegunGrantWithoutAttestedSave()
	{
		const auto directory = SeedBoundCharacter("abandon-grant-unsaved", [](Json& a_root) {
			a_root["profiles"][std::string{ kAccount } + "#1"]["startingGrant"]["recovery"] = Json{ { "state", "intent" },
				{ "characterId", "AAAA1111" }, { "baselineSave", kSave }, { "baselineSha256", std::string(64, 'a') } };
		});
		auto fixture = OpenFixture(directory);
		const auto key = std::string{ kAccount } + "#1";
		assert(fixture.store->GetSaveAttestations(kAccount).empty());
		SelectAndRebind(fixture, 12, 1);
		AssertSlotResult(Send(*fixture.host, 12, AbandonPacket(1), 5.0, 104.0), 12, "abandon", false, "slotCommitted");
		assert(fixture.store->GetInventory(key).has_value());
		assert(fixture.store->GetStartingGrant(key)->at("recovery").at("state") == "intent");
		std::filesystem::remove_all(directory);
	}

	void ReconcileTwice(Fixture& a_fixture, std::uint32_t a_connectionId)
	{
		for (std::uint32_t revision = 0; revision < 2; ++revision) {
			const auto items = revision % 2 == 0 ? Catalog() : Json::array();
			const auto request = F4T::InventorySnapshot::NormalizeRequest("first-join-" + std::to_string(revision), revision, items);
			assert(request);
			const auto outcome = a_fixture.gate->ReconcileInventory(a_connectionId, *request);
			assert(outcome && outcome->changed);
		}
	}

	void TestFirstJoinQuitBeforeSaveRejoinDiscardsAndStartsFresh()
	{
		auto fixture = MakeFixture("first-join-quit-before-save", std::nullopt);
		const auto key = std::string{ kAccount } + "#1";
		CreateSelectAndBind(fixture, 10, 11, 1, 1.0);
		assert(fixture.store->InitializeInventory(key, Json::array()).at("revision") == 0);
		assert(fixture.store->GetInventory(key).has_value());
		assert(fixture.store->GetInventory(key)->at("revision") == 0);
		assert(!fixture.store->GetStartingGrant(key).has_value());
		ReconcileTwice(fixture, 11);
		assert(fixture.store->GetInventory(key)->at("revision") == 2);
		assert(fixture.store->GetSaveAttestations(kAccount).empty());
		fixture.host->HandleDisconnected(11, 105.0);

		SelectAndRebind(fixture, 12, 1);
		const auto result = Send(*fixture.host, 12, AbandonPacket(1), 5.0, 106.0);
		AssertSlotResult(result, 12, "abandon", true);
		assert(RequirePacket(result, 12, "sessionEnded").at("code") == "character_abandoned");
		assert(!fixture.store->GetInventory(key).has_value());

		CreateSelectAndBind(fixture, 20, 21, 1, 200.0);
		assert(fixture.store->InitializeInventory(key, Json::array()).at("revision") == 0);
		assert(fixture.store->GetInventory(key).has_value());
		assert(fixture.store->GetInventory(key)->at("revision") == 0);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestFirstJoinQuitAfterAttestationRejoinStillRefuses()
	{
		auto fixture = MakeFixture("first-join-quit-after-attest", std::nullopt);
		const auto key = std::string{ kAccount } + "#1";
		CreateSelectAndBind(fixture, 10, 11, 1, 1.0);
		assert(fixture.store->InitializeInventory(key, Json::array()).at("revision") == 0);
		ReconcileTwice(fixture, 11);
		AttestAccountSave(fixture);
		fixture.host->HandleDisconnected(11, 105.0);

		SelectAndRebind(fixture, 12, 1);
		AssertSlotResult(Send(*fixture.host, 12, AbandonPacket(1), 5.0, 106.0), 12, "abandon", false, "slotCommitted");
		assert(fixture.store->GetInventory(key)->at("revision") == 2);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonDiscardsProgressionAndAppearance()
	{
		const auto directory = SeedBoundCharacter("abandon-progression", [](Json& a_root) {
			auto& state = a_root["profiles"][std::string{ kAccount } + "#1"]["playerState"];
			state["progression"] = Json::object();
			state["appearance"] = Json::object();
		});
		auto fixture = OpenFixture(directory);
		const auto key = std::string{ kAccount } + "#1";
		SelectAndRebind(fixture, 12, 1);
		AssertSlotResult(Send(*fixture.host, 12, AbandonPacket(1), 5.0, 104.0), 12, "abandon", true);
		assert(!fixture.store->GetPlayerState(key).has_value());
		assert(!fixture.store->GetInventory(key).has_value());
		std::filesystem::remove_all(directory);
	}

	void TestAbandonDiscardsEquippedItemsAndPowerArmor()
	{
		const auto directory = SeedBoundCharacter("abandon-equipped", [](Json& a_root) {
			auto& state = a_root["profiles"][std::string{ kAccount } + "#1"]["playerState"];
			state["equippedItems"] = Json::array();
			state["powerArmor"] = Json::object();
		});
		auto fixture = OpenFixture(directory);
		SelectAndRebind(fixture, 12, 1);
		AssertSlotResult(Send(*fixture.host, 12, AbandonPacket(1), 5.0, 104.0), 12, "abandon", true);
		assert(!fixture.store->GetPlayerState(std::string{ kAccount } + "#1").has_value());
		std::filesystem::remove_all(directory);
	}

	void TestAbandonRefusesMismatchOnOccupiedSlot()
	{
		const auto directory = SeedBoundCharacter("abandon-mismatch", [](Json& a_root) {
			a_root["profiles"][std::string{ kAccount } + "#2"]["playerState"]["progression"] = Json::object();
		});
		auto fixture = OpenFixture(directory);
		const auto key = std::string{ kAccount } + "#1";
		const auto other = std::string{ kAccount } + "#2";
		SelectAndRebind(fixture, 11, 1);
		AssertSlotResult(Send(*fixture.host, 11, AbandonPacket(2), 5.0, 104.0), 11, "abandon", false, "slotMismatch");
		AssertSlotResult(Send(*fixture.host, 11, AbandonPacket(2), 5.1, 104.1), 11, "abandon", false, "slotMismatch");
		assert(fixture.gate->ProfileForConnection(11) == key);
		assert(fixture.store->GetPlayerState(key).has_value());
		assert(fixture.store->GetPlayerState(other).has_value());
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonClearsEmptySlotWithoutBinding()
	{
		auto fixture = MakeFixture("abandon-unbound-empty");
		const auto key = std::string{ kAccount } + "#1";
		JoinSlot(*fixture.host, 10, kAccount, 1.0, 100.0);
		AssertSlotResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "Hero" } }, 2.0, 101.0), 10, "create", true);
		AssertSlotResult(Send(*fixture.host, 10, SlotPacket("characterSelect", 1), 3.0, 102.0), 10, "select", true);
		fixture.host->HandleDisconnected(10, 102.5);
		assert(fixture.store->EraseCharacterSlotRecord(kAccount, 1));
		assert(fixture.store->EraseProfileByKey(key));
		const auto ready = RequirePacket(Reconnect(*fixture.host, 11, 4.0, 103.0), 11, "sessionReady");
		assert(!ready.contains("persistenceProfile"));
		AssertSlotResult(Send(*fixture.host, 11, AbandonPacket(1), 5.0, 104.0), 11, "abandon", false, "slotEmpty");
		assert(fixture.host->ActiveCount() == 1);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonDiscardsRemnantWithoutBinding()
	{
		auto fixture = MakeFixture("abandon-remnant");
		const auto key = std::string{ kAccount } + "#1";
		F4T::Identity::TokenHash hash{};
		hash[0] = 1;
		fixture.store->SaveResumeCredential(key, hash, std::nullopt, true, 2000.0);
		assert(fixture.store->InitializeInventory(key, Catalog()).at("initialized") == true);
		JoinSlot(*fixture.host, 10, kAccount, 1.0, 100.0);
		assert(!fixture.gate->ProfileForConnection(10).has_value());
		AssertSlotResult(Send(*fixture.host, 10, AbandonPacket(1), 2.0, 101.0), 10, "abandon", true);
		assert(!fixture.store->GetInventory(key).has_value());
		assert(fixture.host->ActiveCount() == 1);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonRefusesUnboundCommittedRemnant()
	{
		auto fixture = MakeFixture("abandon-remnant-committed");
		const auto key = std::string{ kAccount } + "#1";
		F4T::Identity::TokenHash hash{};
		hash[0] = 1;
		fixture.store->SaveResumeCredential(key, hash, std::nullopt, true, 2000.0);
		assert(fixture.store->InitializeInventory(key, Catalog()).at("initialized") == true);
		const F4T::SaveProfileLogic::AttestedSave baseline{ kSave, std::string(64, 'a'), 0 };
		(void)fixture.store->RecordSaveAttestation(kAccount, baseline);
		assert(fixture.store->BeginStartingGrant(key, "AAAA1111", baseline));
		JoinSlot(*fixture.host, 10, kAccount, 1.0, 100.0);
		assert(!fixture.gate->ProfileForConnection(10).has_value());
		AssertSlotResult(Send(*fixture.host, 10, AbandonPacket(1), 2.0, 101.0), 10, "abandon", false, "notBound");
		assert(fixture.store->GetInventory(key).has_value());
		assert(fixture.store->GetStartingGrant(key)->at("recovery").at("state") == "intent");
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonRefusesEmptySlot()
	{
		auto fixture = MakeFixture("abandon-empty");
		const auto key = std::string{ kAccount } + "#1";
		CreateSelectAndBind(fixture, 10, 11, 1, 1.0);
		assert(fixture.store->EraseCharacterSlotRecord(kAccount, 1));
		assert(fixture.store->EraseProfileByKey(key));
		AssertSlotResult(Send(*fixture.host, 11, AbandonPacket(1), 5.0, 104.0), 11, "abandon", false, "slotEmpty");
		assert(fixture.gate->ProfileForConnection(11) == key);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonRateLimited()
	{
		auto fixture = MakeFixture("abandon-rate");
		CreateSelectAndBind(fixture, 10, 11, 1, 1.0);
		for (int attempt = 0; attempt < 4; ++attempt) {
			AssertSlotResult(Send(*fixture.host, 11, AbandonPacket(2), 5.0, 104.0), 11, "abandon", false, "slotEmpty");
		}
		AssertSlotResult(Send(*fixture.host, 11, AbandonPacket(1), 5.0, 104.0), 11, "abandon", false, "rateLimited");
		assert(fixture.store->GetPlayerState(std::string{ kAccount } + "#1").has_value());
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAdminDiscardsUnbegunCharacterWithProgression()
	{
		const auto directory = SeedBoundCharacter("admin-discard", [](Json& a_root) {
			a_root["profiles"][std::string{ kAccount } + "#1"]["playerState"]["progression"] = Json::object();
		});
		auto fixture = OpenFixture(directory);
		const auto key = std::string{ kAccount } + "#1";
		assert(fixture.host->AbandonSlotForAdmin(kAccount, 1, 6.0).has_value());
		assert(!fixture.store->GetPlayerState(key).has_value());
		assert(!fixture.store->GetInventory(key).has_value());
		std::filesystem::remove_all(directory);
	}

	void TestAdminRefusesBegunGrant()
	{
		auto fixture = MakeFixture("admin-begun");
		const auto key = std::string{ kAccount } + "#1";
		CreateSelectAndBind(fixture, 10, 11, 1, 1.0);
		assert(fixture.store->InitializeInventory(key, Catalog()).at("initialized") == true);
		const F4T::SaveProfileLogic::AttestedSave baseline{ kSave, std::string(64, 'a'), 0 };
		(void)fixture.store->RecordSaveAttestation(kAccount, baseline);
		assert(fixture.store->BeginStartingGrant(key, "AAAA1111", baseline));
		fixture.host->HandleDisconnected(11, 105.0);
		assert(!fixture.host->AbandonSlotForAdmin(kAccount, 1, 6.0).has_value());
		assert(fixture.store->GetStartingGrant(key)->at("recovery").at("state") == "intent");
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAdminRefusesRevisionAboveZero()
	{
		const auto directory = SeedBoundCharacter("admin-revision", [](Json& a_root) {
			SetInventoryRevision(a_root, 3);
		});
		auto fixture = OpenFixture(directory);
		assert(!fixture.host->AbandonSlotForAdmin(kAccount, 1, 6.0).has_value());
		assert(fixture.store->GetInventory(std::string{ kAccount } + "#1").has_value());
		std::filesystem::remove_all(directory);
	}

	void TestAdminHookRequiresConfirmationAndDisconnect()
	{
		auto fixture = MakeFixture("admin-hook");
		const auto key = std::string{ kAccount } + "#1";
		CreateSelectAndBind(fixture, 10, 11, 1, 1.0);
		fixture.host->HandleDisconnected(11, 105.0);
		ProtocolHost* host = fixture.host.get();
		AdminCommandHooks hooks;
		bool online = true;
		CommonwealthOnline::Server::InstallFirstJoinAbandonHook(
			hooks,
			[](std::string_view) { return true; },
			[&online](std::string_view) { return !online; },
			[host](std::string_view a_account, std::uint32_t a_slot, bool a_withoutSave) { return host->AbandonSlotForAdmin(a_account, a_slot, 6.0, a_withoutSave).has_value(); });
		const AdminCommandDispatcher dispatcher(hooks);
		const Json request{ { "cmd", "first-join-abandon" }, { "profileId", key }, { "confirm", "discard-unbegun-first-join" } };
		assert(dispatcher.Dispatch(request).value("ok", false) == false);
		online = false;
		assert(dispatcher.Dispatch(request).value("ok", false) == true);
		assert(!fixture.store->GetPlayerState(key).has_value());
		std::filesystem::remove_all(fixture.directory);
	}
	void TestAbandonWithoutSlotDiscardsUnstartedLeftovers()
	{
		auto fixture = MakeFixture("abandon-unscoped");
		const auto key = std::string{ kAccount } + "#2";
		F4T::Identity::TokenHash hash{};
		hash[0] = 1;
		fixture.store->SaveResumeCredential(key, hash, std::nullopt, true, 2000.0);
		assert(fixture.store->InitializeInventory(key, Catalog()).at("initialized") == true);
		JoinSlot(*fixture.host, 10, kAccount, 1.0, 100.0);
		assert(!fixture.gate->ProfileForConnection(10).has_value());
		const auto result = Send(*fixture.host, 10, Json{ { "type", "characterAbandon" } }, 2.0, 101.0);
		const auto packet = RequirePacket(result, 10, "characterResult");
		assert(packet.at("ok") == true);
		assert(packet.value("slot", 0u) == 2u);
		assert(!fixture.store->GetInventory(key).has_value());
		assert(fixture.host->ActiveCount() == 1);
		AssertSlotResult(Send(*fixture.host, 10, Json{ { "type", "characterAbandon" } }, 3.0, 102.0), 10, "abandon", false, "slotEmpty");
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonWithoutSlotRefusesVisibleCharacter()
	{
		auto fixture = MakeFixture("abandon-unscoped-visible");
		CreateSelectAndBind(fixture, 10, 11, 1, 1.0);
		fixture.host->HandleDisconnected(11, 103.5);
		JoinSlot(*fixture.host, 12, kAccount, 70.0, 200.0);
		assert(!fixture.gate->ProfileForConnection(12).has_value());
		AssertSlotResult(Send(*fixture.host, 12, Json{ { "type", "characterAbandon" } }, 71.0, 201.0), 12, "abandon", false, "notBound");
		assert(fixture.store->GetCharacterSlotRecords(kAccount).contains(1));
		std::filesystem::remove_all(fixture.directory);
	}
	void TestAbandonWithoutSlotLeavesJoinInProgress()
	{
		auto fixture = MakeFixture("abandon-unscoped-live");
		const auto key = std::string{ kAccount } + "#1";
		assert(fixture.store->InitializeInventory(key, Catalog()).at("initialized") == true);
		JoinSlot(*fixture.host, 10, kAccount, 1.0, 100.0);
		JoinSlot(*fixture.host, 12, kAccount, 2.0, 101.0);
		fixture.gate->RebindConnectionProfile(10, key);
		assert(!fixture.gate->ProfileForConnection(12).has_value());
		AssertSlotResult(Send(*fixture.host, 12, Json{ { "type", "characterAbandon" } }, 3.0, 102.0), 12, "abandon", false, "rateLimited");
		assert(fixture.store->GetInventory(key).has_value());
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonWithoutSlotRefusesAmbiguousAccount()
	{
		auto fixture = MakeFixture("abandon-unscoped-mixed");
		CreateSelectAndBind(fixture, 10, 11, 1, 1.0);
		assert(fixture.store->InitializeInventory(std::string{ kAccount } + "#2", Catalog()).at("initialized") == true);
		fixture.host->HandleDisconnected(11, 103.5);
		JoinSlot(*fixture.host, 12, kAccount, 70.0, 200.0);
		assert(!fixture.gate->ProfileForConnection(12).has_value());
		AssertSlotResult(Send(*fixture.host, 12, Json{ { "type", "characterAbandon" } }, 71.0, 201.0), 12, "abandon", false, "notBound");
		assert(fixture.store->GetInventory(std::string{ kAccount } + "#2").has_value());
		assert(fixture.store->GetCharacterSlotRecords(kAccount).contains(1));
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonWithoutSlotConfirmsAdminDiscard()
	{
		auto fixture = MakeFixture("abandon-unscoped-admin");
		assert(fixture.store->InitializeInventory(std::string{ kAccount } + "#1", Catalog()).at("initialized") == true);
		assert(fixture.host->AbandonSlotForAdmin(kAccount, 1, 106.0).has_value());
		JoinSlot(*fixture.host, 12, kAccount, 70.0, 200.0);
		assert(!fixture.gate->ProfileForConnection(12).has_value());
		const auto packet = RequirePacket(Send(*fixture.host, 12, Json{ { "type", "characterAbandon" } }, 71.0, 201.0), 12, "characterResult");
		assert(packet.at("ok") == true);
		assert(packet.value("slot", 0u) == 1u);
		assert(!fixture.store->GetInventory(std::string{ kAccount } + "#1").has_value());
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonWithoutSlotRetryAfterLostReplyWithOtherOccupiedSlot()
	{
		auto fixture = MakeFixture("abandon-unscoped-retry");
		const auto key = std::string{ kAccount } + "#1";
		assert(fixture.store->InitializeInventory(key, Catalog()).at("initialized") == true);
		assert(fixture.host->AbandonSlotForAdmin(kAccount, 1, 106.0).has_value());
		JoinSlot(*fixture.host, 12, kAccount, 70.0, 200.0);
		assert(!fixture.gate->ProfileForConnection(12).has_value());
		const auto first = RequirePacket(Send(*fixture.host, 12, Json{ { "type", "characterAbandon" } }, 71.0, 201.0), 12, "characterResult");
		assert(first.at("ok") == true);
		assert(first.value("slot", 0u) == 1u);
		const auto lostReply = RequirePacket(Send(*fixture.host, 12, Json{ { "type", "characterAbandon" } }, 72.0, 202.0), 12, "characterResult");
		assert(lostReply.at("ok") == true);
		assert(lostReply.value("slot", 0u) == 1u);
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 1u);
		CommitSlot(fixture, 2);
		AssertSlotResult(Send(*fixture.host, 12, Json{ { "type", "characterAbandon" } }, 73.0, 203.0), 12, "abandon", false, "notBound");
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 1u);
		assert(fixture.store->GetInventory(std::string{ kAccount } + "#2").has_value());
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonConfirmationSurvivesRefusedOperations()
	{
		auto fixture = MakeFixture("abandon-confirmation-refused");
		CreateSelectAndBind(fixture, 10, 11, 2, 1.0);
		assert(fixture.store->InitializeInventory(std::string{ kAccount } + "#1", Catalog()).at("initialized") == true);
		fixture.host->HandleDisconnected(11, 103.5);
		assert(fixture.host->AbandonSlotForAdmin(kAccount, 1, 106.0).has_value());
		JoinSlot(*fixture.host, 12, kAccount, 70.0, 200.0);
		AssertSlotResult(Send(*fixture.host, 12, Json{ { "type", "characterCreate" }, { "slot", 2 }, { "name", "Second" } }, 71.0, 201.0), 12, "create", false, "slotOccupied");
		AssertSlotResult(Send(*fixture.host, 12, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "Hero" } }, 72.0, 202.0), 12, "create", false, "nameTaken");
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 1u);
		JoinSlot(*fixture.host, 13, kAccount, 74.0, 204.0);
		AssertSlotResult(Send(*fixture.host, 13, SlotPacket("characterSelect", 1), 75.0, 205.0), 13, "select", false, "slotEmpty");
		AssertSlotResult(Send(*fixture.host, 13, SlotPacket("characterSelect", 7), 76.0, 206.0), 13, "select", false, "badSlot");
		AssertSlotResult(Send(*fixture.host, 13, Json{ { "type", "characterDelete" }, { "slot", 2 }, { "confirmToken", "bogus" } }, 77.0, 207.0), 13, "delete", false, "badToken");
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 1u);
		JoinSlot(*fixture.host, 14, kAccount, 80.0, 210.0);
		AssertSlotResult(Send(*fixture.host, 14, AbandonPacket(2), 81.0, 211.0), 14, "abandon", false, "notBound");
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 1u);
		assert(fixture.store->EraseCharacterSlotRecord(kAccount, 2));
		assert(fixture.store->EraseProfileByKey(std::string{ kAccount } + "#2"));
		JoinSlot(*fixture.host, 15, kAccount, 84.0, 214.0);
		const auto retried = RequirePacket(Send(*fixture.host, 15, Json{ { "type", "characterAbandon" } }, 85.0, 215.0), 15, "characterResult");
		assert(retried.at("ok") == true);
		assert(retried.value("slot", 0u) == 1u);
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 1u);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonConfirmationDiesWhenMarkedSlotReusedAndEmptied()
	{
		auto fixture = MakeFixture("abandon-confirmation-reuse");
		const auto reusedKey = std::string{ kAccount } + "#1";
		CreateSelectAndBind(fixture, 10, 11, 2, 1.0);
		assert(fixture.store->InitializeInventory(reusedKey, Catalog()).at("initialized") == true);
		fixture.host->HandleDisconnected(11, 103.5);
		assert(fixture.host->AbandonSlotForAdmin(kAccount, 1, 106.0).has_value());
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 1u);
		JoinSlot(*fixture.host, 12, kAccount, 70.0, 200.0);
		AssertSlotResult(Send(*fixture.host, 12, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "Second" } }, 71.0, 201.0), 12, "create", true);
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).empty());
		assert(fixture.store->InitializeInventory(reusedKey, Catalog()).at("initialized") == true);
		AssertSlotResult(Send(*fixture.host, 12, SlotPacket("characterSelect", 1), 72.0, 202.0), 12, "select", true);
		fixture.host->HandleDisconnected(12, 202.5);
		const auto ready = RequirePacket(Reconnect(*fixture.host, 13, 73.0, 203.0), 13, "sessionReady");
		assert(ready.at("persistenceProfile") == reusedKey);
		AssertSlotResult(Send(*fixture.host, 13, AbandonPacket(1), 74.0, 204.0), 13, "abandon", true);
		assert(!fixture.store->GetPlayerState(reusedKey).has_value());
		assert(fixture.store->InitializeInventory(std::string{ kAccount } + "#0", Catalog()).at("initialized") == true);
		JoinSlot(*fixture.host, 14, kAccount, 80.0, 210.0);
		AssertSlotResult(Send(*fixture.host, 14, Json{ { "type", "characterAbandon" } }, 81.0, 211.0), 14, "abandon", false, "notBound");
		assert(fixture.store->GetInventory(std::string{ kAccount } + "#0").has_value());
		assert(fixture.store->GetCharacterSlotRecords(kAccount).contains(2));
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonConfirmationClearsOnSuccessfulScopedAbandon()
	{
		auto fixture = MakeFixture("abandon-confirmation-scoped");
		CreateSelectAndBind(fixture, 10, 11, 1, 1.0);
		assert(fixture.store->InitializeInventory(std::string{ kAccount } + "#1", Catalog()).at("initialized") == true);
		assert(fixture.store->InitializeInventory(std::string{ kAccount } + "#0", Catalog()).at("initialized") == true);
		assert(fixture.host->AbandonSlotForAdmin(kAccount, 0, 106.0).has_value());
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 1u);
		AssertSlotResult(Send(*fixture.host, 11, AbandonPacket(1), 5.0, 104.0), 11, "abandon", true);
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).empty());
		assert(!fixture.store->GetInventory(std::string{ kAccount } + "#1").has_value());
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonConfirmationClearsOnSuccessfulSelect()
	{
		auto fixture = MakeFixture("abandon-confirmation-select");
		CreateSelectAndBind(fixture, 10, 11, 2, 1.0);
		assert(fixture.store->InitializeInventory(std::string{ kAccount } + "#1", Catalog()).at("initialized") == true);
		fixture.host->HandleDisconnected(11, 103.5);
		assert(fixture.host->AbandonSlotForAdmin(kAccount, 1, 106.0).has_value());
		JoinSlot(*fixture.host, 12, kAccount, 70.0, 200.0);
		AssertSlotResult(Send(*fixture.host, 12, SlotPacket("characterSelect", 2), 71.0, 201.0), 12, "select", true);
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).empty());
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonConfirmationClearsOnSuccessfulDelete()
	{
		auto fixture = MakeFixture("abandon-confirmation-delete");
		CreateSelectAndBind(fixture, 10, 11, 2, 1.0);
		assert(fixture.store->InitializeInventory(std::string{ kAccount } + "#1", Catalog()).at("initialized") == true);
		fixture.host->HandleDisconnected(11, 103.5);
		assert(fixture.host->AbandonSlotForAdmin(kAccount, 1, 106.0).has_value());
		JoinSlot(*fixture.host, 12, kAccount, 70.0, 200.0);
		const auto challenge = RequirePacket(Send(*fixture.host, 12, SlotPacket("characterDelete", 2), 71.0, 201.0), 12, "characterDeleteChallenge");
		AssertSlotResult(Send(*fixture.host, 12, Json{ { "type", "characterDelete" }, { "slot", 2 }, { "confirmToken", challenge.at("confirmToken").get<std::string>() } }, 72.0, 202.0), 12, "delete", true);
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).empty());
		assert(!fixture.store->GetCharacterSlotRecords(kAccount).contains(2));
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonConfirmationKeepsEachAdminAbandon()
	{
		auto fixture = MakeFixture("abandon-confirmation-two");
		assert(fixture.store->InitializeInventory(std::string{ kAccount } + "#0", Catalog()).at("initialized") == true);
		assert(fixture.store->InitializeInventory(std::string{ kAccount } + "#1", Catalog()).at("initialized") == true);
		assert(fixture.host->AbandonSlotForAdmin(kAccount, 0, 106.0).has_value());
		assert(fixture.host->AbandonSlotForAdmin(kAccount, 1, 107.0).has_value());
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 2u);
		JoinSlot(*fixture.host, 12, kAccount, 70.0, 200.0);
		const auto retried = RequirePacket(Send(*fixture.host, 12, Json{ { "type", "characterAbandon" } }, 71.0, 201.0), 12, "characterResult");
		assert(retried.at("ok") == true);
		assert(retried.value("slot", 0u) == 0u);
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 2u);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonConfirmationHonouredAfterLongDelay()
	{
		auto fixture = MakeFixture("abandon-confirmation-long-delay");
		assert(fixture.store->InitializeInventory(std::string{ kAccount } + "#1", Catalog()).at("initialized") == true);
		assert(fixture.host->AbandonSlotForAdmin(kAccount, 1, 106.0).has_value());
		JoinSlot(*fixture.host, 12, kAccount, 70.0, 200.0);
		const auto retried = RequirePacket(Send(*fixture.host, 12, Json{ { "type", "characterAbandon" } }, 71.0, 1000000.0), 12, "characterResult");
		assert(retried.at("ok") == true);
		assert(retried.value("slot", 0u) == 1u);
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 1u);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonConfirmationRefusedWhileAnotherLeftoverExists()
	{
		auto fixture = MakeFixture("abandon-confirmation-other-leftover");
		CreateSelectAndBind(fixture, 10, 11, 2, 1.0);
		assert(fixture.store->InitializeInventory(std::string{ kAccount } + "#0", Catalog()).at("initialized") == true);
		assert(fixture.store->InitializeInventory(std::string{ kAccount } + "#1", Catalog()).at("initialized") == true);
		fixture.host->HandleDisconnected(11, 103.5);
		assert(fixture.host->AbandonSlotForAdmin(kAccount, 0, 106.0).has_value());
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 1u);
		JoinSlot(*fixture.host, 12, kAccount, 70.0, 200.0);
		AssertSlotResult(Send(*fixture.host, 12, Json{ { "type", "characterAbandon" } }, 71.0, 201.0), 12, "abandon", false, "notBound");
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 1u);
		assert(fixture.store->GetInventory(std::string{ kAccount } + "#1").has_value());
		assert(fixture.store->EraseProfileByKey(std::string{ kAccount } + "#1"));
		assert(fixture.store->EraseCharacterSlotRecord(kAccount, 2));
		assert(fixture.store->EraseProfileByKey(std::string{ kAccount } + "#2"));
		const auto retried = RequirePacket(Send(*fixture.host, 12, Json{ { "type", "characterAbandon" } }, 72.0, 202.0), 12, "characterResult");
		assert(retried.at("ok") == true);
		assert(retried.value("slot", 0u) == 0u);
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 1u);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonConfirmationRefusedWhileRecordOnlySlotExists()
	{
		auto fixture = MakeFixture("abandon-confirmation-record-only");
		CreateSelectAndBind(fixture, 10, 11, 2, 1.0);
		assert(fixture.store->InitializeInventory(std::string{ kAccount } + "#1", Catalog()).at("initialized") == true);
		fixture.host->HandleDisconnected(11, 103.5);
		assert(fixture.host->AbandonSlotForAdmin(kAccount, 1, 106.0).has_value());
		JoinSlot(*fixture.host, 12, kAccount, 70.0, 200.0);
		AssertSlotResult(Send(*fixture.host, 12, Json{ { "type", "characterAbandon" } }, 71.0, 201.0), 12, "abandon", false, "notBound");
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 1u);
		assert(fixture.store->GetCharacterSlotRecords(kAccount).contains(2));
		assert(fixture.store->EraseCharacterSlotRecord(kAccount, 2));
		assert(fixture.store->EraseProfileByKey(std::string{ kAccount } + "#2"));
		const auto retried = RequirePacket(Send(*fixture.host, 12, Json{ { "type", "characterAbandon" } }, 72.0, 202.0), 12, "characterResult");
		assert(retried.at("ok") == true);
		assert(retried.value("slot", 0u) == 1u);
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 1u);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonConfirmationRefusedWhileRevisionZeroInventoryExists()
	{
		auto fixture = MakeFixture("abandon-confirmation-revision-zero");
		CreateSelectAndBind(fixture, 10, 11, 2, 1.0);
		assert(fixture.store->InitializeInventory(std::string{ kAccount } + "#1", Catalog()).at("initialized") == true);
		assert(fixture.store->InitializeInventory(std::string{ kAccount } + "#2", Catalog()).at("initialized") == true);
		fixture.host->HandleDisconnected(11, 103.5);
		assert(fixture.host->AbandonSlotForAdmin(kAccount, 1, 106.0).has_value());
		JoinSlot(*fixture.host, 12, kAccount, 70.0, 200.0);
		AssertSlotResult(Send(*fixture.host, 12, Json{ { "type", "characterAbandon" } }, 71.0, 201.0), 12, "abandon", false, "notBound");
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 1u);
		assert(fixture.store->GetInventory(std::string{ kAccount } + "#2").has_value());
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonConfirmationRefusedWhileCommittedSlotExists()
	{
		const auto directory = SeedBoundCharacter("abandon-confirmation-committed", [](Json& a_root) {
			SetInventoryRevision(a_root, 2);
		});
		auto fixture = OpenFixture(directory);
		JoinSlot(*fixture.host, 12, kAccount, 70.0, 200.0);
		AssertSlotResult(Send(*fixture.host, 12, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "Other" } }, 71.0, 201.0), 12, "create", true);
		fixture.host->HandleDisconnected(12, 71.5);
		assert(fixture.host->AbandonSlotForAdmin(kAccount, 0, 106.0).has_value());
		JoinSlot(*fixture.host, 13, kAccount, 80.0, 210.0);
		AssertSlotResult(Send(*fixture.host, 13, Json{ { "type", "characterAbandon" } }, 81.0, 211.0), 13, "abandon", false, "notBound");
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 1u);
		assert(fixture.store->GetInventory(std::string{ kAccount } + "#1").has_value());
		assert(fixture.store->GetInventory(std::string{ kAccount } + "#1")->at("revision") == 2);
		std::filesystem::remove_all(directory);
	}

	void TestAbandonConfirmationRefusedWhileSingleUnstartedLeftoverExists()
	{
		auto fixture = MakeFixture("abandon-confirmation-single-leftover");
		assert(fixture.store->InitializeInventory(std::string{ kAccount } + "#0", Catalog()).at("initialized") == true);
		assert(fixture.store->InitializeInventory(std::string{ kAccount } + "#1", Catalog()).at("initialized") == true);
		assert(fixture.host->AbandonSlotForAdmin(kAccount, 0, 106.0).has_value());
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 1u);
		JoinSlot(*fixture.host, 12, kAccount, 70.0, 200.0);
		AssertSlotResult(Send(*fixture.host, 12, Json{ { "type", "characterAbandon" } }, 71.0, 201.0), 12, "abandon", false, "notBound");
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 1u);
		assert(fixture.store->GetInventory(std::string{ kAccount } + "#1").has_value());
		assert(fixture.store->EraseProfileByKey(std::string{ kAccount } + "#1"));
		const auto retried = RequirePacket(Send(*fixture.host, 12, Json{ { "type", "characterAbandon" } }, 72.0, 202.0), 12, "characterResult");
		assert(retried.at("ok") == true);
		assert(retried.value("slot", 1u) == 0u);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestAbandonConfirmationSurvivesListButNotCreate()
	{
		auto fixture = MakeFixture("abandon-confirmation-list");
		const auto key = std::string{ kAccount } + "#1";
		CreateSelectAndBind(fixture, 10, 11, 2, 1.0);
		assert(fixture.store->InitializeInventory(key, Catalog()).at("initialized") == true);
		fixture.host->HandleDisconnected(11, 103.5);
		assert(fixture.host->AbandonSlotForAdmin(kAccount, 1, 106.0).has_value());
		JoinSlot(*fixture.host, 12, kAccount, 70.0, 200.0);
		const auto listed = RequirePacket(Send(*fixture.host, 12, Json{ { "type", "characterList" } }, 71.0, 201.0), 12, "characterListState");
		assert(listed.at("slots").size() == 3u);
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 1u);
		assert(fixture.store->EraseCharacterSlotRecord(kAccount, 2));
		assert(fixture.store->EraseProfileByKey(std::string{ kAccount } + "#2"));
		const auto retried = RequirePacket(Send(*fixture.host, 12, Json{ { "type", "characterAbandon" } }, 72.0, 202.0), 12, "characterResult");
		assert(retried.at("ok") == true);
		assert(retried.value("slot", 0u) == 1u);
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 1u);
		AssertSlotResult(Send(*fixture.host, 12, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "Second" } }, 73.0, 203.0), 12, "create", true);
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).empty());
		std::filesystem::remove_all(fixture.directory);
	}

	void TestFirstJoinMarkStoreInvalidation()
	{
		auto fixture = MakeFixture("first-join-mark-invalidation");
		const Json record{ { "name", "Hero" } };
		assert(fixture.store->SetCharacterSlotRecord(kAccount, 1, record));
		assert(fixture.store->SetFirstJoinAbandonedSlot(kAccount, 1, 100.0));
		assert(fixture.store->SetCharacterSlotRecord(kAccount, 1, record));
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).empty());
		assert(fixture.store->SetFirstJoinAbandonedSlot(kAccount, 1, 100.0));
		assert(fixture.store->EraseCharacterSlotRecord(kAccount, 1, true));
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 1u);
		assert(fixture.store->SetCharacterSlotRecord(kAccount, 1, record));
		assert(fixture.store->EraseCharacterSlotRecord(kAccount, 1, false));
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).empty());
		assert(fixture.store->SetCharacterSlotRecord(kAccount, 1, record));
		assert(fixture.store->SetFirstJoinAbandonedSlot(kAccount, 1, 100.0));
		assert(fixture.store->SetFirstJoinAbandonedSlot(kAccount, 1, 101.0));
		assert(fixture.store->GetFirstJoinAbandonedSlots(kAccount).size() == 1u);
		assert(fixture.store->SetFirstJoinAbandonedSlot(kAccount, 2, 102.0));
		assert(fixture.store->SetCharacterSlotRecord(kAccount, 1, record));
		const auto marks = fixture.store->GetFirstJoinAbandonedSlots(kAccount);
		assert(marks.size() == 1u);
		assert(marks.front().slot == 2u);
		std::filesystem::remove_all(fixture.directory);
	}
}

int main()
{
	TestAbandonDiscardsBoundUncommittedSlot();
	TestAbandonRefusedWhileStoreUnavailable();
	TestAbandonRefusesCommittedSlot();
	TestAbandonRefusesRevisionAboveZeroWithAttestedSave();
	TestAbandonIgnoresOtherSlotsAttestation();
	TestAbandonRefusesOwnSlotAttestation();
	TestAbandonRefusesSaveAttestedUnderTwoSlots();
	TestAbandonRefusesLegacyUnscopedAttestationForAnySlot();
	TestAbandonDiscardsRevisionAboveZeroWithoutAttestedSave();
	TestAbandonRefusesBegunGrantWithoutAttestedSave();
	TestFirstJoinQuitBeforeSaveRejoinDiscardsAndStartsFresh();
	TestFirstJoinQuitAfterAttestationRejoinStillRefuses();
	TestAbandonDiscardsProgressionAndAppearance();
	TestAbandonDiscardsEquippedItemsAndPowerArmor();
	TestAbandonRefusesMismatchOnOccupiedSlot();
	TestAbandonClearsEmptySlotWithoutBinding();
	TestAbandonDiscardsRemnantWithoutBinding();
	TestAbandonRefusesUnboundCommittedRemnant();
	TestAbandonWithoutSlotDiscardsUnstartedLeftovers();
	TestAbandonWithoutSlotRefusesVisibleCharacter();
	TestAbandonWithoutSlotLeavesJoinInProgress();
	TestAbandonWithoutSlotRefusesAmbiguousAccount();
	TestAbandonWithoutSlotConfirmsAdminDiscard();
	TestAbandonWithoutSlotRetryAfterLostReplyWithOtherOccupiedSlot();
	TestAbandonConfirmationSurvivesRefusedOperations();
	TestAbandonConfirmationDiesWhenMarkedSlotReusedAndEmptied();
	TestAbandonConfirmationClearsOnSuccessfulScopedAbandon();
	TestAbandonConfirmationClearsOnSuccessfulSelect();
	TestAbandonConfirmationClearsOnSuccessfulDelete();
	TestAbandonConfirmationKeepsEachAdminAbandon();
	TestAbandonConfirmationHonouredAfterLongDelay();
	TestAbandonConfirmationRefusedWhileAnotherLeftoverExists();
	TestAbandonConfirmationRefusedWhileRecordOnlySlotExists();
	TestAbandonConfirmationRefusedWhileSingleUnstartedLeftoverExists();
	TestAbandonConfirmationRefusedWhileRevisionZeroInventoryExists();
	TestAbandonConfirmationRefusedWhileCommittedSlotExists();
	TestAbandonConfirmationSurvivesListButNotCreate();
	TestAbandonRefusesEmptySlot();
	TestAbandonRateLimited();
	TestAdminDiscardsUnbegunCharacterWithProgression();
	TestAdminRefusesBegunGrant();
	TestAdminRefusesRevisionAboveZero();
	TestAdminHookRequiresConfirmationAndDisconnect();
	TestFirstJoinMarkStoreInvalidation();
	return 0;
}

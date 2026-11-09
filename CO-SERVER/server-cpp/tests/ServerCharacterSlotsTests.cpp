

#include "ServerFactionTestSupport.h"
#include "F4TLoadOrder.h"
#include "F4TQuestProtocol.h"

#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace FactionTestSupport;
using CommonwealthOnline::Server::Delivery;
using CommonwealthOnline::Server::DurableStateFileStore;
using CommonwealthOnline::Server::PersistenceGate;
using CommonwealthOnline::Server::ProtocolHost;

namespace
{
	constexpr const char* kAccountA = "co-slots-acct-a";
	constexpr const char* kAccountB = "co-slots-acct-b";

	struct Fixture
	{
		std::filesystem::path directory;
		std::unique_ptr<DurableStateFileStore> store;
		std::unique_ptr<PersistenceGate> gate;
		std::unique_ptr<ProtocolHost> host;
	};

	Fixture MakeFixture(std::size_t a_slots, std::string_view a_tag)
	{
		Fixture fixture;
		fixture.directory = TempDirectory(a_tag);
		const auto path = fixture.directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		fixture.store = std::make_unique<DurableStateFileStore>(path, clock, instanceId);
		fixture.gate = std::make_unique<PersistenceGate>(*fixture.store);
		fixture.host = std::make_unique<ProtocolHost>("Character Slots");
		assert(fixture.host->EnableCharacterSlots(fixture.gate.get(), a_slots));
		return fixture;
	}

	Json SlotPacket(std::string_view a_type, std::uint32_t a_slot)
	{
		return Json{ { "type", std::string{ a_type } }, { "slot", a_slot } };
	}

	void AdmitAccount(DurableStateFileStore& a_store, std::string_view a_account)
	{
		F4T::Identity::TokenHash hash{};
		hash[0] = 1;
		a_store.SaveResumeCredential(a_account, hash, std::nullopt, true, 2000.0);
	}

	void AssertResult(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_op, bool a_ok, std::string_view a_reason = {})
	{
		const auto result = RequirePacket(a_frames, a_connectionId, "characterResult");
		assert(result.at("op") == std::string{ a_op });
		assert(result.at("ok") == a_ok);
		if (!a_ok) {
			assert(result.at("reason") == std::string{ a_reason });
		}
	}

	std::vector<OutboundFrame> HelloRaw(
		ProtocolHost& a_host,
		std::uint32_t a_connectionId,
		std::string_view a_account,
		bool a_slotAware,
		double a_mono,
		double a_unix,
		bool a_reconnectResume = false,
		bool a_questSync = false)
	{
		a_host.HandleConnected(a_connectionId, a_unix);
		a_host.BindSessionProfile(a_connectionId, a_account);
		Json hello{ { "type", "hello" }, { "protocolVersion", 2 } };
		if (a_slotAware) {
			hello["slotProtocol"] = 1;
		}
		if (a_reconnectResume) {
			hello["capabilities"] = Json::array({ std::string{ F4T::Identity::ReconnectResumeCapability } });
		}
		if (a_questSync) {
			hello["capabilities"] = Json::array({ std::string{ F4T::QuestProtocol::kSyncCapability } });
		}
		return a_host.HandleMessage(a_connectionId, EncodeJson(hello), a_mono, a_unix);
	}

	void TestCreateListSelect()
	{
		auto fixture = MakeFixture(3, "slots-create");
		const auto joined = Join(*fixture.host, 10, kAccountA, 1.0, 100.0);
		(void)joined;
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "Hero" } }, 2.0, 101.0), 10, "create", true);
		const auto list = RequirePacket(Send(*fixture.host, 10, Json{ { "type", "characterList" } }, 3.0, 102.0), 10, "characterListState");
		assert(list.at("slots").size() == 3);
		assert(list.at("slots").at(0).value("empty", false));
		assert(list.at("slots").at(1).at("name") == "Hero");
		const auto selected = RequirePacket(Send(*fixture.host, 10, SlotPacket("characterSelect", 1), 4.0, 103.0), 10, "characterResult");
		assert(selected.at("ok") == true && selected.at("needsReconnect") == true);
		assert(fixture.gate->ProfileForConnection(10) == std::string{ std::string{ kAccountA } + "#0" });
		AssertResult(Send(*fixture.host, 10, SlotPacket("characterSelect", 0), 5.0, 104.0), 10, "select", false, "slotEmpty");
		std::filesystem::remove_all(fixture.directory);
	}

	void TestMigrationAdoptsLegacyProfile()
	{
		const auto directory = TempDirectory("slots-migrate");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore seed(path, clock, instanceId);
			assert(seed.UpdatePlayerState(kAccountB, Json{ { "characterName", "Legacy" } }));
			seed.WaitForCommits();
		}
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Character Slots" };
		assert(host.EnableCharacterSlots(&gate, 3));
		Join(host, 10, kAccountB, 1.0, 100.0);
		const auto list = RequirePacket(Send(host, 10, Json{ { "type", "characterList" } }, 2.0, 101.0), 10, "characterListState");
		assert(list.at("slots").at(0).at("name") == "Legacy");
		assert(gate.QueueAcceptedPlayerState(10, Json{ { "type", "playerState" }, { "characterName", "LegacyRenamed" } }));
		assert(gate.FlushPendingPlayerStates(true));
		const auto stored = store.GetPlayerState(std::string{ kAccountB } + "#0");
		assert(stored && stored->at("characterName") == "LegacyRenamed");
		std::filesystem::remove_all(directory);
	}

	void TestMigrationConflictDisablesSlots()
	{
		const auto directory = TempDirectory("slots-conflict");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore seed(path, clock, instanceId);
			assert(seed.UpdatePlayerState(kAccountA, Json{ { "characterName", "Original" } }));
			assert(seed.UpdatePlayerState(std::string{ kAccountA } + "#0", Json{ { "characterName", "HalfRun" } }));
			seed.WaitForCommits();
		}
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Character Slots" };
		assert(!host.EnableCharacterSlots(&gate, 3));
		Join(host, 10, kAccountA, 1.0, 100.0);
		AssertResult(
			Send(host, 10, Json{ { "type", "characterList" } }, 2.0, 101.0), 10, "list", false, "slotsUnavailable");
		const auto kept = store.GetPlayerState(kAccountA);
		assert(kept && kept->at("characterName") == "Original");
		std::filesystem::remove_all(directory);
	}

	void TestSameAccountHelloSupersedesOldSession()
	{
		auto fixture = MakeFixture(3, "slots-supersede");
		Join(*fixture.host, 10, kAccountA, 1.0, 100.0);
		const auto replaced = HelloRaw(*fixture.host, 20, kAccountA, true, 1.1, 100.1);
		assert(RequirePacket(replaced, 10, "sessionEnded").at("code") == "session_superseded");
		assert(RequirePacket(replaced, 20, "sessionReady").at("characterSlots").at("bound") == false);
		assert(!HasPacket(replaced, 20, "disconnect"));
		assert(fixture.host->ActiveCount() == 1);
		assert(!fixture.gate->ProfileForConnection(10).has_value());
		fixture.host->HandleDisconnected(10, 101.0);
		assert(fixture.host->ActiveCount() == 1);
		const auto legacy = HelloRaw(*fixture.host, 30, kAccountA, false, 2.0, 102.0);
		assert(HasPacket(legacy, 30, "sessionReady"));
		assert(HasPacket(legacy, 20, "sessionEnded"));
		std::filesystem::remove_all(fixture.directory);
	}

	void TestDeleteIsPermanent()
	{
		auto fixture = MakeFixture(3, "slots-delete");
		Join(*fixture.host, 10, kAccountA, 1.0, 1000.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "Doomed" } }, 2.0, 1001.0), 10, "create", true);
		const auto challenge = RequirePacket(
			Send(*fixture.host, 10, SlotPacket("characterDelete", 1), 3.0, 1002.0), 10, "characterDeleteChallenge");
		const auto token = challenge.at("confirmToken").get<std::string>();
		assert(!token.empty());
		const auto reissued = RequirePacket(
			Send(*fixture.host, 10, SlotPacket("characterDelete", 1), 3.1, 1002.0), 10, "characterDeleteChallenge");
		const auto reissuedToken = reissued.at("confirmToken").get<std::string>();
		AssertResult(
			Send(*fixture.host, 10, Json{ { "type", "characterDelete" }, { "slot", 1 }, { "confirmToken", token } }, 3.2, 1002.0),
			10, "delete", false, "badToken");
		AssertResult(
			Send(*fixture.host, 10, Json{ { "type", "characterDelete" }, { "slot", 1 }, { "confirmToken", reissuedToken } }, 3.3, 1002.0),
			10, "delete", true);
		AssertResult(
			Send(*fixture.host, 10, Json{ { "type", "characterDelete" }, { "slot", 1 }, { "confirmToken", reissuedToken } }, 3.4, 1002.0),
			10, "delete", false, "badToken");
		const auto listed = RequirePacket(Send(*fixture.host, 10, Json{ { "type", "characterList" } }, 4.0, 1003.0), 10, "characterListState");
		assert(listed.at("slots").at(1).value("empty", false));
		AssertResult(Send(*fixture.host, 10, SlotPacket("characterSelect", 1), 4.1, 1003.0), 10, "select", false, "slotEmpty");
		assert(!fixture.store->GetCharacterSlotRecords(kAccountA).contains(1));
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "Fresh" } }, 5.0, 1004.0), 10, "create", true);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestLegacySoftDeletedRecordPurged()
	{
		auto fixture = MakeFixture(3, "slots-legacy-soft-deleted");
		Join(*fixture.host, 10, kAccountA, 1.0, 1000.0);
		assert(fixture.store->SetCharacterSlotRecord(kAccountA, 2, Json{ { "name", "Ephemeral" }, { "createdAt", 101.0 }, { "lastPlayedAt", 102.0 }, { "deletedAt", 1002.0 } }));
		const auto listed = RequirePacket(Send(*fixture.host, 10, Json{ { "type", "characterList" } }, 4.0, 1003.0), 10, "characterListState");
		assert(listed.at("slots").at(2).value("empty", false));
		assert(!fixture.store->GetCharacterSlotRecords(kAccountA).contains(2));
		AssertResult(Send(*fixture.host, 10, SlotPacket("characterSelect", 2), 4.1, 1003.0), 10, "select", false, "slotEmpty");
		std::filesystem::remove_all(fixture.directory);
	}

	void TestDeleteLeavesOtherAccountSlots()
	{
		auto fixture = MakeFixture(3, "slots-cross-account");
		Join(*fixture.host, 10, kAccountA, 1.0, 1000.0);
		Join(*fixture.host, 20, kAccountB, 1.1, 1000.1);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "Mine" } }, 2.0, 1001.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 20, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "Theirs" } }, 2.1, 1001.1), 20, "create", true);
		const auto challenge = RequirePacket(
			Send(*fixture.host, 10, SlotPacket("characterDelete", 1), 3.0, 1002.0), 10, "characterDeleteChallenge");
		AssertResult(
			Send(*fixture.host, 10, Json{ { "type", "characterDelete" }, { "slot", 1 }, { "confirmToken", challenge.at("confirmToken").get<std::string>() } }, 3.1, 1002.0),
			10, "delete", true);
		assert(!fixture.store->GetPlayerState(std::string{ kAccountA } + "#1").has_value());
		const auto theirs = RequirePacket(Send(*fixture.host, 20, Json{ { "type", "characterList" } }, 4.0, 1003.0), 20, "characterListState");
		assert(theirs.at("slots").at(1).at("name") == "Theirs");
		std::filesystem::remove_all(fixture.directory);
	}

	void TestDeleteTokenExpires()
	{
		auto fixture = MakeFixture(3, "slots-token");
		Join(*fixture.host, 10, kAccountA, 1.0, 1000.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "Brief" } }, 2.0, 1001.0), 10, "create", true);
		const auto challenge = RequirePacket(
			Send(*fixture.host, 10, SlotPacket("characterDelete", 1), 3.0, 1002.0), 10, "characterDeleteChallenge");
		AssertResult(
			Send(*fixture.host, 10, Json{ { "type", "characterDelete" }, { "slot", 1 }, { "confirmToken", challenge.at("confirmToken").get<std::string>() } }, 200.0, 1003.0),
			10, "delete", false, "badToken");
		std::filesystem::remove_all(fixture.directory);
	}

	void TestNamesSlotsAndInputValidation()
	{
		auto fixture = MakeFixture(1, "slots-names");
		Join(*fixture.host, 10, kAccountA, 1.0, 100.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "Solo" } }, 2.0, 101.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "Other" } }, 2.1, 101.0), 10, "create", false, "slotsFull");
		AssertResult(Send(*fixture.host, 10, SlotPacket("characterSelect", 4), 2.2, 101.0), 10, "select", false, "badSlot");
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterSelect" } }, 2.3, 101.0), 10, "select", false, "invalidRequest");
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "  padded" } }, 2.4, 101.0), 10, "create", false, "invalidRequest");
		std::filesystem::remove_all(fixture.directory);

		auto named = MakeFixture(3, "slots-badnames");
		Join(*named.host, 10, kAccountB, 1.0, 100.0);
		AssertResult(Send(*named.host, 10, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "Hero" } }, 2.0, 101.0), 10, "create", true);
		AssertResult(Send(*named.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "hero" } }, 2.1, 101.0), 10, "create", false, "nameTaken");
		AssertResult(Send(*named.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "Bad#Name" } }, 2.2, 101.0), 10, "create", false, "invalidRequest");
		AssertResult(Send(*named.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "" } }, 2.3, 101.0), 10, "create", false, "invalidRequest");
		AssertResult(Send(*named.host, 10, SlotPacket("characterDelete", 0), 2.4, 101.0), 10, "delete", false, "slotActive");
		std::filesystem::remove_all(named.directory);
	}

	void TestForgedSlotKeyRejected()
	{
		auto fixture = MakeFixture(3, "slots-forged");
		fixture.host->HandleConnected(10, 100.0);
		fixture.host->BindSessionProfile(10, std::string{ kAccountA } + "#1");
		const auto frames = fixture.host->HandleMessage(
			10, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 1.0, 100.0);
		(void)frames;
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterList" } }, 2.0, 101.0), 10, "list", false, "slotsUnavailable");
		std::filesystem::remove_all(fixture.directory);
	}

	void TestInventoryAdmissionBoundsSlots()
	{
		auto fixture = MakeFixture(3, "slots-admission-bound");
		AdmitAccount(*fixture.store, kAccountA);
		const auto catalog = Json::array({ Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", 5 } } });
		bool threw = false;
		try { (void)fixture.store->InitializeInventory(std::string{ kAccountA } + "#8", catalog); }
		catch (const std::invalid_argument&) { threw = true; }
		assert(threw && !fixture.store->GetInventory(std::string{ kAccountA } + "#8").has_value());
		assert(fixture.store->InitializeInventory(std::string{ kAccountA } + "#7", catalog).at("initialized") == true);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestInventoryRefusedWithoutOwnerResume()
	{
		auto fixture = MakeFixture(3, "slots-admission-owner");
		AdmitAccount(*fixture.store, kAccountA);
		const auto catalog = Json::array({ Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", 5 } } });
		bool threw = false;
		try { (void)fixture.store->InitializeInventory(std::string{ kAccountB } + "#1", catalog); }
		catch (const std::invalid_argument&) { threw = true; }
		assert(threw && !fixture.store->GetInventory(std::string{ kAccountB } + "#1").has_value());
		assert(fixture.store->InitializeInventory(std::string{ kAccountA } + "#1", catalog).at("initialized") == true);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestInventoryIsolationBetweenSlots()
	{
		auto fixture = MakeFixture(3, "slots-isolation");
		Join(*fixture.host, 10, kAccountA, 1.0, 100.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "Zero" } }, 2.0, 101.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "One" } }, 2.1, 101.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, SlotPacket("characterSelect", 1), 3.0, 102.0), 10, "select", true);
		fixture.host->HandleDisconnected(10, 102.5);
		const auto reconnect = RequirePacket(HelloRaw(*fixture.host, 11, kAccountA, true, 4.0, 103.0), 11, "sessionReady");
		assert(reconnect.at("persistenceProfile") == std::string{ std::string{ kAccountA } + "#1" });
		assert(fixture.gate->QueueAcceptedPlayerState(11, Json{ { "type", "playerState" }, { "characterName", "OneRenamed" } }));
		assert(fixture.gate->FlushPendingPlayerStates(true));
		AssertResult(Send(*fixture.host, 11, SlotPacket("characterSelect", 0), 4.5, 103.5), 11, "select", true);
		fixture.host->HandleDisconnected(11, 104.0);
		const auto back = RequirePacket(HelloRaw(*fixture.host, 12, kAccountA, true, 5.0, 104.5), 12, "sessionReady");
		assert(back.at("persistenceProfile") == std::string{ std::string{ kAccountA } + "#0" });
		assert(fixture.gate->QueueAcceptedPlayerState(12, Json{ { "type", "playerState" }, { "characterName", "ZeroRenamed" } }));
		assert(fixture.gate->FlushPendingPlayerStates(true));
		const auto zero = fixture.store->GetPlayerState(std::string{ kAccountA } + "#0");
		const auto one = fixture.store->GetPlayerState(std::string{ kAccountA } + "#1");
		assert(zero && zero->at("characterName") == "ZeroRenamed");
		assert(one && one->at("characterName") == "OneRenamed");
		std::filesystem::remove_all(fixture.directory);
	}

	void TestDeletedSlotReusedWithoutOldData()
	{
		auto fixture = MakeFixture(3, "slots-reuse");
		AdmitAccount(*fixture.store, kAccountA);
		Join(*fixture.host, 10, kAccountA, 1.0, 100.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "Old" } }, 2.0, 101.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 2 }, { "name", "Sibling" } }, 3.0, 102.0), 10, "create", true);
		const auto key = std::string{ kAccountA } + "#1";
		const auto catalog = Json::array({ Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", 5 } } });
		assert(fixture.store->InitializeInventory(key, catalog).at("initialized") == true);
		assert(fixture.store->GetInventory(key).has_value());
		const auto challenge = RequirePacket(Send(*fixture.host, 10, SlotPacket("characterDelete", 1), 4.0, 103.0), 10, "characterDeleteChallenge");
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterDelete" }, { "slot", 1 }, { "confirmToken", challenge.at("confirmToken") } }, 5.0, 104.0), 10, "delete", true);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "New" } }, 6.0, 105.0), 10, "create", true);
		const auto state = fixture.store->GetPlayerState(key);
		assert(state && state->at("characterName") == "New");
		assert(!fixture.store->GetInventory(key).has_value());
		const auto sibling = fixture.store->GetPlayerState(std::string{ kAccountA } + "#2");
		assert(sibling && sibling->at("characterName") == "Sibling");
		fixture.store->WaitForCommits();
		{
			DurableStateFileStore reopened(fixture.directory / "durable-state.json", [] { return 1000.0; }, [] { return std::string{ "0123456789abcdef0123456789abcdef" }; });
			const auto saved = reopened.GetPlayerState(key);
			assert(saved && saved->at("characterName") == "New");
			assert(!reopened.GetInventory(key).has_value());
		}
		std::filesystem::remove_all(fixture.directory);
	}

	void TestOldClientFallback()
	{
		auto fixture = MakeFixture(3, "slots-legacy");
		const auto frames = HelloRaw(*fixture.host, 10, kAccountA, false, 1.0, 100.0);
		const auto ready = RequirePacket(frames, 10, "sessionReady");
		assert(ready.at("characterSlots").at("supported") == true);
		assert(ready.at("characterSlots").at("legacy") == true);
		assert(ready.at("characterSlots").contains("notice"));
		assert(fixture.gate->ProfileForConnection(10) == std::string{ std::string{ kAccountA } + "#0" });
		const auto awareFrames = HelloRaw(*fixture.host, 20, kAccountB, true, 2.0, 101.0);
		const auto awareReady = RequirePacket(awareFrames, 20, "sessionReady");
		assert(!awareReady.at("characterSlots").contains("legacy"));
		std::filesystem::remove_all(fixture.directory);
	}

	void TestRateLimitAndUnknownPackets()
	{
		auto fixture = MakeFixture(3, "slots-rate");
		Join(*fixture.host, 10, kAccountA, 1.0, 100.0);
		// Constraint: the bucket holds 4 at 4/s; the first four pass, the next two limit.
		for (int i = 0; i < 6; ++i) {
			const auto frames = Send(*fixture.host, 10, Json{ { "type", "characterList" } }, 2.0, 101.0);
			if (i < 4) {
				assert(HasPacket(frames, 10, "characterListState"));
				assert(!HasPacket(frames, 10, "characterResult"));
			} else {
				AssertResult(frames, 10, "list", false, "rateLimited");
			}
		}
		const auto refilled = Send(*fixture.host, 10, Json{ { "type", "characterList" } }, 3.0, 102.0);
		assert(HasPacket(refilled, 10, "characterListState"));
		const auto unknown = Send(*fixture.host, 10, Json{ { "type", "characterFrobnicate" } }, 9.0, 103.0);
		assert(!HasPacket(unknown, 10, "characterResult"));
		std::filesystem::remove_all(fixture.directory);
	}

	void TestSessionReadyBindsSlotZero()
	{
		auto fixture = MakeFixture(3, "slots-ready");
		const auto unbound = RequirePacket(HelloRaw(*fixture.host, 10, kAccountA, true, 1.0, 100.0), 10, "sessionReady");
		assert(!unbound.contains("persistenceProfile"));
		assert(unbound.at("characterSlots").at("bound") == false);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "Zero" } }, 2.0, 101.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, SlotPacket("characterSelect", 0), 3.0, 102.0), 10, "select", true);
		fixture.host->HandleDisconnected(10, 102.5);
		const auto ready = RequirePacket(HelloRaw(*fixture.host, 20, kAccountA, false, 4.0, 103.0), 20, "sessionReady");
		assert(ready.at("persistenceProfile") == std::string{ std::string{ kAccountA } + "#0" });
		AdmitAccount(*fixture.store, kAccountA);
		PersistenceGate readyGate(
			*fixture.store, Json::array({ Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", 5 } } }));
		OutboundFrame readyFrame{ 20, Delivery::ReliableOrdered, EncodeJson(ready) };
		std::string error;
		assert(readyGate.PrepareSessionReady(readyFrame, error));
		const auto replay = readyGate.SessionReadyDelivered(readyFrame, 100.0);
		assert(replay && replay->profileId == std::string{ std::string{ kAccountA } + "#0" });
		assert(readyGate.ProfileForConnection(20) == std::string{ std::string{ kAccountA } + "#0" });
		assert(fixture.store->GetInventory(std::string{ kAccountA } + "#0").has_value());
		assert(!fixture.store->GetInventory(kAccountA).has_value());
		std::filesystem::remove_all(fixture.directory);
	}

	void TestLegacyClientReadyBindsSlotZero()
	{
		auto fixture = MakeFixture(3, "slots-legacy-ready");
		const auto frames = HelloRaw(*fixture.host, 11, kAccountA, false, 1.0, 100.0);
		const auto ready = RequirePacket(frames, 11, "sessionReady");
		assert(ready.at("characterSlots").at("legacy") == true);
		// Constraint: old clients persist under slot 0; only the notice differs.
		assert(ready.at("persistenceProfile") == std::string{ std::string{ kAccountA } + "#0" });
		AdmitAccount(*fixture.store, kAccountA);
		PersistenceGate readyGate(
			*fixture.store, Json::array({ Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", 5 } } }));
		OutboundFrame readyFrame{ 11, Delivery::ReliableOrdered, EncodeJson(ready) };
		std::string error;
		assert(readyGate.PrepareSessionReady(readyFrame, error));
		const auto replay = readyGate.SessionReadyDelivered(readyFrame, 100.0);
		assert(replay && replay->profileId == std::string{ std::string{ kAccountA } + "#0" });
		assert(readyGate.ProfileForConnection(11) == std::string{ std::string{ kAccountA } + "#0" });
		assert(!fixture.store->GetInventory(kAccountA).has_value());
		std::filesystem::remove_all(fixture.directory);
	}

	void TestSlotsFullCountsPlayerDataWithoutRecord()
	{
		auto fixture = MakeFixture(3, "slots-full-orphan");
		Join(*fixture.host, 10, kAccountA, 1.0, 100.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "Zero" } }, 2.0, 101.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "One" } }, 2.1, 101.0), 10, "create", true);
		assert(fixture.store->UpdatePlayerState(std::string{ kAccountA } + "#2", Json{ { "characterName", "Ghost" } }));
		fixture.store->WaitForCommits();
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 2 }, { "name", "Third" } }, 2.2, 101.0), 10, "create", false, "slotsFull");
		std::filesystem::remove_all(fixture.directory);
	}

	void TestLegacyHelloBindsLiveSlotAfterZeroDeleted()
	{
		auto fixture = MakeFixture(3, "slots-legacy-deleted-zero");
		Join(*fixture.host, 10, kAccountA, 1.0, 100.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "Zero" } }, 2.0, 101.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "One" } }, 2.1, 101.0), 10, "create", true);
		fixture.host->HandleDisconnected(10, 102.0);
		HelloRaw(*fixture.host, 20, kAccountA, true, 3.0, 103.0);
		const auto challenge = RequirePacket(Send(*fixture.host, 20, SlotPacket("characterDelete", 0), 4.0, 104.0), 20, "characterDeleteChallenge");
		AssertResult(
			Send(*fixture.host, 20, Json{ { "type", "characterDelete" }, { "slot", 0 }, { "confirmToken", challenge.at("confirmToken").get<std::string>() } }, 4.1, 104.1),
			20, "delete", true);
		fixture.host->HandleDisconnected(20, 105.0);
		const auto legacy = RequirePacket(HelloRaw(*fixture.host, 30, kAccountA, false, 5.0, 106.0), 30, "sessionReady");
		assert(legacy.at("persistenceProfile") == std::string{ std::string{ kAccountA } + "#1" });
		assert(legacy.at("characterSlots").at("bound") == true);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestMigrationRerunIsStable()
	{
		const auto directory = TempDirectory("slots-rerun");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore seed(path, clock, instanceId);
			assert(seed.UpdatePlayerState(kAccountA, Json{ { "characterName", "Legacy" } }));
			F4T::Identity::TokenHash hash{};
			hash[0] = 1;
			seed.SaveResumeCredential(kAccountA, hash, std::nullopt, true, 2000.0);
			seed.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			std::string conflict;
			assert(store.MigrateCharacterSlotsToSlotZero(conflict));
			assert(conflict.empty());
			assert(store.MigrateCharacterSlotsToSlotZero(conflict));
			assert(conflict.empty());
			const auto moved = store.GetPlayerState(std::string{ kAccountA } + "#0");
			assert(moved && moved->at("characterName") == "Legacy");
			assert(!store.GetPlayerState(kAccountA).has_value());
			assert(store.GetResumeCredential(kAccountA).has_value());
			assert(!store.GetResumeCredential(std::string{ kAccountA } + "#0").has_value());
		}
		std::filesystem::remove_all(directory);
	}

	void TestDeleteTokenBindsSlotAndAccount()
	{
		auto fixture = MakeFixture(3, "slots-token-misuse");
		Join(*fixture.host, 10, kAccountA, 1.0, 1000.0);
		Join(*fixture.host, 20, kAccountB, 1.1, 1000.1);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "Doomed" } }, 2.0, 1001.0), 10, "create", true);
		const auto first = RequirePacket(
			Send(*fixture.host, 10, SlotPacket("characterDelete", 1), 3.0, 1002.0), 10, "characterDeleteChallenge");
		const auto firstToken = first.at("confirmToken").get<std::string>();
		AssertResult(
			Send(*fixture.host, 10, Json{ { "type", "characterDelete" }, { "slot", 2 }, { "confirmToken", firstToken } }, 3.1, 1002.0),
			10, "delete", false, "badToken");
		const auto second = RequirePacket(
			Send(*fixture.host, 10, SlotPacket("characterDelete", 1), 3.2, 1002.0), 10, "characterDeleteChallenge");
		const auto secondToken = second.at("confirmToken").get<std::string>();
		AssertResult(
			Send(*fixture.host, 20, Json{ { "type", "characterDelete" }, { "slot", 1 }, { "confirmToken", secondToken } }, 3.3, 1002.0),
			20, "delete", false, "badToken");
		const auto third = RequirePacket(
			Send(*fixture.host, 10, SlotPacket("characterDelete", 1), 3.4, 1002.0), 10, "characterDeleteChallenge");
		AssertResult(
			Send(*fixture.host, 10, Json{ { "type", "characterDelete" }, { "slot", 1 }, { "confirmToken", third.at("confirmToken").get<std::string>() } }, 3.5, 1002.0),
			10, "delete", true);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestDeleteLeavesActiveSlot()
	{
		auto fixture = MakeFixture(3, "slots-purge-active");
		Join(*fixture.host, 10, kAccountA, 1.0, 1000.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "Zero" } }, 2.0, 1001.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "One" } }, 2.1, 1001.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, SlotPacket("characterSelect", 0), 3.0, 1002.0), 10, "select", true);
		const auto challenge = RequirePacket(
			Send(*fixture.host, 10, SlotPacket("characterDelete", 1), 3.1, 1002.0), 10, "characterDeleteChallenge");
		AssertResult(
			Send(*fixture.host, 10, Json{ { "type", "characterDelete" }, { "slot", 1 }, { "confirmToken", challenge.at("confirmToken").get<std::string>() } }, 3.2, 1002.0),
			10, "delete", true);
		const auto listed = RequirePacket(
			Send(*fixture.host, 10, Json{ { "type", "characterList" } }, 4.0, 1003.0), 10, "characterListState");
		assert(listed.at("slots").at(1).value("empty", false));
		assert(listed.at("slots").at(0).at("name") == "Zero");
		assert(fixture.gate->ProfileForConnection(10) == std::string{ std::string{ kAccountA } + "#0" });
		std::filesystem::remove_all(fixture.directory);
	}

	void TestSelectDefersBindingToReconnect()
	{
		auto fixture = MakeFixture(3, "slots-reconnect");
		Join(*fixture.host, 10, kAccountA, 1.0, 100.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "Zero" } }, 2.0, 101.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "One" } }, 2.1, 101.0), 10, "create", true);
		const auto stay = RequirePacket(Send(*fixture.host, 10, SlotPacket("characterSelect", 0), 3.0, 102.0), 10, "characterResult");
		assert(stay.at("ok") == true && stay.at("needsReconnect") == true);
		const auto switched = RequirePacket(Send(*fixture.host, 10, SlotPacket("characterSelect", 1), 4.0, 103.0), 10, "characterResult");
		assert(switched.at("ok") == true && switched.at("needsReconnect") == true);
		assert(fixture.gate->ProfileForConnection(10) == std::string{ std::string{ kAccountA } + "#0" });
		assert(fixture.gate->QueueAcceptedPlayerState(10, Json{ { "type", "playerState" }, { "characterName", "StillZero" } }));
		assert(fixture.gate->FlushPendingPlayerStates(true));
		const auto zero = fixture.store->GetPlayerState(std::string{ kAccountA } + "#0");
		assert(zero && zero->at("characterName") == "StillZero");
		const auto repeated = RequirePacket(Send(*fixture.host, 10, SlotPacket("characterSelect", 1), 5.0, 104.0), 10, "characterResult");
		assert(repeated.at("needsReconnect") == true);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestDownedStateFollowsCharacterSlot()
	{
		auto fixture = MakeFixture(3, "slots-downed");
		assert(fixture.host->EnableInventoryTransactions(*fixture.gate));
		AdmitAccount(*fixture.store, kAccountA);
		const std::string zeroQuest{ "quest-11111111111111111111111111111111" };
		assert(fixture.store->SetCharacterSlotRecord(kAccountA, 0, Json{ { "name", "Zero" }, { "questProfile", zeroQuest } }));
		assert(fixture.store->SetCharacterSlotRecord(kAccountA, 1, Json{ { "name", "One" }, { "questProfile", "quest-22222222222222222222222222222222" } }));
		assert(fixture.store->InitializeInventory(std::string{ kAccountA } + "#0", Json::array()).at("initialized") == true);
		fixture.host->HandleConnected(10, 101.0);
		fixture.host->BindSessionProfile(10, kAccountA);
		const auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		const auto joined = Send(*fixture.host, 10, Json{ { "type", "hello" }, { "protocolVersion", 2 },
			{ "capabilities", Json::array({ "inventory-stable-v1", "component-snapshot-v1", "component-snapshot-channels-v1" }) } }, now, 101.0);
		const auto ready = RequirePacket(joined, 10, "sessionReady");
		assert(ready.at("persistenceProfile") == std::string{ std::string{ kAccountA } + "#0" });
		F4T::LoadOrder::PluginList list; list.full.push_back({ F4T::LoadOrder::PluginKind::full, 0, "Fallout4.esm" });
		const auto encoded = F4T::LoadOrder::EncodePluginList(list);
		assert(encoded);
		(void)Send(*fixture.host, 10, Json{ { "type", "pluginList" }, { "data", *encoded } }, now, 101.0);
		(void)Send(*fixture.host, 10, Json{ { "type", "transform" }, { "cellId", "00000002" }, { "worldspaceId", "00000001" },
			{ "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 }, { "angleZ", 0.0 }, { "movementType", "spawn" } }, now, 101.0);
		assert(HasPacket(fixture.host->AdminKillPlayer(ready.at("playerId").get<std::uint32_t>()), 10, "died"));
		assert(fixture.store->GetActiveDeathIntent(zeroQuest) && fixture.store->GetActiveDeathIntent(zeroQuest)->at("state") == "dead");
		AssertResult(Send(*fixture.host, 10, SlotPacket("characterSelect", 1), 3.0, 102.0), 10, "select", true);
		fixture.host->HandleDisconnected(10, 103.0);
		const auto other = HelloRaw(*fixture.host, 20, kAccountA, true, 4.0, 104.0);
		assert(!HasPacket(other, 20, "playerDied"));
		assert(!HasPacket(other, 20, "died"));
		AssertResult(Send(*fixture.host, 20, SlotPacket("characterSelect", 0), 5.0, 105.0), 20, "select", true);
		fixture.host->HandleDisconnected(20, 106.0);
		const auto original = HelloRaw(*fixture.host, 30, kAccountA, true, 7.0, 107.0);
		assert(HasPacket(original, 30, "playerDied"));
		std::filesystem::remove_all(fixture.directory);
	}

	void TestDeleteMigratedSlotZero()
	{
		const auto directory = TempDirectory("slots-migrated-zero");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore seed(path, clock, instanceId);
			assert(seed.UpdatePlayerState(kAccountA, Json{ { "characterName", "Legacy" } }));
			seed.WaitForCommits();
		}
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Character Slots" };
		assert(host.EnableCharacterSlots(&gate, 3));
		Join(host, 10, kAccountA, 1.0, 100.0);
		AssertResult(Send(host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "Second" } }, 2.0, 101.0), 10, "create", true);
		AssertResult(Send(host, 10, SlotPacket("characterSelect", 1), 3.0, 102.0), 10, "select", true);
		host.HandleDisconnected(10, 103.0);
		const auto ready = RequirePacket(HelloRaw(host, 20, kAccountA, true, 4.0, 104.0), 20, "sessionReady");
		assert(ready.at("characterSlots").at("bound") == true);
		assert(ready.at("persistenceProfile") == std::string{ std::string{ kAccountA } + "#1" });
		AssertResult(Send(host, 20, SlotPacket("characterSelect", 1), 4.5, 104.5), 20, "select", true);
		const auto challenge = RequirePacket(
			Send(host, 20, SlotPacket("characterDelete", 0), 5.0, 105.0), 20, "characterDeleteChallenge");
		AssertResult(
			Send(host, 20, Json{ { "type", "characterDelete" }, { "slot", 0 }, { "confirmToken", challenge.at("confirmToken").get<std::string>() } }, 5.1, 105.1),
			20, "delete", true);
		const auto listed = RequirePacket(Send(host, 20, Json{ { "type", "characterList" } }, 5.2, 105.2), 20, "characterListState");
		assert(listed.at("slots").at(0).value("empty", false));
		assert(!listed.at("slots").at(0).contains("name"));
		std::filesystem::remove_all(directory);
	}

	void TestPendingSelectionNeverBindsDeletedSlot()
	{
		auto fixture = MakeFixture(3, "slots-deleted-hello");
		Join(*fixture.host, 10, kAccountA, 1.0, 100.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "Zero" } }, 2.0, 101.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "One" } }, 2.1, 101.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, SlotPacket("characterSelect", 1), 3.0, 102.0), 10, "select", true);
		const auto challenge = RequirePacket(
			Send(*fixture.host, 10, SlotPacket("characterDelete", 1), 4.0, 104.0), 10, "characterDeleteChallenge");
		AssertResult(
			Send(*fixture.host, 10, Json{ { "type", "characterDelete" }, { "slot", 1 }, { "confirmToken", challenge.at("confirmToken").get<std::string>() } }, 4.1, 104.1),
			10, "delete", true);
		fixture.host->HandleDisconnected(10, 105.0);
		const auto first = RequirePacket(HelloRaw(*fixture.host, 20, kAccountA, true, 6.0, 106.0), 20, "sessionReady");
		assert(!first.contains("persistenceProfile"));
		assert(first.at("characterSlots").at("bound") == false);
		fixture.host->HandleDisconnected(20, 106.5);
		const auto second = RequirePacket(HelloRaw(*fixture.host, 30, kAccountA, true, 7.0, 107.0), 30, "sessionReady");
		assert(!second.contains("persistenceProfile"));
		assert(second.at("characterSlots").at("bound") == false);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestStaleSelectionNeverBindsOnReconnect()
	{
		auto fixture = MakeFixture(3, "slots-stale-selection");
		HelloRaw(*fixture.host, 10, kAccountA, true, 1.0, 100.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "Only" } }, 2.0, 101.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, SlotPacket("characterSelect", 1), 3.0, 102.0), 10, "select", true);
		fixture.host->HandleDisconnected(10, 102.5);
		assert(fixture.store->EraseCharacterSlotRecord(kAccountA, 1));
		assert(fixture.store->EraseProfileByKey(std::string{ kAccountA } + "#1"));
		const auto ready = RequirePacket(HelloRaw(*fixture.host, 10, kAccountA, true, 5.0, 105.0), 10, "sessionReady");
		assert(ready.at("characterSlots").at("bound") == false);
		assert(!ready.at("characterSlots").contains("selected"));
		assert(!ready.contains("persistenceProfile"));
		std::filesystem::remove_all(fixture.directory);
	}

	void TestSlotAwareHelloStaysUnboundUntilSelect()
	{
		auto fixture = MakeFixture(3, "slots-last-played");
		HelloRaw(*fixture.host, 10, kAccountB, true, 1.0, 100.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "Zero" } }, 2.0, 101.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "One" } }, 2.1, 105.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 2 }, { "name", "Two" } }, 2.2, 105.0), 10, "create", true);
		const auto ready = RequirePacket(HelloRaw(*fixture.host, 20, kAccountB, true, 3.0, 107.0), 20, "sessionReady");
		assert(!ready.contains("persistenceProfile"));
		assert(ready.at("characterSlots").at("bound") == false);
		AssertResult(Send(*fixture.host, 20, SlotPacket("characterSelect", 1), 3.5, 107.5), 20, "select", true);
		assert(!fixture.gate->ProfileForConnection(20).has_value());
		fixture.host->HandleDisconnected(20, 107.6);
		const auto bound = RequirePacket(HelloRaw(*fixture.host, 30, kAccountB, true, 4.0, 108.0), 30, "sessionReady");
		assert(bound.at("characterSlots").at("bound") == true);
		assert(bound.at("persistenceProfile") == std::string{ std::string{ kAccountB } + "#1" });
		assert(fixture.gate->ProfileForConnection(30) == std::string{ std::string{ kAccountB } + "#1" });
		std::filesystem::remove_all(fixture.directory);
	}

	void TestReconnectBindsPendingSelectionOnce()
	{
		auto fixture = MakeFixture(3, "slots-reconnect-once");
		Join(*fixture.host, 10, kAccountA, 1.0, 100.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "Zero" } }, 2.0, 101.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "One" } }, 2.1, 101.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, SlotPacket("characterSelect", 1), 3.0, 102.0), 10, "select", true);
		fixture.host->HandleDisconnected(10, 102.5);
		const auto legacy = RequirePacket(HelloRaw(*fixture.host, 20, kAccountA, false, 3.5, 102.6), 20, "sessionReady");
		assert(legacy.at("persistenceProfile") == std::string{ std::string{ kAccountA } + "#0" });
		fixture.host->HandleDisconnected(20, 102.7);
		const auto bound = RequirePacket(HelloRaw(*fixture.host, 30, kAccountA, true, 4.0, 103.0), 30, "sessionReady");
		assert(bound.at("persistenceProfile") == std::string{ std::string{ kAccountA } + "#1" });
		assert(bound.at("characterSlots").at("selected") == 1);
		assert(fixture.gate->ProfileForConnection(30) == std::string{ std::string{ kAccountA } + "#1" });
		fixture.host->HandleDisconnected(30, 103.5);
		const auto again = RequirePacket(HelloRaw(*fixture.host, 40, kAccountA, true, 5.0, 104.0), 40, "sessionReady");
		assert(!again.contains("persistenceProfile"));
		assert(again.at("characterSlots").at("bound") == false);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestPendingSelectionExpires()
	{
		auto fixture = MakeFixture(3, "slots-expiry");
		Join(*fixture.host, 10, kAccountA, 1.0, 100.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "Zero" } }, 2.0, 101.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "One" } }, 2.1, 101.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, SlotPacket("characterSelect", 1), 3.0, 102.0), 10, "select", true);
		fixture.host->HandleDisconnected(10, 102.5);
		const auto late = RequirePacket(HelloRaw(*fixture.host, 20, kAccountA, true, 64.0, 103.0), 20, "sessionReady");
		assert(!late.contains("persistenceProfile"));
		assert(late.at("characterSlots").at("bound") == false);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestPendingSelectionIsAccountScoped()
	{
		auto fixture = MakeFixture(3, "slots-account-scope");
		Join(*fixture.host, 10, kAccountA, 1.0, 100.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "Zero" } }, 2.0, 101.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "One" } }, 2.1, 101.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, SlotPacket("characterSelect", 1), 3.0, 102.0), 10, "select", true);
		fixture.host->HandleDisconnected(10, 102.5);
		const auto other = RequirePacket(HelloRaw(*fixture.host, 20, kAccountB, true, 4.0, 103.0), 20, "sessionReady");
		assert(!other.contains("persistenceProfile"));
		const auto own = RequirePacket(HelloRaw(*fixture.host, 30, kAccountA, true, 5.0, 104.0), 30, "sessionReady");
		assert(own.at("persistenceProfile") == std::string{ std::string{ kAccountA } + "#1" });
		std::filesystem::remove_all(fixture.directory);
	}

	void TestSupersedeRosterDropsOldSession()
	{
		auto fixture = MakeFixture(3, "slots-supersede-roster");
		assert(fixture.host->EnableFactions(nullptr));
		const auto oldPlayerId = Join(*fixture.host, 10, kAccountA, 1.0, 100.0).playerId;
		Join(*fixture.host, 30, kAccountB, 1.05, 100.05);
		const auto frames = HelloRaw(*fixture.host, 20, kAccountA, true, 1.1, 100.1);
		const auto newPlayerId = RequirePacket(frames, 20, "sessionReady").at("playerId").get<std::uint32_t>();
		bool rosterWithoutNewSession = false;
		for (const auto& frame : frames) {
			if (frame.connectionId != 30) {
				continue;
			}
			const auto packet = DecodePacket(frame);
			if (packet.value("type", std::string{}) != "playerRoster") {
				continue;
			}
			bool listsNew = false;
			for (const auto& player : packet.at("players")) {
				assert(player.at("playerId") != oldPlayerId);
				listsNew = listsNew || player.at("playerId") == newPlayerId;
			}
			rosterWithoutNewSession = rosterWithoutNewSession || !listsNew;
		}
		assert(rosterWithoutNewSession);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestDroppedPlayerReclaimsIdWithinGrace()
	{
		ProtocolHost host("Reclaim");
		const auto first = HelloRaw(host, 10, kAccountA, false, 1.0, 100.0, true);
		const auto firstId = RequirePacket(first, 10, "sessionReady").at("playerId");
		host.HandleDisconnected(10, 101.0);
		const auto otherReady = RequirePacket(HelloRaw(host, 20, kAccountB, false, 1.5, 101.5), 20, "sessionReady");
		assert(otherReady.at("playerId") != firstId);
		assert(!otherReady.contains("resumed"));
		const auto resumed = RequirePacket(HelloRaw(host, 30, kAccountA, false, 2.0, 102.0, true), 30, "sessionReady");
		assert(resumed.at("playerId") == firstId);
		assert(resumed.at("resumed") == true);
		host.HandleDisconnected(30, 103.0);
		const auto expired = RequirePacket(HelloRaw(host, 40, kAccountA, false, 3.0, 164.0, true), 40, "sessionReady");
		assert(!expired.contains("resumed"));
	}

	void TestResumeRequiresReconnectCapability()
	{
		ProtocolHost host("Reclaim Capability");
		const auto first = RequirePacket(HelloRaw(host, 10, kAccountA, false, 1.0, 100.0, true), 10, "sessionReady");
		host.HandleDisconnected(10, 101.0);
		const auto legacy = RequirePacket(HelloRaw(host, 20, kAccountA, false, 1.5, 101.5), 20, "sessionReady");
		assert(legacy.at("playerId") != first.at("playerId"));
		assert(!legacy.contains("resumed"));
		host.HandleDisconnected(20, 102.0);
		const auto capable = RequirePacket(HelloRaw(host, 30, kAccountA, false, 2.0, 102.5, true), 30, "sessionReady");
		assert(capable.at("playerId") == first.at("playerId"));
		assert(capable.at("resumed") == true);
	}

	void TestHeldPlayersCapAtFourPerMaximumPlayer()
	{
		ProtocolHost host("Hold Cap");
		host.SetAdvertisedMaximumPlayers(1);
		std::vector<Json> readies;
		for (std::uint32_t step = 0; step < 5; ++step) {
			const auto account = std::string{ "co-hold-cap-" } + std::to_string(step);
			const auto connectionId = 10 + step;
			readies.push_back(RequirePacket(HelloRaw(host, connectionId, account, false, 1.0 + step, 100.0 + step, true), connectionId, "sessionReady"));
			host.HandleDisconnected(connectionId, 101.0 + step);
		}
		const auto evicted = RequirePacket(HelloRaw(host, 20, "co-hold-cap-0", false, 10.0, 110.0, true), 20, "sessionReady");
		assert(evicted.at("playerId") != readies.front().at("playerId"));
		assert(!evicted.contains("resumed"));
		const auto retained = RequirePacket(HelloRaw(host, 30, "co-hold-cap-4", false, 11.0, 111.0, true), 30, "sessionReady");
		assert(retained.at("playerId") == readies.back().at("playerId"));
		assert(retained.at("resumed") == true);
	}
	void TestWorldStateHostHandoffNamesNewHostConnection()
	{
		ProtocolHost host("Host Handoff");
		HelloRaw(host, 10, kAccountA, false, 1.0, 100.0);
		HelloRaw(host, 20, kAccountB, false, 1.1, 100.1);
		assert(host.WorldStateHostConnectionId() == 10);
		host.HandleDisconnected(10, 101.0);
		assert(host.WorldStateHostConnectionId() == 20);
		host.HandleDisconnected(20, 102.0);
		assert(!host.WorldStateHostConnectionId().has_value());
	}
	Json HoldBoundSlotOne(ProtocolHost& a_host)
	{
		(void)HelloRaw(a_host, 10, kAccountA, true, 1.0, 100.0);
		AssertResult(Send(a_host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "Hero" } }, 2.0, 101.0), 10, "create", true);
		AssertResult(Send(a_host, 10, SlotPacket("characterSelect", 1), 3.0, 102.0), 10, "select", true);
		a_host.HandleDisconnected(10, 103.0);
		const auto bound = RequirePacket(HelloRaw(a_host, 20, kAccountA, true, 4.0, 104.0, true), 20, "sessionReady");
		assert(bound.at("characterSlots").at("bound") == true);
		assert(bound.at("characterSlots").at("selected") == 1);
		assert(!bound.contains("resumed"));
		a_host.HandleDisconnected(20, 105.0);
		return bound;
	}
	void TestHeldSlotRebindsOnResume()
	{
		auto fixture = MakeFixture(3, "slots-resume");
		const auto bound = HoldBoundSlotOne(*fixture.host);
		const auto resumed = RequirePacket(HelloRaw(*fixture.host, 30, kAccountA, true, 5.0, 106.0, true), 30, "sessionReady");
		assert(resumed.at("playerId") == bound.at("playerId"));
		assert(resumed.at("resumed") == true);
		assert(resumed.at("characterSlots").at("bound") == true);
		assert(resumed.at("characterSlots").at("selected") == 1);
		assert(resumed.at("persistenceProfile") == bound.at("persistenceProfile"));
		assert(fixture.gate->ProfileForConnection(30) == std::string{ std::string{ kAccountA } + "#1" });
		const auto list = RequirePacket(Send(*fixture.host, 30, Json{ { "type", "characterList" } }, 6.0, 106.5), 30, "characterListState");
		assert(list.at("selectedSlot") == 1);
		assert(fixture.host->ActiveCount() == 1);
		std::filesystem::remove_all(fixture.directory);
	}
	void TestResumeWithoutCapabilityLeavesNoBinding()
	{
		auto fixture = MakeFixture(3, "slots-no-capability");
		const auto bound = HoldBoundSlotOne(*fixture.host);
		const auto plain = RequirePacket(HelloRaw(*fixture.host, 30, kAccountA, true, 5.0, 106.0), 30, "sessionReady");
		assert(plain.at("characterSlots").at("bound") == false);
		assert(!plain.contains("resumed"));
		assert(plain.at("playerId") != bound.at("playerId"));
		assert(!fixture.gate->ProfileForConnection(30).has_value());
		const auto resumed = RequirePacket(HelloRaw(*fixture.host, 40, kAccountA, true, 6.0, 107.0, true), 40, "sessionReady");
		assert(resumed.at("playerId") == bound.at("playerId"));
		assert(resumed.at("resumed") == true);
		assert(resumed.at("characterSlots").at("selected") == 1);
		std::filesystem::remove_all(fixture.directory);
	}
	void TestExpiredHoldLeavesNoBinding()
	{
		auto fixture = MakeFixture(3, "slots-expired-hold");
		const auto bound = HoldBoundSlotOne(*fixture.host);
		const auto expired = RequirePacket(HelloRaw(*fixture.host, 30, kAccountA, true, 6.0, 166.0, true), 30, "sessionReady");
		assert(expired.at("characterSlots").at("bound") == false);
		assert(!expired.contains("resumed"));
		assert(expired.at("playerId") != bound.at("playerId"));
		assert(!fixture.gate->ProfileForConnection(30).has_value());
		std::filesystem::remove_all(fixture.directory);
	}
	void TestQuickSecondResumeDoesNotDoubleBind()
	{
		auto fixture = MakeFixture(3, "slots-double-resume");
		const auto bound = HoldBoundSlotOne(*fixture.host);
		const auto first = RequirePacket(HelloRaw(*fixture.host, 30, kAccountA, true, 5.0, 106.0, true), 30, "sessionReady");
		assert(first.at("playerId") == bound.at("playerId"));
		const auto second = HelloRaw(*fixture.host, 40, kAccountA, true, 5.1, 106.1, true);
		assert(RequirePacket(second, 30, "sessionEnded").at("code") == "session_superseded");
		const auto resumed = RequirePacket(second, 40, "sessionReady");
		assert(resumed.at("playerId") == bound.at("playerId"));
		assert(resumed.at("resumed") == true);
		assert(resumed.at("characterSlots").at("bound") == true);
		assert(resumed.at("characterSlots").at("selected") == 1);
		assert(!fixture.gate->ProfileForConnection(30).has_value());
		assert(fixture.gate->ProfileForConnection(40) == std::string{ std::string{ kAccountA } + "#1" });
		assert(fixture.host->ActiveCount() == 1);
		std::filesystem::remove_all(fixture.directory);
	}
	void TestPendingSelectionOverridesHeldSlot()
	{
		auto fixture = MakeFixture(3, "slots-pending-override");
		const auto bound = HoldBoundSlotOne(*fixture.host);
		(void)HelloRaw(*fixture.host, 30, kAccountA, true, 5.0, 106.0);
		AssertResult(Send(*fixture.host, 30, Json{ { "type", "characterCreate" }, { "slot", 2 }, { "name", "Second" } }, 5.1, 106.1), 30, "create", true);
		AssertResult(Send(*fixture.host, 30, SlotPacket("characterSelect", 2), 5.2, 106.2), 30, "select", true);
		const auto chosen = RequirePacket(HelloRaw(*fixture.host, 40, kAccountA, true, 5.3, 106.3, true), 40, "sessionReady");
		assert(chosen.at("characterSlots").at("bound") == true);
		assert(chosen.at("characterSlots").at("selected") == 2);
		assert(!chosen.contains("resumed"));
		assert(chosen.at("playerId") != bound.at("playerId"));
		assert(fixture.gate->ProfileForConnection(40) == std::string{ std::string{ kAccountA } + "#2" });
		std::filesystem::remove_all(fixture.directory);
	}
	void TestHeldSlotDeletedWhileAwayGivesNewId()
	{
		auto fixture = MakeFixture(3, "slots-held-deleted");
		const auto bound = HoldBoundSlotOne(*fixture.host);
		(void)HelloRaw(*fixture.host, 30, kAccountA, true, 5.0, 106.0);
		const auto challenge = RequirePacket(
			Send(*fixture.host, 30, SlotPacket("characterDelete", 1), 5.1, 106.1), 30, "characterDeleteChallenge");
		AssertResult(
			Send(*fixture.host, 30, Json{ { "type", "characterDelete" }, { "slot", 1 }, { "confirmToken", challenge.at("confirmToken").get<std::string>() } }, 5.2, 106.2),
			30, "delete", true);
		const auto returned = RequirePacket(HelloRaw(*fixture.host, 40, kAccountA, true, 6.0, 107.0, true), 40, "sessionReady");
		assert(returned.at("playerId") != bound.at("playerId"));
		assert(returned.at("characterSlots").at("bound") == false);
		assert(!returned.contains("resumed"));
		assert(!returned.contains("persistenceProfile"));
		assert(!fixture.gate->ProfileForConnection(40).has_value());
		std::filesystem::remove_all(fixture.directory);
	}
}
int main()
{
	TestCreateListSelect();
	TestMigrationAdoptsLegacyProfile();
	TestMigrationConflictDisablesSlots();
	TestSameAccountHelloSupersedesOldSession();
	TestDeleteIsPermanent();
	TestLegacySoftDeletedRecordPurged();
	TestDeleteLeavesOtherAccountSlots();
	TestDeleteTokenExpires();
	TestNamesSlotsAndInputValidation();
	TestForgedSlotKeyRejected();
	TestInventoryIsolationBetweenSlots();
	TestDeletedSlotReusedWithoutOldData();
	TestOldClientFallback();
	TestRateLimitAndUnknownPackets();
	TestSessionReadyBindsSlotZero();
	TestLegacyClientReadyBindsSlotZero();
	TestMigrationRerunIsStable();
	TestDeleteTokenBindsSlotAndAccount();
	TestDeleteLeavesActiveSlot();
	TestSelectDefersBindingToReconnect();
	TestDownedStateFollowsCharacterSlot();
	TestDeleteMigratedSlotZero();
	TestPendingSelectionNeverBindsDeletedSlot();
	TestStaleSelectionNeverBindsOnReconnect();
	TestSlotAwareHelloStaysUnboundUntilSelect();
	TestReconnectBindsPendingSelectionOnce();
	TestPendingSelectionExpires();
	TestPendingSelectionIsAccountScoped();
	TestSupersedeRosterDropsOldSession();
	TestDroppedPlayerReclaimsIdWithinGrace();
	TestWorldStateHostHandoffNamesNewHostConnection();
	TestResumeRequiresReconnectCapability();
	TestHeldPlayersCapAtFourPerMaximumPlayer();
	TestHeldSlotRebindsOnResume();
	TestResumeWithoutCapabilityLeavesNoBinding();
	TestExpiredHoldLeavesNoBinding();
	TestQuickSecondResumeDoesNotDoubleBind();
	TestPendingSelectionOverridesHeldSlot();
	TestHeldSlotDeletedWhileAwayGivesNewId();
	TestSlotsFullCountsPlayerDataWithoutRecord();
	TestLegacyHelloBindsLiveSlotAfterZeroDeleted();
	TestInventoryAdmissionBoundsSlots();
	TestInventoryRefusedWithoutOwnerResume();
	std::cout << "All character slot tests passed.\n";
	return 0;
}

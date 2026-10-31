#include "ServerFactionTestSupport.h"

#include <cassert>
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

	Fixture MakeFixture(std::size_t a_slots, std::uint32_t a_retentionDays, std::string_view a_tag)
	{
		Fixture fixture;
		fixture.directory = TempDirectory(a_tag);
		const auto path = fixture.directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		fixture.store = std::make_unique<DurableStateFileStore>(path, clock, instanceId);
		fixture.gate = std::make_unique<PersistenceGate>(*fixture.store);
		fixture.host = std::make_unique<ProtocolHost>("Character Slots");
		assert(fixture.host->EnableCharacterSlots(fixture.gate.get(), a_slots, a_retentionDays));
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
		double a_unix)
	{
		a_host.HandleConnected(a_connectionId, a_unix);
		a_host.BindSessionProfile(a_connectionId, a_account);
		Json hello{ { "type", "hello" }, { "protocolVersion", 2 } };
		if (a_slotAware) {
			hello["slotProtocol"] = 1;
		}
		return a_host.HandleMessage(a_connectionId, EncodeJson(hello), a_mono, a_unix);
	}

	void TestCreateListSelect()
	{
		auto fixture = MakeFixture(3, 7, "slots-create");
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
		assert(host.EnableCharacterSlots(&gate, 3, 7));
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
		assert(!host.EnableCharacterSlots(&gate, 3, 7));
		Join(host, 10, kAccountA, 1.0, 100.0);
		AssertResult(
			Send(host, 10, Json{ { "type", "characterList" } }, 2.0, 101.0), 10, "list", false, "slotsUnavailable");
		const auto kept = store.GetPlayerState(kAccountA);
		assert(kept && kept->at("characterName") == "Original");
		std::filesystem::remove_all(directory);
	}

	void TestSameAccountHelloSupersedesOldSession()
	{
		auto fixture = MakeFixture(3, 7, "slots-supersede");
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

	void TestDeleteRestorePurge()
	{
		auto fixture = MakeFixture(3, 7, "slots-delete");
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
		AssertResult(Send(*fixture.host, 10, SlotPacket("characterSelect", 1), 4.1, 1003.0), 10, "select", false, "slotDeleted");
		AssertResult(Send(*fixture.host, 10, SlotPacket("characterRestore", 1), 5.0, 1004.0), 10, "restore", true);
		const auto relisted = RequirePacket(Send(*fixture.host, 10, Json{ { "type", "characterList" } }, 6.0, 1005.0), 10, "characterListState");
		assert(relisted.at("slots").at(1).at("name") == "Doomed");
		std::filesystem::remove_all(fixture.directory);
	}

	void TestPurgeAfterRetention()
	{
		auto fixture = MakeFixture(3, 7, "slots-purge");
		Join(*fixture.host, 10, kAccountA, 1.0, 1000.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 2 }, { "name", "Ephemeral" } }, 2.0, 1001.0), 10, "create", true);
		const auto challenge = RequirePacket(
			Send(*fixture.host, 10, SlotPacket("characterDelete", 2), 3.0, 1002.0), 10, "characterDeleteChallenge");
		AssertResult(
			Send(*fixture.host, 10, Json{ { "type", "characterDelete" }, { "slot", 2 }, { "confirmToken", challenge.at("confirmToken").get<std::string>() } }, 3.1, 1002.0),
			10, "delete", true);
		const double afterRetention = 1002.0 + 8.0 * 86400.0;
		const auto listed = RequirePacket(
			Send(*fixture.host, 10, Json{ { "type", "characterList" } }, 4.0, afterRetention), 10, "characterListState");
		assert(listed.at("slots").at(2).value("empty", false));
		assert(!fixture.store->GetPlayerState(std::string{ kAccountA } + "#2").has_value());
		AssertResult(Send(*fixture.host, 10, SlotPacket("characterRestore", 2), 4.1, afterRetention), 10, "restore", false, "slotEmpty");
		std::filesystem::remove_all(fixture.directory);
	}

	void TestDeleteTokenExpires()
	{
		auto fixture = MakeFixture(3, 7, "slots-token");
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
		auto fixture = MakeFixture(1, 7, "slots-names");
		Join(*fixture.host, 10, kAccountA, 1.0, 100.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "Solo" } }, 2.0, 101.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "Other" } }, 2.1, 101.0), 10, "create", false, "slotsFull");
		AssertResult(Send(*fixture.host, 10, SlotPacket("characterSelect", 4), 2.2, 101.0), 10, "select", false, "badSlot");
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterSelect" } }, 2.3, 101.0), 10, "select", false, "invalidRequest");
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "  padded" } }, 2.4, 101.0), 10, "create", false, "invalidRequest");
		std::filesystem::remove_all(fixture.directory);

		auto named = MakeFixture(3, 7, "slots-badnames");
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
		auto fixture = MakeFixture(3, 7, "slots-forged");
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
		auto fixture = MakeFixture(3, 7, "slots-admission-bound");
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
		auto fixture = MakeFixture(3, 7, "slots-admission-owner");
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
		auto fixture = MakeFixture(3, 7, "slots-isolation");
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

	void TestOldClientFallback()
	{
		auto fixture = MakeFixture(3, 7, "slots-legacy");
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
		auto fixture = MakeFixture(3, 7, "slots-rate");
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
		auto fixture = MakeFixture(3, 7, "slots-ready");
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
		auto fixture = MakeFixture(3, 7, "slots-legacy-ready");
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
		auto fixture = MakeFixture(3, 7, "slots-full-orphan");
		Join(*fixture.host, 10, kAccountA, 1.0, 100.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "Zero" } }, 2.0, 101.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "One" } }, 2.1, 101.0), 10, "create", true);
		assert(fixture.store->UpdatePlayerState(std::string{ kAccountA } + "#2", Json{ { "characterName", "Ghost" } }));
		fixture.store->WaitForCommits();
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 2 }, { "name", "Third" } }, 2.2, 101.0), 10, "create", false, "slotsFull");
		std::filesystem::remove_all(fixture.directory);
	}

	void TestLegacyHelloNeverBindsDeletedSlotZero()
	{
		auto fixture = MakeFixture(3, 7, "slots-legacy-deleted-zero");
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
		assert(!legacy.contains("persistenceProfile"));
		assert(legacy.at("characterSlots").at("bound") == false);
		assert(legacy.at("characterSlots").at("notice").get<std::string>().find("no character is loaded") != std::string::npos);
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
		auto fixture = MakeFixture(3, 7, "slots-token-misuse");
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

	void TestPurgeSkipsActiveSlot()
	{
		auto fixture = MakeFixture(3, 7, "slots-purge-active");
		Join(*fixture.host, 10, kAccountA, 1.0, 1000.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "Zero" } }, 2.0, 1001.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "One" } }, 2.1, 1001.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, SlotPacket("characterSelect", 0), 3.0, 1002.0), 10, "select", true);
		const auto challenge = RequirePacket(
			Send(*fixture.host, 10, SlotPacket("characterDelete", 1), 3.1, 1002.0), 10, "characterDeleteChallenge");
		AssertResult(
			Send(*fixture.host, 10, Json{ { "type", "characterDelete" }, { "slot", 1 }, { "confirmToken", challenge.at("confirmToken").get<std::string>() } }, 3.2, 1002.0),
			10, "delete", true);
		const double afterRetention = 1002.0 + 8.0 * 86400.0;
		const auto listed = RequirePacket(
			Send(*fixture.host, 10, Json{ { "type", "characterList" } }, 4.0, afterRetention), 10, "characterListState");
		assert(listed.at("slots").at(1).value("empty", false));
		assert(listed.at("slots").at(0).at("name") == "Zero");
		assert(fixture.gate->ProfileForConnection(10) == std::string{ std::string{ kAccountA } + "#0" });
		std::filesystem::remove_all(fixture.directory);
	}

	void TestSelectDefersBindingToReconnect()
	{
		auto fixture = MakeFixture(3, 7, "slots-reconnect");
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
		assert(host.EnableCharacterSlots(&gate, 3, 7));
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
		assert(listed.at("slots").at(0).value("deleted", false));
		assert(listed.at("slots").at(0).value("empty", false));
		AssertResult(Send(host, 20, SlotPacket("characterRestore", 0), 6.0, 106.0), 20, "restore", true);
		const auto relisted = RequirePacket(Send(host, 20, Json{ { "type", "characterList" } }, 7.0, 107.0), 20, "characterListState");
		assert(relisted.at("slots").at(0).at("name") == "Legacy");
		std::filesystem::remove_all(directory);
	}

	void TestPendingSelectionNeverBindsDeletedSlot()
	{
		auto fixture = MakeFixture(3, 7, "slots-deleted-hello");
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
		auto fixture = MakeFixture(3, 7, "slots-stale-selection");
		HelloRaw(*fixture.host, 10, kAccountA, true, 1.0, 100.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "Only" } }, 2.0, 101.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, SlotPacket("characterSelect", 1), 3.0, 102.0), 10, "select", true);
		fixture.host->HandleDisconnected(10, 102.5);
		assert(fixture.store->SetCharacterSlotRecord(kAccountA, 1, Json{ { "name", "Only" }, { "createdAt", 101.0 }, { "lastPlayedAt", 102.0 }, { "deletedAt", 104.0 } }));
		const auto ready = RequirePacket(HelloRaw(*fixture.host, 10, kAccountA, true, 5.0, 105.0), 10, "sessionReady");
		assert(ready.at("characterSlots").at("bound") == false);
		assert(!ready.at("characterSlots").contains("selected"));
		assert(!ready.contains("persistenceProfile"));
		std::filesystem::remove_all(fixture.directory);
	}

	void TestSlotAwareHelloStaysUnboundUntilSelect()
	{
		auto fixture = MakeFixture(3, 7, "slots-last-played");
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
		auto fixture = MakeFixture(3, 7, "slots-reconnect-once");
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
		auto fixture = MakeFixture(3, 7, "slots-expiry");
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
		auto fixture = MakeFixture(3, 7, "slots-account-scope");
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
		auto fixture = MakeFixture(3, 7, "slots-supersede-roster");
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
}

int main()
{
	TestCreateListSelect();
	TestMigrationAdoptsLegacyProfile();
	TestMigrationConflictDisablesSlots();
	TestSameAccountHelloSupersedesOldSession();
	TestDeleteRestorePurge();
	TestPurgeAfterRetention();
	TestDeleteTokenExpires();
	TestNamesSlotsAndInputValidation();
	TestForgedSlotKeyRejected();
	TestInventoryIsolationBetweenSlots();
	TestOldClientFallback();
	TestRateLimitAndUnknownPackets();
	TestSessionReadyBindsSlotZero();
	TestLegacyClientReadyBindsSlotZero();
	TestMigrationRerunIsStable();
	TestDeleteTokenBindsSlotAndAccount();
	TestPurgeSkipsActiveSlot();
	TestSelectDefersBindingToReconnect();
	TestDeleteMigratedSlotZero();
	TestPendingSelectionNeverBindsDeletedSlot();
	TestStaleSelectionNeverBindsOnReconnect();
	TestSlotAwareHelloStaysUnboundUntilSelect();
	TestReconnectBindsPendingSelectionOnce();
	TestPendingSelectionExpires();
	TestPendingSelectionIsAccountScoped();
	TestSupersedeRosterDropsOldSession();
	TestSlotsFullCountsPlayerDataWithoutRecord();
	TestLegacyHelloNeverBindsDeletedSlotZero();
	TestInventoryAdmissionBoundsSlots();
	TestInventoryRefusedWithoutOwnerResume();
	std::cout << "All character slot tests passed.\n";
	return 0;
}

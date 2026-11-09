#include "ServerFactionTestSupport.h"
#include "F4TQuestProtocol.h"
#include "ServerCharacterSlots.h"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace FactionTestSupport;
using CommonwealthOnline::Server::CharacterSlotService;
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

	bool Contains(const std::vector<std::string>& a_ids, const std::string& a_id)
	{
		return std::find(a_ids.begin(), a_ids.end(), a_id) != a_ids.end();
	}

	std::vector<std::string> PendingAfterReopen(Fixture& a_fixture)
	{
		a_fixture.store->WaitForCommits();
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore reopened(a_fixture.directory / "durable-state.json", clock, instanceId);
		return reopened.PendingQuestErase();
	}

	void TestNewCharacterQuestProgressIsIsolated()
	{
		auto fixture = MakeFixture(3, "slots-quest-isolation");
		assert(fixture.host->EnableQuestSync(fixture.gate.get(), F4T::QuestProtocol::Mode::CoopStory, {}));
		(void)HelloRaw(*fixture.host, 10, kAccountA, true, 1.0, 100.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "First" } }, 2.0, 101.0), 10, "create", true);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 2 }, { "name", "Second" } }, 3.0, 102.0), 10, "create", true);
		const auto records = fixture.store->GetCharacterSlotRecords(kAccountA);
		const auto firstQuest = records.at(1).at("questProfile").get<std::string>();
		const auto secondQuest = records.at(2).at("questProfile").get<std::string>();
		assert(firstQuest != secondQuest);
		AssertResult(Send(*fixture.host, 10, SlotPacket("characterSelect", 1), 4.0, 103.0), 10, "select", true);
		fixture.host->HandleDisconnected(10, 103.5);
		(void)HelloRaw(*fixture.host, 11, kAccountA, true, 5.0, 104.0, false, true);
		(void)Send(*fixture.host, 11, Json{ { "type", "questReport" }, { "quest", "01CC2A:fallout4.esm" }, { "stage", 2 } }, 6.0, 105.0);
		(void)fixture.gate->FlushPendingPlayerStates(true);
		const auto quests = fixture.store->GetQuests();
		assert(quests.at("tutorialExit").contains(firstQuest));
		assert(!quests.at("tutorialExit").contains(secondQuest));
		AssertResult(Send(*fixture.host, 11, SlotPacket("characterSelect", 2), 7.0, 106.0), 11, "select", true);
		fixture.host->HandleDisconnected(11, 106.5);
		const auto secondHello = HelloRaw(*fixture.host, 12, kAccountA, true, 8.0, 107.0, false, true);
		assert(HasPacket(secondHello, 12, "questPolicy"));
		for (const auto& frame : secondHello) {
			const auto packet = DecodePacket(frame);
			if (frame.connectionId != 12 || packet.value("type", std::string{}) != "questSync") continue;
			for (const auto& stage : packet.at("stages")) assert(stage.at("quest") != "01CC2A:fallout4.esm");
		}
		(void)Send(*fixture.host, 12, Json{ { "type", "questReport" }, { "quest", "01CC2A:fallout4.esm" }, { "stage", 2 } }, 9.0, 108.0);
		(void)fixture.gate->FlushPendingPlayerStates(true);
		const auto after = fixture.store->GetQuests();
		assert(after.at("tutorialExit").contains(firstQuest));
		assert(after.at("tutorialExit").contains(secondQuest));
		const auto accountKeys = fixture.store->PersistedAccountKeys();
		assert(std::find(accountKeys.begin(), accountKeys.end(), kAccountA) != accountKeys.end());
		assert(std::find(accountKeys.begin(), accountKeys.end(), firstQuest) == accountKeys.end());
		assert(std::find(accountKeys.begin(), accountKeys.end(), secondQuest) == accountKeys.end());
		std::filesystem::remove_all(fixture.directory);
	}

	void TestDuplicateQuestIdentityAcrossAccountsIsRejected()
	{
		auto fixture = MakeFixture(3, "slots-duplicate-quest");
		const std::string id = "quest-0123456789abcdef0123456789abcdef";
		assert(fixture.store->SetCharacterSlotRecord(kAccountA, 1, Json{ { "name", "One" }, { "questProfile", id } }));
		assert(fixture.store->SetCharacterSlotRecord(kAccountB, 1, Json{ { "name", "Two" }, { "questProfile", id } }));
		assert(fixture.store->QuestProfileOwnedByAnotherAccount(kAccountA, id));
		assert(fixture.store->QuestProfileOwnedByAnotherAccount(kAccountB, id));
		fixture.store->SaveQuests(Json{ { "profiles", Json{ { id, Json::array() } } } });
		const auto keys = fixture.store->PersistedAccountKeys();
		assert(std::find(keys.begin(), keys.end(), kAccountA) != keys.end());
		assert(std::find(keys.begin(), keys.end(), kAccountB) != keys.end());
		std::filesystem::remove_all(fixture.directory);
	}

	void TestLegacyCharactersStartSeparateQuestHistories()
	{
		auto fixture = MakeFixture(3, "slots-legacy-quests");
		assert(fixture.store->SetCharacterSlotRecord(kAccountA, 1, Json{ { "name", "LegacyOne" } }));
		assert(fixture.store->SetCharacterSlotRecord(kAccountA, 2, Json{ { "name", "LegacyTwo" } }));
		assert(fixture.store->UpdatePlayerState(std::string{ kAccountA } + "#1", Json{ { "characterName", "LegacyOne" } }));
		assert(fixture.store->UpdatePlayerState(std::string{ kAccountA } + "#2", Json{ { "characterName", "LegacyTwo" } }));
		fixture.store->SaveQuests(Json{ { "profiles", Json{ { kAccountA, Json::array({ Json{ { "quest", "01CC2A:fallout4.esm" }, { "stage", 10 }, { "completed", true } } }) } } }, { "tutorialExit", Json{ { kAccountA, 10 } } } });
		assert(fixture.host->EnableQuestSync(fixture.gate.get(), F4T::QuestProtocol::Mode::CoopStory, {}));
		(void)HelloRaw(*fixture.host, 10, kAccountA, true, 1.0, 100.0);
		AssertResult(Send(*fixture.host, 10, SlotPacket("characterSelect", 1), 2.0, 101.0), 10, "select", true);
		fixture.host->HandleDisconnected(10, 101.5);
		(void)HelloRaw(*fixture.host, 11, kAccountA, true, 3.0, 102.0, false, true);
		const auto firstQuest = fixture.store->GetCharacterSlotRecords(kAccountA).at(1).at("questProfile").get<std::string>();
		const auto firstStatus = fixture.host->QuestStatus();
		assert(!firstStatus.at("profiles").contains(firstQuest));
		assert(!fixture.store->GetQuests().at("tutorialExit").contains(firstQuest));
		AssertResult(Send(*fixture.host, 11, SlotPacket("characterSelect", 2), 4.0, 103.0), 11, "select", true);
		fixture.host->HandleDisconnected(11, 103.5);
		(void)HelloRaw(*fixture.host, 12, kAccountA, true, 5.0, 104.0, false, true);
		const auto secondQuest = fixture.store->GetCharacterSlotRecords(kAccountA).at(2).at("questProfile").get<std::string>();
		assert(firstQuest != secondQuest);
		assert(!fixture.host->QuestStatus().at("profiles").contains(secondQuest));
		assert(fixture.host->QuestStatus().at("profiles").contains(kAccountA));
		assert(fixture.store->GetQuests().at("tutorialExit").contains(kAccountA));
		fixture.store->WaitForCommits();
		std::ifstream input(fixture.directory / "durable-state.json");
		const auto saved = Json::parse(input);
		const auto& backup = saved.at("characterSlots").at(kAccountA).at("_legacyAccountQuestBackup");
		assert(backup.at("profiles").at(0).at("stage") == 10);
		assert(backup.at("tutorialExit") == 10);
		input.close();
		std::filesystem::remove_all(fixture.directory);
	}

	void TestEmptyLegacyQuestIdentitySurvivesReconnect()
	{
		auto fixture = MakeFixture(3, "slots-empty-legacy-quest");
		const auto readState = [&] {
			fixture.store->WaitForCommits();
			std::ifstream input(fixture.directory / "durable-state.json");
			return Json::parse(input);
		};
		const auto first = RequirePacket(HelloRaw(*fixture.host, 10, kAccountA, false, 1.0, 100.0), 10, "sessionReady");
		assert(first.at("characterSlots").at("bound") == true);
		const auto original = readState().at("characterSlots").at(kAccountA).at("_legacyQuest0").get<std::string>();
		assert(fixture.store->GetCharacterSlotRecords(kAccountA).empty());
		fixture.host->HandleDisconnected(10, 100.5);
		const auto second = RequirePacket(HelloRaw(*fixture.host, 11, kAccountA, false, 2.0, 101.0), 11, "sessionReady");
		assert(second.at("characterSlots").at("bound") == true);
		assert(readState().at("characterSlots").at(kAccountA).at("_legacyQuest0") == original);
		assert(fixture.store->UpdatePlayerState(std::string{ kAccountA } + "#0", Json{ { "characterName", "Legacy" } }));
		fixture.host->HandleDisconnected(11, 101.5);
		(void)HelloRaw(*fixture.host, 12, kAccountA, true, 3.0, 102.0);
		AssertResult(Send(*fixture.host, 12, SlotPacket("characterSelect", 0), 4.0, 103.0), 12, "select", true);
		fixture.host->HandleDisconnected(12, 103.5);
		(void)HelloRaw(*fixture.host, 13, kAccountA, true, 5.0, 104.0);
		assert(fixture.store->GetCharacterSlotRecords(kAccountA).at(0).at("questProfile") == original);
		assert(!readState().at("characterSlots").at(kAccountA).contains("_legacyQuest0"));
		std::filesystem::remove_all(fixture.directory);
	}

	void TestNewSlotPreservesPendingLegacyQuestHistory()
	{
		auto fixture = MakeFixture(3, "slots-pending-legacy-quest");
		assert(fixture.host->EnableQuestSync(fixture.gate.get(), F4T::QuestProtocol::Mode::CoopStory, {}));
		(void)HelloRaw(*fixture.host, 10, kAccountA, false, 1.0, 100.0, false, true);
		(void)Send(*fixture.host, 10, Json{ { "type", "questReport" }, { "quest", "01CC2A:fallout4.esm" }, { "stage", 2 } }, 2.0, 101.0);
		(void)fixture.gate->FlushPendingPlayerStates(true);
		std::ifstream beforeFile(fixture.directory / "durable-state.json");
		const auto before = Json::parse(beforeFile);
		const auto oldQuest = before.at("characterSlots").at(kAccountA).at("_legacyQuest0").get<std::string>();
		assert(before.at("quests").at("tutorialExit").contains(oldQuest));
		beforeFile.close();
		fixture.host->HandleDisconnected(10, 101.5);
		(void)HelloRaw(*fixture.host, 11, kAccountA, true, 3.0, 102.0);
		AssertResult(Send(*fixture.host, 11, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "Fresh" } }, 4.0, 103.0), 11, "create", true);
		fixture.store->WaitForCommits();
		std::ifstream afterFile(fixture.directory / "durable-state.json");
		const auto after = Json::parse(afterFile);
		const auto freshQuest = after.at("characterSlots").at(kAccountA).at("0").at("questProfile").get<std::string>();
		assert(freshQuest != oldQuest);
		assert(after.at("characterSlots").at(kAccountA).at("_legacyQuestArchive").at(0) == oldQuest);
		assert(after.at("quests").at("tutorialExit").contains(oldQuest));
		assert(!after.at("quests").at("tutorialExit").contains(freshQuest));
		afterFile.close();
		std::filesystem::remove_all(fixture.directory);
	}

		void TestReplacedCharacterQuestProgressIsRetired()
		{
			auto fixture = MakeFixture(3, "slots-quest-retire");
			assert(fixture.host->EnableQuestSync(fixture.gate.get(), F4T::QuestProtocol::Mode::CoopStory, {}));
			(void)HelloRaw(*fixture.host, 10, kAccountA, true, 1.0, 100.0, false, true);
			AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "First" } }, 2.0, 101.0), 10, "create", true);
			AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 2 }, { "name", "Second" } }, 3.0, 102.0), 10, "create", true);
			AssertResult(Send(*fixture.host, 10, SlotPacket("characterSelect", 1), 4.0, 103.0), 10, "select", true);
			fixture.host->HandleDisconnected(10, 103.5);
			(void)HelloRaw(*fixture.host, 11, kAccountA, true, 5.0, 104.0, false, true);
			(void)Send(*fixture.host, 11, Json{ { "type", "questReport" }, { "quest", "01CC2A:fallout4.esm" }, { "stage", 2 } }, 6.0, 105.0);
			AssertResult(Send(*fixture.host, 11, SlotPacket("characterSelect", 2), 7.0, 106.0), 11, "select", true);
			fixture.host->HandleDisconnected(11, 106.5);
			(void)HelloRaw(*fixture.host, 12, kAccountA, true, 8.0, 107.0, false, true);
			(void)Send(*fixture.host, 12, Json{ { "type", "questReport" }, { "quest", "01CC2A:fallout4.esm" }, { "stage", 2 } }, 9.0, 108.0);
			const auto records = fixture.store->GetCharacterSlotRecords(kAccountA);
			const auto firstQuest = records.at(1).at("questProfile").get<std::string>();
			const auto secondQuest = records.at(2).at("questProfile").get<std::string>();
			assert(firstQuest != secondQuest);
			const auto before = fixture.host->QuestStatus().at("profiles");
			assert(before.contains(firstQuest));
			assert(before.contains(secondQuest));
			const auto challenge = RequirePacket(Send(*fixture.host, 12, SlotPacket("characterDelete", 1), 10.0, 109.0), 12, "characterDeleteChallenge");
			AssertResult(Send(*fixture.host, 12, Json{ { "type", "characterDelete" }, { "slot", 1 }, { "confirmToken", challenge.at("confirmToken").get<std::string>() } }, 10.1, 109.0), 12, "delete", true);
			AssertResult(Send(*fixture.host, 12, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "Replacement" } }, 11.0, 110.0), 12, "create", true);
			const auto after = fixture.host->QuestStatus().at("profiles");
			assert(!after.contains(firstQuest));
			assert(after.contains(secondQuest));
			(void)fixture.gate->FlushPendingPlayerStates(true);
			const auto saved = fixture.store->GetQuests();
			assert(!saved.at("tutorialExit").contains(firstQuest));
			assert(saved.at("tutorialExit").contains(secondQuest));
			assert(!saved.at("profiles").contains(firstQuest));
			assert(!Contains(fixture.store->PendingQuestErase(), firstQuest));
			assert(saved.at("profiles").contains(secondQuest));
			std::filesystem::remove_all(fixture.directory);
		}

	std::vector<OutboundFrame> DispatchSlotPacket(
		CharacterSlotService& a_service,
		F4T::PacketRateLimit::TokenBucket& a_bucket,
		std::string_view a_account,
		const Json& a_packet,
		double a_monotonicSeconds)
	{
		const CharacterSlotService::SessionMatches matches = [](std::uint32_t, std::string_view) { return false; };
		const auto type = a_packet.at("type").get<std::string>();
		auto frames = a_service.Dispatch(type, 10, a_account, a_packet, a_monotonicSeconds, 1000.0, matches, a_bucket);
		assert(frames);
		return *frames;
	}

	void DeleteSlotThroughService(CharacterSlotService& a_service, std::string_view a_account, std::uint32_t a_slot)
	{
		F4T::PacketRateLimit::TokenBucket bucket;
		const auto challenge = DispatchSlotPacket(a_service, bucket, a_account, SlotPacket("characterDelete", a_slot), 1.0);
		const auto token = RequirePacket(challenge, 10, "characterDeleteChallenge").at("confirmToken").get<std::string>();
		AssertResult(
			DispatchSlotPacket(a_service, bucket, a_account, Json{ { "type", "characterDelete" }, { "slot", a_slot }, { "confirmToken", token } }, 1.1),
			10,
			"delete",
			true);
	}

	void TestConfirmedDeleteQueuesSlotQuestProfileOnce()
	{
		auto fixture = MakeFixture(3, "slots-erased-quest");
		CharacterSlotService service;
		assert(service.Attach(fixture.gate.get(), {}));
		assert(fixture.store->SetCharacterSlotRecord(kAccountA, 1, Json{ { "name", "One" }, { "questProfile", "quest-0123456789abcdef0123456789abcdef" } }));
		assert(fixture.store->UpdatePlayerState(std::string{ kAccountA } + "#1", Json{ { "characterName", "One" } }));
		DeleteSlotThroughService(service, kAccountA, 1);
		assert(!fixture.store->GetCharacterSlotRecords(kAccountA).contains(1));
		assert(!fixture.store->GetPlayerState(std::string{ kAccountA } + "#1").has_value());
		const std::vector<std::string> expectedErased{ std::string{ kAccountA } + "#1", std::string{ kAccountA } };
		assert(service.TakeErasedProfiles() == expectedErased);
		assert(fixture.store->PendingQuestErase() == std::vector<std::string>{ "quest-0123456789abcdef0123456789abcdef" });
		assert(PendingAfterReopen(fixture) == std::vector<std::string>{ "quest-0123456789abcdef0123456789abcdef" });
		std::filesystem::remove_all(fixture.directory);
	}

	void TestDeleteWithoutQuestProfileQueuesNothing()
	{
		auto fixture = MakeFixture(3, "slots-erased-no-quest");
		CharacterSlotService service;
		assert(service.Attach(fixture.gate.get(), {}));
		assert(fixture.store->SetCharacterSlotRecord(kAccountA, 1, Json{ { "name", "Legacy" } }));
		DeleteSlotThroughService(service, kAccountA, 1);
		assert(fixture.store->PendingQuestErase().empty());
		std::filesystem::remove_all(fixture.directory);
	}

	void TestDeleteQueuesOnlyDeletedSlotQuestProfile()
	{
		auto fixture = MakeFixture(3, "slots-erased-quest-scope");
		CharacterSlotService service;
		assert(service.Attach(fixture.gate.get(), {}));
		assert(fixture.store->SetCharacterSlotRecord(kAccountA, 1, Json{ { "name", "One" }, { "questProfile", "quest-0123456789abcdef0123456789abcdef" } }));
		assert(fixture.store->SetCharacterSlotRecord(kAccountA, 2, Json{ { "name", "Two" }, { "questProfile", "quest-fedcba9876543210fedcba9876543210" } }));
		assert(fixture.store->SetCharacterSlotRecord(kAccountB, 1, Json{ { "name", "Three" }, { "questProfile", "quest-00112233445566778899aabbccddeeff" } }));
		DeleteSlotThroughService(service, kAccountA, 1);
		assert(fixture.store->PendingQuestErase() == std::vector<std::string>{ "quest-0123456789abcdef0123456789abcdef" });
		std::filesystem::remove_all(fixture.directory);
	}

	void TestActiveSoftDeletedRecordSurvivesReconcile()
	{
		auto fixture = MakeFixture(3, "slots-active-soft-deleted");
		assert(fixture.store->SetCharacterSlotRecord(kAccountA, 1, Json{ { "name", "Bound" }, { "createdAt", 100.0 }, { "lastPlayedAt", 100.0 }, { "questProfile", "quest-0123456789abcdef0123456789abcdef" } }));
		assert(fixture.store->UpdatePlayerState(std::string{ kAccountA } + "#1", Json{ { "characterName", "Bound" } }));
		const auto hello = RequirePacket(HelloRaw(*fixture.host, 10, kAccountA, false, 1.0, 100.0), 10, "sessionReady");
		assert(hello.at("characterSlots").at("bound") == true);
		assert(fixture.store->SetCharacterSlotRecord(kAccountA, 1, Json{ { "name", "Bound" }, { "createdAt", 100.0 }, { "lastPlayedAt", 100.0 }, { "questProfile", "quest-0123456789abcdef0123456789abcdef" }, { "deletedAt", 1001.0 } }));
		const auto listed = RequirePacket(Send(*fixture.host, 10, Json{ { "type", "characterList" } }, 2.0, 1002.0), 10, "characterListState");
		assert(listed.at("selectedSlot") == 1);
		assert(!listed.at("slots").at(1).value("empty", false));
		assert(fixture.store->GetCharacterSlotRecords(kAccountA).contains(1));
		std::filesystem::remove_all(fixture.directory);
	}

	void TestOrphanRecordAgesOutOutsideActiveSlot()
	{
		auto fixture = MakeFixture(3, "slots-orphan-age");
		CharacterSlotService service;
		assert(service.Attach(fixture.gate.get(), {}));
		const double now = 200000.0;
		assert(fixture.store->SetCharacterSlotRecord(kAccountA, 1, Json{ { "name", "Recent" }, { "createdAt", now - 86399.0 }, { "questProfile", "quest-fedcba9876543210fedcba9876543210" } }));
		assert(fixture.store->SetCharacterSlotRecord(kAccountA, 2, Json{ { "name", "Stale" }, { "createdAt", now - 90000.0 }, { "lastPlayedAt", now - 86400.0 }, { "questProfile", "quest-0123456789abcdef0123456789abcdef" } }));
		F4T::PacketRateLimit::TokenBucket bucket;
		const CharacterSlotService::SessionMatches matches = [](std::uint32_t, std::string_view) { return false; };
		assert(service.Dispatch("characterList", 10, kAccountA, Json{ { "type", "characterList" } }, 1.0, now, matches, bucket));
		const auto records = fixture.store->GetCharacterSlotRecords(kAccountA);
		assert(records.contains(1));
		assert(!records.contains(2));
		assert(fixture.store->PendingQuestErase() == std::vector<std::string>{ "quest-0123456789abcdef0123456789abcdef" });
		std::filesystem::remove_all(fixture.directory);
	}

	void TestOrphanQuestIdentityIsDurableBeforeCleanup()
	{
		auto fixture = MakeFixture(3, "slots-pending-orphan");
		const std::string orphanQuest = "quest-0123456789abcdef0123456789abcdef";
		assert(fixture.store->SetCharacterSlotRecord(kAccountA, 2, Json{ { "name", "Ghost" }, { "createdAt", 1000.0 }, { "lastPlayedAt", 1000.0 }, { "questProfile", orphanQuest } }));
		(void)HelloRaw(*fixture.host, 10, kAccountA, true, 1.0, 1000.0 + 8.0 * 86400.0);
		assert(!fixture.store->GetCharacterSlotRecords(kAccountA).contains(2));
		assert(!Contains(fixture.store->PendingQuestErase(), orphanQuest));
		assert(!Contains(PendingAfterReopen(fixture), orphanQuest));
		std::filesystem::remove_all(fixture.directory);
	}

	Json OldQuestBlob(const std::string& a_questProfile)
	{
		return Json{ { "profiles", Json{ { a_questProfile, Json::array({ Json{ { "quest", "01CC2A:fallout4.esm" }, { "stage", 10 }, { "completed", true } } }) } } },
			{ "profileActivity", Json{ { a_questProfile, 3u } } }, { "solo", Json{ { a_questProfile, Json::array() } } }, { "tutorialExit", Json{ { a_questProfile, 10u } } } };
	}

	void DeleteSlotOne(Fixture& a_fixture, std::uint32_t a_connectionId)
	{
		const auto challenge = RequirePacket(Send(*a_fixture.host, a_connectionId, SlotPacket("characterDelete", 1), 3.0, 102.0), a_connectionId, "characterDeleteChallenge");
		AssertResult(
			Send(*a_fixture.host, a_connectionId, Json{ { "type", "characterDelete" }, { "slot", 1 }, { "confirmToken", challenge.at("confirmToken").get<std::string>() } }, 3.1, 102.0),
			a_connectionId, "delete", true);
	}

	void ReopenWithEditedDocument(Fixture& a_fixture, const std::function<void(Json&)>& a_edit)
	{
		a_fixture.store->WaitForCommits();
		a_fixture.host.reset();
		a_fixture.gate.reset();
		a_fixture.store.reset();
		const auto path = a_fixture.directory / "durable-state.json";
		Json document;
		{
			std::ifstream in(path);
			in >> document;
		}
		a_edit(document);
		{
			std::ofstream out(path, std::ios::trunc);
			out << document.dump();
		}
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		a_fixture.store = std::make_unique<DurableStateFileStore>(path, clock, instanceId);
		a_fixture.gate = std::make_unique<PersistenceGate>(*a_fixture.store);
		a_fixture.host = std::make_unique<ProtocolHost>("Character Slots");
	}

	void TestSyncOffDeleteErasesStoredQuestDataThenEnableStaysClean()
	{
		auto fixture = MakeFixture(3, "slots-pending-enable");
		(void)HelloRaw(*fixture.host, 10, kAccountA, true, 1.0, 100.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "First" } }, 2.0, 101.0), 10, "create", true);
		const auto oldQuest = fixture.store->GetCharacterSlotRecords(kAccountA).at(1).at("questProfile").get<std::string>();
		assert(fixture.store->SaveQuests(OldQuestBlob(oldQuest)));
		DeleteSlotOne(fixture, 10);
		assert(!Contains(fixture.store->PendingQuestErase(), oldQuest));
		for (const char* table : { "profiles", "profileActivity", "solo", "tutorialExit" }) {
			assert(!fixture.store->GetQuests().at(table).contains(oldQuest));
		}
		assert(!Contains(PendingAfterReopen(fixture), oldQuest));
		fixture.host = std::make_unique<ProtocolHost>("Character Slots");
		assert(fixture.host->EnableCharacterSlots(fixture.gate.get(), 3));
		assert(fixture.host->EnableQuestSync(fixture.gate.get(), F4T::QuestProtocol::Mode::CoopStory, {}));
		assert(!fixture.host->QuestStatus().at("profiles").contains(oldQuest));
		std::filesystem::remove_all(fixture.directory);
	}

	void TestSyncOffRestartDrainsMarkersOfEarlierRun()
	{
		auto fixture = MakeFixture(3, "slots-sync-off-restart");
		const std::string oldQuest = "quest-0123456789abcdef0123456789abcdef";
		const std::string keptQuest = "quest-fedcba9876543210fedcba9876543210";
		assert(fixture.store->SaveQuests(Json{ { "profiles", Json{ { oldQuest, Json::array() }, { keptQuest, Json::array() } } }, { "tutorialExit", Json{ { oldQuest, 10u }, { keptQuest, 4u } } } }));
		ReopenWithEditedDocument(fixture, [&](Json& a_document) { a_document["pendingQuestErase"] = Json::array({ oldQuest }); });
		assert(fixture.host->EnableCharacterSlots(fixture.gate.get(), 3));
		assert(fixture.store->PendingQuestErase().empty());
		const auto quests = fixture.store->GetQuests();
		assert(!quests.at("profiles").contains(oldQuest) && quests.at("profiles").contains(keptQuest));
		assert(!quests.at("tutorialExit").contains(oldQuest) && quests.at("tutorialExit").contains(keptQuest));
		assert(PendingAfterReopen(fixture).empty());
		std::filesystem::remove_all(fixture.directory);
	}

	void TestSyncOffFailedEraseKeepsMarkersThenRetries()
	{
		auto fixture = MakeFixture(3, "slots-sync-off-failed-erase");
		const std::string oldQuest = "quest-0123456789abcdef0123456789abcdef";
		assert(fixture.store->SaveQuests(Json{ { "profiles", Json{ { oldQuest, Json::array() } } } }));
		ReopenWithEditedDocument(fixture, [&](Json& a_document) { a_document["pendingQuestErase"] = Json::array({ oldQuest }); });
		const auto backup = std::filesystem::path{ (fixture.directory / "durable-state.json").string() + ".bak" };
		std::filesystem::remove(backup);
		std::filesystem::create_directories(backup / "occupied");
		assert(fixture.host->EnableCharacterSlots(fixture.gate.get(), 3));
		assert(Contains(fixture.store->PendingQuestErase(), oldQuest));
		assert(fixture.store->GetQuests().at("profiles").contains(oldQuest));
		std::filesystem::remove_all(backup);
		(void)HelloRaw(*fixture.host, 10, kAccountA, true, 1.0, 100.0);
		(void)Send(*fixture.host, 10, Json{ { "type", "characterList" } }, 2.0, 101.0);
		assert(fixture.store->PendingQuestErase().empty());
		assert(!fixture.store->GetQuests().at("profiles").contains(oldQuest));
		std::filesystem::remove_all(fixture.directory);
	}

	void TestRejectedDeferredQuestSaveStaysPending()
	{
		auto fixture = MakeFixture(3, "slots-rejected-quest-save");
		fixture.store->SetDeferCommits(true);
		const Json bad{ { "text", std::string{ 1, '\xff' } } };
		fixture.gate->QueueQuests(bad);
		for (int attempt = 0; attempt < 2; ++attempt) {
			bool threw = false;
			try {
				(void)fixture.gate->FlushQuests(true);
			} catch (const std::exception&) {
				threw = true;
			}
			assert(threw);
		}
		fixture.gate->QueueQuests(Json{ { "stages", 1 } });
		assert(fixture.gate->FlushQuests(true));
		fixture.store->WaitForCommits();
		assert(fixture.store->GetQuests().at("stages") == 1);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestRejectedDirectQuestSavePreservesQueuedUpdate()
	{
		auto fixture = MakeFixture(3, "slots-direct-quest-save");
		fixture.store->SetDeferCommits(true);
		fixture.gate->QueueQuests(Json{ { "stages", 7 } });
		assert(!fixture.gate->SaveQuests(Json{ { "text", std::string{ 1, '\xff' } } }));
		assert(fixture.gate->FlushQuests(true));
		fixture.store->WaitForCommits();
		assert(fixture.store->GetQuests().at("stages") == 7);
		fixture.gate->QueueQuests(Json{ { "stages", 8 } });
		assert(fixture.gate->SaveQuests(Json{ { "stages", 9 } }));
		assert(!fixture.gate->FlushQuests(true));
		fixture.store->WaitForCommits();
		assert(fixture.store->GetQuests().at("stages") == 9);
		std::filesystem::remove_all(fixture.directory);
	}

	void TestFullCleanupQueueKeepsSlotAndStateOnDelete()
	{
		auto fixture = MakeFixture(3, "slots-full-queue-delete");
		(void)HelloRaw(*fixture.host, 10, kAccountA, true, 1.0, 100.0);
		AssertResult(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "First" } }, 2.0, 101.0), 10, "create", true);
		ReopenWithEditedDocument(fixture, [](Json& a_document) {
			a_document["pendingQuestErase"] = Json::array();
			for (int i = 0; i < 4096; ++i) {
				a_document["pendingQuestErase"].push_back("quest-queued-" + std::to_string(i));
			}
		});
		assert(fixture.host->EnableCharacterSlots(fixture.gate.get(), 3));
		assert(fixture.store->PendingQuestErase().empty());
		(void)HelloRaw(*fixture.host, 10, kAccountA, true, 1.0, 200.0);
		const auto challenge = RequirePacket(Send(*fixture.host, 10, SlotPacket("characterDelete", 1), 3.0, 202.0), 10, "characterDeleteChallenge");
		AssertResult(
			Send(*fixture.host, 10, Json{ { "type", "characterDelete" }, { "slot", 1 }, { "confirmToken", challenge.at("confirmToken").get<std::string>() } }, 3.1, 202.0),
			10, "delete", true);
		assert(!fixture.store->GetCharacterSlotRecords(kAccountA).contains(1));
		assert(fixture.store->PendingQuestErase().empty());
		std::filesystem::remove_all(fixture.directory);
	}

	void TestFullCleanupQueueRefusesNewIdInsteadOfDropping()
	{
		Json root{ { "pendingQuestErase", Json::array() } };
		for (int i = 0; i < 4096; ++i) {
			root["pendingQuestErase"].push_back("quest-" + std::to_string(i));
		}
		const Json fresh{ { "questProfile", "quest-fresh" } };
		assert(!CommonwealthOnline::Server::QueuePendingQuestErase(root, fresh, Json::object()));
		assert(root.at("pendingQuestErase").size() == 4096);
		const Json queued{ { "questProfile", "quest-7" } };
		assert(CommonwealthOnline::Server::QueuePendingQuestErase(root, queued, Json::object()));
		assert(root.at("pendingQuestErase").size() == 4096);
	}
}

int main()
{
	TestNewCharacterQuestProgressIsIsolated();
	TestDuplicateQuestIdentityAcrossAccountsIsRejected();
	TestLegacyCharactersStartSeparateQuestHistories();
	TestEmptyLegacyQuestIdentitySurvivesReconnect();
	TestNewSlotPreservesPendingLegacyQuestHistory();
	TestReplacedCharacterQuestProgressIsRetired();
	TestConfirmedDeleteQueuesSlotQuestProfileOnce();
	TestDeleteWithoutQuestProfileQueuesNothing();
	TestDeleteQueuesOnlyDeletedSlotQuestProfile();
	TestActiveSoftDeletedRecordSurvivesReconcile();
	TestOrphanRecordAgesOutOutsideActiveSlot();
	TestOrphanQuestIdentityIsDurableBeforeCleanup();
	TestSyncOffDeleteErasesStoredQuestDataThenEnableStaysClean();
	TestSyncOffRestartDrainsMarkersOfEarlierRun();
	TestSyncOffFailedEraseKeepsMarkersThenRetries();
	TestFullCleanupQueueRefusesNewIdInsteadOfDropping();
	TestRejectedDeferredQuestSaveStaysPending();
	TestRejectedDirectQuestSavePreservesQueuedUpdate();
	TestFullCleanupQueueKeepsSlotAndStateOnDelete();
	return 0;
}

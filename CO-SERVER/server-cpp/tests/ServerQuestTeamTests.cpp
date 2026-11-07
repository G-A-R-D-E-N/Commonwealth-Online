#include "ServerQuestTestSupport.h"

#include "F4TQuestProtocol.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace QuestTestSupport;

namespace
{
	struct Client
	{
		std::uint32_t connectionId{};
		std::uint32_t playerId{};
	};

	constexpr const char* kStoryChain[] = {
		kTutorialQuest,
		"0229E5:fallout4.esm",
		"01F25E:fallout4.esm",
		"0229E6:fallout4.esm",
		"0229E7:fallout4.esm",
		"0229E8:fallout4.esm",
		"0229E9:fallout4.esm"
	};
	constexpr const char* kChainTarget = "0229EA:fallout4.esm";

	void ClearTutorial(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		Send(a_host, a_connectionId, Report(kTutorialQuest, kTutorialExitStage), a_monotonicSeconds, a_unixTimeSeconds);
	}

	void MarkReady(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		Send(a_host, a_connectionId, Json{ { "type", "questReady" } }, a_monotonicSeconds, a_unixTimeSeconds);
	}

	Client Connect(
		ProtocolHost& a_host,
		std::uint32_t a_connectionId,
		const std::string& a_profileId,
		bool a_questSync,
		bool a_readyCapable,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		a_host.BindSessionProfile(a_connectionId, a_profileId);
		Client client;
		client.connectionId = a_connectionId;
		client.playerId = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		Json capabilities = Json::array({ std::string{ F4T::QuestProtocol::kFireteamCapability } });
		if (a_questSync) capabilities.push_back(std::string{ F4T::QuestProtocol::kSyncCapability });
		if (a_readyCapable) capabilities.push_back(std::string{ F4T::QuestProtocol::kReadyCapability });
		const auto hello = Send(a_host, a_connectionId, Json{
			{ "type", "hello" },
			{ "protocolVersion", 2 },
			{ "capabilities", capabilities } }, a_monotonicSeconds, a_unixTimeSeconds + 0.1);
		assert(HasPacket(hello, a_connectionId, "sessionReady"));
		return client;
	}

	Client JoinTeamMember(
		ProtocolHost& a_host,
		std::uint32_t a_connectionId,
		const std::string& a_profileId,
		bool a_readyCapable,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		const auto client = Connect(a_host, a_connectionId, a_profileId, true, a_readyCapable, a_monotonicSeconds, a_unixTimeSeconds);
		ClearTutorial(a_host, a_connectionId, a_monotonicSeconds + 0.5, a_unixTimeSeconds + 0.5);
		return client;
	}

	std::vector<OutboundFrame> FormTeam(ProtocolHost& a_host, const Client& a_leader, const Client& a_member, double a_monotonicSeconds)
	{
		const auto invite = Send(a_host, a_leader.connectionId, Json{
			{ "type", std::string{ F4T::QuestProtocol::kFireteamInviteType } },
			{ "targetPlayerId", a_member.playerId } }, a_monotonicSeconds, a_monotonicSeconds + 1000.0);
		const auto inviteId = RequirePacket(invite, a_member.connectionId, F4T::QuestProtocol::kFireteamInviteType).at("inviteId").get<std::string>();
		const auto accepted = Send(a_host, a_member.connectionId, Json{
			{ "type", std::string{ F4T::QuestProtocol::kFireteamRespondType } },
			{ "inviteId", inviteId },
			{ "accept", true } }, a_monotonicSeconds + 0.1, a_monotonicSeconds + 1000.1);
		assert(HasPacket(accepted, a_leader.connectionId, F4T::QuestProtocol::kFireteamSnapshotType));
		return accepted;
	}

	int StageOf(ProtocolHost& a_host, const std::string& a_profileId, const char* a_quest)
	{
		const auto profiles = a_host.QuestStatus().value("profiles", Json::object());
		if (!profiles.contains(a_profileId)) return -1;
		const auto quest = F4T::QuestProtocol::FormatFormKey(Key(a_quest));
		for (const auto& entry : profiles.at(a_profileId)) {
			if (entry.value("quest", std::string{}) == quest) return entry.at("stage").get<int>();
		}
		return -1;
	}

	bool CompletedOf(ProtocolHost& a_host, const std::string& a_profileId, const char* a_quest)
	{
		const auto profiles = a_host.QuestStatus().value("profiles", Json::object());
		if (!profiles.contains(a_profileId)) return false;
		const auto quest = F4T::QuestProtocol::FormatFormKey(Key(a_quest));
		for (const auto& entry : profiles.at(a_profileId)) {
			if (entry.value("quest", std::string{}) == quest) return entry.value("completed", false);
		}
		return false;
	}

	void TestLeaderAheadAdoptsLateGuest()
	{
		ProtocolHost host{ "Quest Team Leader Ahead" };
		assert(EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
		const auto leader = JoinTeamMember(host, 1, "p-leader", false, 1.0, 1001.0);
		Send(host, leader.connectionId, Report(kQuest, 3), 10.0, 1010.0);
		const auto guest = JoinTeamMember(host, 2, "p-guest", false, 20.0, 1020.0);
		assert(StageOf(host, "p-guest", kQuest) == -1);
		const auto accepted = FormTeam(host, leader, guest, 30.0);
		assert(HasPacket(accepted, guest.connectionId, "questStage"));
		assert(StageOf(host, "p-guest", kQuest) == 3);
		assert(host.Stats().questTeamResynced == 1);
	}

	void TestSlotTeamAdoptionStaysWithSelectedCharacter()
	{
		const auto directory = std::filesystem::temp_directory_path() / ("co-quest-slot-team-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(directory);
		{
			DurableStateFileStore store(directory / "durable-state.json", [] { return 1000.0; }, [] { return std::string{ "0123456789abcdef0123456789abcdef" }; });
			PersistenceGate gate(store);
			ProtocolHost host{ "Quest Team Slot Isolation" };
			assert(host.EnableCharacterSlots(&gate, 3));
			assert(store.UpdatePlayerState("p-leader#0", Json{ { "characterName", "Leader" } }));
			assert(store.UpdatePlayerState("p-member#0", Json{ { "characterName", "Member" } }));
			assert(EnableStoryFixture(host, &gate, Mode::CoopStory, {}));
			const auto leader = JoinTeamMember(host, 1, "p-leader", false, 1.0, 1001.0);
			const auto member = JoinTeamMember(host, 2, "p-member", false, 2.0, 1002.0);
			const auto memberQuest = store.GetCharacterSlotRecords("p-member").at(0).at("questProfile").get<std::string>();
			const auto created = Send(host, 2, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "Alternate" } }, 3.0, 1003.0);
			assert(RequirePacket(created, 2, "characterResult").at("ok") == true);
			const auto alternateQuest = store.GetCharacterSlotRecords("p-member").at(1).at("questProfile").get<std::string>();
			Send(host, leader.connectionId, Report(kQuest, 3), 10.0, 1010.0);
			FormTeam(host, leader, member, 20.0);
			assert(StageOf(host, memberQuest, kQuest) == 3);
			assert(StageOf(host, alternateQuest, kQuest) == -1);
			const auto selected = Send(host, 2, Json{ { "type", "characterSelect" }, { "slot", 1 } }, 21.0, 1021.0);
			assert(RequirePacket(selected, 2, "characterResult").at("ok") == true);
			host.HandleDisconnected(2, 1021.5);
			host.HandleConnected(3, 1022.0);
			host.BindSessionProfile(3, "p-member");
			const auto hello = Send(host, 3, Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "slotProtocol", 1 }, { "capabilities", Json::array({ std::string{ F4T::QuestProtocol::kSyncCapability }, std::string{ F4T::QuestProtocol::kFireteamCapability } }) } }, 22.0, 1022.1);
			assert(HasPacket(hello, 3, "sessionReady"));
			assert(StageOf(host, memberQuest, kQuest) == 3);
			assert(StageOf(host, alternateQuest, kQuest) == -1);
		}
		std::filesystem::remove_all(directory);
	}

	void TestSharedBroadcastPreservesSlotProgress()
	{
		const auto directory = std::filesystem::temp_directory_path() / ("co-quest-slot-broadcast-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(directory);
		{
			DurableStateFileStore store(directory / "durable-state.json", [] { return 1000.0; }, [] { return std::string{ "0123456789abcdef0123456789abcdef" }; });
			PersistenceGate gate(store);
			ProtocolHost host{ "Quest Slot Broadcast" };
			assert(host.EnableCharacterSlots(&gate, 3));
			assert(store.UpdatePlayerState("p-leader#0", Json{ { "characterName", "Leader" } }));
			assert(store.UpdatePlayerState("p-member#0", Json{ { "characterName", "Member" } }));
			assert(EnableStoryFixture(host, &gate, Mode::CoopStory, {}));
			const auto leader = JoinTeamMember(host, 1, "p-leader", false, 1.0, 1001.0);
			const auto member = JoinTeamMember(host, 2, "p-member", false, 2.0, 1002.0);
			const auto memberQuest = store.GetCharacterSlotRecords("p-member").at(0).at("questProfile").get<std::string>();
			(void)host.SetQuestSolo("p-member", Key(kQuest), true, 1003.0);
			(void)Send(host, member.connectionId, Report(kQuest, 10), 4.0, 1004.0);
			assert(StageOf(host, memberQuest, kQuest) == 10);
			(void)host.SetQuestSolo("p-member", Key(kQuest), false, 1005.0);
			const auto lower = Send(host, leader.connectionId, Report(kQuest, 5), 6.0, 1006.0);
			assert(!HasPacket(lower, member.connectionId, "questStage"));
			const auto higher = Send(host, leader.connectionId, Report(kQuest, 11), 7.0, 1007.0);
			assert(HasPacket(higher, member.connectionId, "questStage"));
			assert(StageOf(host, memberQuest, kQuest) == 11);
			store.WaitForCommits();
			host.HandleDisconnected(member.connectionId, 1008.0);
			(void)host.HandleConnected(3, 1009.0);
			host.BindSessionProfile(3, "p-member");
			const auto replay = Send(host, 3, Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ std::string{ F4T::QuestProtocol::kSyncCapability } }) } }, 9.0, 1009.1);
			bool found = false;
			for (const auto& frame : replay) {
				if (frame.connectionId != 3) continue;
				const auto packet = DecodePacket(frame);
				if (packet.value("type", std::string{}) != "questSync") continue;
				for (const auto& stage : packet.at("stages")) {
					if (stage.at("quest") == kQuest && stage.at("stage") == 11) found = true;
				}
			}
			assert(found);
		}
		std::filesystem::remove_all(directory);
	}

	void TestGuestAheadAdoptsLeader()
	{
		ProtocolHost host{ "Quest Team Guest Ahead" };
		assert(EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
		const auto leader = JoinTeamMember(host, 1, "p-leader", false, 1.0, 1001.0);
		const auto guest = JoinTeamMember(host, 2, "p-guest", false, 2.0, 1002.0);
		Send(host, guest.connectionId, Report(kQuest, 5), 10.0, 1010.0);
		assert(StageOf(host, "p-leader", kQuest) == -1);
		const auto accepted = FormTeam(host, leader, guest, 20.0);
		assert(HasPacket(accepted, leader.connectionId, "questStage"));
		assert(StageOf(host, "p-leader", kQuest) == 5);
		assert(StageOf(host, "p-guest", kQuest) == 5);
		assert(host.Stats().questTeamResynced == 1);
	}

	void TestAdoptionCompletesPrerequisitesTransitively()
	{
		ProtocolHost host{ "Quest Team Chain" };
		assert(EnableDefaultStory(host, nullptr, Mode::CoopStory, {}));
		const auto leader = JoinTeamMember(host, 1, "p-leader", false, 1.0, 1001.0);
		double at = 10.0;
		for (const char* quest : kStoryChain) {
			assert(!HasPacket(Send(host, leader.connectionId, Report(quest, 2, true), at, at + 1000.0), leader.connectionId, "questNotReachable"));
			assert(CompletedOf(host, "p-leader", quest));
			at += 1.0;
		}
		Send(host, leader.connectionId, Report(kChainTarget, 2), at, at + 1000.0);
		assert(StageOf(host, "p-leader", kChainTarget) == 2);

		const auto guest = JoinTeamMember(host, 2, "p-guest", false, 40.0, 1040.0);
		assert(StageOf(host, "p-guest", kChainTarget) == -1);
		FormTeam(host, leader, guest, 50.0);
		for (const char* quest : kStoryChain) {
			assert(CompletedOf(host, "p-guest", quest));
		}
		assert(StageOf(host, "p-guest", kChainTarget) == 2);
		assert(!CompletedOf(host, "p-guest", kChainTarget));
	}

	void TestAheadMemberIsUntouchedAndSoloPlayerIsIgnored()
	{
		ProtocolHost host{ "Quest Team Never Lowered" };
		assert(EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
		const auto leader = JoinTeamMember(host, 1, "p-leader", false, 1.0, 1001.0);
		Send(host, leader.connectionId, Report(kQuest, 3), 10.0, 1010.0);
		const auto member = JoinTeamMember(host, 2, "p-member", false, 2.0, 1002.0);
		Send(host, member.connectionId, Report(kQuest, 5), 11.0, 1011.0);
		const auto accepted = FormTeam(host, leader, member, 20.0);
		assert(HasPacket(accepted, leader.connectionId, "questStage"));
		assert(!HasPacket(accepted, member.connectionId, "questStage"));
		assert(StageOf(host, "p-leader", kQuest) == 5);
		assert(StageOf(host, "p-member", kQuest) == 5);
		assert(host.PollTeamQuestSync(30.0, 1030.0).empty());
		assert(StageOf(host, "p-member", kQuest) == 5);
		assert(host.Stats().questTeamResynced == 1);

		const auto solo = JoinTeamMember(host, 3, "p-solo", false, 40.0, 1040.0);
		Send(host, solo.connectionId, Report(kOtherQuest, 3), 41.0, 1041.0);
		assert(host.PollTeamQuestSync(42.0, 1042.0).empty());
		assert(StageOf(host, "p-solo", kOtherQuest) == 3);
		assert(StageOf(host, "p-leader", kOtherQuest) == -1);
		assert(host.Stats().questTeamResynced == 1);
	}

	void TestFreeRoamAndInstancedStoryDoNotAdopt()
	{
		for (const auto mode : { Mode::FreeRoam, Mode::InstancedStory }) {
			ProtocolHost host{ "Quest Team Modes" };
			assert(EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
			const auto leader = JoinTeamMember(host, 1, "p-leader", false, 1.0, 1001.0);
			Send(host, leader.connectionId, Report(kQuest, 3), 10.0, 1010.0);
			const auto guest = JoinTeamMember(host, 2, "p-guest", false, 2.0, 1002.0);
			(void)host.SetQuestMode(mode, 15.0);
			FormTeam(host, leader, guest, 20.0);
			assert(StageOf(host, "p-guest", kQuest) == -1);
			assert(host.PollTeamQuestSync(30.0, 1030.0).empty());
			assert(StageOf(host, "p-guest", kQuest) == -1);
			(void)host.SetQuestMode(Mode::CoopStory, 31.0);
			assert(HasPacket(host.PollTeamQuestSync(32.0, 1032.0), guest.connectionId, "questStage"));
			assert(StageOf(host, "p-guest", kQuest) == 3);
		}
	}

	void TestSoloAndBlockedOverridesAreNotAdopted()
	{
		{
			ProtocolHost host{ "Quest Team Solo Override" };
			assert(EnableStoryFixture(host, nullptr, Mode::CoopStory, { OverrideEntry{ Key(kQuest), Override::Solo } }));
			const auto leader = JoinTeamMember(host, 1, "p-leader", false, 1.0, 1001.0);
			const auto member = JoinTeamMember(host, 2, "p-member", false, 2.0, 1002.0);
			Send(host, leader.connectionId, Report(kQuest, 3), 10.0, 1010.0);
			assert(StageOf(host, "p-leader", kQuest) == 3);
			FormTeam(host, leader, member, 20.0);
			assert(StageOf(host, "p-member", kQuest) == -1);
			Send(host, leader.connectionId, Report(kOtherQuest, 3), 30.0, 1030.0);
			(void)host.PollTeamQuestSync(31.0, 1031.0);
			assert(StageOf(host, "p-member", kOtherQuest) == 3);
			assert(host.Stats().questTeamResynced == 1);
		}

		const auto directory = TempDirectory("team-blocked");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Quest Team Blocked Seed" };
			assert(EnableStoryFixture(host, &gate, Mode::CoopStory, {}));
			const auto leader = JoinTeamMember(host, 1, "p-leader", false, 1.0, 1001.0);
			Send(host, leader.connectionId, Report(kQuest, 3), 10.0, 1010.0);
			assert(StageOf(host, "p-leader", kQuest) == 3);
			assert(gate.FlushQuests(true));
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Quest Team Blocked Restart" };
			assert(EnableStoryFixture(host, &gate, Mode::CoopStory, { OverrideEntry{ Key(kQuest), Override::Blocked } }));
			const auto leader = JoinTeamMember(host, 1, "p-leader", false, 2.0, 1002.0);
			const auto member = JoinTeamMember(host, 2, "p-member", false, 3.0, 1003.0);
			assert(StageOf(host, "p-leader", kQuest) == 3);
			FormTeam(host, leader, member, 20.0);
			assert(StageOf(host, "p-member", kQuest) == -1);
			assert(host.Stats().questTeamResynced == 0);
		}
		std::filesystem::remove_all(directory);
	}

	void TestIneligibleTeammatesNeitherAdoptNorLead()
	{
		ProtocolHost host{ "Quest Team Ineligible" };
		assert(EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
		const auto leader = JoinTeamMember(host, 1, "p-leader", false, 1.0, 1001.0);
		Send(host, leader.connectionId, Report(kQuest, 3), 10.0, 1010.0);

		const auto rookie = Connect(host, 2, "p-rookie", true, false, 2.0, 1002.0);
		FormTeam(host, leader, rookie, 20.0);
		assert(StageOf(host, "p-rookie", kQuest) == -1);
		Send(host, rookie.connectionId, Report(kQuest, 9), 21.0, 1021.0);
		assert(host.PollTeamQuestSync(30.0, 1030.0).empty());
		assert(StageOf(host, "p-leader", kQuest) == 3);
		assert(StageOf(host, "p-rookie", kQuest) == 9);

		const auto legacy = Connect(host, 3, "p-legacy", false, false, 40.0, 1040.0);
		ClearTutorial(host, legacy.connectionId, 40.5, 1040.5);
		FormTeam(host, leader, legacy, 50.0);
		assert(StageOf(host, "p-legacy", kQuest) == -1);
		Send(host, legacy.connectionId, Report(kQuest, 7), 60.0, 1060.0);
		assert(host.PollTeamQuestSync(70.0, 1070.0).empty());
		assert(StageOf(host, "p-leader", kQuest) == 3);

		ClearTutorial(host, rookie.connectionId, 80.0, 1080.0);
		(void)host.PollTeamQuestSync(90.0, 1090.0);
		assert(StageOf(host, "p-leader", kQuest) == 9);
		assert(StageOf(host, "p-legacy", kQuest) != 9);
	}

	void TestPollResyncsDivergenceAndThrottlesToOncePerSecond()
	{
		ProtocolHost host{ "Quest Team Poll" };
		assert(EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
		const auto leader = JoinTeamMember(host, 1, "p-leader", false, 1.0, 1001.0);
		const auto first = JoinTeamMember(host, 2, "p-first", true, 2.0, 1002.0);
		const auto second = JoinTeamMember(host, 3, "p-second", true, 3.0, 1003.0);
		Send(host, leader.connectionId, Report(kQuest, 3), 10.0, 1010.0);
		FormTeam(host, leader, first, 20.0);
		FormTeam(host, leader, second, 21.0);
		assert(StageOf(host, "p-first", kQuest) == -1);
		assert(StageOf(host, "p-second", kQuest) == -1);

		MarkReady(host, first.connectionId, 22.0, 1022.0);
		const auto polled = host.PollTeamQuestSync(23.0, 1023.0);
		assert(HasPacket(polled, first.connectionId, "questStage"));
		assert(!HasPacket(polled, second.connectionId, "questStage"));
		assert(StageOf(host, "p-first", kQuest) == 3);
		assert(host.Stats().questTeamResynced == 1);

		MarkReady(host, second.connectionId, 23.2, 1023.2);
		assert(host.PollTeamQuestSync(23.5, 1023.5).empty());
		assert(StageOf(host, "p-second", kQuest) == -1);
		assert(host.Stats().questTeamResynced == 1);

		assert(HasPacket(host.PollTeamQuestSync(24.0, 1024.0), second.connectionId, "questStage"));
		assert(StageOf(host, "p-second", kQuest) == 3);
		assert(host.Stats().questTeamResynced == 2);
	}

	void TestFailedSaveRollsBackTheAdoptionAndEmitsNothing()
	{
		const auto directory = TempDirectory("team-save-failure");
		const auto path = directory / "durable-state.json";
		const auto backup = std::filesystem::path{ path.string() + ".bak" };
		{
			const auto clock = [] { return 1000.0; };
			const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Quest Team Save Failure" };
			assert(EnableStoryFixture(host, &gate, Mode::CoopStory, {}));
			const auto leader = JoinTeamMember(host, 1, "p-leader", false, 1.0, 1001.0);
			const auto member = JoinTeamMember(host, 2, "p-member", true, 2.0, 1002.0);
			FormTeam(host, leader, member, 20.0);
			Send(host, leader.connectionId, Report(kQuest, 3), 10.0, 1010.0);
			MarkReady(host, member.connectionId, 22.0, 1022.0);
			assert(gate.FlushQuests(true));
			store.WaitForCommits();

			std::filesystem::remove_all(backup);
			std::filesystem::create_directories(backup / "occupied");
			assert(host.PollTeamQuestSync(23.0, 1023.0).empty());
			assert(StageOf(host, "p-member", kQuest) == -1);
			assert(host.Stats().questTeamResynced == 0);

			std::filesystem::remove_all(backup);
			assert(HasPacket(host.PollTeamQuestSync(24.0, 1024.0), member.connectionId, "questStage"));
			assert(StageOf(host, "p-member", kQuest) == 3);
			assert(host.Stats().questTeamResynced == 1);
		}
		std::filesystem::remove_all(directory);
	}
}

int main()
{
	TestLeaderAheadAdoptsLateGuest();
	TestSlotTeamAdoptionStaysWithSelectedCharacter();
	TestSharedBroadcastPreservesSlotProgress();
	TestGuestAheadAdoptsLeader();
	TestAdoptionCompletesPrerequisitesTransitively();
	TestAheadMemberIsUntouchedAndSoloPlayerIsIgnored();
	TestFreeRoamAndInstancedStoryDoNotAdopt();
	TestSoloAndBlockedOverridesAreNotAdopted();
	TestIneligibleTeammatesNeitherAdoptNorLead();
	TestPollResyncsDivergenceAndThrottlesToOncePerSecond();
	TestFailedSaveRollsBackTheAdoptionAndEmitsNothing();
	return 0;
}

#include "ServerQuestTestSupport.h"

namespace
{
	using namespace QuestTestSupport;

	constexpr const char* kStoryIntro = "01CC2A:fallout4.esm";
	constexpr const char* kStoryNext = "0229E5:fallout4.esm";
	constexpr const char* kStoryFar = "0229EA:fallout4.esm";
	constexpr const char* kStoryFarMissing = "0229E9:fallout4.esm";
	constexpr const char* kStoryLocked = "06B4FD:fallout4.esm";
	constexpr const char* kStoryUnknown = "DEAD99:fallout4.esm";

	bool SyncHasQuest(const std::vector<OutboundFrame>& a_frames, const char* a_quest)
	{
		for (const auto& packet : FramesOfType(a_frames, "questSync")) {
			for (const auto& stage : packet.at("stages")) {
				if (stage.at("quest") == a_quest) return true;
			}
		for (const auto& completed : packet.at("completed")) {
				if (completed.is_string() && completed.get<std::string>() == a_quest) return true;
			}
		}
		return false;
	}

	void TestStoryGuardRefusesJumpAhead()
	{
		ProtocolHost host{ "Quest Story Jump" };
		assert(EnableDefaultStory(host, nullptr, Mode::CoopStory, {}));
		Join(host, 10, "p10", 100.0, 1000.0);
		assert(HasPacket(Send(host, 10, Report(kStoryIntro, 10), 101.0, 1001.0), 10, "questStage"));
		const auto refused = Send(host, 10, Report(kStoryFar, 10), 102.0, 1002.0);
		assert(!HasPacket(refused, 10, "questStage"));
		assert(FramesOfType(refused, "questNotReachable").size() == 1);
		const auto frame = RequirePacket(refused, 10, "questNotReachable");
		assert(frame.at("reason") == "prerequisite_missing");
		assert(frame.at("missing") == kStoryFarMissing);
		assert(host.Stats().questStoryRefused == 1);
		assert(host.QuestStatus().at("stages").size() == 1);
	}

	void TestStoryGuardAcceptsNextAfterPrerequisite()
	{
		ProtocolHost host{ "Quest Story Next" };
		assert(EnableDefaultStory(host, nullptr, Mode::CoopStory, {}));
		Join(host, 10, "p10", 100.0, 1000.0);
		assert(HasPacket(Send(host, 10, Report(kStoryIntro, 2, true), 101.0, 1001.0), 10, "questStage"));
		const auto accepted = Send(host, 10, Report(kStoryNext, 10), 102.0, 1002.0);
		assert(FramesOfType(accepted, "questStage").size() == 1);
		assert(host.Stats().questStoryRefused == 0);
		assert(host.QuestStatus().at("stages").size() == 2);
	}

	void TestStoryGuardForwardOnlyAndRegression()
	{
		ProtocolHost host{ "Quest Story Forward" };
		assert(EnableDefaultStory(host, nullptr, Mode::CoopStory, {}));
		Join(host, 10, "p10", 100.0, 1000.0);
		assert(HasPacket(Send(host, 10, Report(kStoryIntro, 2, true), 101.0, 1001.0), 10, "questStage"));
		assert(HasPacket(Send(host, 10, Report(kStoryNext, 10), 102.0, 1002.0), 10, "questStage"));
		assert(HasPacket(Send(host, 10, Report(kStoryNext, 20), 103.0, 1003.0), 10, "questStage"));
		const auto regression = Send(host, 10, Report(kStoryNext, 5), 104.0, 1004.0);
		assert(!HasPacket(regression, 10, "questStage"));
		assert(RequirePacket(regression, 10, "questNotReachable").at("reason") == "stage_regression");
		assert(host.Stats().questStoryRefused == 1);
		assert(host.QuestStatus().at("stages").size() == 2);
	}

	void TestStoryGuardUnknownAndNotStartable()
	{
		ProtocolHost host{ "Quest Story Unknown" };
		assert(EnableDefaultStory(host, nullptr, Mode::CoopStory, {}));
		Join(host, 10, "p10", 100.0, 1000.0);
		const auto unknown = Send(host, 10, Report(kStoryUnknown, 10), 101.0, 1001.0);
		assert(!HasPacket(unknown, 10, "questStage"));
		assert(RequirePacket(unknown, 10, "questNotReachable").at("reason") == "unknown_quest");
		const auto locked = Send(host, 10, Report(kStoryLocked, 10), 102.0, 1002.0);
		assert(!HasPacket(locked, 10, "questStage"));
		assert(RequirePacket(locked, 10, "questNotReachable").at("reason") == "not_startable");
		assert(host.Stats().questStoryRefused == 2);
		assert(host.QuestStatus().at("stages").empty());
	}

	void TestStoryGuardLateJoinerStaysBehindTheIntro()
	{
		ProtocolHost host{ "Quest Story Late" };
		assert(EnableDefaultStory(host, nullptr, Mode::CoopStory, {}));
		Join(host, 10, "p10", 100.0, 1000.0);
		assert(HasPacket(Send(host, 10, Report(kStoryIntro, 2, true), 101.0, 1001.0), 10, "questStage"));
		assert(HasPacket(Send(host, 10, Report(kStoryNext, 10), 102.0, 1002.0), 10, "questStage"));
		const auto late = Join(host, 20, "p20", 103.0, 1003.0);
		assert(SyncHasQuest(late.hello, kStoryIntro));
		assert(!SyncHasQuest(late.hello, kStoryNext));
		assert(!SyncHasQuest(late.hello, kStoryFar));
		const auto jump = Send(host, 20, Report(kStoryNext, 10), 104.0, 1004.0);
		assert(!HasPacket(jump, 20, "questStage"));
		assert(RequirePacket(jump, 20, "questNotReachable").at("reason") == "prerequisite_missing");
		assert(RequirePacket(jump, 20, "questNotReachable").at("missing") == kStoryIntro);
		assert(HasPacket(Send(host, 20, Report(kStoryIntro, 2, true), 105.0, 1005.0), 20, "questStage"));
		assert(HasPacket(Send(host, 20, Report(kStoryNext, 20, true), 106.0, 1006.0), 20, "questStage"));
		assert(HasPacket(Send(host, 20, Report("01F25E:fallout4.esm", 5), 107.0, 1007.0), 20, "questStage"));
	}

	void TestStoryGuardDoesNotBorrowAnotherProfile()
	{
		ProtocolHost host{ "Quest Story Per Profile" };
		assert(EnableDefaultStory(host, nullptr, Mode::CoopStory, {}));
		Join(host, 10, "p10", 100.0, 1000.0);
		Join(host, 20, "p20", 100.1, 1000.1);
		const auto intro = Send(host, 10, Report(kStoryIntro, 2, true), 101.0, 1001.0);
		assert(HasPacket(intro, 10, "questStage"));
		const auto next = Send(host, 10, Report(kStoryNext, 10), 102.0, 1002.0);
		assert(HasPacket(next, 10, "questStage"));
		assert(!HasPacket(next, 20, "questStage"));
		const auto borrowed = Send(host, 20, Report(kStoryNext, 10), 103.0, 1003.0);
		assert(!HasPacket(borrowed, 20, "questStage"));
		assert(RequirePacket(borrowed, 20, "questNotReachable").at("reason") == "prerequisite_missing");
		assert(host.Stats().questStoryRefused == 1);
	}

	void TestStoryGuardSoloProgressAdvancesTheChain()
	{
		ProtocolHost host{ "Quest Story Solo" };
		assert(EnableDefaultStory(host, nullptr, Mode::InstancedStory, {}));
		Join(host, 10, "p10", 100.0, 1000.0);
		const auto intro = Send(host, 10, Report(kStoryIntro, 2, true), 101.0, 1001.0);
		assert(!HasPacket(intro, 10, "questNotReachable"));
		assert(host.QuestStatus().at("stages").empty());
		const auto next = Send(host, 10, Report(kStoryNext, 10), 102.0, 1002.0);
		assert(!HasPacket(next, 10, "questNotReachable"));
		assert(host.Stats().questStoryRefused == 0);
	}

	void TestStoryGuardModesAndFreeRoam()
	{
		for (const auto mode : { Mode::CoopStory, Mode::FactionWar, Mode::InstancedStory }) {
			ProtocolHost host{ "Quest Story Modes" };
			assert(EnableDefaultStory(host, nullptr, mode, {}));
			Join(host, 10, "p10", 100.0, 1000.0);
			const auto refused = Send(host, 10, Report(kStoryFar, 10), 101.0, 1001.0);
			assert(!HasPacket(refused, 10, "questStage"));
			assert(RequirePacket(refused, 10, "questNotReachable").at("reason") == "prerequisite_missing");
		}
		ProtocolHost freeRoam{ "Quest Story FreeRoam" };
		assert(EnableDefaultStory(freeRoam, nullptr, Mode::FreeRoam, {}));
		Join(freeRoam, 10, "p10", 100.0, 1000.0);
		const auto refused = Send(freeRoam, 10, Report(kStoryIntro, 10), 101.0, 1001.0);
		assert(!HasPacket(refused, 10, "questStage"));
		assert(RequirePacket(refused, 10, "questNotReachable").at("reason") == "not_startable");
		assert(freeRoam.QuestStatus().at("stages").empty());
	}

	void TestStoryGuardBlockedAndGatedFirst()
	{
		ProtocolHost blocked{ "Quest Story Blocked" };
		assert(EnableDefaultStory(blocked, nullptr, Mode::CoopStory, { OverrideEntry{ Key(kStoryNext), Override::Blocked } }));
		Join(blocked, 10, "p10", 100.0, 1000.0);
		assert(HasPacket(Send(blocked, 10, Report(kStoryIntro, 2, true), 101.0, 1001.0), 10, "questStage"));
		const auto dropped = Send(blocked, 10, Report(kStoryNext, 10), 102.0, 1002.0);
		assert(!HasPacket(dropped, 10, "questStage"));
		assert(!HasPacket(dropped, 10, "questNotReachable"));
		assert(blocked.Stats().questReportsRejected == 1);
		assert(blocked.Stats().questStoryRefused == 0);

		ProtocolHost gated{ "Quest Story Gated" };
		assert(EnableDefaultStory(gated, nullptr, Mode::CoopStory, {}));
		Join(gated, 10, "p10", 100.0, 1000.0);
		assert(HasPacket(Send(gated, 10, Report(kStoryIntro, 2, true), 101.0, 1001.0), 10, "questStage"));
		assert(!gated.SetQuestGate(Key(kStoryNext), 10, false, 102.0).empty());
		const auto held = Send(gated, 10, Report(kStoryNext, 10), 103.0, 1003.0);
		assert(!HasPacket(held, 10, "questStage"));
		assert(!HasPacket(held, 10, "questNotReachable"));
		assert(gated.QuestStatus().at("held").size() == 1);

		ProtocolHost unreached{ "Quest Story Gated Unreached" };
		assert(EnableDefaultStory(unreached, nullptr, Mode::CoopStory, {}));
		Join(unreached, 10, "p10", 100.0, 1000.0);
		assert(!unreached.SetQuestGate(Key(kStoryFar), 10, false, 102.0).empty());
		const auto refused = Send(unreached, 10, Report(kStoryFar, 10), 103.0, 1003.0);
		assert(!HasPacket(refused, 10, "questStage"));
		assert(RequirePacket(refused, 10, "questNotReachable").at("reason") == "prerequisite_missing");
		assert(unreached.Stats().questStagesHeld == 0);
		assert(unreached.QuestStatus().at("held").empty());
	}

	void TestStoryGuardSoloProgressSurvivesRestart()
	{
		const auto directory = TempDirectory("solo-restart");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Quests Solo Restart" };
			assert(EnableDefaultStory(host, &gate, Mode::InstancedStory, {}));
			Join(host, 10, "p10", 100.0, 1000.0);
			const auto intro = Send(host, 10, Report(kStoryIntro, 2, true), 101.0, 1001.0);
			assert(!HasPacket(intro, 10, "questNotReachable"));
			assert(host.QuestStatus().at("stages").empty());
			assert(host.QuestStatus().at("profiles").at("p10").size() == 1);
			assert(gate.FlushQuests(true));
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Quests Solo Restart" };
			assert(EnableDefaultStory(host, &gate, Mode::InstancedStory, {}));
			const auto stored = gate.GetQuests();
			assert(stored.at("profiles").at("p10").at(0).at("completed") == true);
			Join(host, 10, "p10", 100.0, 2000.0);
			const auto next = Send(host, 10, Report(kStoryNext, 10), 102.0, 2002.0);
			assert(!HasPacket(next, 10, "questNotReachable"));
			assert(host.Stats().questStoryRefused == 0);
		}
		std::filesystem::remove_all(directory);
	}

	void TestStoryGuardLegacyClientOutOfOrderRefused()
	{
		ProtocolHost host{ "Quest Story Legacy" };
		assert(EnableDefaultStory(host, nullptr, Mode::CoopStory, {}));
		const auto welcome = host.HandleConnected(11, 2000.0);
		assert(!welcome.empty());
		host.BindSessionProfile(11, "p11");
		const Json hello{
			{ "type", "hello" },
			{ "protocolVersion", 2 },
			{ "capabilities", Json::array({ std::string{ F4T::QuestProtocol::kSyncCapability } }) }
		};
		assert(HasPacket(Send(host, 11, hello, 200.0, 2000.1), 11, "sessionReady"));
		const auto first = Send(host, 11, Report(kStoryFar, 10), 201.0, 2001.0);
		assert(!HasPacket(first, 11, "questStage"));
		assert(RequirePacket(first, 11, "questNotReachable").at("reason") == "prerequisite_missing");
		assert(FramesOfType(first, "questNotReady").size() == 1);
		assert(RequirePacket(first, 11, "questNotReady").at("reason") == "updateRequired");
		const auto second = Send(host, 11, Report(kStoryFar, 11), 202.0, 2002.0);
		assert(!HasPacket(second, 11, "questStage"));
		assert(FramesOfType(second, "questNotReady").empty());
		assert(FramesOfType(second, "questNotReachable").size() == 1);
		assert(host.QuestStatus().at("stages").empty());
	}

	void TestStoryGuardReconnectRederives()
	{
		ProtocolHost host{ "Quest Story Reconnect" };
		assert(EnableDefaultStory(host, nullptr, Mode::CoopStory, {}));
		Join(host, 10, "p10", 100.0, 1000.0);
		assert(HasPacket(Send(host, 10, Report(kStoryIntro, 2, true), 101.0, 1001.0), 10, "questStage"));
		assert(HasPacket(Send(host, 10, Report(kStoryNext, 10), 102.0, 1002.0), 10, "questStage"));
		host.HandleDisconnected(10, 103.0);
		const auto rejoined = Join(host, 30, "p10", 104.0, 1004.0);
		assert(SyncHasQuest(rejoined.hello, kStoryNext));
		assert(HasPacket(Send(host, 30, Report(kStoryNext, 20), 105.0, 1005.0), 30, "questStage"));
		assert(host.Stats().questReadyRejected == 0);
	}
}

int main()
{
	TestStoryGuardRefusesJumpAhead();
	TestStoryGuardAcceptsNextAfterPrerequisite();
	TestStoryGuardForwardOnlyAndRegression();
	TestStoryGuardUnknownAndNotStartable();
	TestStoryGuardLateJoinerStaysBehindTheIntro();
	TestStoryGuardDoesNotBorrowAnotherProfile();
	TestStoryGuardSoloProgressAdvancesTheChain();
	TestStoryGuardModesAndFreeRoam();
	TestStoryGuardBlockedAndGatedFirst();
	TestStoryGuardSoloProgressSurvivesRestart();
	TestStoryGuardLegacyClientOutOfOrderRefused();
	TestStoryGuardReconnectRederives();
	return 0;
}

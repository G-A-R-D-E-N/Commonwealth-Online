#include "ServerQuestTestSupport.h"

namespace
{
	using namespace QuestTestSupport;

	void TestEnablement()
	{
		ProtocolHost plain{ "Quests Off" };
		assert(!plain.QuestSyncEnabled());
		assert(plain.QuestMode() == Mode::CoopStory);
		Join(plain, 10, "p10", 100.0, 1000.0);
		assert(!HasPacket(Send(plain, 10, Report(kQuest, 10), 101.0, 1001.0), 10, "questStage"));
		assert(!HasPacket(Join(plain, 20, "p20", 102.0, 1002.0).hello, 20, "questSync"));
		assert(plain.Stats().packetsRejected > 0);
		assert(plain.QuestStatus().at("enabled") == false);

		ProtocolHost host{ "Quests" };
		assert(EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
		assert(host.QuestSyncEnabled());
		assert(host.QuestMode() == Mode::CoopStory);
		assert(!host.EnableQuestSync(nullptr, Mode::CoopStory, {}));
		Join(host, 10, "p10", 100.0, 1000.0);
		assert(!host.EnableQuestSync(nullptr, Mode::CoopStory, {}));
		assert(host.QuestStatus().at("enabled") == true);
		assert(host.QuestStatus().at("mode") == "coop-story");
	}

	void TestLiveRelayAndForwardOnly()
	{
		ProtocolHost host{ "Quests" };
		assert(EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
		const auto first = Join(host, 10, "p10", 100.0, 1000.0);
		Join(host, 20, "p20", 100.1, 1000.1);

		const auto applied = Send(host, 10, Report(kQuest, 30), 101.0, 1001.0);
		const auto relayed = FramesOfType(applied, "questStage");
		assert(relayed.size() == 2);
		assert(relayed.front().at("quest") == kQuest);
		assert(relayed.front().at("stage") == 30);
		assert(relayed.front().at("fromPlayerId") == first.playerId);
		assert(!relayed.front().contains("completed"));
		assert(HasPacket(applied, 10, "questStage"));
		assert(HasPacket(applied, 20, "questStage"));

		assert(!HasPacket(Send(host, 10, Report(kQuest, 30), 102.0, 1002.0), 20, "questStage"));
		assert(!HasPacket(Send(host, 10, Report(kQuest, 20), 103.0, 1003.0), 20, "questStage"));
		assert(HasPacket(Send(host, 10, Report(kQuest, 40), 104.0, 1004.0), 20, "questStage"));

		const auto completed = Send(host, 10, Report(kQuest, 40, true), 105.0, 1005.0);
		assert(FramesOfType(completed, "questStage").size() == 2);
		assert(FramesOfType(completed, "questStage").front().at("completed") == true);

		const auto other = Send(host, 20, Report(kOtherQuest, 5, true), 106.0, 1006.0);
		assert(FramesOfType(other, "questStage").front().at("fromPlayerId") != first.playerId);
		assert(FramesOfType(other, "questStage").front().at("completed") == true);

		assert(host.Stats().questStagesRelayed == 4);
		assert(host.Stats().questReportsRejected == 2);
		assert(host.Stats().questReportsReceived == 8);
		const auto status = host.QuestStatus();
		assert(status.at("stages").size() == 2);
		assert(status.at("completed").size() == 2);
		assert(status.at("held").empty());
	}

	void TestReplayOnJoinAndReconnect()
	{
		ProtocolHost host{ "Quests" };
		assert(EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
		const auto first = Join(host, 10, "p10", 100.0, 1000.0);
		assert(!HasPacket(first.hello, 10, "questSync"));
		assert(!HasPacket(first.hello, 10, "questPolicy"));
		assert(host.Stats().questSyncReplays == 0);
		Send(host, 10, Report(kQuest, 30), 101.0, 1001.0);
		Send(host, 10, Report(kOtherQuest, 5, true), 102.0, 1002.0);

		const auto late = Join(host, 20, "p20", 103.0, 1003.0);
		const auto sync = RequirePacket(late.hello, 20, "questSync");
		assert(sync.at("stages").size() == 2);
		assert(sync.at("stages").at(0).at("quest") == kQuest);
		assert(sync.at("stages").at(0).at("stage") == 30);
		assert(sync.at("stages").at(1).at("quest") == kOtherQuest);
		assert(sync.at("completed").size() == 1);
		assert(sync.at("completed").at(0) == kOtherQuest);
		const auto parsed = F4T::QuestProtocol::NormalizeSync(sync);
		assert(parsed.has_value());
		assert(parsed->stages.size() == 2);
		assert(parsed->completed.size() == 1);
		assert(HasPacket(late.hello, 20, "questPolicy"));
		assert(host.Stats().questSyncReplays == 1);

		host.HandleDisconnected(20, 1004.0);
		const auto reconnected = Join(host, 30, "p20", 105.0, 1005.0);
		assert(RequirePacket(reconnected.hello, 30, "questSync").at("stages").size() == 2);
		assert(host.Stats().questSyncReplays == 2);
	}

	void TestPolicyOnJoin()
	{
		ProtocolHost host{ "Quests" };
		assert(EnableStoryFixture(host, nullptr, Mode::FactionWar, { OverrideEntry{ Key(kQuest), Override::Solo } }));
		const auto joined = Join(host, 10, "p10", 100.0, 1000.0);
		const auto policy = RequirePacket(joined.hello, 10, "questPolicy");
		assert(policy.at("mode") == "faction-war");
		assert(policy.at("version") == 1);
		assert(policy.at("soloQuests").empty());
		assert(policy.at("gates").empty());
		const auto parsed = F4T::QuestProtocol::NormalizePolicy(policy);
		assert(parsed.has_value());
		assert(parsed->mode == Mode::FactionWar);
		assert(host.QuestStatus().at("overrides").size() == 1);
		assert(host.QuestStatus().at("overrides").at(0).at("policy") == "solo");
	}

	void TestSoloNotRelayed()
	{
		ProtocolHost host{ "Quests" };
		assert(EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
		Join(host, 10, "p10", 100.0, 1000.0);
		Join(host, 20, "p20", 100.1, 1000.1);

		const auto solo = host.SetQuestSolo("p10", Key(kQuest), true, 1002.0);
		assert(HasPacket(solo, 10, "questPolicy"));
		assert(!HasPacket(solo, 20, "questPolicy"));
		const auto policy = RequirePacket(solo, 10, "questPolicy");
		assert(policy.at("soloQuests").size() == 1);
		assert(policy.at("soloQuests").at(0) == kQuest);
		assert(policy.at("version") == 2);
		assert(host.IsQuestSolo("p10", Key(kQuest)));
		assert(!host.IsQuestSolo("p20", Key(kQuest)));

		const auto dropped = Send(host, 10, Report(kQuest, 30), 103.0, 1003.0);
		assert(!HasPacket(dropped, 10, "questStage"));
		assert(!HasPacket(dropped, 20, "questStage"));
		assert(host.Stats().questStagesRelayed == 0);
		assert(host.QuestStatus().at("stages").empty());

		assert(HasPacket(Send(host, 10, Report(kOtherQuest, 5), 104.0, 1004.0), 20, "questStage"));
		assert(!host.IsQuestSolo("p10", Key(kOtherQuest)));

		assert(HasPacket(host.SetQuestSolo("p10", Key(kQuest), false, 105.0), 10, "questPolicy"));
		assert(!host.IsQuestSolo("p10", Key(kQuest)));
		assert(HasPacket(Send(host, 10, Report(kQuest, 30), 106.0, 1006.0), 20, "questStage"));
		assert(host.SetQuestSolo("unknown", Key(kThirdQuest), true, 107.0).empty());
		assert(host.IsQuestSolo("unknown", Key(kThirdQuest)));
		assert(!HasPacket(host.SetQuestSolo("unknown", Key(kThirdQuest), false, 108.0), 20, "questPolicy"));
		assert(!host.IsQuestSolo("unknown", Key(kThirdQuest)));
	}

	void TestPlayerScopeRequest()
	{
		ProtocolHost host{ "Quests Scope" };
		assert(EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
		Join(host, 10, "p10", 100.0, 1000.0);
		Join(host, 20, "p20", 100.1, 1000.1);

		const auto solo = Send(host, 10, ScopeRequestPacket(kQuest, "solo"), 101.0, 1001.0);
		assert(HasPacket(solo, 10, "questPolicy"));
		assert(!HasPacket(solo, 20, "questPolicy"));
		const auto soloPolicy = RequirePacket(solo, 10, "questPolicy");
		assert(soloPolicy.at("soloQuests").size() == 1);
		assert(soloPolicy.at("soloQuests").at(0) == kQuest);
		assert(soloPolicy.at("version") == 2);
		assert(host.IsQuestSolo("p10", Key(kQuest)));
		assert(!host.IsQuestSolo("p20", Key(kQuest)));
		assert(host.Stats().questScopesReceived == 1);
		assert(host.Stats().questScopesRejected == 0);
		assert(host.Stats().questScopesApplied == 1);

		const auto privateReport = Send(host, 10, Report(kQuest, 30), 102.0, 1002.0);
		assert(!HasPacket(privateReport, 10, "questStage"));
		assert(!HasPacket(privateReport, 20, "questStage"));
		assert(host.Stats().questStagesRelayed == 0);

		const auto shared = Send(host, 10, ScopeRequestPacket(kQuest, "shared"), 103.0, 1003.0);
		assert(HasPacket(shared, 10, "questPolicy"));
		assert(RequirePacket(shared, 10, "questPolicy").at("soloQuests").empty());
		assert(!host.IsQuestSolo("p10", Key(kQuest)));
		assert(host.Stats().questScopesApplied == 2);
		assert(HasPacket(Send(host, 10, Report(kQuest, 30), 104.0, 1004.0), 20, "questStage"));

		assert(HasPacket(Send(host, 10, Json{ { "type", "questScope" } }, 105.0, 1005.0), 10, "questPolicy"));
		assert(HasPacket(Send(host, 10, Json{ { "type", "questScope" }, { "quest", "bogus" }, { "scope", "solo" } }, 106.0, 1006.0), 10, "questPolicy"));
		assert(HasPacket(Send(host, 10, Json{ { "type", "questScope" }, { "quest", kQuest }, { "scope", "held" } }, 107.0, 1007.0), 10, "questPolicy"));
		assert(HasPacket(Send(host, 10, Json{ { "type", "questScope" }, { "quest", kQuest }, { "scope", 1 } }, 108.0, 1008.0), 10, "questPolicy"));
		assert(host.Stats().questScopesRejected == 4);

		ProtocolHost pinned{ "Quests Scope Pinned" };
		assert(EnableStoryFixture(pinned, nullptr, Mode::CoopStory, { OverrideEntry{ Key(kQuest), Override::Solo } }));
		Join(pinned, 10, "p10", 100.0, 1000.0);
		assert(HasPacket(Send(pinned, 10, ScopeRequestPacket(kQuest, "shared"), 101.0, 1001.0), 10, "questPolicy"));
		assert(pinned.Stats().questScopesRejected == 1);
		assert(pinned.Stats().questScopesApplied == 0);
		assert(pinned.IsQuestSolo("p10", Key(kQuest)) == false);

		ProtocolHost plain{ "Quests Scope Capability" };
		assert(EnableStoryFixture(plain, nullptr, Mode::CoopStory, {}));
		JoinWithoutQuestSync(plain, 20, "p20", 100.0, 1000.0);
		const auto rejectedBefore = plain.Stats().packetsRejected;
		assert(Send(plain, 20, ScopeRequestPacket(kQuest, "solo"), 101.0, 1001.0).empty());
		assert(plain.Stats().packetsRejected == rejectedBefore);
		assert(plain.Stats().questScopesReceived == 0);
	}

	void TestQuestScopeReplyAndThrottle()
	{
		ProtocolHost duplicate{ "Quests Scope Duplicate" };
		assert(EnableStoryFixture(duplicate, nullptr, Mode::CoopStory, {}));
		Join(duplicate, 10, "p10", 100.0, 1000.0);
		Join(duplicate, 20, "p10", 100.0, 1000.1);
		const auto first = Send(duplicate, 10, ScopeRequestPacket(kQuest, "solo"), 101.0, 1001.0);
		assert(HasPacket(first, 10, "questPolicy"));
		assert(!HasPacket(first, 20, "questPolicy"));
		const auto second = Send(duplicate, 20, ScopeRequestPacket(kQuest, "shared"), 102.0, 1002.0);
		assert(HasPacket(second, 20, "questPolicy"));
		assert(!HasPacket(second, 10, "questPolicy"));
		assert(duplicate.Stats().questScopesApplied == 2);
		const auto repeated = Send(duplicate, 20, ScopeRequestPacket(kQuest, "shared"), 103.0, 1003.0);
		assert(HasPacket(repeated, 20, "questPolicy"));
		assert(!HasPacket(repeated, 10, "questPolicy"));
		assert(duplicate.Stats().questScopesApplied == 2);

		ProtocolHost limited{ "Quests Scope Throttle" };
		assert(EnableStoryFixture(limited, nullptr, Mode::CoopStory, {}));
		Join(limited, 30, "p30", 100.0, 1000.0);
		for (int index = 0; index < 8; ++index) {
			const auto scope = index % 2 == 0 ? "solo" : "shared";
			assert(HasPacket(Send(limited, 30, ScopeRequestPacket(kQuest, scope), 101.0, 1001.0), 30, "questPolicy"));
		}
		assert(Send(limited, 30, ScopeRequestPacket(kQuest, "solo"), 101.0, 1001.0).empty());
		assert(limited.Stats().questScopesApplied == 8);
		assert(limited.Stats().questScopesRejected == 1);
	}

	void TestGateHoldAndRelease()
	{
		ProtocolHost host{ "Quests" };
		assert(EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
		const auto first = Join(host, 10, "p10", 100.0, 1000.0);
		Join(host, 20, "p20", 100.1, 1000.1);

		const auto gated = host.SetQuestGate(Key(kQuest), 30, false, 1002.0);
		assert(HasPacket(gated, 10, "questPolicy"));
		const auto policy = RequirePacket(gated, 20, "questPolicy");
		assert(policy.at("gates").size() == 1);
		assert(policy.at("gates").at(0).at("quest") == kQuest);
		assert(policy.at("gates").at(0).at("stage") == 30);
		assert(policy.at("version") == 2);
		assert(host.IsQuestGated(Key(kQuest), 30));
		assert(host.SetQuestGate(Key(kQuest), 30, false, 1002.5).empty());
		assert(host.SetQuestGate(Key(kQuest), 31, true, 1002.6).empty());

		const auto held = Send(host, 10, Report(kQuest, 30), 103.0, 1003.0);
		assert(!HasPacket(held, 10, "questStage"));
		assert(!HasPacket(held, 20, "questStage"));
		assert(host.Stats().questStagesHeld == 1);
		assert(host.Stats().questStagesRelayed == 0);
		assert(host.QuestStatus().at("held").size() == 1);
		assert(host.QuestStatus().at("stages").empty());
		assert(host.QuestStatus().at("held").at(0).at("fromPlayerId") == first.playerId);

		const auto released = host.SetQuestGate(Key(kQuest), 30, true, 104.0);
		assert(HasPacket(released, 10, "questStage"));
		assert(HasPacket(released, 20, "questStage"));
		assert(HasPacket(released, 20, "questPolicy"));
		const auto stage = RequirePacket(released, 20, "questStage");
		assert(stage.at("quest") == kQuest);
		assert(stage.at("stage") == 30);
		assert(stage.at("fromPlayerId") == first.playerId);
		assert(RequirePacket(released, 20, "questPolicy").at("gates").empty());
		assert(!host.IsQuestGated(Key(kQuest), 30));
		assert(host.QuestStatus().at("held").empty());
		assert(host.QuestStatus().at("stages").size() == 1);
		assert(host.Stats().questStagesRelayed == 1);

		assert(HasPacket(Send(host, 10, Report(kQuest, 40), 105.0, 1005.0), 20, "questStage"));
		assert(host.QuestStatus().at("stages").at(0).at("stage") == 40);
	}

	void TestGateMatchesExactStage()
	{
		ProtocolHost host{ "Quests Gate Exact" };
		assert(EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
		Join(host, 10, "p10", 100.0, 1000.0);
		Join(host, 20, "p20", 100.1, 1000.1);

		assert(!host.SetQuestGate(Key(kQuest), 30, false, 1002.0).empty());
		assert(HasPacket(Send(host, 10, Report(kQuest, 29), 101.5, 1002.2), 20, "questStage"));
		assert(!HasPacket(Send(host, 10, Report(kQuest, 30), 102.0, 1002.5), 20, "questStage"));
		assert(host.Stats().questStagesHeld == 1);
		assert(host.QuestStatus().at("held").size() == 1);
		assert(host.QuestStatus().at("held").at(0).at("stage") == 30);

		assert(HasPacket(Send(host, 10, Report(kQuest, 31), 103.0, 1003.5), 20, "questStage"));
		assert(host.Stats().questStagesHeld == 1);
		assert(host.QuestStatus().at("stages").at(0).at("stage") == 31);
	}

	void TestRateLimitPrecedesParsing()
	{
		ProtocolHost reports{ "Quests Rate Limit Order" };
		assert(EnableStoryFixture(reports, nullptr, Mode::CoopStory, {}));
		Join(reports, 10, "p10", 100.0, 1000.0);
		Join(reports, 20, "p20", 100.1, 1000.1);
		for (int index = 0; index < 8; ++index) {
			assert(Send(reports, 10, Json{ { "type", "questReport" }, { "quest", "bogus" } }, 100.0, 1001.0).empty());
		}
		assert(reports.Stats().questReportsRejected == 8);
		assert(!HasPacket(Send(reports, 10, Report(kQuest, 10), 100.0, 1002.0), 20, "questStage"));
		assert(reports.Stats().questReportsRejected == 9);
		assert(reports.Stats().questStagesRelayed == 0);
		assert(reports.QuestStatus().at("stages").empty());

		ProtocolHost scopes{ "Quests Scope Rate Limit Order" };
		assert(EnableStoryFixture(scopes, nullptr, Mode::CoopStory, {}));
		Join(scopes, 10, "p10", 100.0, 1000.0);
		for (int index = 0; index < 8; ++index) {
			assert(HasPacket(Send(scopes, 10, Json{ { "type", "questScope" } }, 100.0, 1001.0), 10, "questPolicy"));
		}
		assert(scopes.Stats().questScopesRejected == 8);
		assert(Send(scopes, 10, ScopeRequestPacket(kQuest, "solo"), 100.0, 1002.0).empty());
		assert(scopes.Stats().questScopesRejected == 9);
		assert(!scopes.IsQuestSolo("p10", Key(kQuest)));
	}

	void TestRateLimit()
	{
		ProtocolHost host{ "Quests" };
		assert(EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
		Join(host, 10, "p10", 100.0, 1000.0);
		Join(host, 20, "p20", 100.1, 1000.1);
		for (std::uint16_t stage = 1; stage <= 8; ++stage) {
			assert(HasPacket(Send(host, 10, Report(kQuest, stage), 100.0, 1000.0 + stage), 20, "questStage"));
		}
		assert(host.Stats().questStagesRelayed == 8);
		const auto limited = Send(host, 10, Report(kQuest, 9), 100.0, 1009.0);
		assert(!HasPacket(limited, 10, "questStage"));
		assert(host.Stats().questReportsRejected == 1);
		assert(host.Stats().questStagesRelayed == 8);
		assert(host.QuestStatus().at("stages").at(0).at("stage") == 8);
		assert(HasPacket(Send(host, 10, Report(kQuest, 9), 101.0, 1010.0), 20, "questStage"));
	}

	void TestModeChangeStopsSharing()
	{
		ProtocolHost host{ "Quests" };
		assert(EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
		Join(host, 10, "p10", 100.0, 1000.0);
		Join(host, 20, "p20", 100.1, 1000.1);
		assert(HasPacket(Send(host, 10, Report(kQuest, 10), 101.0, 1001.0), 20, "questStage"));

		const auto changed = host.SetQuestMode(Mode::FreeRoam, 102.0);
		assert(HasPacket(changed, 10, "questPolicy"));
		assert(HasPacket(changed, 20, "questPolicy"));
		assert(RequirePacket(changed, 20, "questPolicy").at("mode") == "free-roam");
		assert(host.QuestMode() == Mode::FreeRoam);
		assert(host.SetQuestMode(Mode::FreeRoam, 103.0).empty());
		assert(!HasPacket(Send(host, 10, Report(kOtherQuest, 5), 104.0, 1004.0), 20, "questStage"));
		assert(host.Stats().questStagesRelayed == 1);

		assert(HasPacket(host.SetQuestMode(Mode::InstancedStory, 105.0), 20, "questPolicy"));
		assert(!HasPacket(Send(host, 10, Report(kThirdQuest, 5), 106.0, 1006.0), 20, "questStage"));
		assert(host.Stats().questStagesRelayed == 1);
		assert(host.QuestStatus().at("stages").size() == 1);
	}

	void TestOverrides()
	{
		ProtocolHost blocked{ "Quests" };
		assert(EnableStoryFixture(blocked, nullptr, Mode::CoopStory, { OverrideEntry{ Key(kQuest), Override::Blocked } }));
		Join(blocked, 10, "p10", 100.0, 1000.0);
		Join(blocked, 20, "p20", 100.1, 1000.1);
		assert(!HasPacket(Send(blocked, 10, Report(kQuest, 10), 101.0, 1001.0), 20, "questStage"));
		assert(blocked.QuestStatus().at("stages").empty());
		assert(HasPacket(Send(blocked, 10, Report(kOtherQuest, 10), 102.0, 1002.0), 20, "questStage"));

		ProtocolHost forced{ "Quests" };
		assert(EnableStoryFixture(forced, nullptr, Mode::InstancedStory, { OverrideEntry{ Key(kQuest), Override::Shared } }));
		Join(forced, 10, "p10", 100.0, 1000.0);
		Join(forced, 20, "p20", 100.1, 1000.1);
		assert(HasPacket(Send(forced, 10, Report(kQuest, 10), 101.0, 1001.0), 20, "questStage"));
		assert(!HasPacket(Send(forced, 10, Report(kOtherQuest, 10), 102.0, 1002.0), 20, "questStage"));
	}

	void TestMalformedReports()
	{
		ProtocolHost host{ "Quests" };
		assert(EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
		Join(host, 10, "p10", 100.0, 1000.0);
		assert(!HasPacket(Send(host, 10, Json{ { "type", "questReport" } }, 101.0, 1001.0), 10, "questStage"));
		assert(!HasPacket(Send(host, 10, Json{ { "type", "questReport" }, { "quest", "bogus" }, { "stage", 10 } }, 102.0, 1002.0), 10, "questStage"));
		assert(!HasPacket(Send(host, 10, Json{ { "type", "questReport" }, { "quest", kQuest }, { "stage", 70000 } }, 103.0, 1003.0), 10, "questStage"));
		assert(!HasPacket(Send(host, 10, Json{ { "type", "questReport" }, { "quest", kQuest }, { "stage", 10 }, { "completed", 1 } }, 104.0, 1004.0), 10, "questStage"));
		const auto firstReport = Send(host, 10, Json{ { "type", "questReport" }, { "quest", kQuest }, { "stage", 10 }, { "completed", true } }, 105.0, 1005.0);
		assert(HasPacket(firstReport, 10, "questStage"));
		assert(FramesOfType(firstReport, "questStage").front().at("completed") == true);
		assert(host.Stats().questReportsRejected == 4);
		assert(host.Stats().questStagesRelayed == 1);
		assert(host.QuestStatus().at("stages").at(0).at("stage") == 10);
	}

	void TestPersistenceRoundTrip()
	{
		const auto directory = TempDirectory("persist");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Quests Persist" };
			assert(EnableStoryFixture(host, &gate, Mode::CoopStory, {}));
			Join(host, 10, "p10", 100.0, 1000.0);
			Send(host, 10, Report(kQuest, 30, true), 101.0, 1001.0);
			Send(host, 10, Report(kOtherQuest, 5), 102.0, 1002.0);
			assert(gate.FlushQuests(true));
			assert(!host.SetQuestGate(Key(kThirdQuest), 12, false, 103.0).empty());
			assert(!host.SetQuestSolo("p10", Key(kQuest), true, 104.0).empty());
			assert(!host.SetQuestMode(Mode::FactionWar, 105.0).empty());
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Quests Persist" };
			assert(EnableStoryFixture(host, &gate, Mode::CoopStory, {}));
			assert(host.QuestMode() == Mode::FactionWar);
			assert(host.QuestStatus().at("mode") == "faction-war");
			const auto joined = Join(host, 20, "p20", 100.0, 2000.0);
			const auto sync = RequirePacket(joined.hello, 20, "questSync");
			assert(sync.at("stages").size() == 2);
			assert(sync.at("stages").at(0).at("quest") == kQuest);
			assert(sync.at("stages").at(0).at("stage") == 30);
			assert(sync.at("completed").size() == 1);
			assert(sync.at("completed").at(0) == kQuest);
			const auto policy = RequirePacket(joined.hello, 20, "questPolicy");
			assert(policy.at("mode") == "faction-war");
			assert(policy.at("gates").size() == 1);
			assert(policy.at("gates").at(0).at("quest") == kThirdQuest);
			assert(policy.at("gates").at(0).at("stage") == 12);
			assert(policy.at("version") == 4);
			assert(policy.at("soloQuests").empty());
			const auto returning = Join(host, 30, "p10", 100.1, 2000.1);
			assert(RequirePacket(returning.hello, 30, "questPolicy").at("soloQuests").size() == 1);
			const auto held = Send(host, 20, Report(kThirdQuest, 12), 102.0, 2002.0);
			assert(!HasPacket(held, 20, "questStage"));
			assert(host.Stats().questStagesHeld == 1);
			assert(host.QuestStatus().at("held").size() == 1);
			assert(host.QuestStatus().at("held").at(0).at("stage") == 12);
			const auto released = host.SetQuestGate(Key(kThirdQuest), 12, true, 2003.0);
			assert(HasPacket(released, 20, "questStage"));
			assert(HasPacket(released, 30, "questStage"));
			assert(host.QuestStatus().at("held").empty());
		}
		std::filesystem::remove_all(directory);
	}

	void TestCorruptStoredStateIsRecovered()
	{
		const auto directory = TempDirectory("corrupt");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Quests Corrupt" };
			assert(EnableStoryFixture(host, &gate, Mode::CoopStory, {}));
			Join(host, 10, "p10", 100.0, 1000.0);
			Send(host, 10, Report(kQuest, 30), 101.0, 1001.0);
			store.WaitForCommits();
			store.SaveQuests(Json{ { "stages", "not-an-array" } });
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			assert(gate.GetQuests().at("stages") == "not-an-array");
			ProtocolHost host{ "Quests Corrupt" };
			assert(EnableStoryFixture(host, &gate, Mode::CoopStory, {}));
			assert(host.QuestSyncEnabled());
			assert(host.QuestStatus().at("stages").empty());
			assert(host.QuestStatus().at("completed").empty());
			assert(gate.GetQuests().at("stages") == "not-an-array");
			const auto raw = Json::parse(ReadText(path));
			assert(raw.at("quests_backup").at("stages") == "not-an-array");

			const auto joined = Join(host, 20, "p20", 100.0, 2000.0);
			assert(!HasPacket(joined.hello, 20, "questSync"));
			assert(HasPacket(Send(host, 20, Report(kQuest, 30), 101.0, 2001.0), 20, "questStage"));
			assert(host.QuestStatus().at("stages").at(0).at("quest") == kQuest);
			assert(gate.FlushQuests(true));
			store.WaitForCommits();
			assert(gate.GetQuests().at("stages").size() == 1);
		}
		std::filesystem::remove_all(directory);
	}

	void TestJoinWithoutQuestCapability()
	{
		ProtocolHost host{ "Quests Capability" };
		assert(EnableStoryFixture(host, nullptr, Mode::FactionWar, {}));
		const auto capable = Join(host, 10, "p10", 100.0, 1000.0);
		assert(HasPacket(capable.hello, 10, "questPolicy"));
		assert(!HasPacket(capable.hello, 10, "questSync"));

		const auto plain = JoinWithoutQuestSync(host, 20, "p20", 100.1, 1000.1);
		assert(!HasPacket(plain.hello, 20, "questSync"));
		assert(!HasPacket(plain.hello, 20, "questPolicy"));

		const auto rejectedBefore = host.Stats().packetsRejected;
		const auto receivedBefore = host.Stats().questReportsReceived;
		const auto ignored = Send(host, 20, Report(kQuest, 30), 101.0, 1001.0);
		assert(ignored.empty());
		assert(host.Stats().packetsRejected == rejectedBefore);
		assert(host.Stats().questReportsReceived == receivedBefore);
		assert(host.QuestStatus().at("stages").empty());

		const auto applied = Send(host, 10, Report(kQuest, 30), 102.0, 1002.0);
		assert(HasPacket(applied, 10, "questStage"));
		assert(!HasPacket(applied, 20, "questStage"));

		assert(HasPacket(host.SetQuestMode(Mode::FreeRoam, 103.0), 10, "questPolicy"));
		assert(!HasPacket(host.SetQuestMode(Mode::InstancedStory, 104.0), 20, "questPolicy"));
		assert(HasPacket(host.SetQuestSolo("p10", Key(kQuest), true, 105.0), 10, "questPolicy"));
		assert(host.SetQuestSolo("p20", Key(kOtherQuest), true, 106.0).empty());

		host.HandleDisconnected(20, 107.0);
		assert(!HasPacket(JoinWithoutQuestSync(host, 30, "p30", 108.0, 1008.0).hello, 30, "questSync"));
		assert(HasPacket(Join(host, 40, "p40", 109.0, 1009.0).hello, 40, "questSync"));
	}

	void TestSyncChunking()
	{
		const auto directory = TempDirectory("chunk");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		const auto plugin = std::string(F4T::QuestProtocol::kMaximumPluginChars, 'x');
		Json stages = Json::array();
		Json completed = Json::array();
		for (std::size_t index = 1; index <= 512; ++index) {
			char digits[7] = {};
			std::snprintf(digits, sizeof(digits), "%06zX", index);
			stages.push_back(Json{ { "quest", std::string{ digits } + ":" + plugin }, { "stage", index % 65536 } });
			completed.push_back(std::string{ digits } + ":" + plugin);
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			store.SaveQuests(Json{ { "version", 3 }, { "stages", stages }, { "completed", completed } });
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Quests Chunk" };
			assert(EnableStoryFixture(host, &gate, Mode::CoopStory, {}));
			const auto joined = Join(host, 10, "p10", 100.0, 1000.0);
			const auto syncs = FramesOfType(joined.hello, "questSync");
			assert(syncs.size() > 1);
			std::size_t stagesSeen = 0;
			std::size_t completedSeen = 0;
			for (const auto& frame : joined.hello) {
				const auto packet = DecodePacket(frame);
				if (packet.value("type", std::string{}) != "questSync") continue;
				assert(frame.payload.size() <= F4T::QuestProtocol::kMaximumSyncFrameBytes);
				assert(frame.payload.size() < F4T::ReceiveFrame::DefaultMaximumFrameBytes);
				const auto parsed = F4T::QuestProtocol::NormalizeSync(packet);
				assert(parsed.has_value());
				stagesSeen += parsed->stages.size();
				completedSeen += parsed->completed.size();
			}
			assert(stagesSeen == 512);
			assert(completedSeen == 512);
			assert(HasPacket(joined.hello, 10, "questPolicy"));
			assert(host.Stats().questSyncReplays == 1);
		}
		std::filesystem::remove_all(directory);
	}

	void TestCaseInsensitiveQuestKeys()
	{
		ProtocolHost host{ "Quests Case" };
		assert(EnableStoryFixture(host, nullptr, Mode::CoopStory, { OverrideEntry{ Key(kMixedCaseQuest), Override::Blocked } }));
		Join(host, 10, "p10", 100.0, 1000.0);
		Join(host, 20, "p20", 100.1, 1000.1);

		assert(!HasPacket(Send(host, 10, Report(kQuest, 10), 101.0, 1001.0), 20, "questStage"));
		assert(host.QuestStatus().at("stages").empty());
		const auto applied = Send(host, 10, Report(kMixedCaseOther, 5), 102.0, 1002.0);
		assert(HasPacket(applied, 20, "questStage"));
		assert(RequirePacket(applied, 20, "questStage").at("quest") == kThirdQuest);
		assert(host.QuestStatus().at("stages").at(0).at("quest") == kThirdQuest);
		assert(!HasPacket(Send(host, 10, Report(kThirdQuest, 5), 103.0, 1003.0), 20, "questStage"));
		assert(HasPacket(Send(host, 10, Report(kThirdQuest, 6), 104.0, 1004.0), 20, "questStage"));
		assert(host.QuestStatus().at("stages").size() == 1);

		assert(!host.SetQuestGate(Key(kMixedCaseOther), 9, false, 105.0).empty());
		assert(!HasPacket(Send(host, 10, Report(kThirdQuest, 9), 106.0, 1006.0), 20, "questStage"));
		assert(host.QuestStatus().at("held").size() == 1);
		assert(HasPacket(host.SetQuestGate(Key(kThirdQuest), 9, true, 107.0), 20, "questStage"));
		assert(host.QuestStatus().at("held").empty());

		assert(!host.SetQuestSolo("p10", Key(kMixedCaseOther), true, 108.0).empty());
		assert(!HasPacket(Send(host, 10, Report(kThirdQuest, 10), 109.0, 1009.0), 20, "questStage"));
		assert(host.QuestStatus().at("stages").at(0).at("stage") == 9);
	}

	void TestRollbackOnPersistenceFailure()
	{
		const auto directory = TempDirectory("rollback");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Quests Rollback" };
		assert(EnableStoryFixture(host, &gate, Mode::CoopStory, {}));
		Join(host, 10, "p10", 100.0, 1000.0);
		assert(HasPacket(Send(host, 10, Report(kQuest, 30), 101.0, 1001.0), 10, "questStage"));
		store.WaitForCommits();

		const auto backup = std::filesystem::path{ path.string() + ".bak" };
		std::filesystem::remove_all(backup);
		std::filesystem::create_directories(backup / "occupied");

		const auto rejectedBefore = host.Stats().questReportsRejected;
		assert(HasPacket(Send(host, 10, Report(kQuest, 40), 102.0, 1002.0), 10, "questStage"));
		assert(host.Stats().questReportsRejected == rejectedBefore);
		assert(host.QuestStatus().at("stages").at(0).at("stage") == 40);
		assert(host.Stats().questStagesRelayed == 2);
		bool flushFailed = false;
		try {
			(void)gate.FlushQuests(true);
		} catch (const std::exception&) {
			flushFailed = true;
		}
		assert(flushFailed);

		assert(HasPacket(Send(host, 10, Report(kQuest, 50), 102.5, 1002.5), 10, "questStage"));
		assert(gate.FlushQuests(false));
		store.WaitForCommits();
		assert(host.QuestStatus().at("stages").at(0).at("stage") == 50);
		assert(store.TakePersistenceWarning().has_value());

		assert(host.SetQuestMode(Mode::FreeRoam, 103.0).empty());
		assert(host.QuestMode() == Mode::CoopStory);
		assert(host.QuestStatus().at("mode") == "coop-story");

		assert(host.SetQuestGate(Key(kQuest), 30, false, 104.0).empty());
		assert(!host.IsQuestGated(Key(kQuest), 30));
		assert(host.QuestStatus().at("gates").empty());

		assert(host.SetQuestSolo("p10", Key(kQuest), true, 105.0).empty());
		assert(!host.IsQuestSolo("p10", Key(kQuest)));
		assert(host.QuestStatus().at("solo").empty());
		assert(host.QuestStatus().at("version") == 1);

		std::filesystem::remove_all(backup);
		std::filesystem::remove_all(directory);
	}
	void TestQuestReportsAreBatched()
	{
		const auto directory = TempDirectory("batched");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Quests Batched" };
		assert(EnableStoryFixture(host, &gate, Mode::CoopStory, {}));
		Join(host, 10, "p10", 100.0, 1000.0);

		assert(HasPacket(Send(host, 10, Report(kQuest, 30), 101.0, 1001.0), 10, "questStage"));
		assert(host.QuestStatus().at("stages").size() == 1);
		assert(gate.GetQuests().value("stages", Json::array()).empty());
		assert(gate.FlushQuests(true));
		assert(gate.GetQuests().at("stages").size() == 1);
		assert(!gate.FlushQuests(true));
		assert(HasPacket(Send(host, 10, ScopeRequestPacket(kQuest, "solo"), 102.0, 1002.0), 10, "questPolicy"));
		assert(gate.GetQuests().value("solo", Json::array()).empty());
		assert(gate.FlushQuests(true));
		assert(!gate.GetQuests().at("solo").empty());
		store.WaitForCommits();
		assert(Json::parse(ReadText(path)).at("quests").at("stages").size() == 1);
		std::filesystem::remove_all(directory);
	}

	void TestHostileQuestReportIsNotRelayed()
	{
		ProtocolHost host{ "Quests Hostile" };
		assert(EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
		Join(host, 10, "p10", 100.0, 1000.0);
		Join(host, 20, "p20", 100.1, 1000.1);

		const auto untracked = Send(host, 10, Report(kQuest, 65535), 101.0, 1001.0);
		assert(!HasPacket(untracked, 10, "questStage"));
		assert(!HasPacket(untracked, 20, "questStage"));
		assert(host.QuestStatus().at("stages").empty());
		assert(host.Stats().questReportsRejected == 1);
		assert(host.Stats().questStagesRelayed == 0);

		assert(HasPacket(Send(host, 10, Report(kQuest, 30), 102.0, 1002.0), 20, "questStage"));
		const auto jump = Send(host, 10, Report(kQuest, 65535, true), 103.0, 1003.0);
		assert(!HasPacket(jump, 10, "questStage"));
		assert(!HasPacket(jump, 20, "questStage"));
		assert(host.QuestStatus().at("stages").at(0).at("stage") == 30);
		assert(host.QuestStatus().at("completed").empty());

		assert(HasPacket(Send(host, 10, Report(kQuest, 1000), 104.0, 1004.0), 20, "questStage"));
		const auto healed = Send(host, 20, Report(kQuest, 20), 105.0, 1005.0);
		assert(HasPacket(healed, 10, "questStage"));
		assert(FramesOfType(healed, "questStage").front().at("stage") == 20);
		assert(host.QuestStatus().at("stages").at(0).at("stage") == 20);
		assert(HasPacket(Send(host, 20, Report(kQuest, 30), 106.0, 1006.0), 10, "questStage"));
		assert(host.Stats().questReportsRejected == 2);
		assert(host.Stats().questStagesRelayed == 4);
	}
}

int main()
{
	TestEnablement();
	TestLiveRelayAndForwardOnly();
	TestReplayOnJoinAndReconnect();
	TestPolicyOnJoin();
	TestSoloNotRelayed();
	TestPlayerScopeRequest();
	TestQuestScopeReplyAndThrottle();
	TestGateHoldAndRelease();
	TestRateLimit();
	TestRateLimitPrecedesParsing();
	TestModeChangeStopsSharing();
	TestOverrides();
	TestMalformedReports();
	TestPersistenceRoundTrip();
	TestCorruptStoredStateIsRecovered();
	TestJoinWithoutQuestCapability();
	TestSyncChunking();
	TestCaseInsensitiveQuestKeys();
	TestGateMatchesExactStage();
	TestRollbackOnPersistenceFailure();
	TestQuestReportsAreBatched();
	TestHostileQuestReportIsNotRelayed();
	return 0;
}

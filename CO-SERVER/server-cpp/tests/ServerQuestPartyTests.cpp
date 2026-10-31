#include "ServerFactionTestSupport.h"
#include "ServerQuestTestSupport.h"

#include "F4TQuestProtocol.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace FactionTestSupport;

namespace
{
	using F4T::QuestProtocol::Mode;

	constexpr const char* kQuest = "0A1B2C:fallout4.esm";
	constexpr const char* kOtherQuest = "0000AA:fallout4.esm";
	constexpr const char* kThirdQuest = "123456:dlc.esp";
	constexpr const char* kTutorialQuest = "01CC2A:fallout4.esm";

	Json QuestReport(const char* a_quest, std::uint16_t a_stage, bool a_completed = false)
	{
		Json packet{ { "type", "questReport" }, { "quest", std::string{ a_quest } }, { "stage", a_stage } };
		if (a_completed) packet["completed"] = true;
		return packet;
	}

	Json QuestScope(const char* a_quest, const char* a_scope)
	{
		return Json{ { "type", "questScope" }, { "quest", std::string{ a_quest } }, { "scope", std::string{ a_scope } } };
	}

	std::size_t CountType(const std::vector<OutboundFrame>& a_frames, std::string_view a_type)
	{
		return static_cast<std::size_t>(std::count_if(a_frames.begin(), a_frames.end(), [&](const auto& a_frame) {
			return DecodePacket(a_frame).value("type", std::string{}) == std::string{ a_type };
		}));
	}

	bool SyncHasQuest(const Json& a_sync, const char* a_quest)
	{
		const auto stages = a_sync.at("stages");
		return std::any_of(stages.begin(), stages.end(), [&](const auto& a_stage) {
			return a_stage.at("quest") == a_quest;
		});
	}

	Joined JoinSynced(ProtocolHost& a_host, std::uint32_t a_connectionId, const std::string& a_profileId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		a_host.BindSessionProfile(a_connectionId, a_profileId);
		Joined joined;
		joined.playerId = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		const Json hello{
			{ "type", "hello" },
			{ "protocolVersion", 2 },
			{ "capabilities",
				Json::array({ std::string{ F4T::QuestProtocol::kSyncCapability }, std::string{ F4T::QuestProtocol::kReadyCapability } }) }
		};
		joined.hello = Send(a_host, a_connectionId, hello, a_monotonicSeconds, a_unixTimeSeconds + 0.1);
		assert(HasPacket(joined.hello, a_connectionId, "sessionReady"));
		Send(a_host, a_connectionId, QuestReport(kTutorialQuest, F4T::QuestProtocol::kTutorialExitStage), a_monotonicSeconds + 0.05, a_unixTimeSeconds + 0.15);
		const Json ready{ { "type", "questReady" } };
		Send(a_host, a_connectionId, ready, a_monotonicSeconds + 0.1, a_unixTimeSeconds + 0.2);
		return joined;
	}

	void FormRoster(ProtocolHost& a_host)
	{
		assert(a_host.EnableFactions(nullptr));
		assert(QuestTestSupport::EnableStoryFixture(a_host, nullptr, Mode::CoopStory, {}));
		JoinSynced(a_host, 10, "p10", 1.0, 100.0);
		const auto allyMember = JoinSynced(a_host, 20, "p20", 1.1, 100.1);
		JoinSynced(a_host, 30, "p30", 1.2, 100.2);
		const auto rivalMember = JoinSynced(a_host, 40, "p40", 1.3, 100.3);
		FormFaction(a_host, 10, 20, allyMember.playerId, "Alpha");
		FormFaction(a_host, 30, 40, rivalMember.playerId, "Beta");
	}

	void TestPartyRelayStaysInsideFaction()
	{
		ProtocolHost host{ "Quest Party Relay" };
		FormRoster(host);
		JoinSynced(host, 50, "p50", 2.5, 102.5);
		assert(HasPacket(Send(host, 10, QuestScope(kQuest, "party"), 3.0, 103.0), 10, "questPolicy"));
		assert(HasPacket(Send(host, 20, QuestScope(kQuest, "party"), 3.1, 103.1), 20, "questPolicy"));

		const auto partyStage = Send(host, 10, QuestReport(kQuest, 30), 3.2, 103.2);
		assert(CountType(partyStage, "questStage") == 2);
		assert(HasPacket(partyStage, 10, "questStage"));
		assert(HasPacket(partyStage, 20, "questStage"));
		assert(!HasPacket(partyStage, 30, "questStage"));
		assert(!HasPacket(partyStage, 40, "questStage"));
		assert(!HasPacket(partyStage, 50, "questStage"));

		assert(HasPacket(Send(host, 10, QuestScope(kOtherQuest, "party"), 3.25, 103.25), 10, "questPolicy"));
		assert(HasPacket(Send(host, 20, QuestScope(kOtherQuest, "party"), 3.26, 103.26), 20, "questPolicy"));
		const auto sharedStage = Send(host, 30, QuestReport(kOtherQuest, 5), 3.3, 103.3);
		assert(HasPacket(sharedStage, 30, "questStage"));
		assert(HasPacket(sharedStage, 40, "questStage"));
		assert(HasPacket(sharedStage, 50, "questStage"));
		assert(!HasPacket(sharedStage, 10, "questStage"));
		assert(!HasPacket(sharedStage, 20, "questStage"));

		assert(HasPacket(Send(host, 50, QuestScope(kThirdQuest, "party"), 3.4, 103.4), 50, "questPolicy"));
		const auto factionless = Send(host, 50, QuestReport(kThirdQuest, 9), 3.5, 103.5);
		assert(CountType(factionless, "questStage") == 0);
		assert(host.QuestStatus().at("stages").size() == 2);
	}

	void TestStoryGuardPartyMemberNotPulledOn()
	{
		constexpr const char* kIntro = "01CC2A:fallout4.esm";
		constexpr const char* kNext = "0229E5:fallout4.esm";
		ProtocolHost host{ "Quest Story Party" };
		assert(host.EnableFactions(nullptr));
		assert(host.EnableQuestSync(nullptr, Mode::CoopStory, {}));
		host.SetQuestStoryOrder(F4T::QuestStory::Order::Default());
		JoinSynced(host, 10, "p10", 1.0, 100.0);
		const auto ally = JoinSynced(host, 20, "p20", 1.1, 100.1);
		FormFaction(host, 10, 20, ally.playerId, "Alpha");
		assert(HasPacket(Send(host, 10, QuestReport(kIntro, 2, true), 3.0, 103.0), 10, "questStage"));
		assert(HasPacket(Send(host, 20, QuestScope(kIntro, "party"), 3.1, 103.1), 20, "questPolicy"));
		assert(HasPacket(Send(host, 20, QuestScope(kNext, "party"), 3.2, 103.2), 20, "questPolicy"));
		assert(HasPacket(Send(host, 10, QuestReport(kNext, 10), 3.3, 103.3), 10, "questStage"));
		const auto withheld = Send(host, 10, QuestReport(kNext, 20), 3.4, 103.4);
		assert(HasPacket(withheld, 10, "questStage"));
		assert(!HasPacket(withheld, 20, "questStage"));
		assert(host.Stats().questStoryRefused == 0);
		assert(HasPacket(Send(host, 20, QuestReport(kIntro, 2, true), 3.5, 103.5), 20, "questStage"));
		assert(HasPacket(Send(host, 10, QuestReport(kNext, 30), 3.6, 103.6), 20, "questStage"));
	}

	void TestLateJoinReplayStaysInsideFaction()
	{
		ProtocolHost host{ "Quest Party Replay" };
		FormRoster(host);
		assert(HasPacket(Send(host, 10, QuestScope(kQuest, "party"), 3.0, 103.0), 10, "questPolicy"));
		assert(HasPacket(Send(host, 20, QuestScope(kQuest, "party"), 3.1, 103.1), 20, "questPolicy"));
		assert(HasPacket(Send(host, 10, QuestReport(kQuest, 30), 3.2, 103.2), 20, "questStage"));
		assert(HasPacket(Send(host, 30, QuestReport(kOtherQuest, 5), 3.3, 103.3), 40, "questStage"));

		const auto outsider = JoinSynced(host, 50, "p50", 3.4, 103.4);
		const auto outsiderSync = RequirePacket(outsider.hello, 50, "questSync");
		assert(!SyncHasQuest(outsiderSync, kQuest));
		assert(SyncHasQuest(outsiderSync, kOtherQuest));

		host.HandleDisconnected(40, 103.5);
		const auto rivalBack = JoinSynced(host, 41, "p40", 3.5, 103.6);
		const auto rivalSync = RequirePacket(rivalBack.hello, 41, "questSync");
		assert(!SyncHasQuest(rivalSync, kQuest));
		assert(SyncHasQuest(rivalSync, kOtherQuest));

		host.HandleDisconnected(20, 103.7);
		const auto allyBack = JoinSynced(host, 21, "p20", 3.6, 103.8);
		assert(SyncHasQuest(RequirePacket(allyBack.hello, 21, "questSync"), kQuest));
	}

	void TestScopeReplyCarriesPartyQuests()
	{
		ProtocolHost host{ "Quest Party Policy" };
		FormRoster(host);
		const auto party = RequirePacket(Send(host, 10, QuestScope(kQuest, "party"), 3.0, 103.0), 10, "questPolicy");
		assert(party.at("partyQuests").size() == 1);
		assert(party.at("partyQuests").at(0) == kQuest);
		const auto shared = RequirePacket(Send(host, 10, QuestScope(kQuest, "shared"), 3.1, 103.1), 10, "questPolicy");
		assert(shared.at("partyQuests").empty());
	}

	void TestAdminSoloClearsPartyScope()
	{
		ProtocolHost host{ "Quest Party Admin Solo" };
		FormRoster(host);
		assert(HasPacket(Send(host, 10, QuestScope(kQuest, "party"), 3.0, 103.0), 10, "questPolicy"));
		assert(HasPacket(Send(host, 20, QuestScope(kQuest, "party"), 3.1, 103.1), 20, "questPolicy"));
		const auto policy = RequirePacket(host.SetQuestSolo("p10", *F4T::QuestProtocol::ParseFormKey(kQuest), true, 104.0), 10, "questPolicy");
		assert(policy.at("partyQuests").empty());
		assert(policy.at("soloQuests").size() == 1);
		const auto stage = Send(host, 20, QuestReport(kQuest, 30), 3.2, 103.2);
		assert(HasPacket(stage, 20, "questStage"));
		assert(!HasPacket(stage, 10, "questStage"));
	}

	void TestQuestReportRateLimitStillApplies()
	{
		ProtocolHost host{ "Quest Party Rate" };
		FormRoster(host);
		assert(HasPacket(Send(host, 10, QuestScope(kQuest, "party"), 3.0, 103.0), 10, "questPolicy"));
		for (std::uint16_t stage = 1; stage <= 300; ++stage) {
			Send(host, 10, QuestReport(kQuest, stage), 3.1, 103.1);
		}
		assert(host.Stats().questReportsRejected > 0);
	}

	void TestTutorialGateKeepsLegacyClientWorking()
	{
		ProtocolHost host{ "Quest Tutorial Legacy" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
		const auto welcome = host.HandleConnected(11, 2000.0);
		assert(!welcome.empty());
		host.BindSessionProfile(11, "p11");
		const Json hello{
			{ "type", "hello" },
			{ "protocolVersion", 2 },
			{ "capabilities", Json::array({ std::string{ F4T::QuestProtocol::kSyncCapability } }) }
		};
		assert(HasPacket(Send(host, 11, hello, 200.0, 2000.1), 11, "sessionReady"));

		const auto first = Send(host, 11, QuestReport(kQuest, 30), 201.0, 2001.0);
		assert(HasPacket(first, 11, "questStage"));
		assert(CountType(first, "questNotReady") == 1);
		assert(RequirePacket(first, 11, "questNotReady").at("reason") == "updateRequired");
		const auto second = Send(host, 11, QuestReport(kQuest, 31), 202.0, 2002.0);
		assert(HasPacket(second, 11, "questStage"));
		assert(CountType(second, "questNotReady") == 0);
		assert(host.Stats().questReportsRejected == 0);
		assert(host.Stats().questReportsReceived == 2);
	}

	void TestTutorialGateRejectsQuestWritesUntilReady()
	{
		ProtocolHost host{ "Quest Tutorial" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
		const auto welcome = host.HandleConnected(10, 1000.0);
		assert(!welcome.empty());
		host.BindSessionProfile(10, "p10");
		const Json hello{
			{ "type", "hello" },
			{ "protocolVersion", 2 },
			{ "capabilities",
				Json::array({ std::string{ F4T::QuestProtocol::kSyncCapability }, std::string{ F4T::QuestProtocol::kReadyCapability } }) }
		};
		assert(HasPacket(Send(host, 10, hello, 100.0, 1000.1), 10, "sessionReady"));

		const Json ready{ { "type", "questReady" } };
		const auto bare = Send(host, 10, ready, 100.5, 1000.5);
		assert(RequirePacket(bare, 10, "questNotReady").at("reason") == "notReady");
		assert(host.Stats().questReadyRejected == 1);

		const auto earlyReport = Send(host, 10, QuestReport(kQuest, 30), 101.0, 1001.0);
		assert(!HasPacket(earlyReport, 10, "questStage"));
		assert(RequirePacket(earlyReport, 10, "questNotReady").at("reason") == "notReady");
		const auto earlyScope = Send(host, 10, QuestScope(kQuest, "solo"), 101.1, 1001.1);
		assert(RequirePacket(earlyScope, 10, "questNotReady").at("reason") == "notReady");
		assert(host.Stats().questReportsRejected == 1);
		assert(host.Stats().questScopesRejected == 1);

		assert(Send(host, 10, QuestReport(kTutorialQuest, 1), 101.5, 1001.5).empty());
		assert(RequirePacket(Send(host, 10, ready, 101.6, 1001.6), 10, "questNotReady").at("reason") == "notReady");
		assert(host.Stats().questReadyRejected == 2);

		assert(Send(host, 10, QuestReport(kTutorialQuest, F4T::QuestProtocol::kTutorialExitStage), 102.0, 1002.0).empty());
		assert(CountType(Send(host, 10, ready, 102.1, 1002.1), "questNotReady") == 0);
		assert(HasPacket(Send(host, 10, QuestReport(kQuest, 30), 103.0, 1003.0), 10, "questStage"));
		const auto solo = Send(host, 10, QuestScope(kQuest, "solo"), 104.0, 1004.0);
		assert(RequirePacket(solo, 10, "questPolicy").at("soloQuests").size() == 1);
		assert(host.Stats().questReportsReceived == 4);
		assert(host.Stats().questScopesReceived == 2);
	}
}

int main()
{
	TestPartyRelayStaysInsideFaction();
	TestTutorialGateRejectsQuestWritesUntilReady();
	TestTutorialGateKeepsLegacyClientWorking();
	TestStoryGuardPartyMemberNotPulledOn();
	TestLateJoinReplayStaysInsideFaction();
	TestScopeReplyCarriesPartyQuests();
	TestAdminSoloClearsPartyScope();
	TestQuestReportRateLimitStillApplies();
	return 0;
}

#include "DurableStateFileStore.h"
#include "PersistenceGate.h"
#include "ServerEvents.h"
#include "ServerFactionTestSupport.h"
#include "ServerProtocol.h"
#include "ServerQuestTestSupport.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::DurableStateFileStore;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::PersistenceGate;
	using CommonwealthOnline::Server::ProtocolHost;
	using CommonwealthOnline::Server::ServerEvent;
	using CommonwealthOnline::Server::ServerEventKind;
	using CommonwealthOnline::Server::ServerEvents;

	ServerEvent Event(ServerEventKind a_kind)
	{
		return ServerEvent{ a_kind, 1, 1, "profile", nullptr };
	}

	Json ChatSend(const std::string& a_text)
	{
		return Json{ { "type", "chatSend" }, { "channel", "global" }, { "text", a_text } };
	}

	Json UpdateAccess(const char* a_op, std::uint64_t a_zoneId)
	{
		return Json{ { "type", "settlementAccessUpdate" }, { "buildZoneId", a_zoneId }, { "op", a_op }, { "targetProfileId", "profile-guest" } };
	}

	F4T::Workshop::BuildZonePolicy ZonePolicy(std::uint64_t a_zoneId)
	{
		return F4T::Workshop::BuildZonePolicy{ a_zoneId, F4T::Workshop::FormKey{ "Fallout4.esm", 1 }, 0.0F, 0.0F, 0.0F, 10.0F, 2.0F, 4 };
	}

	Json PlaceObject(std::uint64_t a_operationId, std::uint64_t a_revision, std::uint64_t a_zoneId, double a_x)
	{
		return Json{
			{ "type", "workshopOp" },
			{ "operationId", a_operationId },
			{ "expectedRevision", a_revision },
			{ "buildZoneId", a_zoneId },
			{ "kind", "place" },
			{ "form", Json{ { "plugin", "Fallout4.esm" }, { "localFormId", 0x1234 } } },
			{ "space", Json{ { "plugin", "Fallout4.esm" }, { "localFormId", 1 } } },
			{ "transform", Json{ { "x", a_x }, { "y", 0.0 }, { "z", 0.0 }, { "pitch", 0.0 }, { "yaw", 0.0 }, { "roll", 0.0 }, { "scale", 1.0 } } }
		};
	}

	void TestPriorityOrderAndUnsubscribe()
	{
		ServerEvents events;
		std::vector<int> order;
		events.Subscribe(ServerEventKind::Chat, [&order](const ServerEvent&) { order.push_back(0); return true; });
		const auto middle = events.Subscribe(ServerEventKind::Chat, [&order](const ServerEvent&) { order.push_back(5); return true; }, 5);
		events.Subscribe(ServerEventKind::Chat, [&order](const ServerEvent&) { order.push_back(6); return true; }, 5);
		events.Subscribe(ServerEventKind::Chat, [&order](const ServerEvent&) { order.push_back(9); return true; }, 9);
		events.Notify(Event(ServerEventKind::Chat));
		assert((order == std::vector<int>{ 9, 5, 6, 0 }));
		assert(events.Unsubscribe(middle));
		assert(!events.Unsubscribe(middle));
		order.clear();
		events.Notify(Event(ServerEventKind::Chat));
		assert((order == std::vector<int>{ 9, 6, 0 }));
		assert(events.Subscribe(ServerEventKind::Chat, nullptr) == 0);
	}

	void TestAllowRefusesAndCatchesExceptions()
	{
		ServerEvents events;
		assert(events.Allow(Event(ServerEventKind::Chat)));
		const auto thrower = events.Subscribe(ServerEventKind::Chat, [](const ServerEvent&) -> bool { throw std::runtime_error{ "script fault" }; });
		events.Notify(Event(ServerEventKind::Chat));
		assert(!events.Allow(Event(ServerEventKind::Chat)));
		assert(events.Unsubscribe(thrower));
		assert(events.Allow(Event(ServerEventKind::Chat)));
		const auto refuse = events.Subscribe(ServerEventKind::Chat, [](const ServerEvent&) { return false; });
		assert(!events.Allow(Event(ServerEventKind::Chat)));
		assert(events.Allow(Event(ServerEventKind::QuestReport)));
		assert(events.Unsubscribe(refuse));
		assert(events.Allow(Event(ServerEventKind::Chat)));
		int lowCalls{};
		events.Subscribe(ServerEventKind::Chat, [&lowCalls](const ServerEvent&) { ++lowCalls; return true; });
		events.Subscribe(ServerEventKind::Chat, [](const ServerEvent&) { return false; }, 9);
		assert(!events.Allow(Event(ServerEventKind::Chat)));
		assert(lowCalls == 0);
	}

	void BurnDispatchBudget()
	{
		const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(80);
		while (std::chrono::steady_clock::now() < until) {
		}
	}

	void TestDispatchBudgetOverrunKeepsLaterVetoes()
	{
		ServerEvents slowFirst;
		int laterCalls{};
		slowFirst.Subscribe(ServerEventKind::Chat, [](const ServerEvent&) {
			BurnDispatchBudget();
			return true;
		});
		slowFirst.Subscribe(ServerEventKind::Chat, [&laterCalls](const ServerEvent&) {
			++laterCalls;
			return false;
		});
		assert(!slowFirst.Allow(Event(ServerEventKind::Chat)));
		assert(laterCalls == 1);

		ServerEvents refuserFirst;
		int slowCalls{};
		refuserFirst.Subscribe(ServerEventKind::Chat, [](const ServerEvent&) { return false; });
		refuserFirst.Subscribe(ServerEventKind::Chat, [&slowCalls](const ServerEvent&) {
			++slowCalls;
			BurnDispatchBudget();
			return true;
		});
		assert(!refuserFirst.Allow(Event(ServerEventKind::Chat)));
		assert(slowCalls == 0);

		ServerEvents notified;
		int notifyLaterCalls{};
		notified.Subscribe(ServerEventKind::PlayerDeath, [](const ServerEvent&) {
			BurnDispatchBudget();
			return true;
		});
		notified.Subscribe(ServerEventKind::PlayerDeath, [&notifyLaterCalls](const ServerEvent&) {
			++notifyLaterCalls;
			return true;
		});
		notified.Notify(Event(ServerEventKind::PlayerDeath));
		assert(notifyLaterCalls == 0);
	}

	void TestHandlerMayChangeSubscriptionsDuringNotify()
	{
		ServerEvents events;
		int calls{};
		ServerEvents::Token self{};
		self = events.Subscribe(ServerEventKind::Chat, [&events, &calls, &self](const ServerEvent&) {
			++calls;
			(void)events.Unsubscribe(self);
			(void)events.Subscribe(ServerEventKind::Chat, [](const ServerEvent&) { return true; });
			return true;
		});
		events.Notify(Event(ServerEventKind::Chat));
		assert(calls == 1);
		events.Notify(Event(ServerEventKind::Chat));
		assert(calls == 1);
	}

	void TestLifecycleNotifications()
	{
		ProtocolHost host{ "Events Lifecycle" };
		int joined{};
		int ready{};
		int left{};
		std::uint32_t leftPlayer{};
		host.Events().Subscribe(ServerEventKind::PlayerJoined, [&joined](const ServerEvent&) { ++joined; return true; });
		host.Events().Subscribe(ServerEventKind::PlayerReady, [&ready](const ServerEvent&) { ++ready; return true; });
		host.Events().Subscribe(ServerEventKind::PlayerLeft, [&left, &leftPlayer](const ServerEvent& a_event) {
			++left;
			leftPlayer = a_event.playerId;
			return true;
		});
		const auto player = FactionTestSupport::Join(host, 10, "profile-lifecycle", 1.0, 100.0);
		assert(joined == 1);
		assert(ready == 1);
		host.HandleDisconnected(10, 101.0);
		assert(left == 1);
		assert(leftPlayer == player.playerId);
	}

	void TestChatVetoResyncsSenderAndKeepsHistoryClean()
	{
		ProtocolHost host{ "Events Chat" };
		host.SetChatEnabled(true);
		FactionTestSupport::Join(host, 10, "profile-chat", 1.0, 100.0);
		auto& events = host.Events();
		FactionTestSupport::Join(host, 20, "profile-other", 1.1, 100.2);
		assert(FactionTestSupport::HasPacket(FactionTestSupport::Send(host, 20, ChatSend("prior line"), 1.2, 100.3), 10, "chatMessage"));
		const auto refuse = events.Subscribe(ServerEventKind::Chat, [](const ServerEvent&) { return false; });
		const auto history = FactionTestSupport::Send(host, 10, Json{ { "type", "chatHistoryRequest" } }, 1.5, 100.5);
		assert(FactionTestSupport::HasPacket(history, 10, "chatHistory"));
		const auto before = host.Stats();
		const auto refused = FactionTestSupport::Send(host, 10, ChatSend("vetoed line"), 2.0, 101.0);
		assert(host.Stats().chatRejected == before.chatRejected + 1);
		const auto messages = FactionTestSupport::RequirePacket(refused, 10, "chatHistory").at("messages");
		assert(messages.size() == 1);
		assert(messages.at(0).at("text") == "prior line");
		assert(!messages.at(0).contains("fromProfileId"));
		assert(!messages.at(0).contains("fromSocialKey"));
		assert(!FactionTestSupport::HasPacket(refused, 10, "chatMessage"));
		assert(events.Unsubscribe(refuse));
		const auto accepted = FactionTestSupport::Send(host, 10, ChatSend("allowed line"), 3.0, 102.0);
		assert(FactionTestSupport::HasPacket(accepted, 10, "chatMessage"));
	}

	void TestVetoedChatCountsTowardRateLimit()
	{
		ProtocolHost host{ "Events Chat Limit" };
		host.SetChatEnabled(true);
		FactionTestSupport::Join(host, 10, "profile-spammer", 1.0, 100.0);
		auto& events = host.Events();
		const auto refuse = events.Subscribe(ServerEventKind::Chat, [](const ServerEvent&) { return false; });
		(void)FactionTestSupport::Send(host, 10, ChatSend("spam line"), 2.0, 101.0);
		(void)FactionTestSupport::Send(host, 10, ChatSend("spam line"), 2.5, 101.5);
		assert(events.Unsubscribe(refuse));
		const auto limited = FactionTestSupport::Send(host, 10, ChatSend("spam line"), 3.0, 102.0);
		assert(FactionTestSupport::RequirePacket(limited, 10, "chatRejected").at("reason") == "duplicate");
		assert(!FactionTestSupport::HasPacket(limited, 10, "chatMessage"));
	}

	void TestQuestVetoRefusesAndKeepsStage()
	{
		ProtocolHost host{ "Events Quest" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, QuestTestSupport::Mode::CoopStory, {}));
		QuestTestSupport::Join(host, 10, "profile-quest", 1.0, 100.0);
		auto& events = host.Events();
		const auto refuse = events.Subscribe(ServerEventKind::QuestReport, [](const ServerEvent&) { return false; });
		const auto before = host.Stats();
		QuestTestSupport::Send(host, 10, QuestTestSupport::Report(QuestTestSupport::kQuest, 1), 2.0, 101.0);
		assert(host.Stats().questReportsRejected == before.questReportsRejected + 1);
		assert(host.Stats().questStagesRelayed == before.questStagesRelayed);
		assert(events.Unsubscribe(refuse));
		QuestTestSupport::Send(host, 10, QuestTestSupport::Report(QuestTestSupport::kQuest, 1), 3.0, 102.0);
		assert(host.Stats().questStagesRelayed == before.questStagesRelayed + 1);
	}

	void TestQuestScopeVetoSeesNormalizedPacketOnly()
	{
		ProtocolHost host{ "Events Quest Scope" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, QuestTestSupport::Mode::CoopStory, {}));
		QuestTestSupport::Join(host, 10, "profile-scope", 1.0, 100.0);
		auto& events = host.Events();
		int calls{};
		Json seen;
		const auto watch = events.Subscribe(ServerEventKind::QuestReport, [&calls, &seen](const ServerEvent& a_event) {
			++calls;
			if (a_event.packet != nullptr) seen = *a_event.packet;
			return true;
		});
		const auto before = host.Stats();
		(void)QuestTestSupport::Send(host, 10, Json{ { "type", "questScope" }, { "quest", QuestTestSupport::kQuest }, { "scope", "sideways" } }, 2.0, 101.0);
		assert(calls == 0);
		assert(host.Stats().questScopesRejected == before.questScopesRejected + 1);
		auto packet = QuestTestSupport::ScopeRequestPacket(QuestTestSupport::kQuest, "party");
		packet["unexpected"] = 7;
		(void)QuestTestSupport::Send(host, 10, packet, 3.0, 102.0);
		assert(calls == 1);
		assert(seen == (Json{
			{ "type", "questScope" },
			{ "quest", F4T::QuestProtocol::FormatFormKey(QuestTestSupport::Key(QuestTestSupport::kQuest)) },
			{ "scope", "party" } }));
		assert(events.Unsubscribe(watch));
		const auto refuse = events.Subscribe(ServerEventKind::QuestReport, [](const ServerEvent&) { return false; });
		const auto refusedBefore = host.Stats();
		(void)QuestTestSupport::Send(host, 10, QuestTestSupport::ScopeRequestPacket(QuestTestSupport::kQuest, "solo"), 4.0, 103.0);
		assert(host.Stats().questScopesRejected == refusedBefore.questScopesRejected + 1);
		assert(host.Stats().questScopesApplied == refusedBefore.questScopesApplied);
		assert(events.Unsubscribe(refuse));
	}

	void TestFactionVetoReportsFailureAndState()
	{
		ProtocolHost host{ "Events Faction" };
		assert(host.EnableFactions(nullptr));
		FactionTestSupport::Join(host, 10, "profile-faction", 1.0, 100.0);
		FactionTestSupport::Join(host, 20, "profile-watcher", 1.1, 100.1);
		int calls{};
		auto& events = host.Events();
		const auto refuse = events.Subscribe(ServerEventKind::FactionAction, [&calls](const ServerEvent&) { ++calls; return false; });
		(void)FactionTestSupport::Send(host, 10, Json{ { "type", "factionCreate" } }, 1.5, 100.5);
		assert(calls == 0);
		const auto before = host.Stats();
		const auto refused = FactionTestSupport::Send(host, 10, FactionTestSupport::Create("Veto Team"), 2.0, 101.0);
		assert(calls == 1);
		assert(host.Stats().packetsRejected == before.packetsRejected + 1);
		assert(FactionTestSupport::HasPacket(refused, 10, "factionState"));
		assert(std::ranges::none_of(refused, [](const auto& a_frame) { return a_frame.connectionId == 20; }));
		FactionTestSupport::AssertResult(refused, 10, "create", false, "vetoed");
		assert(events.Unsubscribe(refuse));
		FactionTestSupport::AssertResult(FactionTestSupport::Send(host, 10, FactionTestSupport::Create("Veto Team"), 3.0, 102.0), 10, "create", true);
	}

	void TestNestedFactionDispatchKeepsOuterVeto()
	{
		ProtocolHost host{ "Factions Nested Veto" };
		assert(host.EnableFactions(nullptr));
		FactionTestSupport::Join(host, 10, "p10", 1.0, 100.0);
		FactionTestSupport::Join(host, 20, "p20", 1.1, 100.2);
		bool vetoOuter = true;
		bool inNested = false;
		std::vector<OutboundFrame> nested;
		host.Events().Subscribe(ServerEventKind::FactionAction, [&](const ServerEvent& a_event) {
			if (inNested || a_event.connectionId != 10 || !vetoOuter) return true;
			inNested = true;
			nested = FactionTestSupport::Send(host, 20, FactionTestSupport::Create("Beta Team"), 2.5, 101.5);
			inNested = false;
			return false;
		});
		FactionTestSupport::AssertResult(FactionTestSupport::Send(host, 10, FactionTestSupport::Create("Alpha Team"), 2.0, 101.0), 10, "create", false, "vetoed");
		FactionTestSupport::AssertResult(nested, 20, "create", true);
		vetoOuter = false;
		FactionTestSupport::AssertResult(FactionTestSupport::Send(host, 10, FactionTestSupport::Create("Alpha Team"), 2.6, 101.6), 10, "create", true);
	}

	void TestSettlementAccessVetoRestoresState()
	{
		ProtocolHost host{ "Events Settlement" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy(9)));
		FactionTestSupport::Join(host, 10, "profile-owner", 1.0, 100.0);
		FactionTestSupport::Join(host, 20, "profile-guest", 1.1, 100.2);
		std::string failure;
		const auto ownerFrames = host.SetSettlementOwner(9, "profile-owner", failure);
		assert(failure.empty());
		assert(FactionTestSupport::RequirePacket(ownerFrames, 10, "settlementAccessState").at("ownerProfileId") == "profile-owner");
		auto& events = host.Events();
		const auto refuse = events.Subscribe(ServerEventKind::SettlementAction, [](const ServerEvent&) { return false; });
		const auto refused = FactionTestSupport::Send(host, 10, UpdateAccess("grant", 9), 2.0, 102.0);
		assert(FactionTestSupport::RequirePacket(refused, 10, "settlementAccessReject").at("status") == "notAllowed");
		assert(FactionTestSupport::RequirePacket(refused, 10, "settlementAccessState").at("allowedProfileIds").empty());
		assert(events.Unsubscribe(refuse));
		const auto granted = FactionTestSupport::Send(host, 10, UpdateAccess("grant", 9), 3.0, 103.0);
		assert(FactionTestSupport::RequirePacket(granted, 10, "settlementAccessState").at("allowedProfileIds") == Json::array({ "profile-guest" }));
	}

	void TestSettlementAccessUnknownZoneSkipsVeto()
	{
		ProtocolHost host{ "Events Settlement Zone" };
		FactionTestSupport::Join(host, 10, "profile-owner", 1.0, 100.0);
		int calls{};
		auto& events = host.Events();
		const auto refuse = events.Subscribe(ServerEventKind::SettlementAction, [&calls](const ServerEvent&) { ++calls; return false; });
		const auto refused = FactionTestSupport::Send(host, 10, UpdateAccess("grant", 77), 2.0, 102.0);
		assert(calls == 0);
		assert(FactionTestSupport::RequirePacket(refused, 10, "settlementAccessReject").at("status") == "unknownBuildZone");
		assert(events.Unsubscribe(refuse));
	}

	void TestWorkshopVetoRejectsWithoutCommit()
	{
		ProtocolHost host{ "Events Workshop" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy(9)));
		FactionTestSupport::Join(host, 10, "profile-builder", 1.0, 100.0);
		(void)FactionTestSupport::Send(host, 10, FactionTestSupport::Transform(0.0), 1.5, 100.5);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-builder", failure);
		assert(failure.empty());
		auto& events = host.Events();
		const auto refuse = events.Subscribe(ServerEventKind::SettlementAction, [](const ServerEvent&) { return false; });
		const auto before = host.Stats();
		const auto refused = FactionTestSupport::Send(host, 10, PlaceObject(1, 0, 9, 0.0), 2.0, 102.0);
		assert(host.Stats().workshopOpsRejected == before.workshopOpsRejected + 1);
		assert(FactionTestSupport::HasPacket(refused, 10, "workshopReject"));
		assert(!FactionTestSupport::HasPacket(refused, 10, "workshopCommit"));
		assert(events.Unsubscribe(refuse));
		assert(FactionTestSupport::HasPacket(FactionTestSupport::Send(host, 10, PlaceObject(2, 0, 9, 0.0), 3.0, 103.0), 10, "workshopCommit"));
	}

	void TestWorkshopVetoRunsOncePerOperation()
	{
		ProtocolHost host{ "Events Workshop Once" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy(9)));
		FactionTestSupport::Join(host, 10, "profile-counter", 1.0, 100.0);
		(void)FactionTestSupport::Send(host, 10, FactionTestSupport::Transform(0.0), 1.5, 100.5);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-counter", failure);
		assert(failure.empty());
		auto& events = host.Events();
		int calls{};
		bool allow{ true };
		const auto counter = events.Subscribe(ServerEventKind::SettlementAction, [&calls, &allow](const ServerEvent&) {
			++calls;
			return allow;
		});
		assert(FactionTestSupport::HasPacket(FactionTestSupport::Send(host, 10, PlaceObject(1, 0, 9, 0.0), 2.0, 102.0), 10, "workshopCommit"));
		assert(calls == 1);
		assert(FactionTestSupport::HasPacket(FactionTestSupport::Send(host, 10, PlaceObject(1, 0, 9, 0.0), 3.0, 103.0), 10, "workshopCommit"));
		assert(calls == 2);
		allow = false;
		const auto refused = FactionTestSupport::Send(host, 10, PlaceObject(2, 1, 9, 0.0), 4.0, 104.0);
		assert(calls == 3);
		assert(FactionTestSupport::HasPacket(refused, 10, "workshopReject"));
		assert(!FactionTestSupport::HasPacket(refused, 10, "workshopCommit"));
		assert(events.Unsubscribe(counter));
	}

	void TestInventoryTradeVetoRejectsWithoutAuthority()
	{
		ProtocolHost host{ "Events Inventory" };
		FactionTestSupport::Join(host, 10, "profile-trader", 1.0, 100.0);
		auto& events = host.Events();
		int calls{};
		const auto refuse = events.Subscribe(ServerEventKind::InventoryChange, [&calls](const ServerEvent&) { ++calls; return false; });
		const auto before = host.Stats();
		const auto refused = FactionTestSupport::Send(host, 10, Json{
			{ "type", "inventoryTradeRequest" },
			{ "operationId", "trade-veto" },
			{ "expectedRevision", 0u },
			{ "vendor", Json{ { "cellId", "0000CAFE" }, { "refId", "00001234" } } },
			{ "sell", Json::array({ Json{ { "itemKey", "test.esm:00000123" }, { "count", 1u } } }) },
			{ "buy", Json::array() } }, 2.0, 101.0);
		assert(calls == 1);
		assert(host.Stats().packetsRejected == before.packetsRejected + 1);
		const auto result = FactionTestSupport::RequirePacket(refused, 10, "inventoryTradeResult");
		assert(result.at("operationId") == "trade-veto");
		assert(result.at("status") == "rejected");
		assert(result.at("reason") == "inventoryAuthorityUnavailable");
		assert(events.Unsubscribe(refuse));
	}

	void TestInvalidDropSkipsVetoAndCountsOnce()
	{
		ProtocolHost host{ "Events Drop" };
		FactionTestSupport::Join(host, 10, "profile-dropper", 1.0, 100.0);
		int calls{};
		auto& events = host.Events();
		const auto refuse = events.Subscribe(ServerEventKind::InventoryChange, [&calls](const ServerEvent&) { ++calls; return false; });
		const auto before = host.Stats();
		(void)FactionTestSupport::Send(host, 10, Json{ { "type", "inventoryDropRequest" } }, 2.0, 101.0);
		assert(calls == 0);
		assert(host.Stats().packetsRejected == before.packetsRejected + 1);
		assert(events.Unsubscribe(refuse));
	}

	void TestSettlementCaptureStartVetoRejects()
	{
		ProtocolHost host{ "Events Capture" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy(9)));
		FactionTestSupport::Join(host, 10, "profile-capturer", 1.0, 100.0);
		auto& events = host.Events();
		const auto refuse = events.Subscribe(ServerEventKind::SettlementAction, [](const ServerEvent&) { return false; });
		const auto before = host.Stats();
		const auto refused = FactionTestSupport::Send(host, 10, Json{ { "type", "settlementCapture" }, { "buildZoneId", 9 }, { "op", "start" } }, 2.0, 101.0);
		assert(host.Stats().packetsRejected == before.packetsRejected + 1);
		assert(FactionTestSupport::RequirePacket(refused, 10, "settlementCaptureReject").at("status") == "vetoed");
		const auto state = FactionTestSupport::RequirePacket(refused, 10, "settlementCaptureState");
		assert(state.at("status") == "cancelled" && state.at("capturerProfileId") == "profile-capturer" && state.at("buildZoneId") == 9);
		assert(events.Unsubscribe(refuse));
	}

	void TestPlayerDeathNotifiesOnAdminKill()
	{
		ProtocolHost host{ "Events Death" };
		const auto player = FactionTestSupport::Join(host, 10, "profile-victim", 1.0, 100.0);
		int deaths{};
		std::uint32_t deadPlayer{};
		std::string deadProfile;
		host.Events().Subscribe(ServerEventKind::PlayerDeath, [&deaths, &deadPlayer, &deadProfile](const ServerEvent& a_event) {
			++deaths;
			deadPlayer = a_event.playerId;
			deadProfile = a_event.profileId;
			return true;
		});
		(void)host.AdminKillPlayer(player.playerId);
		assert(deaths == 1);
		assert(deadPlayer == player.playerId);
		assert(deadProfile == "profile-victim");
	}

	void TestVetoedTutorialExitIsNotRecordedOrSaved()
	{
		const auto directory = QuestTestSupport::TempDirectory("tutorial-veto");
		DurableStateFileStore store(directory / "durable-state.json", [] { return 1000.0; }, [] { return std::string{ "0123456789abcdef0123456789abcdef" }; });
		PersistenceGate gate(store);
		ProtocolHost host{ "Events Tutorial Veto" };
		assert(QuestTestSupport::EnableStoryFixture(host, &gate, QuestTestSupport::Mode::CoopStory, {}));
		(void)host.HandleConnected(10, 100.0);
		host.BindSessionProfile(10, "profile-tutorial");
		Json hello{ { "type", "hello" }, { "protocolVersion", 2 } };
		hello["capabilities"] = Json::array(
			{ std::string{ F4T::QuestProtocol::kSyncCapability }, std::string{ F4T::QuestProtocol::kReadyCapability } });
		(void)QuestTestSupport::Send(host, 10, hello, 1.0, 100.1);
		auto& events = host.Events();
		const auto refuse = events.Subscribe(ServerEventKind::QuestReport, [](const ServerEvent&) { return false; });
		const auto before = host.Stats();
		QuestTestSupport::Send(host, 10, QuestTestSupport::Report(QuestTestSupport::kTutorialQuest, QuestTestSupport::kTutorialExitStage), 2.0, 101.0);
		assert(host.Stats().questReportsRejected == before.questReportsRejected + 1);
		assert(gate.GetQuests().value("tutorialExit", Json::object()).empty());
		(void)QuestTestSupport::Send(host, 10, Json{ { "type", "questReady" } }, 3.0, 102.0);
		assert(host.Stats().questReadyRejected == before.questReadyRejected + 1);
		assert(events.Unsubscribe(refuse));
		QuestTestSupport::Send(host, 10, QuestTestSupport::Report(QuestTestSupport::kTutorialQuest, QuestTestSupport::kTutorialExitStage), 4.0, 103.0);
		const auto readyBefore = host.Stats();
		(void)QuestTestSupport::Send(host, 10, Json{ { "type", "questReady" } }, 5.0, 104.0);
		assert(host.Stats().questReadyRejected == readyBefore.questReadyRejected);
	}

	void TestStoryRefusedWriteSkipsLowerPrioritySubscribers()
	{
		ProtocolHost host{ "Events Story Guard Order" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, QuestTestSupport::Mode::FreeRoam, {}));
		QuestTestSupport::Join(host, 10, "profile-story", 1.0, 100.0);
		auto& events = host.Events();
		int lowCalls{};
		const auto low = events.Subscribe(ServerEventKind::QuestReport, [&lowCalls](const ServerEvent&) {
			++lowCalls;
			return true;
		});
		const auto before = host.Stats();
		QuestTestSupport::Send(host, 10, QuestTestSupport::Report(QuestTestSupport::kQuest, 1), 2.0, 101.0);
		assert(lowCalls == 0);
		assert(host.Stats().questStoryRefused == before.questStoryRefused + 1);
		assert(host.Stats().questReportsRejected == before.questReportsRejected + 1);
		assert(host.Stats().questStagesRelayed == before.questStagesRelayed);
		assert(events.Unsubscribe(low));
	}

	void TestThrowingSubscriberCannotApplyStoryRefusedWrite()
	{
		ProtocolHost host{ "Events Story Throw" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, QuestTestSupport::Mode::FreeRoam, {}));
		QuestTestSupport::Join(host, 10, "profile-throw", 1.0, 100.0);
		(void)host.Events().Subscribe(
			ServerEventKind::QuestReport,
			[](const ServerEvent&) -> bool { throw std::runtime_error{ "script fault" }; },
			std::numeric_limits<int>::max());
		const auto before = host.Stats();
		QuestTestSupport::Send(host, 10, QuestTestSupport::Report(QuestTestSupport::kQuest, 1), 2.0, 101.0);
		assert(host.Stats().questStagesRelayed == before.questStagesRelayed);
		assert(host.Stats().questStoryRefused == before.questStoryRefused + 1);
		assert(host.Stats().questReportsRejected == before.questReportsRejected + 1);
		const auto tutorialBefore = host.Stats();
		QuestTestSupport::Send(host, 10, QuestTestSupport::Report(QuestTestSupport::kTutorialQuest, QuestTestSupport::kTutorialExitStage), 3.0, 102.0);
		assert(host.Stats().questStoryRefused == tutorialBefore.questStoryRefused);
		assert(host.Stats().questReportsRejected == tutorialBefore.questReportsRejected + 1);
	}
}

int main()
{
	TestPriorityOrderAndUnsubscribe();
	TestAllowRefusesAndCatchesExceptions();
	TestDispatchBudgetOverrunKeepsLaterVetoes();
	TestHandlerMayChangeSubscriptionsDuringNotify();
	TestLifecycleNotifications();
	TestChatVetoResyncsSenderAndKeepsHistoryClean();
	TestVetoedChatCountsTowardRateLimit();
	TestQuestVetoRefusesAndKeepsStage();
	TestQuestScopeVetoSeesNormalizedPacketOnly();
	TestVetoedTutorialExitIsNotRecordedOrSaved();
	TestStoryRefusedWriteSkipsLowerPrioritySubscribers();
	TestThrowingSubscriberCannotApplyStoryRefusedWrite();
	TestFactionVetoReportsFailureAndState();
	TestNestedFactionDispatchKeepsOuterVeto();
	TestSettlementAccessVetoRestoresState();
	TestSettlementAccessUnknownZoneSkipsVeto();
	TestWorkshopVetoRejectsWithoutCommit();
	TestWorkshopVetoRunsOncePerOperation();
	TestInventoryTradeVetoRejectsWithoutAuthority();
	TestInvalidDropSkipsVetoAndCountsOnce();
	TestSettlementCaptureStartVetoRejects();
	TestPlayerDeathNotifiesOnAdminKill();
	return 0;
}

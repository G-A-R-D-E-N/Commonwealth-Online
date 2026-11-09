#include "F4TPlayerStateValidation.h"
#include "F4TWorkshopRaids.h"
#include "ServerFactionTestSupport.h"

#include "ServerEvents.h"
#include "ServerSettlementAccess.h"
#include "ServerSettlementCapture.h"

#include <cstdint>
#include <format>
#include <limits>
#include <string>

using namespace FactionTestSupport;

namespace
{
	using F4T::Workshop::BuildZonePolicy;
	using F4T::Workshop::FormKey;
	using F4T::Workshop::SettlementState;
	using F4T::SettlementCapture::Presence;
	using CommonwealthOnline::Server::CapturePeer;
	using CommonwealthOnline::Server::ServerEvent;
	using CommonwealthOnline::Server::ServerEventKind;
	using CommonwealthOnline::Server::SettlementAccessService;
	using CommonwealthOnline::Server::SettlementCaptureService;

	constexpr std::uint64_t kZone = 9;
	constexpr double kBenchX = 1000.0;

	BuildZonePolicy ZonePolicy()
	{
		return BuildZonePolicy{ kZone, FormKey{ "Fallout4.esm", 1 }, 0.0F, 0.0F, 0.0F, 5000.0F, 2.0F, 4, true, 1000.0F, 0.0F, 0.0F, 300.0F, {} };
	}

	Json Capture(const char* a_op, std::uint64_t a_zoneId = kZone)
	{
		return Json{ { "type", "settlementCapture" }, { "buildZoneId", a_zoneId }, { "op", a_op } };
	}

	Json Vitals(bool a_downed)
	{
		return Json{ { "type", "playerState" }, { "vitals", Json{ { "healthPercent", a_downed ? 0 : 100 }, { "isDowned", a_downed }, { "isDead", false } } } };
	}

	struct Player
	{
		std::uint32_t connectionId{};
		std::uint32_t sequence{};
	};

	void Place(ProtocolHost& a_host, Player& a_player, double a_x, double a_time, const char* a_movementType = "normal")
	{
		Json transform = Transform(a_x);
		transform["movementType"] = a_movementType;
		a_host.HandleMessage(a_player.connectionId, EncodeSnapshot(transform, Family::Transform, ++a_player.sequence), a_time, a_time + 100.0);
	}

	Player Arrive(ProtocolHost& a_host, std::uint32_t a_connectionId, const std::string& a_profileId, double a_x, bool a_downed = false, Json a_capabilities = Json::array({ "social-names-v1" }))
	{
		Join(a_host, a_connectionId, a_profileId, 1.0, 100.0, a_capabilities);
		Player player{ a_connectionId, 0 };
		Place(a_host, player, a_x, 1.5, "spawn");
		Send(a_host, a_connectionId, Vitals(a_downed), 1.6, 101.6);
		return player;
	}

	std::string StatusIn(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_type)
	{
		return RequirePacket(a_frames, a_connectionId, a_type).at("status").get<std::string>();
	}

	void Hold(ProtocolHost& a_host, std::vector<Player*> a_players, double a_time)
	{
		for (auto* player : a_players) {
			Place(a_host, *player, kBenchX + 100.0, a_time);
		}
	}

	void EquipMininuke(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_monotonic)
	{
		const auto equipped = Json{ { "type", "playerState" }, { "equippedItems", Json::array({ Json{ { "slot", F4T::PlayerStateValidation::kThrowableSlot }, { "form" "Id", std::format("{:08X}", F4T::WorkshopRaids::kMiniNukeSourceFormId) } } }) } };
		(void)Send(a_host, a_connectionId, equipped, a_monotonic, a_monotonic + 100.0);
	}

	void BreakBench(ProtocolHost& a_host, std::uint32_t a_ownerConnectionId, double a_monotonic)
	{
		const auto mininuke = Json{ { "type", "structureHit" }, { "buildZoneId", kZone }, { "objectId", std::uint64_t{ 0 } }, { "explosive", "mininuke" } };
		EquipMininuke(a_host, a_ownerConnectionId, a_monotonic);
		for (int hit = 0; hit < 4; ++hit) {
			const double now = a_monotonic + hit;
			assert(!Send(a_host, a_ownerConnectionId, mininuke, now, now + 100.0).empty());
		}
	}

	void TestUncontestedHoldTransfersOwnership()
	{
		ProtocolHost host{ "Capture Hold" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy()));
		auto owner = Arrive(host, 10, "profile-owner", 0.0);
		auto raider = Arrive(host, 20, "profile-raider", kBenchX + 100.0);
		std::string failure;
		assert(host.SetSettlementOwner(kZone, "profile-owner", failure).size() == 2);
		BreakBench(host, 10, 2.0);
		Place(host, owner, 9000.0, 5.5, "teleport");
		Hold(host, { &raider }, 5.5);

		const auto started = Send(host, 20, Capture("start"), 6.0, 106.0);
		const auto notice = RequirePacket(started, 20, "settlementCaptureState");
		assert(notice.at("status") == "started" && notice.at("raid") == true && notice.at("holdSeconds") == 60.0);
		assert(RequirePacket(started, 10, "settlementCaptureState").at("capturerProfileId") == "profile-raider");

		for (int step = 1; step <= 19; ++step) {
			const double now = 6.0 + 3.0 * step;
			Hold(host, { &raider }, now);
			assert(StatusIn(Send(host, 20, Capture("continue"), now, now + 100.0), 20, "settlementCaptureState") == "holding");
			assert(host.PollSettlementCapture(now).empty());
		}
		Hold(host, { &raider }, 66.0);
		assert(StatusIn(Send(host, 20, Capture("continue"), 66.0, 166.0), 20, "settlementCaptureState") == "holding");
		const auto done = host.PollSettlementCapture(66.0);
		assert(StatusIn(done, 20, "settlementCaptureState") == "captured");
		assert(RequirePacket(done, 20, "settlementCaptureState").at("ownerProfileId") == "profile-raider");
		assert(RequirePacket(done, 10, "settlementCaptureState").at("status") == "captured");
		assert(RequirePacket(done, 10, "settlementCaptureState").at("ownerProfileId") == "profile-raider");
		assert(RequirePacket(done, 10, "settlementCaptureState").at("capturerProfileId") == "profile-raider");
		assert(RequirePacket(done, 20, "settlementAccessState").at("ownerProfileId") == "profile-raider");
		assert(RequirePacket(done, 20, "settlementAccessState").value("protectedUntilUnix", std::uint64_t{ 0 }) > 166);
		assert(RequirePacket(done, 10, "settlementAccessState").at("ownerProfileId") == "profile-raider");
		assert(RequirePacket(Send(host, 10, Json{ { "type", "settlementAccessGet" }, { "buildZoneId", kZone } }, 67.0, 167.0), 10, "settlementAccessState").at("ownerProfileId") == "profile-raider");
		assert(host.PollSettlementCapture(68.0).empty());
		assert(StatusIn(Send(host, 20, Capture("start"), 69.0, 169.0), 20, "settlementCaptureReject") == "alreadyOwner");

		const auto back = Send(host, 10, Capture("start"), 74.0, 174.0);
		assert(StatusIn(back, 10, "settlementCaptureReject") == "workbenchIntact");
	}

	void TestProtectedOwnershipPersistsAndRollsBackTogether()
	{
		const auto directory = TempDirectory("capture-protected-owner");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		SettlementAccessService::Zones zones;
		zones.emplace(kZone, SettlementState{ ZonePolicy() });
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			SettlementAccessService access;
			assert(access.Attach(&gate));
			std::string failure;
			(void)access.SetOwner({}, zones, kZone, "profile-owner", failure, std::nullopt, 1200);
			assert(failure.empty());
			(void)access.SetOwner({}, zones, kZone, "profile-raider", failure, std::nullopt, 9000);
			assert(failure.empty());
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			SettlementAccessService access;
			assert(access.Attach(&gate));
			const auto restored = access.Find(kZone);
			assert(restored && restored->ownerProfileId == "profile-raider" && restored->protectedUntilUnix == 9000);
			std::filesystem::remove_all(directory);
			std::string failure;
			(void)access.SetOwner({}, zones, kZone, "profile-other", failure, std::nullopt, 12000);
			assert(failure == "persistenceUnavailable");
			const auto rolledBack = access.Find(kZone);
			assert(rolledBack && rolledBack->ownerProfileId == "profile-raider" && rolledBack->protectedUntilUnix == 9000);
		}
	}

	void TestContestedHoldResetsUntilRivalLeaves()
	{
		ProtocolHost host{ "Capture Contested" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy()));
		auto owner = Arrive(host, 10, "profile-owner", kBenchX + 100.0);
		auto raider = Arrive(host, 20, "profile-raider", kBenchX + 100.0);
		std::string failure;
		assert(host.SetSettlementOwner(kZone, "profile-owner", failure).size() == 2);
		BreakBench(host, 10, 2.0);
		Hold(host, { &owner, &raider }, 5.5);

		assert(StatusIn(Send(host, 20, Capture("start"), 6.0, 106.0), 20, "settlementCaptureState") == "contested");
		for (int step = 1; step <= 23; ++step) {
			const double now = 6.0 + 3.0 * step;
			Hold(host, { &owner, &raider }, now);
			assert(StatusIn(Send(host, 20, Capture("continue"), now, now + 100.0), 20, "settlementCaptureState") == "contested");
			assert(host.PollSettlementCapture(now).empty());
		}
		host.HandleDisconnected(10, 172.0);
		Hold(host, { &raider }, 77.0);
		assert(StatusIn(host.PollSettlementCapture(77.0), 20, "settlementCaptureState") == "holding");
		for (int step = 1; step <= 20; ++step) {
			const double now = 77.0 + 3.0 * step;
			Hold(host, { &raider }, now);
			assert(StatusIn(Send(host, 20, Capture("continue"), now, now + 100.0), 20, "settlementCaptureState") == "holding");
			if (now < 137.0) {
				assert(host.PollSettlementCapture(now).empty());
			}
		}
		assert(StatusIn(host.PollSettlementCapture(137.0), 20, "settlementCaptureState") == "captured");
	}

	void TestRejectionsAndFailures()
	{
		ProtocolHost host{ "Capture Rejections" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy()));
		Arrive(host, 20, "profile-far", 3000.0);
		Arrive(host, 30, "profile-downed", kBenchX, true);
		Arrive(host, 40, "profile-ok", kBenchX);
		Join(host, 50, "profile-novitals", 1.0, 100.0);

		assert(StatusIn(Send(host, 20, Capture("start"), 2.0, 102.0), 20, "settlementCaptureReject") == "notInRange");
		assert(StatusIn(Send(host, 30, Capture("start"), 2.0, 102.0), 30, "settlementCaptureReject") == "downed");
		assert(StatusIn(Send(host, 50, Capture("start"), 2.0, 102.0), 50, "settlementCaptureReject") == "stalePosition");
		assert(StatusIn(Send(host, 40, Capture("start", 77), 2.0, 102.0), 40, "settlementCaptureReject") == "unknownBuildZone");
		assert(StatusIn(Send(host, 40, Capture("continue"), 2.0, 102.0), 40, "settlementCaptureReject") == "noAttempt");

		const auto before = host.Stats().packetsRejected;
		const auto malformed = Send(host, 40, Json{ { "type", "settlementCapture" }, { "buildZoneId", kZone }, { "op", "toggle" } }, 2.5, 102.5);
		assert(!HasPacket(malformed, 40, "settlementCaptureState") && !HasPacket(malformed, 40, "settlementCaptureReject"));
		assert(host.Stats().packetsRejected == before + 1);

		assert(StatusIn(Send(host, 40, Capture("start"), 3.0, 103.0), 40, "settlementCaptureState") == "started");
		assert(StatusIn(Send(host, 40, Capture("cancel"), 3.5, 103.5), 40, "settlementCaptureState") == "cancelled");
		assert(host.PollSettlementCapture(4.0).empty());

		assert(StatusIn(Send(host, 40, Capture("start"), 4.0, 104.0), 40, "settlementCaptureState") == "started");
		assert(StatusIn(host.PollSettlementCapture(10.0), 40, "settlementCaptureState") == "stalePosition");
		assert(StatusIn(Send(host, 40, Capture("continue"), 10.5, 110.5), 40, "settlementCaptureReject") == "noAttempt");
	}

	void TestEndedContinueNotifiesCapturerAndOwner()
	{
		ProtocolHost host{ "Capture Ended Continue" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy()));
		auto owner = Arrive(host, 10, "profile-owner", 0.0);
		auto raider = Arrive(host, 20, "profile-raider", kBenchX);
		std::string failure;
		assert(host.SetSettlementOwner(kZone, "profile-owner", failure).size() == 2);
		BreakBench(host, 10, 2.0);
		Place(host, owner, 9000.0, 5.5, "teleport");
		Place(host, raider, kBenchX, 5.5);
		assert(StatusIn(Send(host, 20, Capture("start"), 6.0, 106.0), 20, "settlementCaptureState") == "started");
		Place(host, raider, 9000.0, 7.0, "teleport");
		const auto ended = Send(host, 20, Capture("continue"), 7.5, 107.5);
		assert(!HasPacket(ended, 20, "settlementCaptureReject"));
		assert(StatusIn(ended, 20, "settlementCaptureState") == "notInRange");
		assert(StatusIn(ended, 10, "settlementCaptureState") == "notInRange");
	}

	void TestHugeZoneIdIsRejectedWithoutThrowing()
	{
		ProtocolHost host{ "Capture Huge Zone" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy()));
		Arrive(host, 20, "profile-far", 9000.0);
		const auto huge = Json{
			{ "type", "settlementCapture" },
			{ "buildZoneId", std::numeric_limits<std::uint64_t>::max() },
			{ "op", "start" }
		};
		assert(StatusIn(Send(host, 20, huge, 2.0, 102.0), 20, "settlementCaptureReject") == "unknownBuildZone");
	}

	void TestCorruptStoreRejectsCaptureStart()
	{
		const auto directory = TempDirectory("capture-save-failure");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			store.SaveSettlementAccess(Json{ { "not", "a list" } });
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Capture Save Failure" };
			assert(!host.EnableSettlementAccess(&gate));
			assert(host.RegisterWorkshopBuildZone(ZonePolicy()));
			Arrive(host, 20, "profile-raider", kBenchX);
			assert(StatusIn(Send(host, 20, Capture("start"), 2.0, 102.0), 20, "settlementCaptureReject") == "persistenceUnavailable");
			assert(host.PollSettlementCapture(62.0).empty());
			const auto read = Send(host, 20, Json{ { "type", "settlementAccessGet" }, { "buildZoneId", kZone } }, 63.0, 163.0);
			assert(RequirePacket(read, 20, "settlementAccessReject").at("status") == "persistenceUnavailable");
		}
		std::filesystem::remove_all(directory);
	}

	void TestForgedPositionFieldsAreIgnored()
	{
		ProtocolHost host{ "Capture Forged Position" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy()));
		Arrive(host, 20, "profile-far", 9000.0);
		auto forged = Capture("start");
		forged["x"] = kBenchX;
		forged["y"] = 0.0;
		forged["z"] = 0.0;
		forged["workbenchX"] = kBenchX;
		forged["captureRadius"] = 300.0;
		forged["ownerProfileId"] = "profile-far";
		assert(StatusIn(Send(host, 20, forged, 2.0, 102.0), 20, "settlementCaptureReject") == "notInRange");
	}

	void TestZoneRemovalCancelsTheAttempt()
	{
		SettlementCaptureService service;
		SettlementAccessService access;
		SettlementAccessService::Zones zones;
		zones.emplace(kZone, SettlementState{ ZonePolicy() });
		SettlementCaptureService::Peers peers{
			CapturePeer{ 10, Presence{ "profile-owner", true, 0.5, 0.0, 0.0, 0.0, true, true, false } },
			CapturePeer{ 20, Presence{ "profile-raider", true, 0.5, kBenchX, 0.0, 0.0, true, true, false } }
		};
		F4T::PacketRateLimit::TokenBucket bucket;
		const SettlementCaptureService::Ratings ratings{};
		const auto started = service.Dispatch("settlementCapture", 20, "profile-raider", bucket, peers, zones, access, ratings, Capture("start"), 2.0, 102.0, [] { return true; });
		assert(started.has_value());
		assert(StatusIn(*started, 20, "settlementCaptureState") == "started");
		std::string failure;
		(void)access.SetOwner({}, zones, kZone, "profile-owner", failure);
		assert(failure.empty());
		const SettlementAccessService::Zones removed;
		const SettlementCaptureService::CaptureRefusal noRefusal = [](F4T::Workshop::BuildZoneId, const std::string&) { return std::nullopt; };
		const auto cancelled = service.Poll(peers, removed, access, ratings, 3.0, 103.0, noRefusal);
		assert(StatusIn(cancelled, 20, "settlementCaptureState") == "cancelled");
		assert(StatusIn(cancelled, 10, "settlementCaptureState") == "cancelled");
		assert(service.Poll(peers, removed, access, ratings, 4.0, 104.0, noRefusal).empty());
	}

	std::vector<Json> ZonePackets(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		std::vector<Json> packets;
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && DecodePacket(frame).value("type", std::string{}) == "settlementCaptureZones") {
				packets.push_back(DecodePacket(frame));
			}
		}
		return packets;
	}

	void TestCaptureZonesAreSentOnJoinAndWhenTheZoneSetChanges()
	{
		ProtocolHost host{ "Capture Zones" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy()));
		const auto joined = Join(host, 10, "profile-observer", 1.0, 100.0);
		const auto first = ZonePackets(joined.hello, 10);
		assert(first.size() == 1 && first[0].at("zones").size() == 1);
		const auto entry = first[0].at("zones").at(0);
		assert(entry.at("buildZoneId") == kZone && entry.at("x") == kBenchX && entry.at("y") == 0.0 && entry.at("radius") == 300.0);
		assert(ZonePackets(Send(host, 10, Vitals(false), 2.0, 102.0), 10).empty());
		assert(host.RegisterWorkshopBuildZone(BuildZonePolicy{ 10, FormKey{ "Fallout4.esm", 1 }, 0.0F, 0.0F, 0.0F, 5000.0F, 2.0F, 4, true, 1000.0F, 0.0F, 0.0F, 300.0F, {} }));
		const auto extended = ZonePackets(Send(host, 10, Vitals(false), 3.0, 103.0), 10);
		assert(extended.size() == 1 && extended[0].at("zones").size() == 2);
	}

	void TestEveryoneLearnsWhichSettlementsAreBeingCaptured()
	{
		ProtocolHost host{ "Capture Active List" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy()));
		Arrive(host, 30, "profile-observer", 9000.0);
		Arrive(host, 40, "profile-raider", kBenchX);
		const auto active = "settlementCaptureActive";
		assert(!HasPacket(Join(host, 50, "profile-early", 2.0, 102.0).hello, 50, active));

		const auto started = Send(host, 40, Capture("start"), 3.0, 103.0);
		const auto seen = RequirePacket(started, 30, active);
		assert(seen.at("buildZoneIds").size() == 1 && seen.at("buildZoneIds").at(0) == kZone);
		assert(!seen.contains("capturerProfileId"));
		assert(RequirePacket(started, 40, active).at("buildZoneIds").size() == 1);
		assert(!HasPacket(Send(host, 40, Capture("continue"), 3.1, 103.1), 30, active));
		assert(host.PollSettlementCapture(3.2).empty());
		assert(RequirePacket(Join(host, 60, "profile-late", 3.2, 103.2).hello, 60, active).at("buildZoneIds").at(0) == kZone);

		assert(RequirePacket(Send(host, 40, Capture("cancel"), 3.5, 103.5), 30, active).at("buildZoneIds").empty());
		assert(!HasPacket(Join(host, 70, "profile-after", 3.6, 103.6).hello, 70, active));

		assert(StatusIn(Send(host, 40, Capture("start"), 4.0, 104.0), 40, "settlementCaptureState") == "started");
		const auto ended = host.PollSettlementCapture(10.0);
		assert(StatusIn(ended, 40, "settlementCaptureState") == "stalePosition");
		assert(RequirePacket(ended, 30, active).at("buildZoneIds").empty());
		assert(host.PollSettlementCapture(11.0).empty());
	}

	void TestVetoedStartResendsTheLiveAttempt()
	{
		ProtocolHost host{ "Capture Veto Live" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy()));
		auto owner = Arrive(host, 10, "profile-owner", 0.0);
		auto raider = Arrive(host, 20, "profile-raider", kBenchX + 100.0);
		std::string failure;
		assert(host.SetSettlementOwner(kZone, "profile-owner", failure).size() == 2);
		BreakBench(host, 10, 2.0);
		Place(host, owner, 9000.0, 5.5, "teleport");
		Hold(host, { &raider }, 5.5);
		assert(StatusIn(Send(host, 20, Capture("start"), 6.0, 106.0), 20, "settlementCaptureState") == "started");

		const auto refuse = host.Events().Subscribe(ServerEventKind::SettlementAction, [](const ServerEvent&) { return false; });
		const auto refused = Send(host, 20, Capture("start"), 8.0, 108.0);
		assert(StatusIn(refused, 20, "settlementCaptureReject") == "vetoed");
		const auto live = RequirePacket(refused, 20, "settlementCaptureState");
		assert(live.at("status") == "holding" && live.at("capturerProfileId") == "profile-raider" && live.at("ownerProfileId") == "profile-owner");
		assert(host.Events().Unsubscribe(refuse));

		Hold(host, { &raider }, 9.0);
		assert(StatusIn(Send(host, 20, Capture("continue"), 9.0, 109.0), 20, "settlementCaptureState") == "holding");
	}

	void TestCaptureZoneListIsChunkedAndCapped()
	{
		ProtocolHost host{ "Capture Zone Cap" };
		for (std::uint64_t zone = 1; zone <= 1100; ++zone) {
			assert(host.RegisterWorkshopBuildZone(BuildZonePolicy{ zone, FormKey{ "Fallout4.esm", 1 }, 0.0F, 0.0F, 0.0F, 5000.0F, 2.0F, 4, true, 1000.0F, 0.0F, 0.0F, 300.0F, {} }));
		}
		const auto joined = Join(host, 10, "profile-observer", 1.0, 100.0);
		const auto frames = ZonePackets(joined.hello, 10);
		std::size_t listed{};
		for (const auto& packet : frames) {
			assert(packet.at("zones").size() <= 256U);
			listed += packet.at("zones").size();
		}
		assert(frames.size() == 4 && listed == 1024);
	}

	void TestCaptureRadiusIsClampedToSixThousand()
	{
		ProtocolHost host{ "Capture Radius Clamp" };
		assert(host.RegisterWorkshopBuildZone(BuildZonePolicy{ kZone, FormKey{ "Fallout4.esm", 1 }, 0.0F, 0.0F, 0.0F, 5000.0F, 2.0F, 4, true, 1000.0F, 0.0F, 0.0F, 23655.0F, {} }));
		Arrive(host, 20, "profile-far", 8000.0);
		assert(StatusIn(Send(host, 20, Capture("start"), 2.0, 102.0), 20, "settlementCaptureReject") == "notInRange");
		Arrive(host, 30, "profile-near", 6500.0);
		assert(StatusIn(Send(host, 30, Capture("start"), 2.0, 102.0), 30, "settlementCaptureState") == "started");
	}

	void TestRateLimitAllowsSixThenRefillsTwoPerSecond()
	{
		ProtocolHost host{ "Capture Rate" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy()));
		Arrive(host, 40, "profile-ok", kBenchX);
		for (int index = 0; index < 6; ++index) {
			assert(StatusIn(Send(host, 40, Capture("continue"), 50.0, 150.0), 40, "settlementCaptureReject") == "noAttempt");
		}
		assert(StatusIn(Send(host, 40, Capture("continue"), 50.0, 150.0), 40, "settlementCaptureReject") == "rateLimited");
		assert(StatusIn(Send(host, 40, Capture("continue"), 51.0, 151.0), 40, "settlementCaptureReject") == "noAttempt");
	}

	void TestClaimKillsGuardsAndNewOwnerRecoversThem()
	{
		StockedInventory stock{ "claim-guards", 100 };
		ProtocolHost host{ "Capture Claim Guards" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.RegisterWorkshopBuildZone(ZonePolicy()));
		Arrive(host, 10, "profile-owner", 0.0, false, InventoryCapabilities());
		auto raider = Arrive(host, 20, "profile-raider", kBenchX + 100.0);
		std::string failure;
		assert(host.SetSettlementOwner(kZone, "profile-owner", failure).size() == 2);
		const auto recruited = RecruitAcked(host, stock, 10, kZone, 2.0);
		const auto mininuke = Json{ { "type", "structureHit" }, { "buildZoneId", kZone }, { "objectId", std::uint64_t{ 0 } }, { "explosive", "mininuke" } };
		EquipMininuke(host, 10, 2.9);
		for (const double now : { 3.0, 4.5, 6.0, 7.5 }) {
			assert(!Send(host, 10, mininuke, now, now + 100.0).empty());
		}
		const auto settlerId = RequirePacket(recruited, 10, "settlerState").at("settlerId").get<std::uint64_t>();
		assert(!Send(host, 10, Json{ { "type", "settlerAssign" }, { "buildZoneId", kZone }, { "settlerId", settlerId }, { "assignment", "guard" } }, 7.7, 107.7).empty());
		const auto recover = [&](std::uint32_t a_connectionId, double a_monotonic) {
			return Send(host, a_connectionId, Json{ { "type", "settlerRecover" }, { "buildZoneId", kZone }, { "settlerId", settlerId } }, a_monotonic, a_monotonic + 100.0);
		};
		assert(recover(10, 7.8).empty());
		Hold(host, { &raider }, 8.0);
		const auto started = Send(host, 20, Capture("start"), 8.1, 108.1);
		const auto notice = RequirePacket(started, 20, "settlementCaptureState");
		assert(notice.at("status") == "started");
		const double hold = notice.at("holdSeconds").get<double>();
		for (double now = 11.1; now < 8.1 + hold - 1.0; now += 3.0) {
			Hold(host, { &raider }, now);
			assert(StatusIn(Send(host, 20, Capture("continue"), now, now + 100.0), 20, "settlementCaptureState") == "holding");
			assert(host.PollSettlementCapture(now).empty());
		}
		const double done = 8.1 + hold + 1.0;
		Hold(host, { &raider }, done);
		assert(StatusIn(Send(host, 20, Capture("continue"), done, done + 100.0), 20, "settlementCaptureState") == "holding");
		const auto claimed = host.PollSettlementCapture(done, done + 100.0);
		assert(StatusIn(claimed, 20, "settlementCaptureState") == "captured");
		assert(RequirePacket(claimed, 20, "settlerState").at("dead").get<bool>());
		assert(RequirePacket(recover(20, done + 1.0), 20, "settlerState").at("assignment") == "idle");
	}

	void TestSettlerRecruitChargesOnceAndMintsAfterAck()
	{
		StockedInventory stock{ "recruit-ack", 100 };
		ProtocolHost host{ "Settler Recruit Ack" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.RegisterWorkshopBuildZone(ZonePolicy()));
		Arrive(host, 10, "profile-owner", 0.0, false, InventoryCapabilities());
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		const auto requested = Send(host, 10, Json{ { "type", "settlerRecruit" }, { "buildZoneId", kZone } }, 2.0, 102.0);
		assert(FindFrame(requested, 10, "settlerState") == nullptr);
		const auto charge = RequirePacket(requested, 10, "settlementCharge");
		assert(charge.at("purpose") == "settlerRecruit");
		const auto operationId = charge.at("operationId").get<std::uint64_t>();
		const auto flushed = host.FlushWorkshopPersistence(1102.0, 8.0);
		const auto resent = FindFrame(flushed, 10, "settlementCharge");
		assert(resent != nullptr && DecodePacket(*resent).at("operationId").get<std::uint64_t>() == operationId);
		assert(VerifyStartingGrant(host, stock.gate, 10, "profile-owner", 8.4, 108.4));
		(void)ConfirmChargeConsumed(host, 10, stock.store, "profile-owner", charge, 8.45);
		const auto acknowledged = Send(host, 10, Json{ { "type", "settlementChargeResult" }, { "operationId", operationId }, { "ok", true } }, 8.5, 108.5);
		assert(RequirePacket(acknowledged, 10, "settlerState").at("buildZoneId") == kZone);
		assert(Send(host, 10, Json{ { "type", "settlementChargeResult" }, { "operationId", operationId }, { "ok", true } }, 8.6, 108.6).empty());
		const auto settled = host.FlushWorkshopPersistence(1109.0, 9.0);
		assert(FindFrame(settled, 10, "settlementCharge") == nullptr);
	}

	void TestSettlerRecruitRefusesWithoutMaterials()
	{
		ProtocolHost host{ "Settler Recruit Materials" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy()));
		Arrive(host, 10, "profile-owner", 0.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		const auto refused = Send(host, 10, Json{ { "type", "settlerRecruit" }, { "buildZoneId", kZone } }, 2.0, 102.0);
		assert(FindFrame(refused, 10, "settlementCharge") == nullptr);
		assert(FindFrame(refused, 10, "settlerState") == nullptr);
		assert(RequirePacket(refused, 10, "settlementAccessReject").at("status") == "insufficientMaterials");
	}

	void TestSettlerRecruitRefusesNonOwnerAndFailedCommitMintsNothing()
	{
		StockedInventory stock{ "recruit-refusal", 100 };
		ProtocolHost host{ "Settler Recruit Refusal" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.RegisterWorkshopBuildZone(ZonePolicy()));
		Arrive(host, 10, "profile-owner", 0.0);
		Arrive(host, 20, "profile-stranger", 0.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		const auto stranger = Send(host, 20, Json{ { "type", "settlerRecruit" }, { "buildZoneId", kZone } }, 2.0, 102.0);
		assert(FindFrame(stranger, 20, "settlementCharge") == nullptr);
		assert(RequirePacket(stranger, 20, "settlementAccessReject").at("status") == "notOwner");
		const auto requested = Send(host, 10, Json{ { "type", "settlerRecruit" }, { "buildZoneId", kZone } }, 2.1, 102.1);
		const auto operationId = RequirePacket(requested, 10, "settlementCharge").at("operationId").get<std::uint64_t>();
		const auto failed = Send(host, 10, Json{ { "type", "settlementChargeResult" }, { "operationId", operationId }, { "ok", false }, { "reason", "missingMaterials" } }, 2.2, 102.2);
		assert(FindFrame(failed, 10, "settlerState") == nullptr);
		assert(RequirePacket(failed, 10, "settlementAccessReject").at("status") == "insufficientMaterials");
		assert(Send(host, 10, Json{ { "type", "settlementChargeResult" }, { "operationId", operationId }, { "ok", true } }, 2.3, 102.3).empty());
	}
}

int main()
{
	TestUncontestedHoldTransfersOwnership();
	TestProtectedOwnershipPersistsAndRollsBackTogether();
	TestContestedHoldResetsUntilRivalLeaves();
	TestRejectionsAndFailures();
	TestEndedContinueNotifiesCapturerAndOwner();
	TestForgedPositionFieldsAreIgnored();
	TestHugeZoneIdIsRejectedWithoutThrowing();
	TestCorruptStoreRejectsCaptureStart();
	TestZoneRemovalCancelsTheAttempt();
	TestRateLimitAllowsSixThenRefillsTwoPerSecond();
	TestCaptureRadiusIsClampedToSixThousand();
	TestCaptureZonesAreSentOnJoinAndWhenTheZoneSetChanges();
	TestCaptureZoneListIsChunkedAndCapped();
	TestEveryoneLearnsWhichSettlementsAreBeingCaptured();
	TestVetoedStartResendsTheLiveAttempt();
	TestClaimKillsGuardsAndNewOwnerRecoversThem();
	TestSettlerRecruitChargesOnceAndMintsAfterAck();
	TestSettlerRecruitRefusesWithoutMaterials();
	TestSettlerRecruitRefusesNonOwnerAndFailedCommitMintsNothing();
	return 0;
}

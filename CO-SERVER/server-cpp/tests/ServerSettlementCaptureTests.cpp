#include "ServerFactionTestSupport.h"

#include "ServerSettlementAccess.h"
#include "ServerSettlementCapture.h"

#include <cstdint>
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
	using CommonwealthOnline::Server::SettlementAccessService;
	using CommonwealthOnline::Server::SettlementCaptureService;

	constexpr std::uint64_t kZone = 9;
	constexpr double kBenchX = 1000.0;

	BuildZonePolicy ZonePolicy()
	{
		return BuildZonePolicy{ kZone, FormKey{ "Fallout4.esm", 1 }, 0.0F, 0.0F, 0.0F, 5000.0F, 2.0F, 4, true, 1000.0F, 0.0F, 0.0F, 300.0F };
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

	Player Arrive(ProtocolHost& a_host, std::uint32_t a_connectionId, const std::string& a_profileId, double a_x, bool a_downed = false)
	{
		Join(a_host, a_connectionId, a_profileId, 1.0, 100.0);
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

	void TestUncontestedHoldTransfersOwnership()
	{
		ProtocolHost host{ "Capture Hold" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy()));
		Arrive(host, 10, "profile-owner", 9000.0);
		auto raider = Arrive(host, 20, "profile-raider", kBenchX + 100.0);
		std::string failure;
		assert(host.SetSettlementOwner(kZone, "profile-owner", failure).size() == 2);

		const auto started = Send(host, 20, Capture("start"), 2.0, 102.0);
		const auto notice = RequirePacket(started, 20, "settlementCaptureState");
		assert(notice.at("status") == "started" && notice.at("raid") == true && notice.at("holdSeconds") == 60.0);
		assert(RequirePacket(started, 10, "settlementCaptureState").at("capturerProfileId") == "profile-raider");

		for (int step = 1; step <= 19; ++step) {
			const double now = 2.0 + 3.0 * step;
			Hold(host, { &raider }, now);
			assert(StatusIn(Send(host, 20, Capture("continue"), now, now + 100.0), 20, "settlementCaptureState") == "holding");
			assert(host.PollSettlementCapture(now).empty());
		}
		Hold(host, { &raider }, 62.0);
		assert(StatusIn(Send(host, 20, Capture("continue"), 62.0, 162.0), 20, "settlementCaptureState") == "holding");
		const auto done = host.PollSettlementCapture(62.0);
		assert(StatusIn(done, 20, "settlementCaptureState") == "captured");
		assert(RequirePacket(done, 20, "settlementAccessState").at("ownerProfileId") == "profile-raider");
		assert(RequirePacket(done, 10, "settlementAccessState").at("ownerProfileId") == "profile-raider");
		assert(RequirePacket(Send(host, 10, Json{ { "type", "settlementAccessGet" }, { "buildZoneId", kZone } }, 63.0, 163.0), 10, "settlementAccessState").at("ownerProfileId") == "profile-raider");
		assert(host.PollSettlementCapture(64.0).empty());
		assert(StatusIn(Send(host, 20, Capture("start"), 65.0, 165.0), 20, "settlementCaptureReject") == "alreadyOwner");

		const auto back = Send(host, 10, Capture("start"), 70.0, 170.0);
		assert(StatusIn(back, 10, "settlementCaptureReject") == "stalePosition");
	}

	void TestContestedHoldResetsUntilRivalLeaves()
	{
		ProtocolHost host{ "Capture Contested" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy()));
		auto owner = Arrive(host, 10, "profile-owner", kBenchX + 100.0);
		auto raider = Arrive(host, 20, "profile-raider", kBenchX + 100.0);
		std::string failure;
		assert(host.SetSettlementOwner(kZone, "profile-owner", failure).size() == 2);

		assert(StatusIn(Send(host, 20, Capture("start"), 2.0, 102.0), 20, "settlementCaptureState") == "contested");
		for (int step = 1; step <= 23; ++step) {
			const double now = 2.0 + 3.0 * step;
			Hold(host, { &owner, &raider }, now);
			assert(StatusIn(Send(host, 20, Capture("continue"), now, now + 100.0), 20, "settlementCaptureState") == "contested");
			assert(host.PollSettlementCapture(now).empty());
		}
		host.HandleDisconnected(10, 172.0);
		Hold(host, { &raider }, 73.0);
		assert(StatusIn(host.PollSettlementCapture(73.0), 20, "settlementCaptureState") == "holding");
		for (int step = 1; step <= 20; ++step) {
			const double now = 73.0 + 3.0 * step;
			Hold(host, { &raider }, now);
			assert(StatusIn(Send(host, 20, Capture("continue"), now, now + 100.0), 20, "settlementCaptureState") == "holding");
			if (now < 133.0) {
				assert(host.PollSettlementCapture(now).empty());
			}
		}
		assert(StatusIn(host.PollSettlementCapture(133.0), 20, "settlementCaptureState") == "captured");
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
		Arrive(host, 10, "profile-owner", 9000.0);
		auto raider = Arrive(host, 20, "profile-raider", kBenchX);
		std::string failure;
		assert(host.SetSettlementOwner(kZone, "profile-owner", failure).size() == 2);
		assert(StatusIn(Send(host, 20, Capture("start"), 2.0, 102.0), 20, "settlementCaptureState") == "started");
		Place(host, raider, 9000.0, 3.0, "teleport");
		const auto ended = Send(host, 20, Capture("continue"), 3.5, 103.5);
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
		const auto started = service.Dispatch("settlementCapture", 20, "profile-raider", bucket, peers, zones, access, Capture("start"), 2.0);
		assert(started.has_value());
		assert(StatusIn(*started, 20, "settlementCaptureState") == "started");
		std::string failure;
		(void)access.SetOwner({}, zones, kZone, "profile-owner", failure);
		assert(failure.empty());
		const SettlementAccessService::Zones removed;
		const auto cancelled = service.Poll(peers, removed, access, 3.0);
		assert(StatusIn(cancelled, 20, "settlementCaptureState") == "cancelled");
		assert(StatusIn(cancelled, 10, "settlementCaptureState") == "cancelled");
		assert(service.Poll(peers, removed, access, 4.0).empty());
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
		assert(host.RegisterWorkshopBuildZone(BuildZonePolicy{ 10, FormKey{ "Fallout4.esm", 1 }, 0.0F, 0.0F, 0.0F, 5000.0F, 2.0F, 4, true, 1000.0F, 0.0F, 0.0F, 300.0F }));
		const auto extended = ZonePackets(Send(host, 10, Vitals(false), 3.0, 103.0), 10);
		assert(extended.size() == 1 && extended[0].at("zones").size() == 2);
	}

	void TestCaptureZoneListIsChunkedAndCapped()
	{
		ProtocolHost host{ "Capture Zone Cap" };
		for (std::uint64_t zone = 1; zone <= 1100; ++zone) {
			assert(host.RegisterWorkshopBuildZone(BuildZonePolicy{ zone, FormKey{ "Fallout4.esm", 1 }, 0.0F, 0.0F, 0.0F, 5000.0F, 2.0F, 4, true, 1000.0F, 0.0F, 0.0F, 300.0F }));
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
		assert(host.RegisterWorkshopBuildZone(BuildZonePolicy{ kZone, FormKey{ "Fallout4.esm", 1 }, 0.0F, 0.0F, 0.0F, 5000.0F, 2.0F, 4, true, 1000.0F, 0.0F, 0.0F, 23655.0F }));
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
}

int main()
{
	TestUncontestedHoldTransfersOwnership();
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
	return 0;
}

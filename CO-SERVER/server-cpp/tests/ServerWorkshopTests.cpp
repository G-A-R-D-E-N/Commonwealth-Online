#include "DurableStateFileStore.h"
#include "PersistenceGate.h"
#include "ServerProtocol.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::Delivery;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolHost;
	using F4T::Workshop::BuildZonePolicy;
	using F4T::Workshop::FormKey;

	std::vector<std::uint8_t> Encode(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	Json Decode(const OutboundFrame& a_frame)
	{
		return Json::parse(std::string{
			reinterpret_cast<const char*>(a_frame.payload.data()),
			a_frame.payload.size()
		});
	}

	const OutboundFrame* Find(
		const std::vector<OutboundFrame>& a_frames,
		std::uint32_t a_connectionId,
		std::string_view a_type)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && !frame.payload.empty() && frame.payload.front() == '{' &&
				Decode(frame).value("type", std::string{}) == a_type) {
				return &frame;
			}
		}
		return nullptr;
	}

	Json Place(std::uint64_t a_operationId, std::uint64_t a_revision, std::uint64_t a_zoneId, double a_x)
	{
		return Json{
			{ "type", "workshopOp" },
			{ "operationId", a_operationId },
			{ "expectedRevision", a_revision },
			{ "buildZoneId", a_zoneId },
			{ "kind", "place" },
			{ "form", Json{ { "plugin", "Fallout4.esm" }, { "localFormId", 0x1234 } } },
			{ "space", Json{ { "plugin", "Fallout4.esm" }, { "localFormId", 1 } } },
			{ "transform", Json{
				{ "x", a_x },
				{ "y", 0.0 },
				{ "z", 0.0 },
				{ "pitch", 0.0 },
				{ "yaw", 0.0 },
				{ "roll", 0.0 },
				{ "scale", 1.0 }
			} }
		};
	}

	Json Move(
		std::uint64_t a_operationId,
		std::uint64_t a_revision,
		std::uint64_t a_zoneId,
		std::uint64_t a_objectId,
		double a_x)
	{
		return Json{
			{ "type", "workshopOp" },
			{ "operationId", a_operationId },
			{ "expectedRevision", a_revision },
			{ "buildZoneId", a_zoneId },
			{ "kind", "move" },
			{ "objectId", a_objectId },
			{ "space", Json{ { "plugin", "Fallout4.esm" }, { "localFormId", 1 } } },
			{ "transform", Json{
				{ "x", a_x },
				{ "y", 0.0 },
				{ "z", 0.0 },
				{ "pitch", 0.0 },
				{ "yaw", 0.0 },
				{ "roll", 0.0 },
				{ "scale", 1.0 }
			} }
		};
	}

	Json Scrap(
		std::uint64_t a_operationId,
		std::uint64_t a_revision,
		std::uint64_t a_zoneId,
		std::uint64_t a_objectId)
	{
		return Json{
			{ "type", "workshopOp" },
			{ "operationId", a_operationId },
			{ "expectedRevision", a_revision },
			{ "buildZoneId", a_zoneId },
			{ "kind", "scrap" },
			{ "objectId", a_objectId }
		};
	}

	Json ScrapWorldOp(
		std::uint64_t a_operationId,
		std::uint64_t a_revision,
		std::uint64_t a_zoneId,
		std::uint32_t a_formId)
	{
		return Json{
			{ "type", "workshopOp" },
			{ "operationId", a_operationId },
			{ "expectedRevision", a_revision },
			{ "buildZoneId", a_zoneId },
			{ "kind", "scrapWorld" },
			{ "form", Json{ { "plugin", "Fallout4.esm" }, { "localFormId", a_formId } } },
			{ "space", Json{ { "plugin", "Fallout4.esm" }, { "localFormId", 1 } } },
			{ "transform", Json{
				{ "x", 2.0 },
				{ "y", 0.0 },
				{ "z", 0.0 },
				{ "pitch", 0.0 },
				{ "yaw", 0.0 },
				{ "roll", 0.0 },
				{ "scale", 1.0 }
			} }
		};
	}

	Json TransformAt(double a_x, const char* a_movementType)
	{
		return Json{
			{ "type", "transform" },
			{ "cellId", "CAFE" },
			{ "worldspaceId", "F00D" },
			{ "x", a_x },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 },
			{ "movementType", a_movementType }
		};
	}

	void Join(ProtocolHost& a_host, std::uint32_t a_connectionId, const char* a_profile, double a_monotonic)
	{
		a_host.HandleConnected(a_connectionId, a_monotonic);
		assert(!a_host.HandleMessage(a_connectionId, Encode(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), a_monotonic + 0.1, a_monotonic + 1000.1).empty());
		a_host.BindSessionProfile(a_connectionId, a_profile);
	}

	std::vector<OutboundFrame> Send(
		ProtocolHost& a_host,
		std::uint32_t a_connectionId,
		const Json& a_packet,
		double a_monotonic)
	{
		return a_host.HandleMessage(a_connectionId, Encode(a_packet), a_monotonic, a_monotonic + 1000.0);
	}

	void TestCatchUpAndBroadcast()
	{
		ProtocolHost host{ "Workshop Catch Up" };
		assert(host.RegisterWorkshopBuildZone(BuildZonePolicy{ 9, FormKey{ "Fallout4.esm", 1 }, 0.0F, 0.0F, 0.0F, 10.0F, 2.0F, 8 }));
		Join(host, 100, "profile-builder", 1.0);
		Join(host, 101, "profile-inside", 1.0);
		Join(host, 102, "profile-late", 1.0);
		assert(Find(Send(host, 100, TransformAt(0.0, "spawn"), 2.0), 100, "workshopSnapshot") != nullptr);
		assert(Find(Send(host, 101, TransformAt(0.0, "spawn"), 2.0), 101, "workshopSnapshot") != nullptr);

		const auto built = Send(host, 100, Place(1, 0, 9, 1.0), 3.0);
		assert(Find(built, 100, "workshopCommit") != nullptr);
		const auto* peerCommit = Find(built, 101, "workshopCommit");
		assert(peerCommit != nullptr);
		assert(Decode(*peerCommit).at("revision") == 1);
		assert(Decode(*peerCommit).at("playerId") == Decode(*Find(built, 100, "workshopCommit")).at("playerId"));
		assert(Find(built, 102, "workshopCommit") == nullptr);

		const auto stale1 = Send(host, 102, Place(1, 0, 9, 2.0), 4.0);
		assert(Decode(*Find(stale1, 102, "workshopReject")).at("status") == "staleRevision");
		const auto* catchUp = Find(stale1, 102, "workshopSnapshot");
		assert(catchUp != nullptr);
		assert(Decode(*catchUp).at("revision") == 1);
		assert(Decode(*catchUp).at("objects").size() == 1);
		assert(Find(stale1, 100, "workshopSnapshot") == nullptr && Find(stale1, 101, "workshopSnapshot") == nullptr);

		assert(Find(Send(host, 102, Place(2, 0, 9, 2.0), 4.1), 102, "workshopSnapshot") != nullptr);
		const auto limited = Send(host, 102, Place(3, 0, 9, 2.0), 4.2);
		assert(Find(limited, 102, "workshopReject") != nullptr);
		assert(Find(limited, 102, "workshopSnapshot") == nullptr);
		assert(Find(Send(host, 102, Place(4, 0, 9, 2.0), 6.3), 102, "workshopSnapshot") != nullptr);

		const auto caughtUp = Send(host, 102, Place(5, 1, 9, 2.0), 6.4);
		assert(Decode(*Find(caughtUp, 102, "workshopCommit")).at("revision") == 2);
		assert(Find(caughtUp, 100, "workshopCommit") != nullptr);
		assert(Find(caughtUp, 101, "workshopCommit") != nullptr);

		const auto replayed = Send(host, 102, Place(5, 1, 9, 2.0), 6.5);
		assert(Find(replayed, 102, "workshopCommit") != nullptr);
		assert(Find(replayed, 100, "workshopCommit") == nullptr);
		assert(Find(replayed, 101, "workshopCommit") == nullptr);

		(void)Send(host, 101, TransformAt(500.0, "normal"), 7.0);
		const auto afterLeaving = Send(host, 100, Place(2, 2, 9, 3.0), 7.1);
		assert(Find(afterLeaving, 100, "workshopCommit") != nullptr);
		assert(Find(afterLeaving, 101, "workshopCommit") == nullptr);

		const auto runtimeScrap = Send(host, 100, ScrapWorldOp(3, 3, 9, 0x01000000), 7.2);
		assert(Decode(*Find(runtimeScrap, 100, "workshopReject")).at("status") == "invalidRequest");
		assert(Find(runtimeScrap, 101, "workshopReject") == nullptr);
	}

	void TestPersistenceAndZoneFile()
	{
		const auto directory = std::filesystem::temp_directory_path() /
			("co-server-workshop-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(directory);
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		const BuildZonePolicy policy{ 9, FormKey{ "Fallout4.esm", 1 }, 0.0F, 0.0F, 0.0F, 10.0F, 2.0F, 8 };
		using CommonwealthOnline::Server::DurableStateFileStore;
		using CommonwealthOnline::Server::PersistenceGate;
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Workshop Persist" };
			assert(host.EnableWorkshopPersistence(&gate));
			assert(!host.EnableWorkshopPersistence(&gate));
			assert(host.RegisterWorkshopBuildZone(policy));
			Join(host, 10, "profile-a", 1.0);
			assert(Find(Send(host, 10, Place(1, 0, 9, 1.0), 2.0), 10, "workshopCommit") != nullptr);
			assert(Find(Send(host, 10, ScrapWorldOp(2, 1, 9, 0xABCD), 2.1), 10, "workshopCommit") != nullptr);
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Workshop Persist" };
			assert(host.RegisterWorkshopBuildZone(policy));
			assert(host.EnableWorkshopPersistence(&gate));
			Join(host, 20, "profile-b", 1.0);
			const auto entered = Send(host, 20, TransformAt(0.0, "spawn"), 2.0);
			const auto* snapshot = Find(entered, 20, "workshopSnapshot");
			assert(snapshot != nullptr);
			assert(Decode(*snapshot).at("revision") == 2);
			assert(Decode(*snapshot).at("objects").size() == 1);
			assert(Decode(*snapshot).at("tombstones").size() == 1);
			const auto next = Send(host, 20, Place(1, 2, 9, 2.0), 2.1);
			assert(Decode(*Find(next, 20, "workshopCommit")).at("objectId") == 3);
			assert(Decode(*Find(next, 20, "workshopCommit")).at("revision") == 3);
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Workshop Persist" };
			assert(host.EnableWorkshopPersistence(&gate));
			assert(host.RegisterWorkshopBuildZone(policy));
			Join(host, 30, "profile-c", 1.0);
			const auto spawnFrames = Send(host, 30, TransformAt(0.0, "spawn"), 2.0);
			const auto* snapshot = Find(spawnFrames, 30, "workshopSnapshot");
			assert(snapshot != nullptr);
			assert(Decode(*snapshot).at("revision") == 3);
			assert(Decode(*snapshot).at("objects").size() == 2);
			store.SaveWorkshop(Json{ { "version", 1 }, { "zones", Json{ { "9", Json{ { "revision", "bad" } } } } } });
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Workshop Persist" };
			assert(host.EnableWorkshopPersistence(&gate));
			assert(host.RegisterWorkshopBuildZone(policy));
			Join(host, 40, "profile-d", 1.0);
			const auto spawnFrames = Send(host, 40, TransformAt(0.0, "spawn"), 2.0);
			const auto* snapshot = Find(spawnFrames, 40, "workshopSnapshot");
			assert(snapshot != nullptr);
			assert(Decode(*snapshot).at("revision") == 0);
			assert(!Send(host, 40, Place(1, 0, 9, 1.0), 2.1).empty());
			store.WaitForCommits();
			assert(store.GetWorkshop().at("backup").at("9").at("revision") == "bad");
			assert(store.GetWorkshop().at("zones").at("9").at("objects").size() == 1);
		}
		{
			ProtocolHost host{ "Workshop Zone File" };
			const auto zones = directory / "workshop-zones.json";
			assert(host.ConfigureWorkshop(nullptr, zones) == 0);
			{
				std::ofstream output(zones, std::ios::binary);
				output << R"({"version":1,"zones":[{"buildZoneId":4,"space":{"plugin":"Fallout4.esm","localFormId":1},"centerX":0,"centerY":0,"centerZ":0,"radius":50,"maxScale":2,"maxObjects":8},{"buildZoneId":5,"space":{"plugin":"Fallout4.esm","localFormId":1},"centerX":500,"centerY":0,"centerZ":0,"radius":50,"maxScale":2,"maxObjects":8}]})";
			}
			assert(host.ConfigureWorkshop(nullptr, zones) == 2);
			Join(host, 50, "profile-e", 1.0);
			assert(Find(Send(host, 50, Place(1, 0, 4, 1.0), 2.0), 50, "workshopCommit") != nullptr);
			assert(Find(Send(host, 50, Place(2, 0, 5, 501.0), 2.1), 50, "workshopCommit") != nullptr);
			assert(host.ConfigureWorkshop(nullptr, zones) == 0);
			{
				std::ofstream output(zones, std::ios::binary | std::ios::trunc);
				output << R"({"version":1,"zones":[{"buildZoneId":6}]})";
			}
			ProtocolHost invalid{ "Workshop Zone File Invalid" };
			assert(invalid.ConfigureWorkshop(nullptr, zones) == 0);
			Join(invalid, 60, "profile-f", 1.0);
			assert(Decode(*Find(Send(invalid, 60, Place(1, 0, 6, 1.0), 2.0), 60, "workshopReject")).at("status") == "unknownBuildZone");
			{
				std::ofstream output(zones, std::ios::binary | std::ios::trunc);
				output << "not json";
			}
			assert(invalid.ConfigureWorkshop(nullptr, zones) == 0);
		}
		std::filesystem::remove_all(directory);
	}
}

int main()
{
	using CommonwealthOnline::Server::ProtocolHost;
	using F4T::Workshop::BuildZonePolicy;
	using F4T::Workshop::FormKey;

	ProtocolHost host{ "Workshop Test" };
	const BuildZonePolicy zone{
		7,
		FormKey{ "Fallout4.esm", 1 },
		0.0F,
		0.0F,
		0.0F,
		100.0F,
		2.0F,
		4
	};
	assert(host.RegisterWorkshopBuildZone(zone));
	assert(!host.RegisterWorkshopBuildZone(zone));
	BuildZonePolicy secondZone = zone;
	secondZone.buildZoneId = 8;
	assert(host.RegisterWorkshopBuildZone(secondZone));

	const auto welcomeA = host.HandleConnected(10, 100.0);
	const auto playerA = Decode(welcomeA.front()).at("playerId").get<std::uint32_t>();
	assert(!host.HandleMessage(10, Encode(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 1.0, 100.1).empty());
	host.BindSessionProfile(10, "profile-a");

	const auto welcomeB = host.HandleConnected(20, 100.2);
	const auto playerB = Decode(welcomeB.front()).at("playerId").get<std::uint32_t>();
	assert(playerA != playerB);
	assert(!host.HandleMessage(20, Encode(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 1.1, 100.3).empty());
	host.BindSessionProfile(20, "profile-b");

	const auto placedA = host.HandleMessage(10, Encode(Place(1, 0, 7, 10.0)), 2.0, 101.0);
	const auto* placeAForA = Find(placedA, 10, "workshopCommit");
	assert(placeAForA != nullptr);
	assert(Find(placedA, 20, "workshopCommit") == nullptr);
	assert(placeAForA->delivery == Delivery::ReliableOrdered);
	const auto placeACommit = Decode(*placeAForA);
	assert(placeACommit.at("playerId") == playerA);
	assert(placeACommit.at("revision") == 1);
	assert(placeACommit.at("objectId") == 1);
	assert(placeACommit.at("transform").at("x") == 10.0);

	const auto replayA = host.HandleMessage(10, Encode(Place(1, 0, 7, 10.0)), 2.1, 101.1);
	assert(Find(replayA, 10, "workshopCommit") != nullptr);
	assert(Find(replayA, 20, "workshopCommit") == nullptr);

	const auto conflictingReplayA = host.HandleMessage(10, Encode(Place(1, 1, 7, 11.0)), 2.15, 101.15);
	const auto* conflictingReplayReject = Find(conflictingReplayA, 10, "workshopReject");
	assert(conflictingReplayReject != nullptr);
	assert(Find(conflictingReplayA, 10, "workshopCommit") == nullptr);
	assert(Find(conflictingReplayA, 20, "workshopCommit") == nullptr);
	assert(Decode(*conflictingReplayReject).at("status") == "invalidRequest");

	const auto crossZoneReuse = host.HandleMessage(10, Encode(Place(1, 0, 8, 5.0)), 2.16, 101.16);
	const auto* crossZoneReject = Find(crossZoneReuse, 10, "workshopReject");
	assert(crossZoneReject != nullptr);
	assert(Decode(*crossZoneReject).at("status") == "invalidRequest");
	assert(Find(crossZoneReuse, 20, "workshopCommit") == nullptr);

	const auto unknownFirst = host.HandleMessage(10, Encode(Place(200, 0, 98, 5.0)), 2.17, 101.17);
	const auto* unknownFirstReject = Find(unknownFirst, 10, "workshopReject");
	assert(unknownFirstReject != nullptr);
	assert(Decode(*unknownFirstReject).at("status") == "unknownBuildZone");

	BuildZonePolicy lateZone = zone;
	lateZone.buildZoneId = 98;
	assert(host.RegisterWorkshopBuildZone(lateZone));

	const auto unknownReplay = host.HandleMessage(10, Encode(Place(200, 0, 98, 5.0)), 2.18, 101.18);
	assert(Find(unknownReplay, 10, "workshopReject") == nullptr);
	assert(Find(unknownReplay, 10, "workshopCommit") != nullptr);

	const auto lateZonePlace = host.HandleMessage(10, Encode(Place(201, 1, 98, 5.0)), 2.19, 101.19);
	assert(Find(lateZonePlace, 10, "workshopCommit") != nullptr);

	const auto placedB = host.HandleMessage(20, Encode(Place(1, 1, 7, 20.0)), 2.2, 101.2);
	const auto* placeBForB = Find(placedB, 20, "workshopCommit");
	assert(placeBForB != nullptr);
	assert(Find(placedB, 10, "workshopCommit") == nullptr);
	const auto placeBCommit = Decode(*placeBForB);
	assert(placeBCommit.at("playerId") == playerB);
	assert(placeBCommit.at("revision") == 2);
	assert(placeBCommit.at("objectId") == 2);

	const auto stale = host.HandleMessage(10, Encode(Move(2, 1, 7, 1, 30.0)), 2.3, 101.3);
	const auto* staleReject = Find(stale, 10, "workshopReject");
	assert(staleReject != nullptr);
	assert(Find(stale, 20, "workshopReject") == nullptr);
	assert(Decode(*staleReject).at("status") == "staleRevision");
	assert(Decode(*staleReject).at("revision") == 2);

	const auto moved = host.HandleMessage(10, Encode(Move(3, 2, 7, 1, 40.0)), 2.4, 101.4);
	const auto* movedForA = Find(moved, 10, "workshopCommit");
	assert(movedForA != nullptr);
	assert(Find(moved, 20, "workshopCommit") == nullptr);
	assert(Decode(*movedForA).at("revision") == 3);
	assert(Decode(*movedForA).at("transform").at("x") == 40.0);

	const auto outside = host.HandleMessage(10, Encode(Move(4, 3, 7, 1, 101.0)), 2.5, 101.5);
	const auto* outsideReject = Find(outside, 10, "workshopReject");
	assert(outsideReject != nullptr);
	assert(Decode(*outsideReject).at("status") == "outsideBuildZone");

	host.SetWorkshopAuthority([](const std::string& a_profile, std::uint64_t) { return a_profile != "profile-b"; });
	const auto foreignMove = host.HandleMessage(20, Encode(Move(7, 3, 7, 1, 50.0)), 2.55, 101.55);
	const auto* foreignMoveReject = Find(foreignMove, 20, "workshopReject");
	assert(foreignMoveReject != nullptr);
	assert(Find(foreignMove, 10, "workshopReject") == nullptr);
	assert(Decode(*foreignMoveReject).at("status") == "notAuthorized");

	const auto foreignScrap = host.HandleMessage(20, Encode(Scrap(8, 3, 7, 1)), 2.56, 101.56);
	const auto* foreignScrapReject = Find(foreignScrap, 20, "workshopReject");
	assert(foreignScrapReject != nullptr);
	assert(Decode(*foreignScrapReject).at("status") == "notAuthorized");
	assert(Decode(*foreignScrapReject).at("revision") == 3);

	host.SetWorkshopAuthority(nullptr);
	const auto scrapped = host.HandleMessage(20, Encode(Scrap(2, 3, 7, 2)), 2.6, 101.6);
	const auto* scrapForB = Find(scrapped, 20, "workshopCommit");
	assert(scrapForB != nullptr);
	assert(Find(scrapped, 10, "workshopCommit") == nullptr);
	assert(Decode(*scrapForB).at("revision") == 4);
	assert(Decode(*scrapForB).at("kind") == "scrap");

	const auto oldPlaceReplay = host.HandleMessage(10, Encode(Place(1, 0, 7, 10.0)), 2.7, 101.7);
	const auto* oldPlaceCommit = Find(oldPlaceReplay, 10, "workshopCommit");
	assert(oldPlaceCommit != nullptr);
	assert(Decode(*oldPlaceCommit).at("revision") == 1);
	assert(Decode(*oldPlaceCommit).at("transform").at("x") == 10.0);

	const auto unknown = host.HandleMessage(10, Encode(Place(5, 4, 99, 10.0)), 2.8, 101.8);
	const auto* unknownReject = Find(unknown, 10, "workshopReject");
	assert(unknownReject != nullptr);
	assert(Decode(*unknownReject).at("status") == "unknownBuildZone");

	Json malformed = Place(6, 4, 7, 10.0);
	malformed["transform"]["x"] = "bad";
	assert(host.HandleMessage(10, Encode(malformed), 2.9, 101.9).empty());

	host.HandleDisconnected(10, 101.95);
	const auto welcomeReconnect = host.HandleConnected(30, 102.0);
	const auto playerReconnect = Decode(welcomeReconnect.front()).at("playerId").get<std::uint32_t>();
	assert(playerReconnect != playerA);
	assert(!host.HandleMessage(30, Encode(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 3.0, 102.1).empty());
	host.BindSessionProfile(30, "profile-a");

	const auto reconnectReplay = host.HandleMessage(30, Encode(Place(1, 0, 7, 10.0)), 3.05, 102.15);
	const auto* reconnectReplayCommit = Find(reconnectReplay, 30, "workshopCommit");
	assert(reconnectReplayCommit != nullptr);
	assert(Find(reconnectReplay, 30, "workshopReject") == nullptr);
	assert(Decode(*reconnectReplayCommit).at("playerId") == playerReconnect);
	assert(Decode(*reconnectReplayCommit).at("revision") == 1);
	assert(Decode(*reconnectReplayCommit).at("objectId") == 1);
	assert(Decode(*reconnectReplayCommit).at("transform").at("x") == 10.0);

	host.HandleConnected(31, 102.16);
	assert(!host.HandleMessage(31, Encode(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 3.06, 102.165).empty());
	host.BindSessionProfile(31, "profile-a");
	const auto reconnectConflict = host.HandleMessage(31, Encode(Place(1, 0, 7, 99.0)), 3.07, 102.17);
	const auto* reconnectConflictReject = Find(reconnectConflict, 31, "workshopReject");
	assert(reconnectConflictReject != nullptr);
	assert(Find(reconnectConflict, 31, "workshopCommit") == nullptr);
	assert(Decode(*reconnectConflictReject).at("status") == "invalidRequest");
	assert(Decode(*reconnectConflictReject).at("revision") == 4);

	const auto reconnectMoved = host.HandleMessage(30, Encode(Move(10, 4, 7, 1, 60.0)), 3.1, 102.2);
	const auto* reconnectMoveCommit = Find(reconnectMoved, 30, "workshopCommit");
	assert(reconnectMoveCommit != nullptr);
	assert(Decode(*reconnectMoveCommit).at("playerId") == playerReconnect);
	assert(Decode(*reconnectMoveCommit).at("revision") == 5);
	assert(Decode(*reconnectMoveCommit).at("transform").at("x") == 60.0);

	host.SetWorkshopAuthority([](const std::string& a_profile, std::uint64_t) { return a_profile != "profile-b"; });
	const auto foreignAfterReconnect = host.HandleMessage(20, Encode(Scrap(3, 5, 7, 1)), 3.2, 102.3);
	const auto* foreignAfterReconnectReject = Find(foreignAfterReconnect, 20, "workshopReject");
	assert(foreignAfterReconnectReject != nullptr);
	assert(Find(foreignAfterReconnect, 30, "workshopReject") == nullptr);
	assert(Decode(*foreignAfterReconnectReject).at("status") == "notAuthorized");

	host.SetWorkshopAuthority(nullptr);
	const auto reconnectScrapped = host.HandleMessage(30, Encode(Scrap(11, 5, 7, 1)), 3.3, 102.4);
	const auto* reconnectScrapCommit = Find(reconnectScrapped, 30, "workshopCommit");
	assert(reconnectScrapCommit != nullptr);
	assert(Decode(*reconnectScrapCommit).at("revision") == 6);
	assert(Decode(*reconnectScrapCommit).at("kind") == "scrap");

	host.HandleConnected(40, 103.0);
	assert(!host.HandleMessage(40, Encode(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 3.4, 103.1).empty());
	const auto unbound = host.HandleMessage(40, Encode(Place(1, 6, 7, 5.0)), 3.5, 103.2);
	const auto* unboundReject = Find(unbound, 40, "workshopReject");
	assert(unboundReject != nullptr);
	assert(Decode(*unboundReject).at("status") == "invalidRequest");
	assert(Find(unbound, 40, "workshopCommit") == nullptr);

	const auto stats = host.Stats();
	assert(stats.workshopOpsReceived == 23);
	assert(stats.workshopOpsCommitted == 8);
	assert(stats.workshopOpsRejected == 12);

	ProtocolHost snapshotHost{ "Workshop Snapshot Test" };
	const BuildZonePolicy settlement{
		9,
		FormKey{ "Fallout4.esm", 1 },
		0.0F,
		0.0F,
		0.0F,
		10.0F,
		2.0F,
		4
	};
	assert(snapshotHost.RegisterWorkshopBuildZone(settlement));

	snapshotHost.HandleConnected(80, 200.0);
	assert(!snapshotHost.HandleMessage(80, Encode(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 1.0, 200.1).empty());
	snapshotHost.BindSessionProfile(80, "profile-builder");
	const auto built = snapshotHost.HandleMessage(80, Encode(Place(1, 0, 9, 0.0)), 1.1, 200.2);
	assert(Find(built, 80, "workshopCommit") != nullptr);

	snapshotHost.HandleConnected(81, 200.3);
	const auto joinFrames = snapshotHost.HandleMessage(81, Encode(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 1.2, 200.4);
	assert(Find(joinFrames, 81, "workshopSnapshot") == nullptr);
	snapshotHost.BindSessionProfile(81, "profile-guest");

	const auto enteredFirst = snapshotHost.HandleMessage(81, Encode(Json{ { "type", "transform" }, { "cellId", "CAFE" }, { "worldspaceId", "F00D" }, { "x", 0.0 }, { "y", 0.0 }, { "z", 0.0 }, { "angleZ", 0.0 }, { "movementType", "spawn" } }), 2.0, 200.5);
	assert(Find(enteredFirst, 81, "workshopSnapshot") != nullptr);
	const auto stayed = snapshotHost.HandleMessage(81, Encode(Json{ { "type", "transform" }, { "cellId", "CAFE" }, { "worldspaceId", "F00D" }, { "x", 1.0 }, { "y", 0.0 }, { "z", 0.0 }, { "angleZ", 0.0 }, { "movementType", "normal" } }), 3.0, 200.6);
	assert(Find(stayed, 81, "workshopSnapshot") == nullptr);

	Json worldScrap{
		{ "type", "workshopOp" },
		{ "operationId", 2 },
		{ "expectedRevision", 1 },
		{ "buildZoneId", 9 },
		{ "kind", "scrapWorld" },
		{ "form", Json{ { "plugin", "Fallout4.esm" }, { "localFormId", 0xABCD } } },
		{ "space", Json{ { "plugin", "Fallout4.esm" }, { "localFormId", 1 } } },
		{ "transform", Json{
			{ "x", 2.0 },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "pitch", 0.0 },
			{ "yaw", 0.0 },
			{ "roll", 0.0 },
			{ "scale", 1.0 }
		} }
	};
	const auto worldScrapped = snapshotHost.HandleMessage(81, Encode(worldScrap), 3.1, 200.7);
	const auto* worldScrapCommit = Find(worldScrapped, 81, "workshopCommit");
	assert(worldScrapCommit != nullptr);
	assert(Decode(*worldScrapCommit).at("kind") == "scrapWorld");
	assert(Decode(*worldScrapCommit).at("revision") == 2);

	const auto foreignMoved = snapshotHost.HandleMessage(81, Encode(Move(3, 2, 9, 1, 1.0)), 3.2, 200.8);
	assert(Find(foreignMoved, 81, "workshopCommit") != nullptr);
	assert(Decode(*Find(foreignMoved, 81, "workshopCommit")).at("revision") == 3);

	snapshotHost.SetWorkshopAuthority([](const std::string&, std::uint64_t) { return false; });
	const auto denied = snapshotHost.HandleMessage(81, Encode(Scrap(4, 3, 9, 1)), 3.3, 200.9);
	const auto* deniedReject = Find(denied, 81, "workshopReject");
	assert(deniedReject != nullptr);
	assert(Decode(*deniedReject).at("status") == "notAuthorized");
	assert(Decode(*deniedReject).at("revision") == 3);
	snapshotHost.SetWorkshopAuthority(nullptr);

	snapshotHost.SetWorkshopAuthority([](const std::string&, std::uint64_t) { return false; });
	const auto deniedMove = snapshotHost.HandleMessage(81, Encode(Move(5, 3, 9, 1, 2.5)), 3.4, 200.95);
	const auto* deniedMoveReject = Find(deniedMove, 81, "workshopReject");
	assert(deniedMoveReject != nullptr);
	assert(Decode(*deniedMoveReject).at("status") == "notAuthorized");
	snapshotHost.SetWorkshopAuthority(nullptr);
	const auto retriedMove = snapshotHost.HandleMessage(81, Encode(Move(5, 3, 9, 1, 2.5)), 3.5, 200.96);
	const auto* retriedMoveCommit = Find(retriedMove, 81, "workshopCommit");
	assert(retriedMoveCommit != nullptr);
	assert(Decode(*retriedMoveCommit).at("revision") == 4);

	snapshotHost.HandleConnected(82, 201.0);
	const auto lateJoin = snapshotHost.HandleMessage(82, Encode(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 4.0, 201.1);
	assert(Find(lateJoin, 82, "workshopSnapshot") == nullptr);
	const auto lateEntry = snapshotHost.HandleMessage(82, Encode(Json{ { "type", "transform" }, { "cellId", "CAFE" }, { "worldspaceId", "F00D" }, { "x", 0.0 }, { "y", 0.0 }, { "z", 0.0 }, { "angleZ", 0.0 }, { "movementType", "spawn" } }), 4.1, 201.2);
	const auto* lateSnapshot = Find(lateEntry, 82, "workshopSnapshot");
	assert(lateSnapshot != nullptr);
	assert(Decode(*lateSnapshot).at("revision") == 4);
	assert(Decode(*lateSnapshot).at("objects").size() == 1);
	assert(Decode(*lateSnapshot).at("tombstones").size() == 1);
	assert(Decode(*lateSnapshot).at("complete") == true);

	{
		ProtocolHost entryHost{ "Workshop Entry Test" };
		assert(entryHost.RegisterWorkshopBuildZone(BuildZonePolicy{ 9, FormKey{ "Fallout4.esm", 1 }, 0.0F, 0.0F, 0.0F, 10.0F, 2.0F, 4 }));
		assert(entryHost.RegisterWorkshopBuildZone(BuildZonePolicy{ 10, FormKey{ "Fallout4.esm", 1 }, 300.0F, 0.0F, 0.0F, 10.0F, 2.0F, 4 }));
		assert(entryHost.RegisterWorkshopBuildZone(BuildZonePolicy{ 11, FormKey{ "Fallout4.esm", 1 }, 600.0F, 0.0F, 0.0F, 10.0F, 2.0F, 4 }));
		assert(entryHost.RegisterWorkshopBuildZone(BuildZonePolicy{ 12, FormKey{ "Fallout4.esm", 1 }, 900.0F, 0.0F, 0.0F, 10.0F, 2.0F, 4 }));
		const auto transformAt = [](double a_x, const char* a_movementType) {
			return Json{
				{ "type", "transform" },
				{ "cellId", "CAFE" },
				{ "worldspaceId", "F00D" },
				{ "x", a_x },
				{ "y", 0.0 },
				{ "z", 0.0 },
				{ "angleZ", 0.0 },
				{ "movementType", a_movementType }
			};
		};

		entryHost.HandleConnected(90, 300.0);
		assert(!entryHost.HandleMessage(90, Encode(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 1.0, 300.1).empty());
		entryHost.BindSessionProfile(90, "profile-entry");

		const auto spawned = entryHost.HandleMessage(90, Encode(transformAt(0.0, "spawn")), 2.0, 300.2);
		const auto* spawnedSnapshot = Find(spawned, 90, "workshopSnapshot");
		assert(spawnedSnapshot != nullptr);
		assert(Decode(*spawnedSnapshot).at("buildZoneId") == 9);

		const auto stepped = entryHost.HandleMessage(90, Encode(transformAt(300.0, "normal")), 2.1, 300.3);
		const auto* steppedSnapshot = Find(stepped, 90, "workshopSnapshot");
		assert(steppedSnapshot != nullptr);
		assert(Decode(*steppedSnapshot).at("buildZoneId") == 10);

		const auto burstLimited = entryHost.HandleMessage(90, Encode(transformAt(600.0, "normal")), 2.2, 300.4);
		assert(Find(burstLimited, 90, "workshopSnapshot") == nullptr);
		const auto stillLimited = entryHost.HandleMessage(90, Encode(transformAt(600.0, "normal")), 2.3, 300.5);
		assert(Find(stillLimited, 90, "workshopSnapshot") == nullptr);

		const auto refilled = entryHost.HandleMessage(90, Encode(transformAt(600.0, "normal")), 4.3, 300.6);
		const auto* refilledSnapshot = Find(refilled, 90, "workshopSnapshot");
		assert(refilledSnapshot != nullptr);
		assert(Decode(*refilledSnapshot).at("buildZoneId") == 11);

		const auto left = entryHost.HandleMessage(90, Encode(transformAt(900.0, "normal")), 4.4, 300.7);
		assert(Find(left, 90, "workshopSnapshot") == nullptr);

		const auto reentered = entryHost.HandleMessage(90, Encode(transformAt(600.0, "normal")), 6.5, 300.8);
		const auto* reenteredSnapshot = Find(reentered, 90, "workshopSnapshot");
		assert(reenteredSnapshot != nullptr);
		assert(Decode(*reenteredSnapshot).at("buildZoneId") == 11);
		assert(entryHost.Stats().movementRejected == 0);
	}

	TestCatchUpAndBroadcast();
	TestPersistenceAndZoneFile();

	return 0;
}

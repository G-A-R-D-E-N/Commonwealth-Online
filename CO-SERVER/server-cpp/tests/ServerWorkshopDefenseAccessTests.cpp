#include "DurableStateFileStore.h"
#include "F4TPlayerStateValidation.h"
#include "F4TWorkshopRaids.h"
#include "PersistenceGate.h"
#include "ServerFactionTestSupport.h"
#include "ServerProtocol.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace FactionTestSupport;

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::DurableStateFileStore;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::PersistenceGate;
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

	std::vector<Json> TurretEvents(const std::vector<OutboundFrame>& a_frames)
	{
		std::vector<Json> events;
		for (const auto& frame : a_frames) {
			if (frame.payload.empty() || frame.payload.front() != '{') {
				continue;
			}
			const auto packet = Decode(frame);
			if (packet.value("type", std::string{}) == "defenderEvent" && packet.value("kind", std::string{}) == "turretFire") {
				events.push_back(packet);
			}
		}
		return events;
	}

	BuildZonePolicy ZoneAt(std::uint64_t a_zoneId)
	{
		return BuildZonePolicy{ a_zoneId, FormKey{ "Fallout4.esm", 1 }, 0.0F, 0.0F, 0.0F, 10.0F, 2.0F, 8 };
	}

	std::uint32_t Join(ProtocolHost& a_host, std::uint32_t a_connectionId, const char* a_profile, double a_monotonic)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_monotonic);
		assert(!a_host.HandleMessage(a_connectionId, Encode(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), a_monotonic + 0.1, a_monotonic + 1000.1).empty());
		a_host.BindSessionProfile(a_connectionId, a_profile);
		return Decode(welcome.front()).at("playerId").get<std::uint32_t>();
	}

	std::vector<OutboundFrame> Send(
		ProtocolHost& a_host,
		std::uint32_t a_connectionId,
		const Json& a_packet,
		double a_monotonic)
	{
		return a_host.HandleMessage(a_connectionId, Encode(a_packet), a_monotonic, a_monotonic + 1000.0);
	}

	Json TransformAt(double a_x, const char* a_movementType = "spawn")
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

	Json Hit(std::uint64_t a_zoneId, std::uint64_t a_objectId, const char* a_explosive)
	{
		return Json{ { "type", "structureHit" }, { "buildZoneId", a_zoneId }, { "objectId", a_objectId }, { "explosive", a_explosive } };
	}

	Json EquipThrowable(std::uint32_t a_throwableFormId)
	{
		return Json{
			{ "type", "playerState" },
			{ "equippedItems", Json::array({ Json{ { "slot", F4T::PlayerStateValidation::kThrowableSlot }, { "form" "Id", std::format("{:08X}", a_throwableFormId) } } }) }
		};
	}

	Json DoorOp(std::uint64_t a_zoneId, std::uint64_t a_objectId, const char* a_op)
	{
		return Json{ { "type", "doorOp" }, { "buildZoneId", a_zoneId }, { "objectId", a_objectId }, { "op", a_op } };
	}

	Json WireConnect(std::uint64_t a_operationId, std::uint64_t a_zoneId, std::uint64_t a_fromId, std::uint64_t a_toId, std::uint64_t a_wireId)
	{
		return Json{ { "type", "wireOp" }, { "operationId", a_operationId }, { "buildZoneId", a_zoneId }, { "op", "connect" }, { "fromId", a_fromId }, { "toId", a_toId }, { "wireId", a_wireId } };
	}

	std::array<std::uint64_t, 3> PlaceWireEnds(ProtocolHost& a_host)
	{
		std::array<std::uint64_t, 3> ids{};
		std::uint64_t revision = 0;
		for (std::size_t index = 0; index < ids.size(); ++index) {
			const auto frames = Send(a_host, 90, Place(index + 1, revision, 9, 1.0 + static_cast<double>(index)), 2.5 + 0.1 * static_cast<double>(index));
			const auto* commit = Find(frames, 90, "workshopCommit");
			assert(commit != nullptr);
			ids[index] = Decode(*commit).at("objectId").get<std::uint64_t>();
			revision = Decode(*commit).at("revision").get<std::uint64_t>();
		}
		return ids;
	}

	template <typename Scenario>
	void WithWireWorkshop(Scenario a_scenario)
	{
		const auto directory = std::filesystem::temp_directory_path() /
			("co-server-workshop-wires-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(directory);
		const auto zones = directory / "workshop-zones.json";
		{
			std::ofstream output(zones, std::ios::binary);
			output << R"({"version":1,"zones":[]})";
		}
		{
			std::ofstream output(directory / "workshop-pieces.json", std::ios::binary);
			output << R"({"pieces":[{"plugin":"Fallout4.esm","localFormId":4660,"tier":"wood","power":{"outputWatts":0,"drawWatts":0,"needsPower":false,"deviceClass":"other"}}]})";
		}
		ProtocolHost host{ "Workshop Wire Access" };
		assert(host.ConfigureWorkshop(nullptr, zones) == 0);
		assert(host.RegisterWorkshopBuildZone(ZoneAt(9)));
		Join(host, 90, "profile-owner", 1.0);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure);
		assert(failure.empty());
		a_scenario(host);
		std::filesystem::remove_all(directory);
	}

	Json Grant(const char* a_targetProfileId)
	{
		return Json{ { "type", "settlementAccessUpdate" }, { "buildZoneId", 9 }, { "op", "grant" }, { "targetProfileId", a_targetProfileId } };
	}

	F4T::WorkshopRaids::RaidConfig WindowedRaids()
	{
		F4T::WorkshopRaids::RaidConfig raidConfig;
		raidConfig.mode = F4T::WorkshopRaids::RaidMode::Windowed;
		return raidConfig;
	}

	template <typename Scenario>
	void WithTurretWorkshop(bool a_windowed, Scenario a_scenario)
	{
		const auto directory = std::filesystem::temp_directory_path() /
			("co-server-workshop-defense-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(directory);
		const auto path = directory / "durable-state.json";
		const auto zones = directory / "workshop-zones.json";
		{
			std::ofstream output(zones, std::ios::binary);
			output << R"({"version":1,"zones":[{"buildZoneId":9,"space":{"plugin":"Fallout4.esm","localFormId":1},"centerX":0,"centerY":0,"centerZ":0,"radius":10,"maxScale":2,"maxObjects":8}]})";
		}
		{
			std::ofstream output(directory / "workshop-pieces.json", std::ios::binary);
			output << R"({"pieces":[{"plugin":"Fallout4.esm","localFormId":4660,"tier":"wood","power":{"outputWatts":0,"drawWatts":0,"needsPower":false,"deviceClass":"turret"}}]})";
		}
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Workshop Defense Access" };
			assert(host.EnableFactions(nullptr));
			if (a_windowed) {
				host.SetWorkshopRaidConfig(WindowedRaids());
			}
			assert(host.ConfigureWorkshop(&gate, zones) == 1);
			a_scenario(host);
		}
		std::filesystem::remove_all(directory);
	}

	void TestTurretWindowGatesFireOutsideWindow()
	{
		WithTurretWorkshop(true, [](ProtocolHost& a_host) {
			Join(a_host, 90, "profile-owner", 1.0);
			Join(a_host, 92, "profile-raider", 1.0);
			std::string failure;
			(void)a_host.SetSettlementOwner(9, "profile-owner", failure);
			assert(failure.empty());
			assert(!Send(a_host, 90, Json{ { "type", "settlementWindowSet" }, { "buildZoneId", 9 }, { "windowStartHour", 0.0 }, { "windowDurationHours", 2.0 }, { "timezoneOffsetHours", 0.0 } }, 2.0).empty());
			assert(!a_host.HandleMessage(92, Encode(Json{ { "type", "raidDeclare" }, { "buildZoneId", 9 } }), 3.0, 1003.0).empty());
			(void)Send(a_host, 90, TransformAt(6.0), 2.0);
			(void)Send(a_host, 92, TransformAt(0.0), 2.0);
			assert(Find(Send(a_host, 90, Place(1, 0, 9, 6.0), 2.5), 90, "workshopCommit") != nullptr);
			(void)Send(a_host, 92, EquipThrowable(F4T::WorkshopRaids::kMiniNukeSourceFormId), 1803.0);
			(void)a_host.HandleMessage(92, Encode(Hit(9, 0, "mininuke")), 1804.0, 1005.0);
			assert(!TurretEvents(a_host.FlushWorkshopPersistence(1005.0, 1804.5)).empty());
			(void)a_host.HandleMessage(92, Encode(Hit(9, 0, "mininuke")), 1806.0, 2000000.0);
			assert(TurretEvents(a_host.FlushWorkshopPersistence(2000000.0, 1807.0)).empty());
		});
	}

	void TestTurretIgnoresLeaverInsideLeaveBlock()
	{
		WithTurretWorkshop(false, [](ProtocolHost& a_host) {
			Join(a_host, 90, "profile-owner", 1.0);
			const auto leaverId = Join(a_host, 92, "profile-leaver", 1.0);
			Join(a_host, 93, "profile-attacker", 1.0);
			FormFaction(a_host, 90, 92, leaverId, "Raiders");
			std::string failure;
			(void)a_host.SetSettlementOwner(9, "profile-owner", failure);
			assert(failure.empty());
			(void)Send(a_host, 90, TransformAt(6.0), 2.0);
			(void)Send(a_host, 92, TransformAt(5.0), 2.0);
			(void)Send(a_host, 93, TransformAt(0.0), 2.0);
			assert(Find(Send(a_host, 90, Place(1, 0, 9, 6.0), 2.5), 90, "workshopCommit") != nullptr);
			AssertResult(FactionTestSupport::Send(a_host, 92, Leave(), 3.0, 2000000.0), 92, "leave", true);
			(void)Send(a_host, 93, EquipThrowable(F4T::WorkshopRaids::kMiniNukeSourceFormId), 3.0);
			(void)a_host.HandleMessage(93, Encode(Hit(9, 0, "mininuke")), 3.5, 2000000.0);
			const auto events = TurretEvents(a_host.FlushWorkshopPersistence(2000000.0, 4.0));
			assert(!events.empty());
			for (const auto& event : events) {
				assert(event.at("targetPlayerId").get<std::uint32_t>() != leaverId);
			}
		});
	}

	void TestTurretHoldsFireAtFactionMate()
	{
		WithTurretWorkshop(false, [](ProtocolHost& a_host) {
			Join(a_host, 90, "profile-owner", 1.0);
			const auto mateId = Join(a_host, 94, "profile-mate", 1.0);
			const auto raiderId = Join(a_host, 92, "profile-raider", 1.0);
			FormFaction(a_host, 90, 94, mateId, "Guardians");
			std::string failure;
			(void)a_host.SetSettlementOwner(9, "profile-owner", failure);
			assert(failure.empty());
			(void)Send(a_host, 90, TransformAt(6.0), 2.0);
			(void)Send(a_host, 94, TransformAt(5.0), 2.0);
			(void)Send(a_host, 92, TransformAt(0.0), 2.0);
			assert(Find(Send(a_host, 90, Place(1, 0, 9, 6.0), 2.5), 90, "workshopCommit") != nullptr);
			(void)Send(a_host, 92, EquipThrowable(F4T::WorkshopRaids::kMiniNukeSourceFormId), 2.8);
			(void)a_host.HandleMessage(92, Encode(Hit(9, 0, "mininuke")), 3.0, 2000000.0);
			const auto events = TurretEvents(a_host.FlushWorkshopPersistence(2000000.0, 3.5));
			assert(!events.empty());
			for (const auto& event : events) {
				assert(event.at("targetPlayerId").get<std::uint32_t>() == raiderId);
			}
		});
	}

	void TestTurretNeverTargetsAllowlistedBuilderOrOwner()
	{
		WithTurretWorkshop(false, [](ProtocolHost& a_host) {
			const auto ownerId = Join(a_host, 90, "profile-owner", 1.0);
			const auto builderId = Join(a_host, 91, "profile-builder", 1.0);
			Join(a_host, 92, "profile-raider", 1.0);
			std::string failure;
			(void)a_host.SetSettlementOwner(9, "profile-owner", failure);
			assert(failure.empty());
			(void)Send(a_host, 90, Grant("profile-builder"), 1.5);
			(void)Send(a_host, 90, TransformAt(6.0), 2.0);
			(void)Send(a_host, 91, TransformAt(5.0), 2.0);
			(void)Send(a_host, 92, TransformAt(0.0), 2.0);
			assert(Find(Send(a_host, 90, Place(1, 0, 9, 6.0), 2.5), 90, "workshopCommit") != nullptr);
			(void)Send(a_host, 92, EquipThrowable(F4T::WorkshopRaids::kMiniNukeSourceFormId), 2.8);
			(void)a_host.HandleMessage(92, Encode(Hit(9, 0, "mininuke")), 3.0, 2000000.0);
			bool firedAtBuilder = false;
			for (const auto& event : TurretEvents(a_host.FlushWorkshopPersistence(2000000.0, 3.5))) {
				const auto target = event.at("targetPlayerId").get<std::uint32_t>();
				assert(target != ownerId);
				firedAtBuilder = firedAtBuilder || target == builderId;
			}
			assert(!firedAtBuilder);
		});
	}

	void TestWireOpRefusedOutsideZone()
	{
		WithWireWorkshop([](ProtocolHost& a_host) {
			(void)Send(a_host, 90, TransformAt(0.0), 2.0);
			const auto ids = PlaceWireEnds(a_host);
			(void)Send(a_host, 90, TransformAt(50.0, "normal"), 4.0);
			const auto refused = Send(a_host, 90, WireConnect(4, 9, ids[0], ids[1], ids[2]), 4.5);
			assert(Find(refused, 90, "powerState") == nullptr);
			(void)Send(a_host, 90, TransformAt(0.0, "normal"), 5.0);
			const auto connected = Send(a_host, 90, WireConnect(5, 9, ids[0], ids[1], ids[2]), 5.5);
			const auto* state = Find(connected, 90, "powerState");
			assert(state != nullptr);
			assert(Decode(*state).at("wires").size() == 1);
		});
	}

	void TestWireConnectStoresEdgeInReach()
	{
		WithWireWorkshop([](ProtocolHost& a_host) {
			(void)Send(a_host, 90, TransformAt(0.0), 2.0);
			const auto ids = PlaceWireEnds(a_host);
			const auto connected = Send(a_host, 90, WireConnect(4, 9, ids[0], ids[1], ids[2]), 4.5);
			const auto* state = Find(connected, 90, "powerState");
			assert(state != nullptr);
			const auto wires = Decode(*state).at("wires");
			assert(wires.size() == 1);
			assert(wires[0].at("wireId").get<std::uint64_t>() == ids[2]);
			assert(wires[0].at("fromId").get<std::uint64_t>() == ids[0]);
			assert(wires[0].at("toId").get<std::uint64_t>() == ids[1]);
		});
	}

	void TestDoorOpsRequireOwnerOrAllowlist()
	{
		const auto directory = std::filesystem::temp_directory_path() /
			("co-server-workshop-doors-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(directory);
		const auto zones = directory / "workshop-zones.json";
		{
			std::ofstream output(zones, std::ios::binary);
			output << R"({"version":1,"zones":[]})";
		}
		{
			std::ofstream output(directory / "workshop-pieces.json", std::ios::binary);
			output << R"({"pieces":[{"plugin":"Fallout4.esm","localFormId":4660,"tier":"wood","door":true}]})";
		}
		ProtocolHost host{ "Workshop Door Access" };
		assert(host.ConfigureWorkshop(nullptr, zones) == 0);
		assert(host.RegisterWorkshopBuildZone(ZoneAt(9)));
		assert(host.RegisterWorkshopBuildZone(ZoneAt(10)));
		Join(host, 90, "profile-owner", 1.0);
		Join(host, 91, "profile-builder", 1.0);
		Join(host, 92, "profile-stranger", 1.0);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure);
		assert(failure.empty());
		(void)Send(host, 90, Grant("profile-builder"), 1.5);
		(void)Send(host, 90, TransformAt(0.0), 2.0);
		(void)Send(host, 91, TransformAt(0.0), 2.0);
		(void)Send(host, 92, TransformAt(0.0), 2.0);
		const auto door9Commit = Send(host, 90, Place(1, 0, 9, 1.0), 2.5);
		const auto door10Commit = Send(host, 92, Place(2, 0, 10, 1.0), 2.6);
		const auto door9 = Decode(*Find(door9Commit, 90, "workshopCommit")).at("objectId").get<std::uint64_t>();
		const auto door10 = Decode(*Find(door10Commit, 92, "workshopCommit")).at("objectId").get<std::uint64_t>();

		const auto strangerLock = Send(host, 92, DoorOp(9, door9, "lock"), 4.0);
		const auto* refused = Find(strangerLock, 92, "doorState");
		assert(refused != nullptr && !Decode(*refused).at("locked").get<bool>());

		const auto builderLock = Send(host, 91, DoorOp(9, door9, "lock"), 4.5);
		const auto* builderState = Find(builderLock, 91, "doorState");
		assert(builderState != nullptr && Decode(*builderState).at("locked").get<bool>());

		const auto strangerUnlock = Send(host, 92, DoorOp(9, door9, "unlock"), 5.0);
		const auto* strangerState = Find(strangerUnlock, 92, "doorState");
		assert(strangerState != nullptr && Decode(*strangerState).at("locked").get<bool>());

		const auto ownerUnlock = Send(host, 90, DoorOp(9, door9, "unlock"), 5.5);
		const auto* ownerState = Find(ownerUnlock, 90, "doorState");
		assert(ownerState != nullptr && !Decode(*ownerState).at("locked").get<bool>());

		const auto noRecordLock = Send(host, 92, DoorOp(10, door10, "lock"), 6.0);
		const auto* noRecordState = Find(noRecordLock, 92, "doorState");
		assert(noRecordState != nullptr && !Decode(*noRecordState).at("locked").get<bool>());

		std::filesystem::remove_all(directory);
	}
}

int main()
{
	TestTurretWindowGatesFireOutsideWindow();
	TestTurretIgnoresLeaverInsideLeaveBlock();
	TestTurretHoldsFireAtFactionMate();
	TestTurretNeverTargetsAllowlistedBuilderOrOwner();
	TestDoorOpsRequireOwnerOrAllowlist();
	TestWireOpRefusedOutsideZone();
	TestWireConnectStoresEdgeInReach();
	return 0;
}

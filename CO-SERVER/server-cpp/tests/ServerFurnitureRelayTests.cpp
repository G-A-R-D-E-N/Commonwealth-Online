#include "ServerFurnitureRelay.h"

#include "F4TFurnitureSync.h"
#include "F4TInterestScope.h"
#include "F4TSleepBedIdentity.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

namespace
{
	using Json = nlohmann::json;
	using namespace CommonwealthOnline::Server;
	using SessionTypes::Session;
	using SessionTypes::StoredMovementState;

	Json Use(const char* a_action)
	{
		return Json{ { "type", "furnitureUse" }, { "action", a_action } };
	}

	StoredMovementState Movement(const char* a_cellId, const char* a_worldspaceId, double a_x, double a_y)
	{
		StoredMovementState movement;
		movement.cellId = a_cellId;
		movement.worldspaceId = a_worldspaceId;
		movement.x = a_x;
		movement.y = a_y;
		return movement;
	}

	Session Peer(std::uint32_t a_playerId, bool a_capability, std::optional<StoredMovementState> a_movement)
	{
		Session session;
		session.active = true;
		session.playerId = a_playerId;
		if (a_capability) {
			session.compatibilityCapabilities.values.push_back(std::string{ F4T::FurnitureSync::kCapabilityName });
		}
		session.lastMovement = std::move(a_movement);
		return session;
	}

	std::unordered_map<std::uint32_t, Session> World()
	{
		std::unordered_map<std::uint32_t, Session> sessions;
		sessions[1] = Peer(1, true, Movement("0000CAFE", "0000003C", 100.0, 100.0));
		sessions[2] = Peer(2, true, Movement("0000CAFE", "0000003C", 500.0, 500.0));
		sessions[3] = Peer(3, false, Movement("0000CAFE", "0000003C", 500.0, 500.0));
		sessions[4] = Peer(4, true, Movement("0000BEEF", "0000003C", 100.0 + F4T::InterestScope::ExteriorInterestRadius + 1.0, 100.0));
		sessions[5] = Peer(5, true, Movement("0000BEEF", "0000003C", 150.0, 100.0));
		sessions[6] = Peer(6, true, std::nullopt);
		sessions[7] = Peer(7, true, Movement("0000CAFE", "0000003C", 100.0, 100.0));
		sessions[7].active = false;
		sessions[8] = Peer(1, true, Movement("0000CAFE", "0000003C", 100.0, 100.0));
		return sessions;
	}

	void TestEnterAndExitRelayToCapableActivePlayersInInterest()
	{
		for (const char* action : { "enter", "exit" }) {
			auto sessions = World();
			ProtocolStats stats;
			const auto frames = FurnitureRelay::HandleFurnitureUse(sessions, 1, sessions[1], Use(action), 10.0, stats);
			std::set<std::uint32_t> recipients;
			for (const auto& frame : frames) {
				recipients.insert(frame.connectionId);
				const auto relayed = Json::parse(frame.payload.begin(), frame.payload.end());
				assert(relayed.at("type") == "furnitureUse");
				assert(relayed.at("playerId") == 1);
				assert(relayed.at("action") == action);
				assert(relayed.size() == 3);
			}
			assert(recipients == (std::set<std::uint32_t>{ 2, 5 }));
			assert(stats.packetsRejected == 0);
		}
	}

	void TestMalformedAndUnauthorizedSendersAreRejectedWithoutRelay()
	{
		auto sessions = World();
		ProtocolStats stats;
		assert(FurnitureRelay::HandleFurnitureUse(sessions, 1, sessions[1], Use("stand"), 10.0, stats).empty());
		assert(stats.packetsRejected == 1);
		assert(FurnitureRelay::HandleFurnitureUse(sessions, 1, sessions[1], Json{ { "type", "furnitureUse" } }, 10.0, stats).empty());
		assert(stats.packetsRejected == 2);
		assert(FurnitureRelay::HandleFurnitureUse(sessions, 1, sessions[1], Json{ { "type", "furnitureUse" }, { "action", 1 } }, 10.0, stats).empty());
		assert(stats.packetsRejected == 3);
		assert(FurnitureRelay::HandleFurnitureUse(sessions, 3, sessions[3], Use("enter"), 10.0, stats).empty());
		assert(stats.packetsRejected == 4);
		assert(FurnitureRelay::HandleFurnitureUse(sessions, 6, sessions[6], Use("enter"), 10.0, stats).empty());
		assert(stats.packetsRejected == 5);
	}

	void TestRateLimitBurstThenRefill()
	{
		auto sessions = World();
		ProtocolStats stats;
		for (std::size_t i = 0; i < 40; ++i) {
			assert(!FurnitureRelay::HandleFurnitureUse(sessions, 1, sessions[1], Use("enter"), 10.0, stats).empty());
		}
		assert(FurnitureRelay::HandleFurnitureUse(sessions, 1, sessions[1], Use("enter"), 10.0, stats).empty());
		assert(stats.packetsRejected == 1);
		assert(sessions[1].lastSeatPacket);
		assert(!FurnitureRelay::HandleFurnitureUse(sessions, 1, sessions[1], Use("exit"), 10.0, stats).empty());
		assert(!sessions[1].lastSeatPacket && stats.packetsRejected == 1);
		for (std::size_t i = 0; i < 20; ++i) {
			assert(!FurnitureRelay::HandleFurnitureUse(sessions, 1, sessions[1], Use("exit"), 11.0, stats).empty());
		}
		assert(FurnitureRelay::HandleFurnitureUse(sessions, 1, sessions[1], Use("exit"), 11.0, stats).empty());
		assert(stats.packetsRejected == 2);
	}

	void TestSleepRelay()
	{
		auto sessions = World();
		for (const auto id : { 1u, 2u, 4u }) {
			sessions[id].compatibilityCapabilities.values.push_back(std::string{ F4T::SleepBedIdentity::kCapabilityName });
		}
		ProtocolStats stats;
		const Json enter{ { "type", "sleepUse" }, { "action", "enter" }, { "playerId", 99 },
			{ "bed", { { "kind", "workshop" }, { "zone", 2u }, { "object", 3u }, { "extra", true } } }, { "marker", 1u } };
		const auto frames = FurnitureRelay::HandleSleepUse(sessions, 1, sessions[1], enter, 10.0, stats);
		assert(frames.size() == 1 && frames[0].connectionId == 2);
		const auto relayed = Json::parse(frames[0].payload.begin(), frames[0].payload.end());
		assert(relayed.at("playerId") == 1 && relayed.at("action") == "enter");
		assert(relayed.at("bed").size() == 3 && relayed.at("bed").at("object") == 3);
		assert(relayed.at("marker") == 1);
		const auto snapshot = FurnitureRelay::SleepSnapshot(2, sessions[2], sessions[1]);
		assert(snapshot && Json::parse(snapshot->payload.begin(), snapshot->payload.end()) == relayed);
		assert(!FurnitureRelay::SleepSnapshot(5, sessions[5], sessions[1]));
		const auto exit = FurnitureRelay::HandleSleepUse(sessions, 1, sessions[1], Json{ { "action", "exit" } }, 10.0, stats);
		assert(exit.size() == 1 && Json::parse(exit[0].payload.begin(), exit[0].payload.end()).size() == 3);
		assert(!FurnitureRelay::SleepSnapshot(2, sessions[2], sessions[1]));
		assert(FurnitureRelay::HandleSleepUse(sessions, 1, sessions[1], Json{ { "action", "enter" }, { "bed", enter["bed"] } }, 10.0, stats).empty());
		assert(FurnitureRelay::HandleSleepUse(sessions, 1, sessions[1], Json{ { "action", "enter" }, { "bed", enter["bed"] }, { "marker", -1 } }, 10.0, stats).empty());
		assert(FurnitureRelay::HandleSleepUse(sessions, 1, sessions[1], Json{ { "action", "enter" }, { "bed", enter["bed"] }, { "marker", 22 } }, 10.0, stats).empty());
		assert(!FurnitureRelay::SleepSnapshot(2, sessions[2], sessions[1]));
		assert(FurnitureRelay::HandleSleepUse(sessions, 1, sessions[1], Json{ { "action", "enter" }, { "bed", enter["bed"] }, { "marker", 0xffffffffu } }, 10.0, stats).empty());
		assert(FurnitureRelay::HandleSleepUse(sessions, 3, sessions[3], enter, 10.0, stats).empty());
		assert(stats.packetsRejected == 5);
	}

	void TestSleepExitAfterRateLimit()
	{
		auto sessions = World();
		for (const auto id : { 1u, 2u }) sessions[id].compatibilityCapabilities.values.push_back(std::string{ F4T::SleepBedIdentity::kCapabilityName });
		ProtocolStats stats;
		const Json enter{ { "action", "enter" }, { "bed", { { "kind", "form" }, { "key", "001234:fallout4.esm" } } }, { "marker", 0u } };
		assert(!FurnitureRelay::HandleSleepUse(sessions, 1, sessions[1], enter, 10.0, stats).empty());
		for (int i = 0; i < 39; ++i) assert(!FurnitureRelay::HandleFurnitureUse(sessions, 1, sessions[1], Use("enter"), 10.0, stats).empty());
		assert(!FurnitureRelay::HandleSleepUse(sessions, 1, sessions[1], Json{ { "action", "exit" } }, 10.0, stats).empty());
		assert(!sessions[1].lastSleepPacket);
		assert(FurnitureRelay::HandleSleepUse(sessions, 1, sessions[1], Json{ { "action", "exit" } }, 10.0, stats).empty());
		assert(stats.packetsRejected == 1);
	}

	void TestSeatStateIsRetainedUntilExit()
	{
		auto sessions = World();
		ProtocolStats stats;
		assert(FurnitureRelay::HandleFurnitureUse(sessions, 1, sessions[1], Use("enter"), 10.0, stats).size() == 2);
		assert(sessions[1].lastSeatPacket && sessions[1].lastSeatPacket->at("action") == "enter");
		const auto snapshot = FurnitureRelay::SeatSnapshot(2, sessions[2], sessions[1]);
		assert(snapshot && Json::parse(snapshot->payload.begin(), snapshot->payload.end()) == *sessions[1].lastSeatPacket);
		assert(!FurnitureRelay::SeatSnapshot(3, sessions[3], sessions[1]));
		const auto clear = FurnitureRelay::SeatClear(2, sessions[2], 1);
		assert(clear && Json::parse(clear->payload.begin(), clear->payload.end()).at("action") == "exit");
		assert(!FurnitureRelay::SeatClear(3, sessions[3], 1));
		assert(FurnitureRelay::HandleFurnitureUse(sessions, 1, sessions[1], Use("exit"), 10.0, stats).size() == 2);
		assert(!sessions[1].lastSeatPacket);
		assert(!FurnitureRelay::SeatSnapshot(2, sessions[2], sessions[1]));
		assert(stats.packetsRejected == 0);
	}

	void TestSeatClearsOnPausedExitWithoutScope()
	{
		auto sessions = World();
		ProtocolStats stats;
		assert(FurnitureRelay::HandleFurnitureUse(sessions, 1, sessions[1], Use("enter"), 10.0, stats).size() == 2);
		sessions[1].lastMovement.reset();
		sessions[1].visibilityPaused = true;
		assert(FurnitureRelay::HandleFurnitureUse(sessions, 1, sessions[1], Use("exit"), 11.0, stats).empty());
		assert(!sessions[1].lastSeatPacket);
		assert(stats.packetsRejected == 0);
		sessions[1].visibilityPaused = false;
		assert(FurnitureRelay::HandleFurnitureUse(sessions, 1, sessions[1], Use("exit"), 11.0, stats).empty());
		assert(stats.packetsRejected == 1);
	}
}

int main()
{
	TestEnterAndExitRelayToCapableActivePlayersInInterest();
	TestMalformedAndUnauthorizedSendersAreRejectedWithoutRelay();
	TestRateLimitBurstThenRefill();
	TestSleepRelay();
	TestSleepExitAfterRateLimit();
	TestSeatStateIsRetainedUntilExit();
	TestSeatClearsOnPausedExitWithoutScope();
	return 0;
}

#include "ServerShotReplayRelay.h"

#include "F4TInterestScope.h"
#include "F4TShotReplay.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>

namespace
{
	using Json = nlohmann::json;
	using namespace CommonwealthOnline::Server;
	using SessionTypes::Session;
	using SessionTypes::StoredMovementState;

	Json Aim(double a_y = 1.0)
	{
		return Json{ { "type", "projectileAim" }, { "direction", Json{ { "x", 0.0 }, { "y", a_y }, { "z", 0.0 } } } };
	}

	Json AnimEvent(const char* a_event)
	{
		return Json{ { "type", "playerAnimEvent" }, { "event", a_event } };
	}

	Json Explosion()
	{
		return Json{ { "type", "explosionSpawn" }, { "formKey", "0E574F:Fallout4.esm" }, { "x", 1.5 }, { "y", -2.0 }, { "z", 3.5 } };
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
			session.compatibilityCapabilities.values.push_back(std::string{ F4T::ShotReplay::kCapabilityName });
			session.compatibilityCapabilities.values.push_back(std::string{ F4T::ShotReplay::kExplosionCapabilityName });
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

	void TestAimRelaysOnlyToCapableActivePlayersInInterest()
	{
		auto sessions = World();
		ProtocolStats stats;
		const auto frames = ShotReplayRelay::HandleShotReplay(sessions, 1, sessions[1], Aim(), 10.0, stats);
		std::set<std::uint32_t> recipients;
		for (const auto& frame : frames) {
			recipients.insert(frame.connectionId);
			const auto relayed = Json::parse(frame.payload.begin(), frame.payload.end());
			assert(relayed.at("type") == "projectileAim");
			assert(relayed.at("playerId") == 1);
			assert(relayed.at("direction").at("y") == 1.0);
		}
		assert(recipients == (std::set<std::uint32_t>{ 2, 5 }));
		assert(stats.packetsRejected == 0);
	}

	void TestAnimEventRelaysAllowedNamesOnly()
	{
		auto sessions = World();
		ProtocolStats stats;
		const auto frames = ShotReplayRelay::HandleShotReplay(sessions, 1, sessions[1], AnimEvent("reloadStart"), 10.0, stats);
		std::set<std::uint32_t> recipients;
		for (const auto& frame : frames) {
			recipients.insert(frame.connectionId);
			const auto relayed = Json::parse(frame.payload.begin(), frame.payload.end());
			assert(relayed.at("type") == "playerAnimEvent");
			assert(relayed.at("playerId") == 1);
			assert(relayed.at("event") == "reloadStart");
		}
		assert(recipients == (std::set<std::uint32_t>{ 2, 5 }));
		assert(stats.packetsRejected == 0);
		assert(ShotReplayRelay::HandleShotReplay(sessions, 1, sessions[1], AnimEvent("attackStart"), 10.0, stats).empty());
		assert(ShotReplayRelay::HandleShotReplay(sessions, 1, sessions[1], AnimEvent("Death"), 10.0, stats).empty());
		assert(stats.packetsRejected == 2);
	}

	void TestMalformedAndUnauthorizedSendersAreRejectedWithoutRelay()
	{
		auto sessions = World();
		ProtocolStats stats;
		assert(ShotReplayRelay::HandleShotReplay(sessions, 1, sessions[1], Aim(2.0), 10.0, stats).empty());
		assert(stats.packetsRejected == 1);
		assert(ShotReplayRelay::HandleShotReplay(sessions, 3, sessions[3], Aim(), 10.0, stats).empty());
		assert(stats.packetsRejected == 2);
		assert(ShotReplayRelay::HandleShotReplay(sessions, 6, sessions[6], Aim(), 10.0, stats).empty());
		assert(stats.packetsRejected == 3);
	}

	void TestRateLimitBurstThenRefill()
	{
		auto sessions = World();
		ProtocolStats stats;
		for (std::size_t i = 0; i < 40; ++i) {
			assert(!ShotReplayRelay::HandleShotReplay(sessions, 1, sessions[1], Aim(), 10.0, stats).empty());
		}
		assert(ShotReplayRelay::HandleShotReplay(sessions, 1, sessions[1], Aim(), 10.0, stats).empty());
		assert(stats.packetsRejected == 1);
		for (std::size_t i = 0; i < 20; ++i) {
			assert(!ShotReplayRelay::HandleShotReplay(sessions, 1, sessions[1], Aim(), 11.0, stats).empty());
		}
		assert(ShotReplayRelay::HandleShotReplay(sessions, 1, sessions[1], Aim(), 11.0, stats).empty());
		assert(stats.packetsRejected == 2);
	}

	void TestExplosionRelaysOnlyToExplosionCapablePlayersInInterest()
	{
		auto sessions = World();
		sessions[2].compatibilityCapabilities.values = { std::string{ F4T::ShotReplay::kCapabilityName } };
		ProtocolStats stats;
		const auto frames = ShotReplayRelay::HandleExplosionSpawn(sessions, 1, sessions[1], Explosion(), 10.0, stats);
		std::set<std::uint32_t> recipients;
		for (const auto& frame : frames) {
			recipients.insert(frame.connectionId);
			const auto relayed = Json::parse(frame.payload.begin(), frame.payload.end());
			assert(relayed.at("type") == "explosionSpawn");
			assert(relayed.at("playerId") == 1);
			assert(relayed.at("formKey") == "0E574F:fallout4.esm");
			assert(relayed.at("x") == 1.5);
			assert(relayed.at("y") == -2.0);
			assert(relayed.at("z") == 3.5);
		}
		assert(recipients == (std::set<std::uint32_t>{ 5 }));
		assert(stats.packetsRejected == 0);
	}

	void TestExplosionRejectsUnauthorizedAndMalformedSenders()
	{
		auto sessions = World();
		ProtocolStats stats;
		Json outOfRange = Explosion();
		outOfRange["x"] = 2000000.0;
		assert(ShotReplayRelay::HandleExplosionSpawn(sessions, 1, sessions[1], outOfRange, 10.0, stats).empty());
		assert(stats.packetsRejected == 1);
		Json noLocalId = Explosion();
		noLocalId["formKey"] = "000ABC";
		assert(ShotReplayRelay::HandleExplosionSpawn(sessions, 1, sessions[1], noLocalId, 10.0, stats).empty());
		assert(stats.packetsRejected == 2);
		assert(ShotReplayRelay::HandleExplosionSpawn(sessions, 3, sessions[3], Explosion(), 10.0, stats).empty());
		assert(stats.packetsRejected == 3);
		assert(ShotReplayRelay::HandleExplosionSpawn(sessions, 6, sessions[6], Explosion(), 10.0, stats).empty());
		assert(stats.packetsRejected == 4);
		sessions[1].compatibilityCapabilities.values = { std::string{ F4T::ShotReplay::kCapabilityName } };
		assert(ShotReplayRelay::HandleExplosionSpawn(sessions, 1, sessions[1], Explosion(), 10.0, stats).empty());
		assert(stats.packetsRejected == 5);
	}

	void TestExplosionFarFromSenderIsDropped()
	{
		auto sessions = World();
		ProtocolStats stats;
		Json distant = Explosion();
		distant["x"] = 6700.0;
		assert(ShotReplayRelay::HandleExplosionSpawn(sessions, 1, sessions[1], distant, 10.0, stats).empty());
		assert(stats.packetsRejected == 1);
	}

	void TestExplosionNearSenderIsRelayed()
	{
		auto sessions = World();
		ProtocolStats stats;
		Json nearby = Explosion();
		nearby["x"] = 6000.0;
		const auto frames = ShotReplayRelay::HandleExplosionSpawn(sessions, 1, sessions[1], nearby, 10.0, stats);
		std::set<std::uint32_t> recipients;
		for (const auto& frame : frames) {
			recipients.insert(frame.connectionId);
		}
		assert(recipients == (std::set<std::uint32_t>{ 2, 5 }));
		assert(stats.packetsRejected == 0);
	}

	void TestExplosionFriendlyFireOffDropsRelay()
	{
		auto sessions = World();
		ProtocolStats stats;
		ShotReplayRelay::ExplosionFriendlyFire rules;
		rules.friendlyFire = false;
		assert(ShotReplayRelay::HandleExplosionSpawn(sessions, 1, sessions[1], Explosion(), 10.0, stats, rules).empty());
		assert(stats.packetsRejected == 0);
	}

	void TestExplosionFactionGateFollowsFactionFriendlyFire()
	{
		auto sessions = World();
		sessions[1].profileId = "profile-a";
		sessions[2].profileId = "profile-a";
		sessions[5].profileId = "profile-b";
		Json nearby = Explosion();
		nearby["x"] = 6000.0;
		ShotReplayRelay::ExplosionFriendlyFire rules;
		rules.sameFaction = [](const std::string& a_viewer, const std::string& a_other) { return a_viewer == a_other; };
		{
			ProtocolStats stats;
			std::set<std::uint32_t> recipients;
			for (const auto& frame : ShotReplayRelay::HandleExplosionSpawn(sessions, 1, sessions[1], nearby, 10.0, stats, rules)) {
				recipients.insert(frame.connectionId);
			}
			assert(recipients == (std::set<std::uint32_t>{ 5 }));
		}
		rules.factionFriendlyFire = true;
		{
			ProtocolStats stats;
			std::set<std::uint32_t> recipients;
			for (const auto& frame : ShotReplayRelay::HandleExplosionSpawn(sessions, 1, sessions[1], nearby, 11.0, stats, rules)) {
				recipients.insert(frame.connectionId);
			}
			assert(recipients == (std::set<std::uint32_t>{ 2, 5 }));
		}
	}

	void TestExplosionRateLimitBurstThenRefill()
	{
		auto sessions = World();
		ProtocolStats stats;
		for (std::size_t i = 0; i < 20; ++i) {
			assert(!ShotReplayRelay::HandleExplosionSpawn(sessions, 1, sessions[1], Explosion(), 10.0, stats).empty());
		}
		assert(ShotReplayRelay::HandleExplosionSpawn(sessions, 1, sessions[1], Explosion(), 10.0, stats).empty());
		assert(stats.packetsRejected == 1);
		for (std::size_t i = 0; i < 10; ++i) {
			assert(!ShotReplayRelay::HandleExplosionSpawn(sessions, 1, sessions[1], Explosion(), 11.0, stats).empty());
		}
		assert(ShotReplayRelay::HandleExplosionSpawn(sessions, 1, sessions[1], Explosion(), 11.0, stats).empty());
		assert(stats.packetsRejected == 2);
	}

	void TestExplosionWithUnknownFormIsRejected()
	{
		auto sessions = World();
		ProtocolStats stats;
		Json unknownLocalId = Explosion();
		unknownLocalId["formKey"] = "000ABC:Fallout4.esm";
		assert(ShotReplayRelay::HandleExplosionSpawn(sessions, 1, sessions[1], unknownLocalId, 10.0, stats).empty());
		Json unknownPlugin = Explosion();
		unknownPlugin["formKey"] = "0E574F:Mod.esp";
		assert(ShotReplayRelay::HandleExplosionSpawn(sessions, 1, sessions[1], unknownPlugin, 10.0, stats).empty());
		assert(stats.explosionFormsRejected == 2);
		assert(stats.packetsRejected == 2);
	}
}

int main()
{
	TestAimRelaysOnlyToCapableActivePlayersInInterest();
	TestAnimEventRelaysAllowedNamesOnly();
	TestMalformedAndUnauthorizedSendersAreRejectedWithoutRelay();
	TestRateLimitBurstThenRefill();
	TestExplosionRelaysOnlyToExplosionCapablePlayersInInterest();
	TestExplosionRejectsUnauthorizedAndMalformedSenders();
	TestExplosionFarFromSenderIsDropped();
	TestExplosionNearSenderIsRelayed();
	TestExplosionFriendlyFireOffDropsRelay();
	TestExplosionFactionGateFollowsFactionFriendlyFire();
	TestExplosionRateLimitBurstThenRefill();
	TestExplosionWithUnknownFormIsRejected();
	return 0;
}

#include "ServerProtocol.h"

#include "F4TGnsSnapshotEnvelope.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::Delivery;
	using CommonwealthOnline::Server::OutboundFrame;
	using F4T::GnsSnapshotEnvelope::DecodeResult;
	using F4T::GnsSnapshotEnvelope::Family;

	std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	std::vector<std::uint8_t> EncodeSnapshot(const Json& a_packet, Family a_family, std::uint32_t a_sequence)
	{
		const auto json = a_packet.dump();
		std::vector<std::uint8_t> output(F4T::GnsSnapshotEnvelope::HeaderSize + json.size());
		assert(F4T::GnsSnapshotEnvelope::WriteHeader(std::span<std::uint8_t>{ output }, a_family, a_sequence));
		std::copy(json.begin(), json.end(), output.begin() + static_cast<std::ptrdiff_t>(F4T::GnsSnapshotEnvelope::HeaderSize));
		return output;
	}

	Json DecodePacket(const OutboundFrame& a_frame)
	{
		F4T::GnsSnapshotEnvelope::Header header{};
		const auto decode = F4T::GnsSnapshotEnvelope::Decode(a_frame.payload, header);
		std::span<const std::uint8_t> payload{ a_frame.payload };
		if (decode == DecodeResult::Valid) {
			payload = payload.subspan(F4T::GnsSnapshotEnvelope::HeaderSize);
		} else {
			assert(decode == DecodeResult::NotEnvelope);
		}
		return Json::parse(std::string{
			reinterpret_cast<const char*>(payload.data()),
			payload.size()
		});
	}

	const OutboundFrame* FindFrame(
		const std::vector<OutboundFrame>& a_frames,
		std::uint32_t a_connectionId,
		std::string_view a_type)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && !frame.payload.empty() &&
				DecodePacket(frame).value("type", std::string{}) == a_type) {
				return &frame;
			}
		}
		return nullptr;
	}

	std::uint32_t g_currentAttackSequence = 0;

	Json RangedPlayerState(std::uint32_t a_actionSequence, std::string_view a_weapon = "1234")
	{
		g_currentAttackSequence = a_actionSequence;
		return Json{
			{ "type", "playerState" },
			{ "equippedItems", Json::array({ Json{ { "slot", "rightHand" }, { "formId", std::string{ a_weapon } } } }) },
			{ "actionEvents", Json::array({ Json{
				{ "sequence", a_actionSequence },
				{ "type", 3 },
				{ "eventName", "fireSingle" }
			} }) }
		};
	}

	Json UnarmedPlayerState(std::uint32_t a_actionSequence)
	{
		g_currentAttackSequence = a_actionSequence;
		return Json{
			{ "type", "playerState" },
			{ "equippedItems", Json::array({ Json{ { "slot", "rightHand" }, { "formId", "" } } }) },
			{ "actionEvents", Json::array({ Json{
				{ "sequence", a_actionSequence },
				{ "type", 2 },
				{ "eventName", "meleeattackStart" }
			} }) }
		};
	}

	Json MeleePlayerState(std::uint32_t a_actionSequence, std::string_view a_weapon = "1234")
	{
		g_currentAttackSequence = a_actionSequence;
		return Json{
			{ "type", "playerState" },
			{ "equippedItems", Json::array({ Json{ { "slot", "rightHand" }, { "formId", std::string{ a_weapon } } } }) },
			{ "actionEvents", Json::array({ Json{
				{ "sequence", a_actionSequence },
				{ "type", 1 },
				{ "eventName", "meleeattackStart" }
			} }) }
		};
	}

	Json Hit(
		std::uint32_t a_target,
		std::uint32_t a_sequence,
		double a_damage,
		std::string_view a_weapon = "1234",
		std::uint32_t a_attackSequence = 0)
	{
		Json hit{
			{ "type", "combatHit" },
			{ "targetPlayerId", a_target },
			{ "sequence", a_sequence },
			{ "attackSequence", a_attackSequence == 0 ? g_currentAttackSequence : a_attackSequence },
			{ "damage", a_damage }
		};
		if (!a_weapon.empty()) {
			hit["weaponFormId"] = std::string{ a_weapon };
		}
		return hit;
	}
}

int main()
{
	using CommonwealthOnline::Server::ProtocolHost;

	ProtocolHost host{ "World Combat Test" };
	const auto welcomeA = host.HandleConnected(10, 100.0);
	const auto playerA = DecodePacket(welcomeA.front()).at("playerId").get<std::uint32_t>();
	const auto readyA = host.HandleMessage(10, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 1.0, 100.1);
	assert(FindFrame(readyA, 10, "sessionReady") != nullptr);
	const auto* initialHostFrame = FindFrame(readyA, 10, "worldStateHost");
	assert(initialHostFrame != nullptr);
	assert(DecodePacket(*initialHostFrame).at("worldStateHostPlayerId") == playerA);

	const auto welcomeB = host.HandleConnected(20, 100.2);
	const auto playerB = DecodePacket(welcomeB.front()).at("playerId").get<std::uint32_t>();
	assert(!host.HandleMessage(20, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 1.1, 100.3).empty());

	const auto beforeBadWorld = host.Stats();
	const auto nonHostWorld = host.HandleMessage(
		20,
		EncodeJson(Json{ { "type", "worldState" }, { "gameHour", 12.0 } }),
		1.2,
		100.4);
	assert(nonHostWorld.empty());
	assert(host.Stats().worldStateRejected == beforeBadWorld.worldStateRejected + 1);

	const auto hostWorld = host.HandleMessage(
		10,
		EncodeJson(Json{
			{ "type", "worldState" },
			{ "gameHour", 18.5 },
			{ "gameDaysPassed", 42.0 },
			{ "weatherFormId", "1234" }
		}),
		1.3,
		100.5);
	const auto* worldFrame = FindFrame(hostWorld, 20, "worldState");
	assert(worldFrame != nullptr);
	assert(worldFrame->delivery == Delivery::ReliableOrdered);
	const auto world = DecodePacket(*worldFrame);
	assert(world.at("playerId") == playerA);
	assert(world.at("gameHour") == 18.5);
	assert(world.at("weatherFormId") == "00001234");

	const auto beforeMalformedWorld = host.Stats();
	const auto malformedWorld = host.HandleMessage(
		10,
		EncodeJson(Json{ { "type", "worldState" }, { "gameHour", 25.0 } }),
		1.4,
		100.6);
	assert(malformedWorld.empty());
	assert(host.Stats().worldStateRejected == beforeMalformedWorld.worldStateRejected + 1);

	const Json transformA{
		{ "type", "transform" },
		{ "cellId", "CAFE" },
		{ "worldspaceId", "F00D" },
		{ "x", 0.0 },
		{ "y", 0.0 },
		{ "z", 0.0 },
		{ "angleZ", 0.0 },
		{ "movementType", "spawn" }
	};
	host.HandleMessage(10, EncodeSnapshot(transformA, Family::Transform, 1), 2.0, 101.0);
	Json transformB = transformA;
	transformB["x"] = 100.0;
	host.HandleMessage(20, EncodeSnapshot(transformB, Family::Transform, 1), 2.1, 101.1);

	assert(host.HandleMessage(10, EncodeJson(Hit(playerB, 1, 25.0)), 2.15, 101.15).empty());

	host.HandleMessage(10, EncodeJson(RangedPlayerState(10)), 2.2, 101.2);
	const auto hitFrames = host.HandleMessage(10, EncodeJson(Hit(playerB, 1, 25.0)), 2.21, 101.21);
	const auto* hitFrame = FindFrame(hitFrames, 20, "combatHit");
	assert(hitFrame != nullptr);
	assert(hitFrame->delivery == Delivery::ReliableOrdered);
	const auto hit = DecodePacket(*hitFrame);
	assert(hit.at("playerId") == playerA);
	assert(hit.at("targetPlayerId") == playerB);
	assert(hit.at("weaponFormId") == "00001234");

	const auto beforeDuplicate = host.Stats();
	assert(host.HandleMessage(10, EncodeJson(Hit(playerB, 1, 1.0)), 2.22, 101.22).empty());
	assert(host.Stats().combatHitsRejected == beforeDuplicate.combatHitsRejected + 1);

	assert(host.HandleMessage(10, EncodeJson(Hit(playerA, 2, 1.0)), 2.23, 101.23).empty());
	assert(FindFrame(host.HandleMessage(10, EncodeJson(Hit(playerB, 2, 1.0)), 2.24, 101.24), 20, "combatHit") != nullptr);

	host.HandleMessage(10, EncodeJson(RangedPlayerState(11)), 2.3, 101.3);
	assert(host.HandleMessage(10, EncodeJson(Hit(playerB, 3, 10.0, "9999")), 2.31, 101.31).empty());
	assert(FindFrame(host.HandleMessage(10, EncodeJson(Hit(playerB, 3, 10.0)), 2.32, 101.32), 20, "combatHit") != nullptr);

	assert(FindFrame(host.HandleMessage(10, EncodeJson(Hit(playerB, 4, 2.0)), 2.32, 101.32), 20, "combatHit") != nullptr);
	assert(FindFrame(host.HandleMessage(10, EncodeJson(Hit(playerB, 5, 2.0)), 2.32, 101.32), 20, "combatHit") != nullptr);

	Json distantB = transformB;
	distantB["x"] = 100000.0;
	distantB["movementType"] = "teleport";
	host.HandleMessage(20, EncodeSnapshot(distantB, Family::Transform, 2), 2.7, 101.7);
	host.HandleMessage(10, EncodeJson(RangedPlayerState(12)), 2.71, 101.71);
	assert(host.HandleMessage(10, EncodeJson(Hit(playerB, 6, 1.0)), 2.72, 101.72).empty());

	Json adjacentExteriorB = transformB;
	adjacentExteriorB["cellId"] = "BEEF";
	adjacentExteriorB["x"] = 120.0;
	adjacentExteriorB["movementType"] = "cell_change";
	host.HandleMessage(20, EncodeSnapshot(adjacentExteriorB, Family::Transform, 3), 3.8, 102.0);
	host.HandleMessage(10, EncodeJson(RangedPlayerState(13)), 3.81, 102.01);
	assert(FindFrame(host.HandleMessage(10, EncodeJson(Hit(playerB, 6, 1.0)), 3.82, 102.02), 20, "combatHit") != nullptr);

	Json remoteTransformB = adjacentExteriorB;
	remoteTransformB["worldspaceId"] = "DEAD";
	remoteTransformB["movementType"] = "worldspace_change";
	host.HandleMessage(20, EncodeSnapshot(remoteTransformB, Family::Transform, 4), 4.9, 103.0);
	host.HandleMessage(10, EncodeJson(RangedPlayerState(14)), 4.91, 103.01);
	assert(host.HandleMessage(10, EncodeJson(Hit(playerB, 7, 1.0)), 4.92, 103.02).empty());

	Json nearbyB = transformB;
	nearbyB["movementType"] = "worldspace_change";
	host.HandleMessage(20, EncodeSnapshot(nearbyB, Family::Transform, 5), 6.0, 104.0);
	host.HandleMessage(10, EncodeJson(UnarmedPlayerState(15)), 6.01, 104.01);
	assert(FindFrame(host.HandleMessage(10, EncodeJson(Hit(playerB, 7, 5.0, "")), 6.02, 104.02), 20, "combatHit") != nullptr);
	assert(host.HandleMessage(10, EncodeJson(Hit(playerB, 8, 5.0, "")), 6.03, 104.03).empty());

	Json heartbeatA = transformA;
	heartbeatA["movementType"] = "normal";
	host.HandleMessage(10, EncodeSnapshot(heartbeatA, Family::Transform, 2), 7.0, 104.9);

	Json unarmedFarB = nearbyB;
	unarmedFarB["x"] = 1000.0;
	unarmedFarB["movementType"] = "teleport";
	host.HandleMessage(20, EncodeSnapshot(unarmedFarB, Family::Transform, 6), 7.1, 105.0);
	host.HandleMessage(10, EncodeJson(UnarmedPlayerState(16)), 7.11, 105.01);
	assert(host.HandleMessage(10, EncodeJson(Hit(playerB, 8, 5.0, "")), 7.12, 105.02).empty());

	host.HandleMessage(10, EncodeJson(RangedPlayerState(17)), 8.0, 106.0);
	assert(host.HandleMessage(10, EncodeJson(Hit(playerB, 8, 1.0)), 10.01, 108.01).empty());

	Json meleeB = nearbyB;
	meleeB["x"] = 300.0;
	meleeB["movementType"] = "teleport";
	host.HandleMessage(20, EncodeSnapshot(meleeB, Family::Transform, 7), 11.0, 108.5);
	Json meleeHeartbeatA = transformA;
	meleeHeartbeatA["movementType"] = "normal";
	host.HandleMessage(10, EncodeSnapshot(meleeHeartbeatA, Family::Transform, 3), 11.01, 108.51);
	host.HandleMessage(10, EncodeJson(MeleePlayerState(20)), 11.02, 108.52);
	assert(host.HandleMessage(10, EncodeJson(Hit(playerB, 9, 5.0, "")), 11.03, 108.53).empty());
	assert(host.HandleMessage(10, EncodeJson(Hit(playerB, 9, 5.0, "9999")), 11.04, 108.54).empty());
	assert(FindFrame(host.HandleMessage(10, EncodeJson(Hit(playerB, 9, 5.0)), 11.05, 108.55), 20, "combatHit") != nullptr);
	assert(host.HandleMessage(10, EncodeJson(Hit(playerB, 10, 5.0)), 11.06, 108.56).empty());

	Json meleeFarB = nearbyB;
	meleeFarB["x"] = 1000.0;
	meleeFarB["movementType"] = "teleport";
	host.HandleMessage(20, EncodeSnapshot(meleeFarB, Family::Transform, 8), 12.2, 108.7);
	Json meleeHeartbeatA2 = transformA;
	meleeHeartbeatA2["movementType"] = "normal";
	host.HandleMessage(10, EncodeSnapshot(meleeHeartbeatA2, Family::Transform, 4), 12.21, 108.71);
	host.HandleMessage(10, EncodeJson(MeleePlayerState(21)), 12.22, 108.72);
	assert(host.HandleMessage(10, EncodeJson(Hit(playerB, 10, 5.0)), 12.23, 108.73).empty());

	Json namedAttackB = nearbyB;
	namedAttackB["x"] = 150.0;
	namedAttackB["movementType"] = "teleport";
	host.HandleMessage(20, EncodeSnapshot(namedAttackB, Family::Transform, 9), 13.4, 108.9);
	Json namedAttackHeartbeatA = transformA;
	namedAttackHeartbeatA["movementType"] = "normal";
	host.HandleMessage(10, EncodeSnapshot(namedAttackHeartbeatA, Family::Transform, 5), 13.41, 108.91);
	host.HandleMessage(10, EncodeJson(RangedPlayerState(30)), 13.42, 108.92);
	host.HandleMessage(10, EncodeJson(RangedPlayerState(31)), 13.43, 108.93);

	// Exact correlation is not the same thing as "latest action only". A hit
	// delayed behind the next shot may still name its own previous action while
	// that action remains inside the authorization lifetime.
	const auto olderFrames = host.HandleMessage(10, EncodeJson(Hit(playerB, 11, 1.0, "1234", 30)), 13.44, 108.94);
	const auto* olderHit = FindFrame(olderFrames, 20, "combatHit");
	assert(olderHit != nullptr);
	assert(!DecodePacket(*olderHit).contains("attackSequence"));

	// An action that never existed is rejected and does not spend the hit
	// sequence, so the same sequence can still be used by the real action.
	assert(host.HandleMessage(10, EncodeJson(Hit(playerB, 12, 1.0, "1234", 999)), 13.45, 108.95).empty());
	const auto newerFrames = host.HandleMessage(10, EncodeJson(Hit(playerB, 12, 1.0, "1234", 31)), 13.46, 108.96);
	const auto* newerHit = FindFrame(newerFrames, 20, "combatHit");
	assert(newerHit != nullptr);
	assert(!DecodePacket(*newerHit).contains("attackSequence"));

	const auto disconnectFrames = host.HandleDisconnected(10, 109.0);
	const auto* disconnectFrame = FindFrame(disconnectFrames, 20, "disconnect");
	assert(disconnectFrame != nullptr);
	assert(DecodePacket(*disconnectFrame).at("playerId") == playerA);
	const auto* reassignedHostFrame = FindFrame(disconnectFrames, 20, "worldStateHost");
	assert(reassignedHostFrame != nullptr);
	assert(DecodePacket(*reassignedHostFrame).at("worldStateHostPlayerId") == playerB);

	const auto afterHandoffWorld = host.HandleMessage(
		20,
		EncodeJson(Json{ { "type", "worldState" }, { "gameHour", 6.0 } }),
		10.1,
		109.1);
	assert(afterHandoffWorld.empty());
	assert(host.Stats().worldStatePacketsReceived == 2);
	assert(host.Stats().combatHitsRouted >= 7);

	return 0;
}

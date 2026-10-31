#include "ServerProtocol.h"

#include "F4TAuthoritativePacketValidation.h"
#include "F4TGnsSnapshotEnvelope.h"
#include "F4TPlayerStateValidation.h"
#include "F4TTransformValidation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Json = nlohmann::json;
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
		const auto text = a_packet.dump();
		std::vector<std::uint8_t> output(F4T::GnsSnapshotEnvelope::HeaderSize + text.size());
		assert(F4T::GnsSnapshotEnvelope::WriteHeader(std::span<std::uint8_t>{ output }, a_family, a_sequence));
		std::copy(text.begin(), text.end(), output.begin() + static_cast<std::ptrdiff_t>(F4T::GnsSnapshotEnvelope::HeaderSize));
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

	const OutboundFrame& RequireFrame(
		const std::vector<OutboundFrame>& a_frames,
		std::uint32_t a_connectionId,
		std::string_view a_type)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && !frame.payload.empty() &&
				DecodePacket(frame).value("type", std::string{}) == a_type) {
				return frame;
			}
		}
		assert(false && "required parity frame was not found");
		return a_frames.front();
	}

	Json WithoutServerAnnotations(Json a_packet, bool a_removeFullReplace = false)
	{
		a_packet.erase("playerId");
		a_packet.erase("serverTime");
		if (a_removeFullReplace) {
			a_packet.erase("fullReplace");
		}
		return a_packet;
	}

	void RequireByteIdentical(const Json& a_expected, const Json& a_actual)
	{
		assert(a_expected.dump() == a_actual.dump());
	}
}

int main()
{
	using CommonwealthOnline::Server::ProtocolHost;

	ProtocolHost host{ "Parity Server", "shared-core parity fixtures" };
	const auto welcomeA = host.HandleConnected(10, 1000.0);
	const auto playerA = DecodePacket(welcomeA.front()).at("playerId").get<std::uint32_t>();
	const auto readyA = host.HandleMessage(
		10,
		EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }),
		1.0,
		1000.1);
	assert(DecodePacket(RequireFrame(readyA, 10, "sessionReady")).at("playerId") == playerA);

	const auto welcomeB = host.HandleConnected(20, 1000.2);
	const auto playerB = DecodePacket(welcomeB.front()).at("playerId").get<std::uint32_t>();
	const auto readyB = host.HandleMessage(
		20,
		EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }),
		1.1,
		1000.3);
	assert(DecodePacket(RequireFrame(readyB, 20, "sessionReady")).at("playerId") == playerB);

	const Json spawnA{
		{ "type", "transform" },
		{ "x", 0.0 }, { "y", 0.0 }, { "z", 0.0 }, { "angleZ", 0.0 },
		{ "cellId", "CAFE" }, { "worldspaceId", "F00D" }, { "movementType", "spawn" }
	};
	const auto spawnAFrames = host.HandleMessage(10, EncodeSnapshot(spawnA, Family::Transform, 1), 2.0, 1001.0);
	const auto authority = DecodePacket(RequireFrame(spawnAFrames, 10, "npcAuthority"));
	const auto authorityEpoch = authority.at("authorityEpoch").get<std::uint32_t>();
	assert(authority.at("authorityPlayerId") == playerA);

	Json spawnB = spawnA;
	spawnB["x"] = 20.0;
	(void)host.HandleMessage(20, EncodeSnapshot(spawnB, Family::Transform, 1), 2.1, 1001.1);

	const Json transformFixture{
		{ "type", "transform" },
		{ "x", 12.5 }, { "y", -22.0 }, { "z", 33.25 }, { "angleZ", 1.75 },
		{ "cellId", "cafe" }, { "worldspaceId", "f00d" }, { "movementType", "normal" },
		{ "movementSpeed", 222.5 }, { "animationGraphSpeed", -1.0 },
		{ "animationDirection", 45.0 }, { "aimPitch", -12.0 }, { "turnDelta", 90.0 },
		{ "isMoving", true }, { "isSprinting", false },
		{ "actorStateFlags1", 17 }, { "actorStateFlags2", 23 },
		{ "unknownTelemetry", "preserved" }
	};
	const auto normalizedTransform = F4T::TransformValidation::NormalizeTransform(transformFixture);
	assert(normalizedTransform.has_value());
	const auto transformFrames = host.HandleMessage(
		10,
		EncodeSnapshot(transformFixture, Family::Transform, 2),
		2.2,
		1001.2);
	const auto serverTransform = WithoutServerAnnotations(DecodePacket(RequireFrame(transformFrames, 20, "transform")));
	RequireByteIdentical(*normalizedTransform, serverTransform);

	// The attack the combat hit below will name.
	constexpr std::uint32_t combatActionSequence = 7;

	const Json playerStateFixture{
		{ "type", "playerState" },
		{ "characterName", "Parity Nomad" },
		{ "equippedItems", Json::array({
			Json{ { "slot", "rightHand" }, { "formId", "1f66a" } },
			Json{ { "slot", "armor0" }, { "formId", "23432" } }
		}) },
		{ "appearance", Json{
			{ "raceFormId", "14" }, { "height", 1.0 }, { "isFemale", false },
			{ "morphWeight", Json{ { "thin", 0.1 }, { "muscular", 0.2 }, { "large", 0.3 } } }
		} },
		{ "actionEvents", Json::array({ Json{
			{ "sequence", combatActionSequence }, { "type", 3 }, { "eventName", "fireSingle" }
		} }) },
		{ "serverOnlyInjection", "must-be-removed" }
	};
	const auto normalizedPlayerState = F4T::PlayerStateValidation::NormalizePlayerState(playerStateFixture);
	assert(normalizedPlayerState.has_value());
	const auto playerStateFrames = host.HandleMessage(10, EncodeJson(playerStateFixture), 2.3, 1001.3);
	const auto serverPlayerState = WithoutServerAnnotations(DecodePacket(RequireFrame(playerStateFrames, 20, "playerState")));
	RequireByteIdentical(*normalizedPlayerState, serverPlayerState);

	const Json worldStateFixture{
		{ "type", "worldState" },
		{ "gameHour", 17.25 },
		{ "gameDaysPassed", 101.5 },
		{ "weatherFormId", "2b52a" },
		{ "unknownWorldField", "preserved" }
	};
	const auto normalizedWorldState = F4T::AuthoritativePacketValidation::NormalizeWorldState(worldStateFixture);
	assert(normalizedWorldState.has_value());
	const auto worldFrames = host.HandleMessage(10, EncodeJson(worldStateFixture), 2.4, 1001.4);
	const auto serverWorldState = WithoutServerAnnotations(DecodePacket(RequireFrame(worldFrames, 20, "worldState")));
	RequireByteIdentical(*normalizedWorldState, serverWorldState);

	const Json npcFixture{
		{ "type", "npcState" },
		{ "authorityEpoch", authorityEpoch },
		{ "authorityCellId", "cafe" },
		{ "authorityWorldspaceId", "f00d" },
		{ "npcs", Json::array({ Json{
			{ "sourceFormId", "1234" },
			{ "cellId", "cafe" }, { "worldspaceId", "f00d" },
			{ "x", 15.0 }, { "y", 25.0 }, { "z", 35.0 }, { "angleZ", 45.0 },
			{ "name", "Parity NPC" }
		} }) }
	};
	const auto normalizedNpc = F4T::AuthoritativePacketValidation::NormalizeNpcState(npcFixture);
	assert(normalizedNpc.has_value());
	const auto npcFrames = host.HandleMessage(10, EncodeSnapshot(npcFixture, Family::NpcState, 1), 2.5, 1001.5);
	const auto serverNpc = WithoutServerAnnotations(DecodePacket(RequireFrame(npcFrames, 20, "npcState")), true);
	RequireByteIdentical(*normalizedNpc, serverNpc);

	const Json combatFixture{
		{ "type", "combatHit" },
		{ "sequence", 1 },
		{ "attackSequence", combatActionSequence },
		{ "targetPlayerId", playerB },
		{ "damage", 21.5 },
		{ "weaponFormId", "1f66a" },
		{ "unknownCombatField", "preserved" }
	};
	auto normalizedCombat = F4T::AuthoritativePacketValidation::NormalizeCombatHit(combatFixture);
	assert(normalizedCombat.has_value());
	// attackSequence names the attacker's own action so the server can check the
	// hit against the right one. It is consumed there and deliberately not
	// forwarded, so parity is against the rest of the normalized packet.
	assert(normalizedCombat->contains("attackSequence"));
	normalizedCombat->erase("attackSequence");
	const auto combatFrames = host.HandleMessage(10, EncodeJson(combatFixture), 2.6, 1001.6);
	const auto serverCombat = WithoutServerAnnotations(DecodePacket(RequireFrame(combatFrames, 20, "combatHit")));
	RequireByteIdentical(*normalizedCombat, serverCombat);

	Json invalidTransform = transformFixture;
	invalidTransform["x"] = 1.0e12;
	assert(!F4T::TransformValidation::NormalizeTransform(invalidTransform).has_value());
	const auto invalidTransformFrames = host.HandleMessage(
		10,
		EncodeSnapshot(invalidTransform, Family::Transform, 3),
		2.7,
		1001.7);
	assert(invalidTransformFrames.empty());

	Json invalidPlayerState = playerStateFixture;
	invalidPlayerState["characterName"] = std::string(129, 'x');
	assert(!F4T::PlayerStateValidation::NormalizePlayerState(invalidPlayerState).has_value());
	assert(host.HandleMessage(10, EncodeJson(invalidPlayerState), 2.8, 1001.8).empty());

	Json invalidCombat = combatFixture;
	invalidCombat["targetPlayerId"] = 0;
	assert(!F4T::AuthoritativePacketValidation::NormalizeCombatHit(invalidCombat).has_value());
	assert(host.HandleMessage(10, EncodeJson(invalidCombat), 2.9, 1001.9).empty());

	return 0;
}

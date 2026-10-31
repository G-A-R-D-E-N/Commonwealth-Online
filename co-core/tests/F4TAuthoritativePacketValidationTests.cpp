#include "F4TAuthoritativePacketValidation.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cmath>

int main()
{
	using Json = nlohmann::json;
	using namespace F4T::AuthoritativePacketValidation;

	const auto world = NormalizeWorldState(Json{
		{ "type", "worldState" },
		{ "gameHour", 24.0 },
		{ "gameDaysPassed", 10'000'000.0 },
		{ "weatherFormId", "14" },
		{ "unknown", true }
	});
	assert(world.has_value());
	assert(world->at("weatherFormId") == "00000014");
	assert(!world->contains("unknown"));
	assert(!NormalizeWorldState(Json{ { "gameHour", 24.1 } }).has_value());
	assert(!NormalizeWorldState(Json{ { "gameDaysPassed", -1.0 } }).has_value());
	assert(!NormalizeWorldState(Json{ { "weatherFormId", 14 } }).has_value());

	const Json npcPacket{
		{ "type", "npcState" },
		{ "authorityEpoch", 2 },
		{ "authorityCellId", "CAFE" },
		{ "authorityWorldspaceId", "F00D" },
		{ "npcs", Json::array({ Json{
			{ "sourceFormId", "1234" },
			{ "cellId", "CAFE" },
			{ "worldspaceId", 123 },
			{ "x", 1.0 },
			{ "y", 2.0 },
			{ "z", 3.0 },
			{ "angleZ", 4.0 },
			{ "unknownNpcField", "preserve" }
		} }) },
		{ "unknownPacketField", "preserve" }
	};
	const auto npc = NormalizeNpcState(npcPacket);
	assert(npc.has_value());
	assert(npc->at("authorityEpoch") == 2);
	assert(npc->at("authorityCellId") == "0000CAFE");
	assert(npc->at("authorityWorldspaceId") == "0000F00D");
	assert(npc->at("npcs")[0].at("sourceFormId") == "00001234");
	assert(npc->at("npcs")[0].at("cellId") == "0000CAFE");
	assert(npc->at("npcs")[0].at("worldspaceId") == "");
	assert(!npc->at("npcs")[0].contains("unknownNpcField"));
	assert(!npc->contains("unknownPacketField"));

	Json floatingEpoch = npcPacket;
	floatingEpoch["authorityEpoch"] = 3.0;
	const auto floatingNpc = NormalizeNpcState(floatingEpoch);
	assert(floatingNpc.has_value());
	assert(floatingNpc->at("authorityEpoch") == 3);

	Json fractionalEpoch = npcPacket;
	fractionalEpoch["authorityEpoch"] = 3.5;
	assert(!NormalizeNpcState(fractionalEpoch).has_value());

	Json invalidNpc = npcPacket;
	invalidNpc["npcs"][0]["sourceFormId"] = "0";
	assert(!NormalizeNpcState(invalidNpc).has_value());
	invalidNpc = npcPacket;
	invalidNpc["authorityEpoch"] = 0;
	assert(!NormalizeNpcState(invalidNpc).has_value());
	invalidNpc = npcPacket;
	invalidNpc["authorityWorldspaceId"] = 123;
	assert(!NormalizeNpcState(invalidNpc).has_value());
	invalidNpc = npcPacket;
	invalidNpc["npcs"] = Json::array();
	for (int index = 0; index < 65; ++index) {
		invalidNpc["npcs"].push_back(npcPacket.at("npcs")[0]);
	}
	assert(!NormalizeNpcState(invalidNpc).has_value());

	const auto hit = NormalizeCombatHit(Json{
		{ "type", "combatHit" },
		{ "targetPlayerId", 2 },
		{ "sequence", 9 },
		{ "attackSequence", 4 },
		{ "damage", 10.5 },
		{ "weaponFormId", "0" },
		{ "unknown", true }
	});
	assert(hit.has_value());
	assert(hit->at("weaponFormId") == "00000000");
	assert(hit->at("attackSequence") == 4);
	assert(!hit->contains("unknown"));

	const auto floatingHit = NormalizeCombatHit(Json{
		{ "targetPlayerId", 2.0 },
		{ "sequence", 9.0 },
		{ "attackSequence", 4.0 },
		{ "damage", 1.0 }
	});
	assert(floatingHit.has_value());
	assert(floatingHit->at("targetPlayerId") == 2);
	assert(floatingHit->at("sequence") == 9);
	assert(floatingHit->at("attackSequence") == 4);

	assert(!NormalizeCombatHit(Json{ { "targetPlayerId", 1 }, { "sequence", 1 }, { "damage", 1.0 } }).has_value());
	assert(!NormalizeCombatHit(Json{ { "targetPlayerId", 1 }, { "sequence", 1 }, { "attackSequence", 0 }, { "damage", 1.0 } }).has_value());
	assert(!NormalizeCombatHit(Json{ { "targetPlayerId", 1 }, { "sequence", 1 }, { "attackSequence", 1.5 }, { "damage", 1.0 } }).has_value());
	assert(!NormalizeCombatHit(Json{ { "targetPlayerId", 2.5 }, { "sequence", 1 }, { "attackSequence", 1 }, { "damage", 1.0 } }).has_value());
	assert(!NormalizeCombatHit(Json{ { "targetPlayerId", 1 }, { "sequence", 1.5 }, { "attackSequence", 1 }, { "damage", 1.0 } }).has_value());
	assert(!NormalizeCombatHit(Json{ { "targetPlayerId", 0 }, { "sequence", 1 }, { "attackSequence", 1 }, { "damage", 1.0 } }).has_value());
	assert(!NormalizeCombatHit(Json{ { "targetPlayerId", 1 }, { "sequence", 0 }, { "attackSequence", 1 }, { "damage", 1.0 } }).has_value());
	assert(!NormalizeCombatHit(Json{ { "targetPlayerId", 1 }, { "sequence", 1 }, { "attackSequence", 1 }, { "damage", 0.0 } }).has_value());
	assert(!NormalizeCombatHit(Json{ { "targetPlayerId", 1 }, { "sequence", 1 }, { "attackSequence", 1 }, { "damage", 10000.1 } }).has_value());

	Json healthNpc = npcPacket;
	healthNpc["npcs"][0]["health"] = 80.0;
	healthNpc["npcs"][0]["maxHealth"] = 100.0;
	const auto healthy = NormalizeNpcState(healthNpc);
	assert(healthy.has_value());
	assert(healthy->at("npcs")[0].at("health") == 80.0);
	assert(healthy->at("npcs")[0].at("maxHealth") == 100.0);
	const auto unhealthy = NormalizeNpcState(npcPacket);
	assert(unhealthy.has_value());
	assert(!unhealthy->at("npcs")[0].contains("health"));

	Json halfHealth = healthNpc;
	halfHealth["npcs"][0].erase("maxHealth");
	assert(!NormalizeNpcState(halfHealth).has_value());
	halfHealth = healthNpc;
	halfHealth["npcs"][0].erase("health");
	assert(!NormalizeNpcState(halfHealth).has_value());
	Json overHealth = healthNpc;
	overHealth["npcs"][0]["health"] = 101.0;
	assert(!NormalizeNpcState(overHealth).has_value());
	Json negativeHealth = healthNpc;
	negativeHealth["npcs"][0]["health"] = -1.0;
	assert(!NormalizeNpcState(negativeHealth).has_value());
	Json stringHealth = healthNpc;
	stringHealth["npcs"][0]["health"] = "80";
	assert(!NormalizeNpcState(stringHealth).has_value());

	const Json npcHitPacket{
		{ "type", "npcHit" },
		{ "npcId", 42 },
		{ "cellId", "CAFE" },
		{ "worldspaceId", "F00D" },
		{ "sourceFormId", "1234" },
		{ "sequence", 7 },
		{ "attackSequence", 4 },
		{ "damage", 12.5 },
		{ "authorityEpoch", 3 },
		{ "weaponFormId", "1234" },
		{ "unknown", true }
	};
	const auto npcHit = NormalizeNpcHit(npcHitPacket);
	assert(npcHit.has_value());
	assert(npcHit->at("type") == "npcHit");
	assert(npcHit->at("npcId") == 42);
	assert(npcHit->at("cellId") == "0000CAFE");
	assert(npcHit->at("worldspaceId") == "0000F00D");
	assert(npcHit->at("sourceFormId") == "00001234");
	assert(npcHit->at("sequence") == 7);
	assert(npcHit->at("attackSequence") == 4);
	assert(npcHit->at("authorityEpoch") == 3);
	assert(npcHit->at("damage") == 12.5);
	assert(npcHit->at("weaponFormId") == "00001234");
	assert(!npcHit->contains("unknown"));

	Json unarmed = npcHitPacket;
	unarmed.erase("weaponFormId");
	unarmed.erase("worldspaceId");
	const auto unarmedNpcHit = NormalizeNpcHit(unarmed);
	assert(unarmedNpcHit.has_value());
	assert(unarmedNpcHit->at("worldspaceId") == "");
	assert(!unarmedNpcHit->contains("weaponFormId"));

	const auto rejectNpcHit = [&npcHitPacket](const char* a_field, Json a_value) {
		Json packet = npcHitPacket;
		packet[a_field] = std::move(a_value);
		return !NormalizeNpcHit(packet).has_value();
	};
	assert(rejectNpcHit("npcId", 0));
	assert(rejectNpcHit("npcId", 1.5));
	assert(rejectNpcHit("sequence", 0));
	assert(rejectNpcHit("attackSequence", 0));
	assert(rejectNpcHit("authorityEpoch", 0));
	assert(rejectNpcHit("authorityEpoch", 3.5));
	assert(rejectNpcHit("damage", 0.0));
	assert(rejectNpcHit("damage", 10000.1));
	assert(rejectNpcHit("sourceFormId", "0"));
	assert(rejectNpcHit("cellId", 123));
	assert(rejectNpcHit("weaponFormId", 1234));

	Json missingEpoch = npcHitPacket;
	missingEpoch.erase("authorityEpoch");
	assert(!NormalizeNpcHit(missingEpoch).has_value());

	const Json npcDamagePacket{
		{ "type", "npcDamage" },
		{ "targetPlayerId", 2 },
		{ "npcId", 42 },
		{ "cellId", "CAFE" },
		{ "worldspaceId", "F00D" },
		{ "sourceFormId", "1234" },
		{ "sequence", 7 },
		{ "damage", 12.5 },
		{ "authorityEpoch", 3 },
		{ "weaponFormId", "1234" },
		{ "unknown", true }
	};
	const auto npcDamage = NormalizeNpcDamage(npcDamagePacket);
	assert(npcDamage.has_value());
	assert(npcDamage->at("type") == "npcDamage");
	assert(npcDamage->at("targetPlayerId") == 2);
	assert(npcDamage->at("npcId") == 42);
	assert(npcDamage->at("cellId") == "0000CAFE");
	assert(npcDamage->at("worldspaceId") == "0000F00D");
	assert(npcDamage->at("sourceFormId") == "00001234");
	assert(npcDamage->at("sequence") == 7);
	assert(npcDamage->at("authorityEpoch") == 3);
	assert(npcDamage->at("damage") == 12.5);
	assert(npcDamage->at("weaponFormId") == "00001234");
	assert(!npcDamage->contains("unknown"));
	assert(!npcDamage->contains("attackSequence"));

	const auto rejectNpcDamage = [&npcDamagePacket](const char* a_field, Json a_value) {
		Json packet = npcDamagePacket;
		packet[a_field] = std::move(a_value);
		return !NormalizeNpcDamage(packet).has_value();
	};
	assert(rejectNpcDamage("damage", 0.0));
	assert(rejectNpcDamage("damage", 1000.5));
	assert(rejectNpcDamage("damage", std::nan("")));
	assert(rejectNpcDamage("targetPlayerId", 0));
	assert(rejectNpcDamage("npcId", 0));
	assert(rejectNpcDamage("sequence", 0));
	assert(rejectNpcDamage("authorityEpoch", 0));
	assert(rejectNpcDamage("weaponFormId", "XYZ"));

	return 0;
}

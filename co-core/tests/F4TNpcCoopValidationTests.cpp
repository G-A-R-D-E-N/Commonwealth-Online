// source:CO-BETA/docs/protocol/packets.md
#include "F4TNpcCoopValidation.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <string>

int main()
{
	using Json = nlohmann::json;
	using namespace F4T::NpcCoopValidation;

	const Json lootPacket{
		{ "type", "npcLoot" },
		{ "npcId", 42 },
		{ "cellId", "CAFE" },
		{ "worldspaceId", "F00D" },
		{ "sourceFormId", "1234" },
		{ "authorityEpoch", 2 },
		{ "revision", 1 },
		{ "unknownPacketField", "drop" },
		{ "items", Json::array({
			Json{ { "formId", "0001F669" }, { "count", 3 } },
			Json{ { "formId", "0001F669" }, { "count", 2 } },
			Json{ { "formId", "00000F00" }, { "count", 1 } }
		}) }
	};
	const auto loot = NormalizeNpcLoot(lootPacket);
	assert(loot.has_value());
	assert(loot->at("type") == "npcLoot");
	assert(loot->at("npcId") == 42);
	assert(loot->at("cellId") == "0000CAFE");
	assert(loot->at("worldspaceId") == "0000F00D");
	assert(loot->at("sourceFormId") == "00001234");
	assert(loot->at("authorityEpoch") == 2);
	assert(loot->at("revision") == 1);
	assert(!loot->contains("unknownPacketField"));
	assert(loot->at("items").size() == 2);
	assert(loot->at("items")[0] == (Json{ { "formId", "00000F00" }, { "count", 1 } }));
	assert(loot->at("items")[1] == (Json{ { "formId", "0001F669" }, { "count", 5 } }));

	Json emptyLoot = lootPacket;
	emptyLoot["items"] = Json::array();
	assert(NormalizeNpcLoot(emptyLoot).has_value());

	const auto rejectLoot = [&lootPacket](const char* a_field, Json a_value) {
		Json packet = lootPacket;
		packet[a_field] = std::move(a_value);
		return !NormalizeNpcLoot(packet).has_value();
	};
	const auto rejectItems = [&lootPacket](Json a_items) {
		Json packet = lootPacket;
		packet["items"] = std::move(a_items);
		return !NormalizeNpcLoot(packet).has_value();
	};
	assert(rejectLoot("revision", 0));
	assert(rejectLoot("revision", "1"));
	assert(rejectLoot("npcId", 0));
	assert(rejectLoot("authorityEpoch", 0));
	assert(rejectLoot("cellId", "XYZ"));
	assert(rejectLoot("items", "not an array"));
	assert(rejectItems(Json::array({ Json{ { "formId", "0001F669" }, { "count", 0 } } })));
	assert(rejectItems(Json::array({ Json{ { "formId", "0001F669" }, { "count", 65536 } } })));
	assert(rejectItems(Json::array({ Json{ { "formId", "00000000" }, { "count", 1 } } })));
	assert(rejectItems(Json::array({ Json{ { "formId", "FF000000" }, { "count", 1 } } })));
	assert(rejectItems(Json::array({ Json{ { "formId", "1234" }, { "count", 1 } } })));
	assert(rejectItems(Json::array({ Json{ { "formId", "XYZ12345" }, { "count", 1 } } })));
	assert(rejectItems(Json::array({ Json{ { "count", 1 } } })));
	assert(rejectItems(Json::array({
		Json{ { "formId", "0001F669" }, { "count", 65535 } },
		Json{ { "formId", "0001F669" }, { "count", 1 } }
	})));

	Json oversizedItems = Json::array();
	for (int index = 0; index < 65; ++index) {
		oversizedItems.push_back(Json{ { "formId", "0001F669" }, { "count", 1 } });
	}
	assert(rejectItems(oversizedItems));

	const Json takePacket{
		{ "type", "npcLootTake" },
		{ "operationId", "take-1" },
		{ "npcId", 42 },
		{ "cellId", "CAFE" },
		{ "worldspaceId", "F00D" },
		{ "sourceFormId", "1234" },
		{ "authorityEpoch", 2 },
		{ "formId", "0001F669" },
		{ "count", 3 }
	};
	const auto take = NormalizeNpcLootTake(takePacket);
	assert(take.has_value());
	assert(take->at("type") == "npcLootTake");
	assert(take->at("operationId") == "take-1");
	assert(take->at("cellId") == "0000CAFE");
	assert(take->at("formId") == "0001F669");
	assert(take->at("count") == 3);

	const auto rejectTake = [&takePacket](const char* a_field, Json a_value) {
		Json packet = takePacket;
		packet[a_field] = std::move(a_value);
		return !NormalizeNpcLootTake(packet).has_value();
	};
	assert(rejectTake("operationId", ""));
	assert(rejectTake("operationId", std::string(65, 'a')));
	assert(rejectTake("operationId", 7));
	assert(rejectTake("formId", "FF000000"));
	assert(rejectTake("formId", "0001F66"));
	assert(rejectTake("count", 0));
	assert(rejectTake("count", 65536));
	assert(rejectTake("npcId", 0));
	assert(!NormalizeNpcLootTake(Json{ { "type", "npcLootTake" } }).has_value());

	const Json resultPacket{
		{ "type", "npcLootResult" },
		{ "operationId", "take-1" },
		{ "status", "accepted" },
		{ "formId", "0001F669" },
		{ "count", 3 },
		{ "revision", 1 },
		{ "takerPlayerId", 7 }
	};
	const auto result = NormalizeNpcLootResult(resultPacket);
	assert(result.has_value());
	assert(result->at("type") == "npcLootResult");
	assert(result->at("status") == "accepted");
	assert(result->at("revision") == 1);
	assert(result->at("count") == 3);
	assert(result->at("takerPlayerId") == 7);
	assert(!result->contains("reason"));

	Json rejectedResult = resultPacket;
	rejectedResult["status"] = "rejected";
	rejectedResult["reason"] = "authorityOffline";
	rejectedResult["revision"] = 0;
	const auto rejected = NormalizeNpcLootResult(rejectedResult);
	assert(rejected.has_value());
	assert(rejected->at("reason") == "authorityOffline");

	const auto rejectResult = [&resultPacket](const char* a_field, Json a_value) {
		Json packet = resultPacket;
		packet[a_field] = std::move(a_value);
		return !NormalizeNpcLootResult(packet).has_value();
	};
	assert(rejectResult("status", "maybe"));
	assert(rejectResult("status", 1));
	assert(rejectResult("revision", 0));
	assert(rejectResult("count", 0));
	assert(rejectResult("formId", "FF000000"));
	assert(rejectResult("operationId", ""));
	assert(rejectResult("reason", std::string(65, 'r')));
	assert(rejectResult("reason", 1));
	assert(!rejectResult("reason", std::string(64, 'r')));
	assert(rejectResult("takerPlayerId", 0));
	assert(rejectResult("takerPlayerId", "7"));
	Json missingTaker = resultPacket;
	missingTaker.erase("takerPlayerId");
	assert(!NormalizeNpcLootResult(missingTaker).has_value());
	const Json serverRejected{
		{ "type", "npcLootResult" },
		{ "operationId", "take-1" },
		{ "status", "rejected" },
		{ "reason", "authorityOffline" },
		{ "formId", "0001F669" },
		{ "count", 3 },
		{ "revision", 0 },
		{ "takerPlayerId", 7 },
		{ "serverTime", 102.0 }
	};
	assert(NormalizeNpcLootResult(serverRejected).has_value());

	return 0;
}

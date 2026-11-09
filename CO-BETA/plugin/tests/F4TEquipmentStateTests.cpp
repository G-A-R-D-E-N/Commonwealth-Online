#include "F4TEquipmentState.h"
#include "F4TGnsProtocolAdapter.h"

#include <cassert>

int main()
{
	// Fixtures must remain engine-independent; source:co-core/src/validation/F4TPlayerStateValidation.cpp.
	using namespace F4T;
	using Json = nlohmann::json;
	const Json recipe = Json::array({ { { "mod", "00054321" }, { "index", 2 }, { "rank", 3 } } });
	// Synthetic weapon recipe fixtures, not game data; source:co-core/src/validation/F4TPlayerStateValidation.cpp.
	const Json weaponMods = Json::array({
		{ { "mod", "00054321" }, { "index", 2 }, { "rank", 3 } },
		{ { "mod", "000AAAAA" }, { "index", 0 }, { "rank", 0 } },
		{ { "mod", "00054321" }, { "index", 255 }, { "rank", 255 } } });
	const Json weaponWire = Json::array({
		{ { "slot", "body" }, { "formId", "00067890" } },
		{ { "slot", "rightHand" }, { "formId", "00012345" }, { "mods", weaponMods } } });
	const auto weaponDecoded = EquipmentState::Decode(weaponWire);
	assert(weaponDecoded && weaponDecoded->size() == 2 && !weaponDecoded->at(0).mods);
	assert(weaponDecoded->at(1).mods && weaponDecoded->at(1).mods->size() == 3);
	assert(weaponDecoded->at(1).mods->at(1) == (RemotePlayerState::RemoteItemMod{ 0x000AAAAA, 0, 0 }));
	assert(weaponDecoded->at(1).mods->at(2) == (RemotePlayerState::RemoteItemMod{ 0x00054321, 255, 255 }));
	assert(EquipmentState::Encode(*weaponDecoded) == weaponWire);
	auto reordered = *weaponDecoded;
	std::swap(reordered[1].mods->at(0), reordered[1].mods->at(1));
	assert(reordered != *weaponDecoded);
	auto fullRecipe = Json::array();
	for (int i = 0; i < 32; ++i) {
		fullRecipe.push_back({ { "mod", "00054321" }, { "index", i }, { "rank", 1 } });
	}
	const Json fullWire = Json::array({ { { "slot", "rightHand" }, { "formId", "00012345" }, { "mods", fullRecipe } } });
	assert(EquipmentState::Decode(fullWire) && EquipmentState::Decode(fullWire)->at(0).mods->size() == 32);
	assert(EquipmentState::Encode(*EquipmentState::Decode(fullWire)) == fullWire);
	{
		using Mods = std::vector<RemotePlayerState::RemoteItemMod>;
		const Mods base{ { 0x00054321, 2, 3 }, { 0x000AAAAA, 0, 0 } };
		assert(EquipmentState::SameMods(base, base));
		assert(EquipmentState::SameMods(base, { base[1], base[0] }));
		assert(EquipmentState::SameMods({ base[1], base[0] }, base));
		assert(EquipmentState::SameMods({}, {}));
		assert(!EquipmentState::SameMods(base, {}));
		assert(!EquipmentState::SameMods(base, { base[0] }));
		assert(!EquipmentState::SameMods(base, { base[0], { 0x000AAAAA, 0, 1 } }));
		assert(!EquipmentState::SameMods(base, { base[0], { 0x000AAAAA, 1, 0 } }));
		assert(!EquipmentState::SameMods(base, { base[0], { 0x000AAAAB, 0, 0 } }));
		assert(!EquipmentState::SameMods({ base[0], base[0] }, { base[0], base[1] }));
		assert(!EquipmentState::SameMods({ base[0] }, { base[0], base[0] }));
	}
	const Json wire = Json::array({ { { "slot", "rightHand" }, { "formId", "00012345" }, { "mods", recipe } } });
	const auto decoded = EquipmentState::Decode(wire);
	assert(decoded && decoded->size() == 1);
	assert(EquipmentState::Encode(*decoded) == wire);
	const auto relay = PlayerStateValidation::NormalizePlayerState({ { "equippedItems", *EquipmentState::Encode(*decoded) } });
	assert(relay && EquipmentState::Decode((*relay)["equippedItems"]) == decoded);
	auto changed = *decoded;
	changed[0].mods->at(0).rank++;
	assert(changed != *decoded);
	GnsProtocolAdapter::Session session;
	assert(!session.AdaptInbound(Json{ { "type", "welcome" }, { "playerId", 7 }, { "protocolVersion", 2 } }.dump()).protocolViolation);
	assert(!session.AdaptInbound(Json{ { "type", "sessionReady" }, { "playerId", 7 }, { "protocolVersion", 2 },
		{ "serverProtocolVersion", 2 }, { "worldStateHostPlayerId", 7 } }.dump()).protocolViolation);
	assert(session.IsReady());
	Json transform{ { "type", "transform" }, { "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 }, { "angleZ", 0.0 },
		{ "cellId", "0000003C" }, { "worldspaceId", "0000003D" }, { "equippedItems", wire } };
	const auto outbound = session.AdaptOutbound(transform.dump());
	assert(outbound.size() == 2 && outbound[1].delivery == GnsProtocolAdapter::Delivery::ReliableOrdered);
	auto state = Json::parse(outbound[1].payload);
	assert(state["equippedItems"] == wire);
	assert(session.AdaptOutbound(transform.dump()).size() == 1);
	transform["equippedItems"] = *EquipmentState::Encode(changed);
	const auto modChanged = session.AdaptOutbound(transform.dump());
	assert(modChanged.size() == 2 && modChanged[1].delivery == GnsProtocolAdapter::Delivery::ReliableOrdered);
	state["playerId"] = 11;
	assert(!session.AdaptInbound(state.dump()).protocolViolation);
	transform.erase("equippedItems");
	transform["playerId"] = 11;
	const auto incoming = session.AdaptInbound(transform.dump());
	assert(incoming.legacyMessages.size() == 1);
	assert(EquipmentState::Decode(Json::parse(incoming.legacyMessages[0])["equippedItems"]) == decoded);
	const auto legacy = EquipmentState::Decode(Json::array({ { { "slot", "rightHand" }, { "formId", "00012345" } } }));
	assert(legacy && !legacy->at(0).mods);
	assert(!EquipmentState::Encode(*legacy)->at(0).contains("mods"));
	auto emptyRecipe = wire;
	emptyRecipe[0]["mods"] = Json::array();
	assert(EquipmentState::Decode(emptyRecipe)->at(0).mods->empty());
	assert(EquipmentState::Decode(emptyRecipe) != legacy);
	auto invalid = wire;
	invalid[0]["mods"][0]["rank"] = 256;
	assert(!EquipmentState::Decode(invalid));
	invalid = wire;
	invalid.push_back(wire[0]);
	assert(!EquipmentState::Decode(invalid));
	invalid = wire;
	invalid[0]["mods"] = Json::array();
	for (int i = 0; i < 33; ++i) {
		invalid[0]["mods"].push_back(recipe[0]);
	}
	assert(!EquipmentState::Decode(invalid));
	invalid = wire;
	invalid[0]["formId"] = "";
	assert(!EquipmentState::Decode(invalid));
	assert(EquipmentState::Decode(Json::array())->empty());
	invalid = wire;
	invalid[0].erase("formId");
	invalid[0].erase("mods");
	assert(!EquipmentState::Decode(invalid));
	for (const auto& badForm : { Json{}, Json(7), Json(true), Json::object(), Json::array() }) {
		invalid = wire;
		invalid[0]["formId"] = badForm;
		invalid[0].erase("mods");
		assert(!EquipmentState::Decode(invalid));
	}
	invalid = wire;
	invalid[0]["formId"] = "00000000";
	assert(!EquipmentState::Decode(invalid));

	// Constraint: armor recipes ride the same entries as weapons; source:docs/code-notes/equipment-state.md (armor recipe capture).
	const Json armorWire = Json::array({
		{ { "slot", "body" }, { "formId", "00067890" }, { "mods", weaponMods } },
		{ { "slot", "rightHand" }, { "formId", "00012345" } } });
	const auto armorDecoded = EquipmentState::Decode(armorWire);
	assert(armorDecoded && armorDecoded->size() == 2);
	assert(armorDecoded->at(0).mods && armorDecoded->at(0).mods->size() == 3);
	assert(!armorDecoded->at(1).mods);
	assert(EquipmentState::Encode(*armorDecoded) == armorWire);
	const auto armorRelay = PlayerStateValidation::NormalizePlayerState({ { "equippedItems", *EquipmentState::Encode(*armorDecoded) } });
	assert(armorRelay && EquipmentState::Decode((*armorRelay)["equippedItems"]) == armorDecoded);

	auto armorModSwap = *armorDecoded;
	armorModSwap[0].mods->at(0).rank++;
	assert(armorModSwap != *armorDecoded);
	auto armorToBase = *armorDecoded;
	armorToBase[0].mods.reset();
	assert(armorToBase != *armorDecoded);
	assert(!EquipmentState::Encode(armorToBase)->at(0).contains("mods"));
	auto armorToEmpty = *armorDecoded;
	armorToEmpty[0].mods->clear();
	assert(armorToEmpty != *armorDecoded);
	assert(armorToEmpty != armorToBase);

	auto armorOversized = armorWire;
	armorOversized[0]["mods"] = Json::array();
	for (int i = 0; i < 33; ++i) {
		armorOversized[0]["mods"].push_back(recipe[0]);
	}
	assert(!EquipmentState::Decode(armorOversized));

	auto tooManyItems = Json::array();
	for (int i = 0; i < 33; ++i) {
		tooManyItems.push_back({ { "slot", "slot" + std::to_string(i) }, { "formId", "00012345" } });
	}
	assert(!EquipmentState::Decode(tooManyItems));
	assert(!PlayerStateValidation::NormalizePlayerState({ { "equippedItems", tooManyItems } }));

	GnsProtocolAdapter::Session armorSession;
	assert(!armorSession.AdaptInbound(Json{ { "type", "welcome" }, { "playerId", 7 }, { "protocolVersion", 2 } }.dump()).protocolViolation);
	assert(!armorSession.AdaptInbound(Json{ { "type", "sessionReady" }, { "playerId", 7 }, { "protocolVersion", 2 },
		{ "serverProtocolVersion", 2 }, { "worldStateHostPlayerId", 7 } }.dump()).protocolViolation);
	assert(armorSession.IsReady());
	Json armorTransform{ { "type", "transform" }, { "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 }, { "angleZ", 0.0 },
		{ "cellId", "0000003C" }, { "worldspaceId", "0000003D" }, { "equippedItems", armorWire } };
	const auto armorOutbound = armorSession.AdaptOutbound(armorTransform.dump());
	assert(armorOutbound.size() == 2 && armorOutbound[1].delivery == GnsProtocolAdapter::Delivery::ReliableOrdered);
	assert(Json::parse(armorOutbound[1].payload)["equippedItems"] == armorWire);
	assert(armorSession.AdaptOutbound(armorTransform.dump()).size() == 1);
	armorTransform["equippedItems"] = *EquipmentState::Encode(armorModSwap);
	const auto armorModChanged = armorSession.AdaptOutbound(armorTransform.dump());
	assert(armorModChanged.size() == 2 && armorModChanged[1].delivery == GnsProtocolAdapter::Delivery::ReliableOrdered);
	assert(Json::parse(armorModChanged[1].payload)["equippedItems"] == *EquipmentState::Encode(armorModSwap));

	const std::vector<EquipmentState::Item> describedItems{
		{ "rightHand", 0x00012345, std::nullopt },
		{ "body", 0x00067890, std::vector<RemotePlayerState::RemoteItemMod>{} },
		{ "head", 0x00000001, std::vector<RemotePlayerState::RemoteItemMod>{ { 0x00054321, 2, 3 } } } };
	assert(EquipmentState::DescribeItems(describedItems) == "rightHand=00012345(mods=unset), body=00067890(mods=none), head=00000001(mods=[00054321:2:3])");
}

#include "ServerProtocol.h"

#include "F4TGnsSnapshotEnvelope.h"

#include <nlohmann/json.hpp>

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
	using CommonwealthOnline::Server::ProtocolHost;
	using F4T::GnsSnapshotEnvelope::DecodeResult;

	constexpr const char* kFormKey = "form" "Id";

	std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	Json DecodePacket(const OutboundFrame& a_frame)
	{
		F4T::GnsSnapshotEnvelope::Header header{};
		std::span<const std::uint8_t> payload{ a_frame.payload };
		if (F4T::GnsSnapshotEnvelope::Decode(a_frame.payload, header) == DecodeResult::Valid) {
			payload = payload.subspan(F4T::GnsSnapshotEnvelope::HeaderSize);
		}
		return Json::parse(std::string{ reinterpret_cast<const char*>(payload.data()), payload.size() });
	}

	std::vector<Json> PlayerStatesFor(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		std::vector<Json> states;
		for (const auto& frame : a_frames) {
			if (frame.connectionId != a_connectionId || frame.payload.empty()) {
				continue;
			}
			auto packet = DecodePacket(frame);
			if (packet.value("type", std::string{}) == "playerState") {
				assert(frame.delivery == Delivery::ReliableOrdered);
				states.push_back(std::move(packet));
			}
		}
		return states;
	}

	std::string SlotForm(const Json& a_state, std::string_view a_slot)
	{
		for (const auto& item : a_state.at("equippedItems")) {
			if (item.at("slot").get<std::string>() == a_slot) {
				return item.at(kFormKey).get<std::string>();
			}
		}
		assert(false);
		return {};
	}

	Json Equipment(std::string_view a_weapon, std::string_view a_body)
	{
		return Json{
			{ "type", "playerState" },
			{ "equippedItems", Json::array({
				Json{ { "slot", "body" }, { kFormKey, std::string{ a_body } } },
				Json{ { "slot", "rightHand" }, { kFormKey, std::string{ a_weapon } } } }) },
			{ "actionEvents", Json::array() }
		};
	}

	std::uint32_t Join(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_time, std::vector<OutboundFrame>* a_readyFrames = nullptr)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_time);
		const auto playerId = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		auto ready = a_host.HandleMessage(a_connectionId, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), a_time, a_time);
		if (a_readyFrames) {
			*a_readyFrames = std::move(ready);
		}
		return playerId;
	}
}

int main()
{
	ProtocolHost host{ "Equipment Sync Test" };
	const auto playerA = Join(host, 10, 1.0);
	Join(host, 20, 1.1);

	const auto equip = host.HandleMessage(10, EncodeJson(Equipment("1F66A", "23432")), 2.0, 2.0);
	const auto equipAtB = PlayerStatesFor(equip, 20);
	assert(equipAtB.size() == 1);
	assert(equipAtB.front().at("playerId") == playerA);
	assert(SlotForm(equipAtB.front(), "rightHand") == "0001F66A");
	assert(SlotForm(equipAtB.front(), "body") == "00023432");

	const auto swap = host.HandleMessage(10, EncodeJson(Equipment("4822", "23432")), 3.0, 3.0);
	assert(SlotForm(PlayerStatesFor(swap, 20).front(), "rightHand") == "00004822");

	const auto clear = host.HandleMessage(10, EncodeJson(Equipment("", "")), 4.0, 4.0);
	const auto clearAtB = PlayerStatesFor(clear, 20);
	assert(clearAtB.size() == 1);
	assert(SlotForm(clearAtB.front(), "rightHand").empty());
	assert(SlotForm(clearAtB.front(), "body").empty());

	const auto rejected = host.HandleMessage(10, EncodeJson(Json{
		{ "type", "playerState" },
		{ "equippedItems", Json::array({ Json{ { "slot", "body" }, { kFormKey, "zz" } } }) } }), 5.0, 5.0);
	assert(PlayerStatesFor(rejected, 20).empty());

	std::vector<OutboundFrame> lateJoin;
	Join(host, 30, 6.0, &lateJoin);
	const auto replay = PlayerStatesFor(lateJoin, 30);
	std::size_t replayForA = 0;
	for (const auto& state : replay) {
		if (state.at("playerId") == playerA) {
			++replayForA;
			assert(SlotForm(state, "rightHand").empty());
			assert(SlotForm(state, "body").empty());
			assert(!state.contains("actionEvents"));
		}
	}
	assert(replayForA == 1);

	auto moddedEquip = Equipment("1F66A", "23432");
	moddedEquip["equippedItems"][1]["mods"] = Json::array({ Json{ { "mod", "4F21D" }, { "index", 1 }, { "rank", 0 } } });
	const auto reequip = host.HandleMessage(10, EncodeJson(moddedEquip), 7.0, 7.0);
	const auto moddedAtB = PlayerStatesFor(reequip, 20).front().at("equippedItems")[1];
	assert(moddedAtB.at("mods")[0].at("mod") == "0004F21D");
	std::vector<OutboundFrame> lateModded;
	Join(host, 40, 8.0, &lateModded);
	for (const auto& state : PlayerStatesFor(lateModded, 40)) {
		if (state.at("playerId") == playerA) {
			assert(state.at("equippedItems")[1].at("mods")[0].at("index") == 1);
		}
	}
	assert(SlotForm(PlayerStatesFor(reequip, 30).front(), "rightHand") == "0001F66A");
	assert(SlotForm(PlayerStatesFor(reequip, 20).front(), "body") == "00023432");

	auto armored = Equipment("1F66A", "23432");
	armored["powerArmor"] = Json{
		{ "inFrame", true },
		{ "frameBaseFormId", "10A0FF" },
		{ "pieces", Json::array({ Json{ { "slot", "body" }, { kFormKey, "11111" } } }) }
	};
	const auto powerArmor = host.HandleMessage(10, EncodeJson(armored), 9.0, 9.0);
	const auto powerArmorAtB = PlayerStatesFor(powerArmor, 20);
	assert(powerArmorAtB.size() == 1);
	assert(powerArmorAtB.front().at("powerArmor").at("inFrame") == true);
	assert(powerArmorAtB.front().at("powerArmor").at("frameBaseFormId") == "0010A0FF");
	assert(powerArmorAtB.front().at("powerArmor").at("pieces").size() == 1);

	for (const auto connectionId : { 50U, 60U }) {
		std::vector<OutboundFrame> joining;
		Join(host, connectionId, 10.0 + static_cast<double>(connectionId), &joining);
		std::size_t replayedForA = 0;
		for (const auto& state : PlayerStatesFor(joining, connectionId)) {
			if (state.at("playerId") == playerA) {
				++replayedForA;
				assert(state.at("powerArmor").at("inFrame") == true);
				assert(state.at("powerArmor").at("frameBaseFormId") == "0010A0FF");
				assert(state.at("powerArmor").at("pieces").size() == 1);
				assert(state.at("powerArmor").at("pieces").at(0).at(kFormKey) == "00011111");
			}
		}
		assert(replayedForA == 1);
	}

	return 0;
}

#include "ServerProtocol.h"

#include "F4TGnsSnapshotEnvelope.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::Delivery;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolHost;
	using F4T::GnsSnapshotEnvelope::DecodeResult;

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

	Json Vitals(std::int32_t a_percent, bool a_isDowned = false, bool a_isDead = false, std::int32_t a_level = 0)
	{
		Json vitals{
			{ "type", "playerState" },
			{ "vitals", Json{
				{ "healthPercent", a_percent },
				{ "isDowned", a_isDowned },
				{ "isDead", a_isDead } } }
		};
		if (a_level != 0) {
			vitals["vitals"]["level"] = a_level;
		}
		return vitals;
	}

	std::uint32_t Join(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_time, std::vector<OutboundFrame>* a_readyFrames = nullptr)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_time);
		a_host.BindSessionProfile(a_connectionId, "vitals-" + std::to_string(a_connectionId));
		const auto playerId = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		auto ready = a_host.HandleMessage(a_connectionId, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "social-names-v1" }) } }), a_time, a_time);
		if (a_readyFrames) {
			*a_readyFrames = std::move(ready);
		}
		return playerId;
	}
}

int main()
{
	{
		ProtocolHost host{ "Player Vitals Test" };
		assert(host.EnableSocial(nullptr));
		const auto playerA = Join(host, 10, 1.0);
		Join(host, 20, 1.1);

		const auto relay = host.HandleMessage(10, EncodeJson(Vitals(87, false, false, 42)), 2.0, 2.0);
		const auto atB = PlayerStatesFor(relay, 20);
		assert(atB.size() == 1);
		assert(atB.front().at("playerId") == playerA);
		assert(atB.front().at("vitals").at("healthPercent") == 87);
		assert(atB.front().at("vitals").at("level") == 42);
		assert(atB.front().at("vitals").at("isDead") == false);
		assert(!atB.front().contains("actionEvents"));
		assert(PlayerStatesFor(relay, 10).empty());

		const auto downed = host.HandleMessage(10, EncodeJson(Vitals(4, true, false)), 2.5, 2.5);
		assert(PlayerStatesFor(downed, 20).front().at("vitals").at("isDowned") == true);

		const auto dead = host.HandleMessage(10, EncodeJson(Vitals(0, true, true)), 3.0, 3.0);
		const auto deadAtB = PlayerStatesFor(dead, 20).front();
		assert(deadAtB.at("vitals").at("isDead") == true);
		assert(deadAtB.at("vitals").at("healthPercent") == 0);

		std::vector<OutboundFrame> lateJoin;
		Join(host, 30, 4.0, &lateJoin);
		const auto replay = PlayerStatesFor(lateJoin, 30);
		std::size_t replayForA = 0;
		for (const auto& state : replay) {
			if (state.at("playerId") == playerA) {
				++replayForA;
				assert(state.at("vitals").at("healthPercent") == 0);
				assert(state.at("vitals").at("isDead") == true);
				assert(!state.contains("actionEvents"));
			}
		}
		assert(replayForA == 1);
	}

	{
		ProtocolHost host{ "Player Vitals Validation Test" };
		assert(host.EnableSocial(nullptr));
		Join(host, 10, 1.0);
		Join(host, 20, 1.1);

		assert(PlayerStatesFor(host.HandleMessage(10, EncodeJson(Vitals(101)), 2.0, 2.0), 20).empty());
		assert(PlayerStatesFor(host.HandleMessage(10, EncodeJson(Vitals(-1)), 2.1, 2.1), 20).empty());
		assert(PlayerStatesFor(host.HandleMessage(10, EncodeJson(Vitals(1000)), 2.2, 2.2), 20).empty());
		assert(PlayerStatesFor(host.HandleMessage(10, EncodeJson(Json{
			{ "type", "playerState" },
			{ "vitals", Json{ { "healthPercent", 50 }, { "isDowned", 1 } } } }), 2.3, 2.3), 20).empty());
		assert(PlayerStatesFor(host.HandleMessage(10, EncodeJson(Json{
			{ "type", "playerState" },
			{ "vitals", Json{ { "healthPercent", 50 }, { "level", 0 } } } }), 2.4, 2.4), 20).empty());
		assert(PlayerStatesFor(host.HandleMessage(10, EncodeJson(Json{
			{ "type", "playerState" },
			{ "vitals", Json{ { "healthPercent", 50 }, { "level", 65536 } } } }), 2.45, 2.45), 20).empty());
		assert(!PlayerStatesFor(host.HandleMessage(10, EncodeJson(Vitals(50, false, false, 65535)), 2.46, 2.46), 20).empty());
		assert(PlayerStatesFor(host.HandleMessage(10, EncodeJson(Json{
			{ "type", "playerState" },
			{ "vitals", Json::array() } }), 2.5, 2.5), 20).empty());
		assert(PlayerStatesFor(host.HandleMessage(10, EncodeJson(Json{
			{ "type", "playerState" },
			{ "vitals", Json{ { "isDowned", true } } } }), 2.6, 2.6), 20).empty());
		assert(PlayerStatesFor(host.HandleMessage(10, EncodeJson(Json{
			{ "type", "playerState" },
			{ "vitals", Json{ { "healthPercent", 50 }, { "level", 1.5 } } } }), 2.7, 2.7), 20).empty());

		const auto rejectedBefore = host.Stats().packetsRejected;
		Json oversized = Vitals(50);
		oversized["padding"] = std::string(33 * 1024, 'P');
		assert(PlayerStatesFor(host.HandleMessage(10, EncodeJson(oversized), 2.8, 2.8), 20).empty());
		assert(host.Stats().packetsRejected > rejectedBefore);
	}

	{
		ProtocolHost host{ "Player Vitals Rate Limit Test" };
		assert(host.EnableSocial(nullptr));
		Join(host, 10, 1.0);
		Join(host, 20, 1.1);

		std::size_t relayed = 0;
		std::size_t limited = 0;
		for (int index = 0; index < 10; ++index) {
			const auto frames = host.HandleMessage(10, EncodeJson(Vitals(90 - index)), 2.0, 2.0);
			if (PlayerStatesFor(frames, 20).empty()) {
				++limited;
			} else {
				++relayed;
			}
		}
		assert(relayed == 8);
		assert(limited == 2);

		const auto recovered = host.HandleMessage(10, EncodeJson(Vitals(70)), 3.0, 3.0);
		assert(PlayerStatesFor(recovered, 20).size() == 1);

		std::vector<OutboundFrame> lateJoin;
		Join(host, 30, 3.5, &lateJoin);
		std::size_t replayed = 0;
		for (const auto& state : PlayerStatesFor(lateJoin, 30)) {
			assert(state.at("vitals").at("healthPercent") == 70);
			++replayed;
		}
		assert(replayed == 1);
	}

	{
		ProtocolHost host{ "Player Vitals Durable Relay Test" };
		assert(host.EnableSocial(nullptr));
		Join(host, 10, 1.0);
		Join(host, 20, 1.1);

		Json combined = Vitals(55, false, false, 7);
		combined["characterName"] = "Vault Dweller";
		const auto frames = host.HandleMessage(10, EncodeJson(combined), 2.0, 2.0);
		const auto atB = PlayerStatesFor(frames, 20);
		assert(atB.size() == 1);
		assert(atB.front().at("vitals").at("healthPercent") == 55);
		assert(atB.front().at("displayName") == "Stranger");
		assert(!atB.front().contains("characterName"));

		std::vector<OutboundFrame> lateJoin;
		Join(host, 30, 3.0, &lateJoin);
		std::size_t replayed = 0;
		for (const auto& state : PlayerStatesFor(lateJoin, 30)) {
			assert(state.at("displayName") == "Stranger");
			assert(!state.contains("characterName"));
			assert(state.at("vitals").at("level") == 7);
			++replayed;
		}
		assert(replayed == 1);
	}

	return 0;
}

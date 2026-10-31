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
}

int main()
{
	using CommonwealthOnline::Server::ProtocolHost;

	ProtocolHost host{ "Interest Test" };
	const auto welcomeA = host.HandleConnected(10, 100.0);
	const auto playerA = DecodePacket(welcomeA.front()).at("playerId").get<std::uint32_t>();
	assert(!host.HandleMessage(10, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 1.0, 100.1).empty());

	host.HandleConnected(20, 100.2);
	assert(!host.HandleMessage(20, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 1.1, 100.3).empty());

	const Json transformA{
		{ "type", "transform" },
		{ "cellId", "100" },
		{ "worldspaceId", "F00D" },
		{ "x", 0.0 },
		{ "y", 0.0 },
		{ "z", 0.0 },
		{ "angleZ", 0.0 },
		{ "movementType", "spawn" }
	};
	const auto firstA = host.HandleMessage(10, EncodeSnapshot(transformA, Family::Transform, 1), 2.0, 101.0);
	// Connection 20 has not reported a position yet, so it is inside nobody's
	// interest and receives nothing.
	assert(FindFrame(firstA, 20, "transform") == nullptr);

	const Json transformB{
		{ "type", "transform" },
		{ "cellId", "200" },
		{ "worldspaceId", "DEAD" },
		{ "x", 0.0 },
		{ "y", 0.0 },
		{ "z", 0.0 },
		{ "angleZ", 0.0 },
		{ "movementType", "spawn" }
	};
	const auto firstB = host.HandleMessage(20, EncodeSnapshot(transformB, Family::Transform, 1), 2.1, 101.1);
	assert(FindFrame(firstB, 10, "transform") == nullptr);

	Json movedA = transformA;
	movedA["movementType"] = "normal";
	movedA["x"] = 50.0;
	const auto filteredMove = host.HandleMessage(10, EncodeSnapshot(movedA, Family::Transform, 2), 2.2, 101.2);
	assert(FindFrame(filteredMove, 20, "transform") == nullptr);

	const Json playerState{
		{ "type", "playerState" },
		{ "characterName", "Nomad" },
		{ "actionEvents", Json::array({ Json{
			{ "sequence", 1 },
			{ "type", 3 },
			{ "eventName", "fireSingle" }
		} }) }
	};
	const auto stateFrames = host.HandleMessage(10, EncodeJson(playerState), 2.3, 101.3);
	const auto* stateFrame = FindFrame(stateFrames, 20, "playerState");
	assert(stateFrame != nullptr);
	const auto relayedState = DecodePacket(*stateFrame);
	assert(relayedState.at("characterName") == "Nomad");
	assert(!relayedState.contains("actionEvents"));

	const Json npcState{
		{ "type", "npcState" },
		{ "authorityEpoch", 1 },
		{ "authorityCellId", "100" },
		{ "authorityWorldspaceId", "F00D" },
		{ "npcs", Json::array({ Json{
			{ "sourceFormId", "1234" },
			{ "cellId", "100" },
			{ "worldspaceId", "F00D" },
			{ "x", 10.0 },
			{ "y", 10.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 }
		} }) }
	};
	const auto npcFrames = host.HandleMessage(10, EncodeSnapshot(npcState, Family::NpcState, 1), 2.4, 101.4);
	const auto* npcFrame = FindFrame(npcFrames, 20, "npcState");
	assert(npcFrame != nullptr);
	const auto relayedNpc = DecodePacket(*npcFrame);
	assert(relayedNpc.at("playerId") == playerA);
	assert(relayedNpc.at("npcs").empty());

	return 0;
}

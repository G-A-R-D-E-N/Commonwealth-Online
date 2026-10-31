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

	ProtocolHost host{ "Replay Test" };
	const auto welcomeA = host.HandleConnected(10, 100.0);
	const auto playerA = DecodePacket(welcomeA.front()).at("playerId").get<std::uint32_t>();
	assert(!host.HandleMessage(10, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 1.0, 100.1).empty());

	const Json transformA{
		{ "type", "transform" },
		{ "cellId", "CAFE" },
		{ "worldspaceId", "F00D" },
		{ "x", 100.0 },
		{ "y", 200.0 },
		{ "z", 300.0 },
		{ "angleZ", 45.0 },
		{ "movementType", "spawn" }
	};
	host.HandleMessage(10, EncodeSnapshot(transformA, Family::Transform, 1), 2.0, 101.0);

	host.HandleMessage(
		10,
		EncodeJson(Json{
			{ "type", "playerState" },
			{ "characterName", "Existing Player" }
		}),
		2.1,
		101.1);

	const Json npcState{
		{ "type", "npcState" },
		{ "authorityEpoch", 1 },
		{ "authorityCellId", "CAFE" },
		{ "authorityWorldspaceId", "F00D" },
		{ "npcs", Json::array({ Json{
			{ "sourceFormId", "1234" },
			{ "cellId", "CAFE" },
			{ "worldspaceId", "F00D" },
			{ "x", 110.0 },
			{ "y", 210.0 },
			{ "z", 300.0 },
			{ "angleZ", 90.0 }
		} }) }
	};
	const auto ownerNpcFrames = host.HandleMessage(10, EncodeSnapshot(npcState, Family::NpcState, 1), 2.2, 101.2);
	assert(ownerNpcFrames.empty());

	const auto welcomeB = host.HandleConnected(20, 102.0);
	assert(welcomeB.size() == 1);
	const auto readyAndReplay = host.HandleMessage(
		20,
		EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }),
		3.0,
		102.1);
	assert(FindFrame(readyAndReplay, 20, "sessionReady") != nullptr);

	// A session with no reported position is inside nobody's interest, so peer
	// transforms are withheld until it reports one.
	assert(FindFrame(readyAndReplay, 20, "transform") == nullptr);

	const auto* stateReplayFrame = FindFrame(readyAndReplay, 20, "playerState");
	assert(stateReplayFrame != nullptr);
	const auto stateReplay = DecodePacket(*stateReplayFrame);
	assert(stateReplay.at("playerId") == playerA);
	assert(stateReplay.at("characterName") == "Existing Player");
	assert(!stateReplay.contains("actionEvents"));
	assert(FindFrame(readyAndReplay, 20, "npcState") == nullptr);

	Json transformB = transformA;
	transformB["x"] = 120.0;
	const auto scopeEntryFrames = host.HandleMessage(
		20,
		EncodeSnapshot(transformB, Family::Transform, 1),
		3.1,
		102.2);
	const auto* transformReplayFrame = FindFrame(scopeEntryFrames, 20, "transform");
	assert(transformReplayFrame != nullptr);
	const auto transformReplay = DecodePacket(*transformReplayFrame);
	assert(transformReplay.at("playerId") == playerA);
	assert(transformReplay.at("cellId") == "0000CAFE");
	assert(transformReplay.at("x") == 100.0);

	const auto* authorityFrame = FindFrame(scopeEntryFrames, 20, "npcAuthority");
	assert(authorityFrame != nullptr);
	const auto authority = DecodePacket(*authorityFrame);
	assert(authority.at("authorityPlayerId") == playerA);
	assert(authority.at("authorityEpoch") == 1);

	const auto* npcReplayFrame = FindFrame(scopeEntryFrames, 20, "npcState");
	assert(npcReplayFrame != nullptr);
	const auto npcReplay = DecodePacket(*npcReplayFrame);
	assert(npcReplay.at("playerId") == playerA);
	assert(npcReplay.at("authorityEpoch") == 1);
	assert(npcReplay.at("npcs").size() == 1);
	assert(npcReplay.at("npcs")[0].at("sourceFormId") == "00001234");

	return 0;
}

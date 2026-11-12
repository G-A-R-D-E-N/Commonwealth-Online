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
		const auto decode = F4T::GnsSnapshotEnvelope::Decode(a_frame.payload, header);
		std::span<const std::uint8_t> payload{ a_frame.payload };
		if (decode == DecodeResult::Valid) {
			payload = payload.subspan(F4T::GnsSnapshotEnvelope::HeaderSize);
		} else {
			assert(decode == DecodeResult::NotEnvelope);
		}
		return Json::parse(std::string{ reinterpret_cast<const char*>(payload.data()), payload.size() });
	}

	const OutboundFrame* FindFrame(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_type)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && !frame.payload.empty() &&
				DecodePacket(frame).value("type", std::string{}) == a_type) {
				return &frame;
			}
		}
		return nullptr;
	}

	std::vector<OutboundFrame> Join(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_unixTimeSeconds)
	{
		a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		auto ready = a_host.HandleMessage(a_connectionId, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), a_unixTimeSeconds, a_unixTimeSeconds);
		assert(FindFrame(ready, a_connectionId, "sessionReady") != nullptr);
		return ready;
	}

	std::uint32_t PlayerIdOf(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		const auto* frame = FindFrame(a_frames, a_connectionId, "sessionReady");
		assert(frame != nullptr);
		return DecodePacket(*frame).at("playerId").get<std::uint32_t>();
	}

	std::uint32_t WorldStateHostAnnouncedTo(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		const auto* frame = FindFrame(a_frames, a_connectionId, "worldStateHost");
		assert(frame != nullptr);
		return DecodePacket(*frame).at("worldStateHostPlayerId").get<std::uint32_t>();
	}
}

int main()
{
	ProtocolHost host{ "World State Host Handoff Test" };
	const auto joinA = Join(host, 10, 100.0);
	const auto joinB = Join(host, 20, 101.0);
	const auto joinC = Join(host, 30, 102.0);
	const auto playerA = PlayerIdOf(joinA, 10);
	const auto playerB = PlayerIdOf(joinB, 20);
	const auto playerC = PlayerIdOf(joinC, 30);
	assert(WorldStateHostAnnouncedTo(joinA, 10) == playerA);

	const auto world = [](double hour, bool sync) {
		return EncodeJson(Json{ { "type", "worldState" }, { "gameHour", hour }, { "gameDaysPassed", 42.0 }, { "timeSync", sync } });
	};
	assert(FindFrame(host.HandleMessage(10, world(8.0, true), 102.1, 102.1), 20, "worldState") != nullptr);
	assert(host.HandleMessage(10, world(16.0, true), 103.0, 103.0).empty());
	const auto recovered = host.HandleMessage(10, world(16.0, false), 108.0, 108.0);
	const auto* recoveredFrame = FindFrame(recovered, 20, "worldState");
	assert(recoveredFrame != nullptr);
	assert(DecodePacket(*recoveredFrame).at("timeSync") == true);

	const auto afterHostLeaves = host.HandleDisconnected(10, 110.0);
	assert(WorldStateHostAnnouncedTo(afterHostLeaves, 20) == playerB);
	assert(WorldStateHostAnnouncedTo(afterHostLeaves, 30) == playerB);

	const auto afterSuccessorLeaves = host.HandleDisconnected(20, 111.0);
	assert(WorldStateHostAnnouncedTo(afterSuccessorLeaves, 30) == playerC);

	host.HandleDisconnected(30, 112.0);

	const auto joinD = Join(host, 40, 113.0);
	assert(WorldStateHostAnnouncedTo(joinD, 40) == PlayerIdOf(joinD, 40));

	const auto worldDays = [](double days, bool sync) {
		return EncodeJson(Json{ { "type", "worldState" }, { "gameDaysPassed", days }, { "timeSync", sync } });
	};
	Join(host, 50, 114.0);
	assert(host.HandleMessage(40, worldDays(100.0, false), 114.5, 114.5).empty());
	assert(FindFrame(host.HandleMessage(40, worldDays(43.0, false), 114.5, 114.5), 50, "worldState") != nullptr);
	assert(FindFrame(host.HandleMessage(40, worldDays(44.0, false), 115.0, 115.0), 50, "worldState") != nullptr);
	assert(host.HandleMessage(40, worldDays(601.0, false), 116.0, 116.0).empty());
	assert(FindFrame(host.HandleMessage(40, worldDays(601.0, true), 116.0, 116.0), 50, "worldState") != nullptr);
	assert(host.HandleMessage(40, worldDays(1200.0, true), 500.0, 500.0).empty());
	assert(FindFrame(host.HandleMessage(40, worldDays(1200.0, true), 1916.0, 1916.0), 50, "worldState") != nullptr);
	assert(FindFrame(host.HandleMessage(40, worldDays(50.0, false), 1917.0, 1917.0), 50, "worldState") != nullptr);
	return 0;
}

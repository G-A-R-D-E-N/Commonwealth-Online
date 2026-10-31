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
			if (frame.connectionId != a_connectionId || frame.payload.empty()) {
				continue;
			}
			if (DecodePacket(frame).value("type", std::string{}) == a_type) {
				return frame;
			}
		}
		assert(false && "required outbound frame was not found");
		return a_frames.front();
	}
}

int main()
{
	using CommonwealthOnline::Server::ProtocolHost;

	{
		ProtocolHost consoleHost{ "Console Role Server", "", { "co-admin" } };

		const auto adminWelcome = consoleHost.HandleConnected(300, 4000.0);
		const auto adminPlayer = DecodePacket(adminWelcome[0]).at("playerId").get<std::uint32_t>();
		consoleHost.BindSessionProfile(300, "co-admin");
		const auto adminReady = consoleHost.HandleMessage(
			300,
			EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }),
			4.0,
			4000.1);
		const auto& adminReadyFrame = RequireFrame(adminReady, 300, "sessionReady");
		assert(DecodePacket(adminReadyFrame).at("playerId") == adminPlayer);
		assert(DecodePacket(adminReadyFrame).at("consoleAllowed") == true);

		const auto playerWelcome = consoleHost.HandleConnected(400, 4000.2);
		const auto playerId = DecodePacket(playerWelcome[0]).at("playerId").get<std::uint32_t>();
		consoleHost.BindSessionProfile(400, "co-not-an-admin");
		const auto playerReady = consoleHost.HandleMessage(
			400,
			EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }),
			4.1,
			4000.3);
		const auto& playerReadyFrame = RequireFrame(playerReady, 400, "sessionReady");
		assert(DecodePacket(playerReadyFrame).at("playerId") == playerId);
		assert(DecodePacket(playerReadyFrame).at("consoleAllowed") == false);
	}

	{
		ProtocolHost repeatHost{ "Console Repeat Server", "", { "co-repeat-admin" } };

		std::uint32_t previousPlayer = 0;
		for (const std::uint32_t connectionId : { 900u, 1000u }) {
			const auto welcome = repeatHost.HandleConnected(connectionId, 6000.0 + connectionId);
			const auto player = DecodePacket(welcome[0]).at("playerId").get<std::uint32_t>();
			assert(player != previousPlayer);
			previousPlayer = player;
			repeatHost.BindSessionProfile(connectionId, "co-repeat-admin");
			const auto ready = repeatHost.HandleMessage(
				connectionId,
				EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }),
				6.0,
				6000.0 + connectionId);
			const auto& frame = RequireFrame(ready, connectionId, "sessionReady");
			assert(DecodePacket(frame).at("playerId") == player);
			assert(DecodePacket(frame).at("consoleAllowed") == true);
			repeatHost.HandleDisconnected(connectionId, 7000.0 + connectionId);
		}
	}

	return 0;
}

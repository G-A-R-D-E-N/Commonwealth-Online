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

	void Join(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_unixTimeSeconds)
	{
		a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		const auto ready = a_host.HandleMessage(a_connectionId, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), a_unixTimeSeconds, a_unixTimeSeconds);
		assert(FindFrame(ready, a_connectionId, "sessionReady") != nullptr);
	}

	Json WorldPacket(double a_hour, const std::string* a_weatherFormId)
	{
		Json packet{ { "type", "worldState" }, { "gameHour", a_hour }, { "gameDaysPassed", 42.0 }, { "timeSync", true } };
		if (a_weatherFormId) {
			packet["weatherFormId"] = *a_weatherFormId;
		}
		return packet;
	}
}

int main()
{
	{
		ProtocolHost restored{ "World Weather Restore Test" };
		Join(restored, 30, 100.0);
		restored.RestoreLockedWeatherFromWorldState(Json{ { "weatherFormId", "0002B52A" } });
		const std::string engineWeather{ "00000001" };
		const auto drifted = restored.HandleMessage(30, EncodeJson(WorldPacket(8.0, &engineWeather)), 102.0, 102.0);
		const auto* resync = FindFrame(drifted, 30, "serverWorldState");
		assert(resync != nullptr);
		assert(DecodePacket(*resync).at("weatherFormId") == "0002B52A");
	}

	for (const auto& saved : { Json::object(), Json{ { "weatherFormId", 5 } } }) {
		ProtocolHost unlocked{ "World Weather Restore Empty" };
		Join(unlocked, 30, 100.0);
		unlocked.RestoreLockedWeatherFromWorldState(saved);
		const std::string engineWeather{ "00000001" };
		assert(FindFrame(unlocked.HandleMessage(30, EncodeJson(WorldPacket(8.0, &engineWeather)), 102.0, 102.0), 30, "serverWorldState") == nullptr);
	}

	ProtocolHost host{ "World Weather Lock Test" };
	Join(host, 10, 100.0);
	Join(host, 20, 101.0);

	host.SetLockedWeatherFormId(std::string{ "0002B52A" });

	const std::string engineWeather{ "00000001" };
	const auto drifted = host.HandleMessage(10, EncodeJson(WorldPacket(8.0, &engineWeather)), 102.0, 102.0);
	assert(FindFrame(drifted, 20, "worldState") != nullptr);
	const auto* relayedDrift = FindFrame(drifted, 20, "worldState");
	assert(relayedDrift != nullptr);
	assert(!DecodePacket(*relayedDrift).contains("weatherFormId"));
	assert(DecodePacket(*relayedDrift).at("gameHour") == 8.0);
	const auto* resync = FindFrame(drifted, 10, "serverWorldState");
	assert(resync != nullptr);
	assert(DecodePacket(*resync).at("weatherFormId") == "0002B52A");
	assert(!DecodePacket(*resync).contains("timeHHmm"));

	const std::string lockedWeather{ "0002B52A" };
	const auto matching = host.HandleMessage(10, EncodeJson(WorldPacket(8.0, &lockedWeather)), 103.0, 103.0);
	const auto* relayedMatch = FindFrame(matching, 20, "worldState");
	assert(relayedMatch != nullptr);
	assert(DecodePacket(*relayedMatch).at("weatherFormId") == "0002B52A");
	assert(FindFrame(matching, 10, "serverWorldState") == nullptr);

	const auto noWeather = host.HandleMessage(10, EncodeJson(WorldPacket(8.0, nullptr)), 104.0, 104.0);
	const auto* relayedNoWeather = FindFrame(noWeather, 20, "worldState");
	assert(relayedNoWeather != nullptr);
	assert(!DecodePacket(*relayedNoWeather).contains("weatherFormId"));
	assert(FindFrame(noWeather, 10, "serverWorldState") == nullptr);

	host.SetLockedWeatherFormId(std::nullopt);
	const auto unlocked = host.HandleMessage(10, EncodeJson(WorldPacket(8.0, &engineWeather)), 110.0, 110.0);
	const auto* relayedUnlocked = FindFrame(unlocked, 20, "worldState");
	assert(relayedUnlocked != nullptr);
	assert(DecodePacket(*relayedUnlocked).at("weatherFormId") == "00000001");
	assert(FindFrame(unlocked, 10, "serverWorldState") == nullptr);

	host.SetLockedWeatherFormId(std::string{ "0002B52A" });
	const auto weatherOnly = host.HandleMessage(10, EncodeJson(Json{ { "type", "worldState" }, { "weatherFormId", "00000001" } }), 120.0, 120.0);
	assert(FindFrame(weatherOnly, 20, "worldState") == nullptr);
	const auto* weatherOnlyResync = FindFrame(weatherOnly, 10, "serverWorldState");
	assert(weatherOnlyResync != nullptr);
	assert(DecodePacket(*weatherOnlyResync).at("weatherFormId") == "0002B52A");

	const auto interior = host.HandleMessage(10, EncodeJson(Json{ { "type", "worldState" }, { "gameHour", 8.0 }, { "gameDaysPassed", 42.0 }, { "weatherFormId", "" } }), 121.0, 121.0);
	const auto* relayedInterior = FindFrame(interior, 20, "worldState");
	assert(relayedInterior != nullptr);
	assert(DecodePacket(*relayedInterior).at("weatherFormId") == "");
	assert(FindFrame(interior, 10, "serverWorldState") == nullptr);

	return 0;
}

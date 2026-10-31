#include "ServerProtocol.h"

#include "F4TLoadOrder.h"

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

	std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	Json DecodePacket(const OutboundFrame& a_frame)
	{
		return Json::parse(std::string{
			reinterpret_cast<const char*>(a_frame.payload.data()),
			a_frame.payload.size()
		});
	}

	std::uint32_t WelcomePlayerId(const std::vector<OutboundFrame>& a_frames)
	{
		assert(a_frames.size() == 1);
		const auto packet = DecodePacket(a_frames.front());
		assert(packet.at("type") == "welcome");
		return packet.at("playerId").get<std::uint32_t>();
	}

	std::string EncodedList(std::initializer_list<std::string_view> a_names)
	{
		F4T::LoadOrder::PluginList list;
		std::uint16_t index = 0;
		for (const auto name : a_names) {
			list.full.push_back(F4T::LoadOrder::Plugin{ F4T::LoadOrder::PluginKind::full, index++, std::string{ name } });
		}
		const auto encoded = F4T::LoadOrder::EncodePluginList(list);
		assert(encoded.has_value());
		return *encoded;
	}

	Json PluginListPacket(std::string a_data)
	{
		return Json{ { "type", "pluginList" }, { "data", std::move(a_data) } };
	}

	std::vector<Json> PluginListFramesFor(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		std::vector<Json> packets;
		for (const auto& frame : a_frames) {
			if (frame.connectionId != a_connectionId || frame.payload.empty()) {
				continue;
			}
			const auto packet = DecodePacket(frame);
			if (packet.value("type", std::string{}) == "pluginList") {
				packets.push_back(packet);
			}
		}
		return packets;
	}
}

int main()
{
	ProtocolHost host{ "Load Order Test Server", "plugin list routing" };

	const auto playerA = WelcomePlayerId(host.HandleConnected(100, 1000.0));
	const auto readyA = host.HandleMessage(
		100,
		EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }),
		1.0,
		1000.1);
	assert(!readyA.empty());

	const auto listA = EncodedList({ "Fallout4.esm", "ModA.esp" });
	const auto announcedA = host.HandleMessage(100, EncodeJson(PluginListPacket(listA)), 1.1, 1000.2);
	assert(announcedA.empty());
	assert(host.Stats().pluginListsReceived == 1);

	const auto playerB = WelcomePlayerId(host.HandleConnected(200, 1000.3));
	const auto readyB = host.HandleMessage(
		200,
		EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }),
		1.2,
		1000.4);
	const auto nextA = PluginListFramesFor(readyB, 200);
	assert(nextA.size() == 1);
	assert(nextA[0].at("playerId") == playerA);
	assert(nextA[0].at("data") == listA);

	const auto listB = EncodedList({ "Fallout4.esm", "ModB.esp" });
	const auto announcedB = host.HandleMessage(200, EncodeJson(PluginListPacket(listB)), 1.3, 1000.5);
	const auto toA = PluginListFramesFor(announcedB, 100);
	assert(toA.size() == 1);
	assert(toA[0].at("playerId") == playerB);
	assert(toA[0].at("data") == listB);
	assert(PluginListFramesFor(announcedB, 200).empty());

	const auto rejectedBefore = host.Stats().pluginListsRejected;
	assert(host.HandleMessage(200, EncodeJson(PluginListPacket(listB)), 1.4, 1000.6).empty());
	assert(host.Stats().pluginListsRejected == rejectedBefore + 1);

	const auto playerC = WelcomePlayerId(host.HandleConnected(300, 1000.7));
	const auto readyC = host.HandleMessage(
		300,
		EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }),
		1.5,
		1000.8);
	const auto existing = PluginListFramesFor(readyC, 300);
	assert(existing.size() == 2);
	const auto dataA = existing[0].at("playerId") == playerA ? existing[0] : existing[1];
	const auto dataB = existing[0].at("playerId") == playerB ? existing[0] : existing[1];
	assert(dataA.at("data") == listA);
	assert(dataB.at("data") == listB);

	const auto malformedBefore = host.Stats().pluginListsRejected;
	assert(host.HandleMessage(300, EncodeJson(Json{ { "type", "pluginList" }, { "data", "!!" } }), 1.6, 1000.9).empty());
	assert(host.Stats().pluginListsRejected == malformedBefore + 1);
	assert(host.HandleMessage(300, EncodeJson(Json{ { "type", "pluginList" } }), 1.7, 1001.0).empty());
	assert(host.Stats().pluginListsRejected == malformedBefore + 2);

	return 0;
}

#include "co_gns_server_bridge.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <thread>

namespace
{
	using Json = nlohmann::json;

	bool ParsePort(std::string_view a_value, std::uint16_t& a_port)
	{
		std::uint32_t parsed = 0;
		const auto result = std::from_chars(a_value.data(), a_value.data() + a_value.size(), parsed);
		if (result.ec != std::errc{} || result.ptr != a_value.data() + a_value.size() || parsed == 0 || parsed > 65535) {
			return false;
		}
		a_port = static_cast<std::uint16_t>(parsed);
		return true;
	}

	bool SendJson(co_gns_client_handle a_client, const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return co_gns_client_send(
			a_client,
			text.data(),
			static_cast<std::uint32_t>(text.size()),
			CO_GNS_DELIVERY_RELIABLE_ORDERED) == CO_GNS_SEND_SENT;
	}

	bool WaitForPacket(
		co_gns_client_handle a_client,
		std::string_view a_type,
		Json& a_output,
		std::chrono::milliseconds a_timeout)
	{
		std::array<std::uint8_t, 64 * 1024> payload{};
		const auto deadline = std::chrono::steady_clock::now() + a_timeout;
		while (std::chrono::steady_clock::now() < deadline) {
			co_gns_event event{};
			const auto result = co_gns_client_poll(a_client, &event, payload.data(), static_cast<std::uint32_t>(payload.size()));
			if (result < 0) {
				return false;
			}
			if (result == 0) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}
			if (event.type == CO_GNS_EVENT_DISCONNECTED || event.type == CO_GNS_EVENT_OVERSIZE_MESSAGE) {
				return false;
			}
			if (event.type != CO_GNS_EVENT_MESSAGE || event.payload_size == 0 || event.payload_size > payload.size()) {
				continue;
			}
			try {
				const auto packet = Json::parse(std::string{
					reinterpret_cast<const char*>(payload.data()),
					event.payload_size
				});
				if (packet.is_object() && packet.value("type", std::string{}) == a_type) {
					a_output = packet;
					return true;
				}
			} catch (...) {
				return false;
			}
		}
		return false;
	}
}

int main(int argc, char** argv)
{
	if (argc != 3) {
		std::cerr << "Usage: CommonwealthOnlineServerLiveGnsSmoke HOST PORT\n";
		return 2;
	}
	std::uint16_t port = 0;
	if (!ParsePort(argv[2], port)) {
		std::cerr << "Invalid port\n";
		return 2;
	}

	co_gns_client_handle client = nullptr;
	std::array<char, 512> error{};
	if (co_gns_client_create(argv[1], port, &client, error.data(), error.size()) == 0 || client == nullptr) {
		std::cerr << "GNS client create failed: " << error.data() << '\n';
		return 1;
	}

	Json welcome;
	if (!WaitForPacket(client, "welcome", welcome, std::chrono::seconds(5))) {
		std::cerr << "Did not receive welcome\n";
		co_gns_client_destroy(client);
		return 1;
	}
	if (welcome.value("protocolVersion", 0) != 2 || welcome.value("playerId", 0u) == 0) {
		std::cerr << "Invalid welcome packet\n";
		co_gns_client_destroy(client);
		return 1;
	}
	const auto capabilities = welcome.find("capabilities");
	if (capabilities == welcome.end() || !capabilities->is_array() ||
		std::find(capabilities->begin(), capabilities->end(), "gns-message-transport-v1") == capabilities->end() ||
		std::find(capabilities->begin(), capabilities->end(), "gns-snapshot-sequence-v1") == capabilities->end()) {
		std::cerr << "Welcome is missing GNS capabilities\n";
		co_gns_client_destroy(client);
		return 1;
	}

	if (!SendJson(client, Json{
		{ "type", "hello" },
		{ "protocolVersion", 2 },
		{ "clientVersion", "gns-smoke-1" },
		{ "falloutRuntimeVersion", "1.11.240" },
		{ "profileId", "gns-smoke-profile" },
		{ "contentManifestFingerprint", std::string(64, 'a') },
		{ "capabilities", Json::array({ "interest-v1", "player-state-v1", "save-attestation-v1" }) }
	})) {
		std::cerr << "Failed to send hello\n";
		co_gns_client_destroy(client);
		return 1;
	}

	Json ready;
	if (!WaitForPacket(client, "sessionReady", ready, std::chrono::seconds(5))) {
		std::cerr << "Did not receive sessionReady\n";
		co_gns_client_destroy(client);
		return 1;
	}
	if (ready.value("protocolVersion", 0) != 2 || ready.value("playerId", 0u) != welcome.value("playerId", 0u)) {
		std::cerr << "Invalid sessionReady packet\n";
		co_gns_client_destroy(client);
		return 1;
	}

	co_gns_client_disconnect(client, 0, "smoke complete");
	co_gns_client_destroy(client);
	std::cout << "C++ GNS handshake smoke passed\n";
	return 0;
}

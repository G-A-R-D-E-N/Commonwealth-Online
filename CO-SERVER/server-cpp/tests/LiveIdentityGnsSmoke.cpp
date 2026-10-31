#include "co_gns_server_bridge.h"

#include <nlohmann/json.hpp>

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
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

	bool AckSessionReady(co_gns_client_handle a_client, std::string_view a_resumeToken)
	{
		return SendJson(a_client, Json{
			{ "type", "keepAlive" },
			{ "sessionReadyAck", true },
			{ "resumeTokenAck", a_resumeToken }
		});
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
			if (event.type == CO_GNS_EVENT_MESSAGE && event.payload_size > 0 && event.payload_size <= payload.size()) {
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
			if (event.type == CO_GNS_EVENT_OVERSIZE_MESSAGE) {
				return false;
			}
			if (event.type == CO_GNS_EVENT_DISCONNECTED) {
				return false;
			}
		}
		return false;
	}

	std::string PacketCodeOrNone(const Json& a_packet)
	{
		if (!a_packet.is_object()) {
			return "<none>";
		}
		const auto found = a_packet.find("code");
		return found != a_packet.end() && found->is_string()
			? found->get<std::string>()
			: std::string{ "<none>" };
	}

	co_gns_client_handle Connect(std::string_view a_host, std::uint16_t a_port)
	{
		co_gns_client_handle client = nullptr;
		std::array<char, 512> error{};
		const std::string host{ a_host };
		if (co_gns_client_create(host.c_str(), a_port, &client, error.data(), error.size()) == 0 || client == nullptr) {
			std::cerr << "GNS client create failed: " << error.data() << '\n';
			return nullptr;
		}
		Json welcome;
		if (!WaitForPacket(client, "welcome", welcome, std::chrono::seconds(5))) {
			std::cerr << "Did not receive welcome\n";
			co_gns_client_destroy(client);
			return nullptr;
		}
		return client;
	}

	Json IdentityHello(std::optional<std::string> a_resumeToken = std::nullopt)
	{
		Json hello{
			{ "type", "hello" },
			{ "protocolVersion", 2 },
			{ "clientVersion", "identity-smoke-1" },
			{ "falloutRuntimeVersion", "1.11.240" },
			{ "profileId", "identity-smoke-profile" },
			{ "contentManifestFingerprint", std::string(64, 'a') },
			{ "capabilities", Json::array({ "interest-v1", "player-state-v1", "save-attestation-v1" }) }
		};
		if (a_resumeToken) {
			hello["resumeToken"] = *a_resumeToken;
		}
		return hello;
	}

	void Close(co_gns_client_handle a_client)
	{
		if (a_client == nullptr) {
			return;
		}
		(void)co_gns_client_disconnect(a_client, 0, "identity smoke step complete");
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		co_gns_client_destroy(a_client);
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

int main(int argc, char** argv)
{
	if (argc != 3) {
		std::cerr << "Usage: CommonwealthOnlineServerLiveIdentityGnsSmoke HOST PORT\n";
		return 2;
	}
	std::uint16_t port = 0;
	if (!ParsePort(argv[2], port)) {
		std::cerr << "Invalid port\n";
		return 2;
	}
	const std::string_view host{ argv[1] };

	auto first = Connect(host, port);
	if (first == nullptr || !SendJson(first, IdentityHello())) {
		Close(first);
		return 1;
	}
	Json firstReady;
	if (!WaitForPacket(first, "sessionReady", firstReady, std::chrono::seconds(5))) {
		std::cerr << "Identity hello did not receive sessionReady\n";
		Close(first);
		return 1;
	}
	const auto firstToken = firstReady.value("resumeToken", std::string{});
	if (firstReady.value("profileId", std::string{}) != "identity-smoke-profile" ||
		firstReady.value("clientVersion", std::string{}) != "identity-smoke-1" ||
		firstReady.value("falloutRuntimeVersion", std::string{}) != "1.11.240" ||
		firstReady.value("contentManifestFingerprint", std::string{}) != std::string(64, 'a') ||
		firstToken.empty() || firstToken.size() > 256 ||
		firstReady.value("resumeExpiresAt", 0.0) <= 0.0 ||
		!firstReady.value("resumeAckRequired", false) ||
		!firstReady.value("resumeAckViaKeepAlive", false)) {
		std::cerr << "Identity sessionReady metadata is invalid\n";
		Close(first);
		return 1;
	}
	if (!AckSessionReady(first, firstToken)) {
		std::cerr << "Could not acknowledge first sessionReady\n";
		Close(first);
		return 1;
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	Close(first);

	auto missing = Connect(host, port);
	if (missing == nullptr || !SendJson(missing, IdentityHello())) {
		Close(missing);
		return 1;
	}
	Json missingEnded;
	const bool missingReceived = WaitForPacket(missing, "sessionEnded", missingEnded, std::chrono::seconds(5));
	if (!missingReceived || PacketCodeOrNone(missingEnded) != "resume_required") {
		std::cerr << "Reconnect without token was not rejected as resume_required; received code='"
			<< PacketCodeOrNone(missingEnded) << "'\n";
		Close(missing);
		return 1;
	}
	Close(missing);

	auto resumed = Connect(host, port);
	if (resumed == nullptr || !SendJson(resumed, IdentityHello(firstToken))) {
		Close(resumed);
		return 1;
	}
	Json resumedReady;
	if (!WaitForPacket(resumed, "sessionReady", resumedReady, std::chrono::seconds(5))) {
		std::cerr << "Resume token did not receive sessionReady\n";
		Close(resumed);
		return 1;
	}
	const auto rotatedToken = resumedReady.value("resumeToken", std::string{});
	if (rotatedToken.empty() || rotatedToken == firstToken) {
		std::cerr << "Resume token did not rotate\n";
		Close(resumed);
		return 1;
	}
	if (!AckSessionReady(resumed, rotatedToken)) {
		std::cerr << "Could not acknowledge rotated sessionReady\n";
		Close(resumed);
		return 1;
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	Close(resumed);

	auto stale = Connect(host, port);
	if (stale == nullptr || !SendJson(stale, IdentityHello(firstToken))) {
		Close(stale);
		return 1;
	}
	Json staleEnded;
	const bool staleReceived = WaitForPacket(stale, "sessionEnded", staleEnded, std::chrono::seconds(5));
	if (!staleReceived || PacketCodeOrNone(staleEnded) != "invalid_resume") {
		std::cerr << "Rotated-away resume token was not rejected; received code='"
			<< PacketCodeOrNone(staleEnded) << "'\n";
		Close(stale);
		return 1;
	}
	Close(stale);

	std::cout << "C++ GNS identity/resume smoke passed\n";
	return 0;
}

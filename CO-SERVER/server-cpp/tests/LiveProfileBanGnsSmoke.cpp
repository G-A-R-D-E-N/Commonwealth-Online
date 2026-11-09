#include "co_gns_server_bridge.h"

#include <nlohmann/json.hpp>

#ifdef _WIN32
#  include <winsock2.h>
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
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
		if (result.ec != std::errc{} || result.ptr != a_value.data() + a_value.size() || parsed == 0 || parsed > 65535) return false;
		a_port = static_cast<std::uint16_t>(parsed);
		return true;
	}

#ifdef _WIN32
	using AdminSocket = SOCKET;
	constexpr AdminSocket InvalidAdminSocket = INVALID_SOCKET;
	constexpr int AdminSendFlags = 0;
	void CloseAdminSocket(AdminSocket a_socket) { closesocket(a_socket); }
	struct WinsockScope
	{
		WinsockScope()
		{
			WSADATA data{};
			if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error("WSAStartup failed");
		}
		~WinsockScope() { WSACleanup(); }
	};
#else
	using AdminSocket = int;
	constexpr AdminSocket InvalidAdminSocket = -1;
	constexpr int AdminSendFlags = MSG_NOSIGNAL;
	void CloseAdminSocket(AdminSocket a_socket) { close(a_socket); }
	struct WinsockScope
	{
	};
#endif

	Json SendAdmin(std::uint16_t a_port, std::string_view a_token, Json a_request)
	{
		a_request["adminToken"] = a_token;
		const AdminSocket client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (client == InvalidAdminSocket) throw std::runtime_error("admin socket create failed");
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = htons(a_port);
		if (connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
			CloseAdminSocket(client);
			throw std::runtime_error("admin socket connect failed");
		}
		const auto text = a_request.dump() + "\n";
		std::size_t sent = 0;
		while (sent < text.size()) {
			const auto count = send(client, text.data() + sent, static_cast<int>(text.size() - sent), AdminSendFlags);
			if (count <= 0) {
				CloseAdminSocket(client);
				throw std::runtime_error("admin socket send failed");
			}
			sent += static_cast<std::size_t>(count);
		}
		std::string line;
		std::array<char, 4096> buffer{};
		while (line.find('\n') == std::string::npos) {
			const auto count = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
			if (count <= 0) {
				CloseAdminSocket(client);
				throw std::runtime_error("admin socket closed without response");
			}
			line.append(buffer.data(), static_cast<std::size_t>(count));
			if (line.size() > 64 * 1024) {
				CloseAdminSocket(client);
				throw std::runtime_error("admin response exceeded maximum size");
			}
		}
		CloseAdminSocket(client);
		line.resize(line.find('\n'));
		return Json::parse(line);
	}

	bool SendJson(co_gns_client_handle a_client, const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return co_gns_client_send(a_client, text.data(), static_cast<std::uint32_t>(text.size()), CO_GNS_DELIVERY_RELIABLE_ORDERED) == CO_GNS_SEND_SENT;
	}

	bool WaitForPacket(co_gns_client_handle a_client, std::string_view a_type, Json& a_output, std::chrono::milliseconds a_timeout)
	{
		std::array<std::uint8_t, 64 * 1024> payload{};
		const auto deadline = std::chrono::steady_clock::now() + a_timeout;
		while (std::chrono::steady_clock::now() < deadline) {
			co_gns_event event{};
			const auto result = co_gns_client_poll(a_client, &event, payload.data(), static_cast<std::uint32_t>(payload.size()));
			if (result < 0) {
				std::cerr << "Profile-ban smoke: GNS poll failed (" << result << ") while waiting for " << a_type << '\n';
				return false;
			}
			if (result == 0) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}
			if (event.type == CO_GNS_EVENT_MESSAGE && event.payload_size > 0 && event.payload_size <= payload.size()) {
				try {
					const auto packet = Json::parse(std::string{ reinterpret_cast<const char*>(payload.data()), event.payload_size });
					if (packet.is_object() && packet.value("type", std::string{}) == a_type) {
						a_output = packet;
						return true;
					}
				} catch (...) {
					std::cerr << "Profile-ban smoke: unparsable packet while waiting for " << a_type << '\n';
					return false;
				}
			}
			if (event.type == CO_GNS_EVENT_DISCONNECTED) {
				std::cerr << "Profile-ban smoke: GNS disconnected while waiting for " << a_type << " (reason " << event.reason << ", debug '" << event.debug << "')\n";
				return false;
			}
			if (event.type == CO_GNS_EVENT_OVERSIZE_MESSAGE) {
				std::cerr << "Profile-ban smoke: oversize message (" << event.payload_size << " bytes) while waiting for " << a_type << '\n';
				return false;
			}
		}
		return false;
	}

	Json IdentityHello(std::string_view a_profileId)
	{
		return Json{
			{ "type", "hello" },
			{ "protocolVersion", 2 },
			{ "clientVersion", "profile-ban-smoke-1" },
			{ "falloutRuntimeVersion", "1.11.240" },
			{ "profileId", a_profileId },
			{ "contentManifestFingerprint", std::string(64, 'a') },
			{ "capabilities", Json::array({ "interest-v1", "player-state-v1", "save-attestation-v1" }) }
		};
	}

	co_gns_client_handle ConnectGameplay(std::string_view a_host, std::uint16_t a_port, std::string_view a_profileId)
	{
		co_gns_client_handle client = nullptr;
		std::array<char, 512> error{};
		const std::string host{ a_host };
		if (co_gns_client_create(host.c_str(), a_port, &client, error.data(), error.size()) == 0 || client == nullptr) return nullptr;
		Json welcome;
		if (!WaitForPacket(client, "welcome", welcome, std::chrono::seconds(5)) || !SendJson(client, IdentityHello(a_profileId))) {
			co_gns_client_destroy(client);
			return nullptr;
		}
		Json ready;
		if (!WaitForPacket(client, "sessionReady", ready, std::chrono::seconds(5)) || ready.value("playerId", 0u) == 0) {
			co_gns_client_destroy(client);
			return nullptr;
		}
		return client;
	}

	bool ExpectProfileBan(std::string_view a_host, std::uint16_t a_port, std::string_view a_profileId)
	{
		co_gns_client_handle client = nullptr;
		std::array<char, 512> error{};
		const std::string host{ a_host };
		if (co_gns_client_create(host.c_str(), a_port, &client, error.data(), error.size()) == 0 || client == nullptr) return false;
		Json welcome;
		if (!WaitForPacket(client, "welcome", welcome, std::chrono::seconds(5)) || !SendJson(client, IdentityHello(a_profileId))) {
			co_gns_client_destroy(client);
			return false;
		}
		Json ended;
		const bool matched = WaitForPacket(client, "sessionEnded", ended, std::chrono::seconds(5)) && ended.value("code", std::string{}) == "profile_banned";
		co_gns_client_destroy(client);
		return matched;
	}

	void Close(co_gns_client_handle a_client)
	{
		if (a_client == nullptr) return;
		(void)co_gns_client_disconnect(a_client, 0, "profile-ban smoke complete");
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
		co_gns_client_destroy(a_client);
	}
}

int main(int argc, char** argv)
{
	if (argc != 5) {
		std::cerr << "Usage: CommonwealthOnlineServerLiveProfileBanGnsSmoke HOST GAME_PORT ADMIN_PORT TOKEN_FILE\n";
		return 2;
	}
	[[maybe_unused]] const WinsockScope winsock;
	std::uint16_t gamePort = 0;
	std::uint16_t adminPort = 0;
	if (!ParsePort(argv[2], gamePort) || !ParsePort(argv[3], adminPort)) return 2;
	std::ifstream tokenInput(argv[4], std::ios::binary);
	std::string token;
	std::getline(tokenInput, token);
	if (token.empty()) {
		std::cerr << "Profile-ban smoke: admin token file is empty or unreadable\n";
		return 1;
	}

	try {
		const std::string profile = "profile-ban-smoke";
		auto client = ConnectGameplay(argv[1], gamePort, profile);
		if (client == nullptr) {
			std::cerr << "Profile-ban seed client failed to activate\n";
			return 1;
		}
		std::string adminError;
		Json ban;
		std::thread admin([&] {
			try {
				ban = SendAdmin(adminPort, token, Json{
					{ "cmd", "identity_reset" },
					{ "profileId", profile },
					{ "ban", true },
					{ "reason", "profile-ban-smoke" }
				});
			} catch (const std::exception& exception) {
				adminError = exception.what();
			}
		});
		Json ended;
		const bool sawEnded = WaitForPacket(client, "sessionEnded", ended, std::chrono::seconds(5));
		admin.join();
		if (!adminError.empty()) {
			Close(client);
			std::cerr << "Profile-ban smoke exception: " << adminError << '\n';
			return 1;
		}
		if (!ban.value("ok", false) || !ban.contains("data") || !ban.at("data").value("banned", false) ||
			!ban.at("data").value("credentialReset", false) || ban.at("data").value("sessionsEnded", 0) != 1) {
			std::cerr << "Profile-ban identity_reset response: " << ban.dump() << '\n';
			Close(client);
			return 1;
		}
		if (!sawEnded || ended.value("code", std::string{}) != "profile_banned") {
			std::cerr << "Profile-ban client did not receive sessionEnded code profile_banned\n";
			Close(client);
			return 1;
		}
		Close(client);
		if (!ExpectProfileBan(argv[1], gamePort, profile)) {
			std::cerr << "Profile-ban reconnect did not receive sessionEnded code profile_banned\n";
			return 1;
		}
		const auto unban = SendAdmin(adminPort, token, Json{ { "cmd", "profile_unban" }, { "profileId", profile } });
		if (!unban.value("ok", false)) {
			std::cerr << "Profile-ban profile_unban response: " << unban.dump() << '\n';
			return 1;
		}
		client = ConnectGameplay(argv[1], gamePort, profile);
		if (client == nullptr) {
			std::cerr << "Profile-ban reconnect after unban failed to activate\n";
			return 1;
		}
		Close(client);
	} catch (const std::exception& exception) {
		std::cerr << "Profile-ban smoke exception: " << exception.what() << '\n';
		return 1;
	}

	std::cout << "C++ GNS profile-ban smoke passed\n";
	return 0;
}

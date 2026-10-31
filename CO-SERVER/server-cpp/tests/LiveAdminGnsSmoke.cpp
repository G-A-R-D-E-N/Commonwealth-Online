#include "co_gns_server_bridge.h"

#include <nlohmann/json.hpp>

#ifdef _WIN32
#	include <winsock2.h>
#	include <ws2tcpip.h>
#else
#	include <arpa/inet.h>
#	include <netinet/in.h>
#	include <sys/socket.h>
#	include <unistd.h>
#endif

#include <array>
#include <cassert>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
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

	// The admin channel is plain TCP, so this is the one part of the smoke that
	// touches sockets directly and needs both socket APIs.
#ifdef _WIN32
	using AdminSocket = SOCKET;
	constexpr AdminSocket InvalidAdminSocket = INVALID_SOCKET;
	constexpr int AdminSendFlags = 0;

	void CloseAdminSocket(AdminSocket a_socket)
	{
		closesocket(a_socket);
	}

	// Winsock needs process-wide initialisation before the first socket call.
	struct WinsockScope
	{
		WinsockScope()
		{
			WSADATA data{};
			if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
				throw std::runtime_error("WSAStartup failed");
			}
		}

		WinsockScope(const WinsockScope&) = delete;
		WinsockScope& operator=(const WinsockScope&) = delete;

		~WinsockScope()
		{
			WSACleanup();
		}
	};
#else
	using AdminSocket = int;
	constexpr AdminSocket InvalidAdminSocket = -1;
	constexpr int AdminSendFlags = MSG_NOSIGNAL;

	void CloseAdminSocket(AdminSocket a_socket)
	{
		close(a_socket);
	}

	struct WinsockScope
	{
	};
#endif

	Json SendAdmin(std::uint16_t a_port, std::string_view a_token, Json a_request, bool a_authenticate = true)
	{
		if (a_authenticate) {
			a_request["adminToken"] = a_token;
		}
		const AdminSocket client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (client == InvalidAdminSocket) {
			throw std::runtime_error("admin socket create failed");
		}
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
			if (event.type == CO_GNS_EVENT_MESSAGE && event.payload_size > 0 && event.payload_size <= payload.size()) {
				try {
					const auto packet = Json::parse(std::string{
						reinterpret_cast<const char*>(payload.data()), event.payload_size });
					if (packet.is_object() && packet.value("type", std::string{}) == a_type) {
						a_output = packet;
						return true;
					}
				} catch (...) {
					return false;
				}
			}
			if (event.type == CO_GNS_EVENT_DISCONNECTED || event.type == CO_GNS_EVENT_OVERSIZE_MESSAGE) {
				return false;
			}
		}
		return false;
	}

	// A banned address is refused in the admission callback, while the connection
	// is still being established, so there is no accepted session to end and no
	// sessionEnded to receive. What must be true is that the client never gets
	// in: a welcome here would mean the ban did not stop the connection.
	bool WaitForRefusal(co_gns_client_handle a_client, std::chrono::milliseconds a_timeout)
	{
		std::array<std::uint8_t, 64 * 1024> payload{};
		const auto deadline = std::chrono::steady_clock::now() + a_timeout;
		while (std::chrono::steady_clock::now() < deadline) {
			co_gns_event event{};
			const auto result = co_gns_client_poll(a_client, &event, payload.data(), static_cast<std::uint32_t>(payload.size()));
			// A refusal surfaces as a disconnect, and polling a connection the
			// server never accepted may fail outright. Either is the connection
			// being turned away.
			if (result < 0) {
				return true;
			}
			if (result == 0) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}
			if (event.type == CO_GNS_EVENT_DISCONNECTED) {
				return true;
			}
			if (event.type == CO_GNS_EVENT_MESSAGE && event.payload_size > 0 && event.payload_size <= payload.size()) {
				try {
					const auto packet = Json::parse(std::string{
						reinterpret_cast<const char*>(payload.data()), event.payload_size });
					if (packet.is_object() && packet.value("type", std::string{}) == "welcome") {
						return false;
					}
				} catch (...) {
				}
			}
		}
		return false;
	}

	co_gns_client_handle CreateClient(std::string_view a_host, std::uint16_t a_port)
	{
		co_gns_client_handle client = nullptr;
		std::array<char, 512> error{};
		const std::string host{ a_host };
		if (co_gns_client_create(host.c_str(), a_port, &client, error.data(), error.size()) == 0 || client == nullptr) {
			std::cerr << "GNS client create failed: " << error.data() << '\n';
			return nullptr;
		}
		return client;
	}

	Json IdentityHello(std::string_view a_profileId, std::optional<std::string_view> a_resumeToken = std::nullopt)
	{
		Json hello{
			{ "type", "hello" },
			{ "protocolVersion", 2 },
			{ "clientVersion", "admin-smoke-1" },
			{ "falloutRuntimeVersion", "1.11.240" },
			{ "profileId", a_profileId },
			{ "contentManifestFingerprint", std::string(64, 'a') },
			{ "capabilities", Json::array({ "interest-v1", "player-state-v1", "save-attestation-v1" }) }
		};
		if (a_resumeToken) {
			hello["resumeToken"] = *a_resumeToken;
		}
		return hello;
	}

	co_gns_client_handle ConnectGameplay(
		std::string_view a_host,
		std::uint16_t a_port,
		std::string_view a_profileId,
		std::uint32_t& a_playerId,
		std::string* a_resumeToken = nullptr,
		std::optional<std::string_view> a_existingResumeToken = std::nullopt)
	{
		auto client = CreateClient(a_host, a_port);
		if (client == nullptr) {
			return nullptr;
		}
		Json welcome;
		if (!WaitForPacket(client, "welcome", welcome, std::chrono::seconds(5))) {
			co_gns_client_destroy(client);
			return nullptr;
		}
		if (!SendJson(client, IdentityHello(a_profileId, a_existingResumeToken))) {
			co_gns_client_destroy(client);
			return nullptr;
		}
		Json ready;
		if (!WaitForPacket(client, "sessionReady", ready, std::chrono::seconds(5))) {
			co_gns_client_destroy(client);
			return nullptr;
		}
		a_playerId = ready.value("playerId", 0u);
		if (a_playerId == 0) {
			co_gns_client_destroy(client);
			return nullptr;
		}
		if (a_resumeToken != nullptr) {
			*a_resumeToken = ready.value("resumeToken", std::string{});
			if (a_resumeToken->empty()) {
				co_gns_client_destroy(client);
				return nullptr;
			}
		}
		const auto readyToken = ready.value("resumeToken", std::string{});
		if (!readyToken.empty()) {
			(void)SendJson(client, Json{
				{ "type", "keepAlive" },
				{ "sessionReadyAck", true },
				{ "resumeTokenAck", readyToken }
			});
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		return client;
	}

	bool ExpectIdentityRejection(
		std::string_view a_host,
		std::uint16_t a_port,
		std::string_view a_profileId,
		std::optional<std::string_view> a_resumeToken,
		std::string_view a_expectedCode)
	{
		std::string code;
		for (int attempt = 0; attempt < 8; ++attempt) {
			auto client = CreateClient(a_host, a_port);
			if (client == nullptr) {
				return false;
			}
			Json welcome;
			if (!WaitForPacket(client, "welcome", welcome, std::chrono::seconds(5)) ||
				!SendJson(client, IdentityHello(a_profileId, a_resumeToken))) {
				co_gns_client_destroy(client);
				return false;
			}
			Json ended;
			const bool gotEnd = WaitForPacket(client, "sessionEnded", ended, std::chrono::seconds(5));
			code = gotEnd ? ended.value("code", std::string{}) : std::string{ "<none>" };
			co_gns_client_destroy(client);
			std::this_thread::sleep_for(std::chrono::milliseconds(150));
			if (gotEnd && code == a_expectedCode) {
				return true;
			}
			if (code != "duplicate_identity") {
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
		}
		std::cerr << "Identity rejection code was '" << code << "', expected '" << a_expectedCode << "'\n";
		return false;
	}

	void Close(co_gns_client_handle a_client)
	{
		if (a_client == nullptr) {
			return;
		}
		(void)co_gns_client_disconnect(a_client, 0, "admin smoke complete");
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
		co_gns_client_destroy(a_client);
	}
}

int main(int argc, char** argv)
{
	if (argc != 5) {
		std::cerr << "Usage: CommonwealthOnlineServerLiveAdminGnsSmoke HOST GAME_PORT ADMIN_PORT TOKEN_FILE\n";
		return 2;
	}
	// Winsock has to be initialised before the first socket call and torn down
	// when this returns; the POSIX build gets an empty object.
	[[maybe_unused]] const WinsockScope winsock;
	std::uint16_t gamePort = 0;
	std::uint16_t adminPort = 0;
	if (!ParsePort(argv[2], gamePort) || !ParsePort(argv[3], adminPort)) {
		std::cerr << "Invalid port\n";
		return 2;
	}
	std::ifstream tokenInput(argv[4], std::ios::binary);
	std::string token;
	std::getline(tokenInput, token);
	if (token.empty()) {
		std::cerr << "Admin token file was empty\n";
		return 1;
	}

	try {
		const auto unauthorized = SendAdmin(adminPort, token, Json{ { "cmd", "ping" } }, false);
		if (unauthorized.value("ok", true)) {
			std::cerr << "Unauthenticated admin ping was accepted\n";
			return 1;
		}
		const auto ping = SendAdmin(adminPort, token, Json{ { "cmd", "ping" }, { "id", 1 } });
		if (!ping.value("ok", false) || ping.value("id", 0) != 1 || !ping.at("data").value("pong", false)) {
			std::cerr << "Authenticated admin ping failed\n";
			return 1;
		}

		std::uint32_t firstPlayerId = 0;
		auto first = ConnectGameplay(argv[1], gamePort, "admin-smoke-first", firstPlayerId);
		if (first == nullptr) {
			std::cerr << "First gameplay client failed to activate\n";
			return 1;
		}

		const auto status = SendAdmin(adminPort, token, Json{ { "cmd", "status" } });
		if (!status.value("ok", false) || status.at("data").value("connected_clients", 0) < 1) {
			std::cerr << "Admin status did not report active client\n";
			Close(first);
			return 1;
		}
		const auto clients = SendAdmin(adminPort, token, Json{ { "cmd", "clients" } });
		bool foundFirst = false;
		if (clients.value("ok", false)) {
			for (const auto& client : clients.at("data").at("clients")) {
				foundFirst = foundFirst || client.value("player_id", 0u) == firstPlayerId;
			}
		}
		if (!foundFirst) {
			std::cerr << "Admin clients did not include active player\n";
			Close(first);
			return 1;
		}

		const auto time = SendAdmin(adminPort, token, Json{ { "cmd", "world_time" }, { "hhmm", "0630" } });
		if (!time.value("ok", false)) {
			std::cerr << "world_time admin command failed\n";
			Close(first);
			return 1;
		}
		Json timePacket;
		if (!WaitForPacket(first, "serverWorldState", timePacket, std::chrono::seconds(5)) ||
			timePacket.value("timeHHmm", std::string{}) != "0630") {
			std::cerr << "Gameplay client did not receive admin time update\n";
			Close(first);
			return 1;
		}

		const auto weather = SendAdmin(adminPort, token, Json{ { "cmd", "world_weather" }, { "weather", "0x2b52a" } });
		if (!weather.value("ok", false)) {
			std::cerr << "world_weather admin command failed\n";
			Close(first);
			return 1;
		}
		Json weatherPacket;
		if (!WaitForPacket(first, "serverWorldState", weatherPacket, std::chrono::seconds(5)) ||
			weatherPacket.contains("weatherConsoleArg") ||
			weatherPacket.value("weatherFormId", std::string{}) != "0002B52A") {
			std::cerr << "Gameplay client did not receive admin weather update\n";
			Close(first);
			return 1;
		}

		const auto kick = SendAdmin(adminPort, token, Json{ { "cmd", "kick" }, { "playerId", firstPlayerId }, { "reason", "smoke" } });
		if (!kick.value("ok", false)) {
			std::cerr << "Admin kick failed\n";
			Close(first);
			return 1;
		}
		Json kicked;
		if (!WaitForPacket(first, "sessionEnded", kicked, std::chrono::seconds(5)) || kicked.value("code", std::string{}) != "kicked") {
			std::cerr << "Kicked client did not receive sessionEnded\n";
			Close(first);
			return 1;
		}
		Close(first);

		std::uint32_t secondPlayerId = 0;
		auto second = ConnectGameplay(argv[1], gamePort, "admin-smoke-second", secondPlayerId);
		if (second == nullptr) {
			std::cerr << "Second gameplay client failed to activate\n";
			return 1;
		}
		const auto ban = SendAdmin(adminPort, token, Json{ { "cmd", "ban" }, { "playerId", secondPlayerId }, { "reason", "admin-smoke-ban" } });
		if (!ban.value("ok", false)) {
			std::cerr << "Admin ban failed\n";
			Close(second);
			return 1;
		}
		Json banned;
		if (!WaitForPacket(second, "sessionEnded", banned, std::chrono::seconds(5)) || banned.value("code", std::string{}) != "banned") {
			std::cerr << "Banned client did not receive sessionEnded\n";
			Close(second);
			return 1;
		}
		Close(second);

		auto rejected = CreateClient(argv[1], gamePort);
		if (rejected == nullptr) {
			return 1;
		}
		if (!WaitForRefusal(rejected, std::chrono::seconds(5))) {
			std::cerr << "Banned reconnect was not refused before admission\n";
			Close(rejected);
			return 1;
		}
		Close(rejected);

		const auto unban = SendAdmin(adminPort, token, Json{ { "cmd", "unban" }, { "ip", "127.0.0.1" } });
		if (!unban.value("ok", false)) {
			std::cerr << "Admin unban failed\n";
			return 1;
		}
		const auto bans = SendAdmin(adminPort, token, Json{ { "cmd", "bans" } });
		if (!bans.value("ok", false) || !bans.at("data").at("bans").empty()) {
			std::cerr << "Ban list was not empty after unban\n";
			return 1;
		}

		const std::string resetProfile = "admin-smoke-reset";
		std::uint32_t resetPlayerId = 0;
		std::string originalResumeToken;
		auto resetSeed = ConnectGameplay(argv[1], gamePort, resetProfile, resetPlayerId, &originalResumeToken);
		if (resetSeed == nullptr || originalResumeToken.empty()) {
			std::cerr << "Identity reset seed connection failed\n";
			Close(resetSeed);
			return 1;
		}
		Close(resetSeed);

		if (!ExpectIdentityRejection(argv[1], gamePort, resetProfile, std::nullopt, "resume_required")) {
			std::cerr << "Identity reset precondition did not require resume token\n";
			return 1;
		}

		const auto reset = SendAdmin(adminPort, token, Json{ { "cmd", "identity_reset" }, { "profileId", resetProfile } });
		if (!reset.value("ok", false) || reset.at("data").value("profileId", std::string{}) != resetProfile) {
			std::cerr << "Live identity_reset admin command failed\n";
			return 1;
		}

		std::uint32_t resetAfterPlayerId = 0;
		std::string replacementResumeToken;
		auto resetAfter = ConnectGameplay(argv[1], gamePort, resetProfile, resetAfterPlayerId, &replacementResumeToken);
		if (resetAfter == nullptr || replacementResumeToken.empty() || replacementResumeToken == originalResumeToken) {
			std::cerr << "Identity reset did not allow a fresh credential\n";
			Close(resetAfter);
			return 1;
		}
		Close(resetAfter);

		if (!ExpectIdentityRejection(argv[1], gamePort, resetProfile, originalResumeToken, "invalid_resume")) {
			std::cerr << "Pre-reset resume token was not invalidated\n";
			return 1;
		}

		std::uint32_t finalPlayerId = 0;
		auto finalClient = ConnectGameplay(argv[1], gamePort, "admin-smoke-final", finalPlayerId);
		if (finalClient == nullptr || finalPlayerId == 0) {
			std::cerr << "Unbanned client could not reconnect\n";
			Close(finalClient);
			return 1;
		}
		Close(finalClient);
	} catch (const std::exception& exception) {
		std::cerr << "Admin smoke exception: " << exception.what() << '\n';
		return 1;
	}

	std::cout << "C++ GNS admin control smoke passed\n";
	return 0;
}

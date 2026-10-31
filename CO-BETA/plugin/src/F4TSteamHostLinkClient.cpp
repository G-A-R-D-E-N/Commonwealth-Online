#include "F4TSteamHostLinkClient.h"

#include "F4TSteamHostLink.h"
#include "F4TSteamId.h"

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <fstream>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

namespace F4T::SteamHostLink
{
	namespace
	{
		constexpr int kTimeoutMilliseconds = 500;
		// constraint: this total budget must stay above ServerLiveRelay's 3 s wait
		constexpr std::chrono::milliseconds kTotalReplyBudget{ 4000 };
		constexpr std::size_t kMaximumTokenBytes = 256;
		constexpr std::size_t kMaximumResponseBytes = 4096;
		constexpr int kLinkFileAttempts = 3;
		constexpr auto kLinkFileRetryDelay = std::chrono::milliseconds{ 100 };

#ifdef _WIN32
		using NativeSocket = SOCKET;
		constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;

		void CloseSocket(NativeSocket a_socket) { ::closesocket(a_socket); }

		bool SetTimeouts(NativeSocket a_socket)
		{
			const DWORD timeout = kTimeoutMilliseconds;
			return ::setsockopt(a_socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0 &&
			       ::setsockopt(a_socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0;
		}

		struct SocketRuntime
		{
			SocketRuntime() { WSADATA data{}; ready = ::WSAStartup(MAKEWORD(2, 2), &data) == 0; }
			~SocketRuntime() { if (ready) ::WSACleanup(); }
			bool ready{ false };
		};
#else
		using NativeSocket = int;
		constexpr NativeSocket kInvalidSocket = -1;

		void CloseSocket(NativeSocket a_socket) { ::close(a_socket); }

		bool SetTimeouts(NativeSocket a_socket)
		{
			timeval timeout{ 0, kTimeoutMilliseconds * 1000 };
			return ::setsockopt(a_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
			       ::setsockopt(a_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0;
		}

		struct SocketRuntime
		{
			bool ready{ true };
		};
#endif

		std::string ReadFirstLine(const std::filesystem::path& a_path)
		{
			std::ifstream input(a_path, std::ios::binary);
			std::string line;
			if (!input || !std::getline(input, line)) return {};
			while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
			return line;
		}

		bool IsTokenText(std::string_view a_token)
		{
			if (a_token.empty() || a_token.size() > kMaximumTokenBytes) return false;
			for (const unsigned char ch : a_token) {
				if (ch <= 0x20 || ch >= 0x7f) return false;
			}
			return true;
		}

		bool WouldBlock()
		{
#ifdef _WIN32
			const auto error = ::WSAGetLastError();
			return error == WSAEWOULDBLOCK || error == WSAETIMEDOUT;
#else
			return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
		}

		bool SendAll(NativeSocket a_socket, std::string_view a_payload)
		{
			while (!a_payload.empty()) {
				const auto sent = ::send(a_socket, a_payload.data(), static_cast<int>(a_payload.size()), 0);
				if (sent <= 0) return false;
				a_payload.remove_prefix(static_cast<std::size_t>(sent));
			}
			return true;
		}
	}

	std::optional<LinkEndpoint> ReadEndpoint(const std::filesystem::path& a_directory, std::uint16_t a_gamePort)
	{
		const auto portText = ReadFirstLine(PortPath(a_directory, a_gamePort));
		std::uint32_t port = 0;
		const auto* const end = portText.data() + portText.size();
		const auto [parsed, error] = std::from_chars(portText.data(), end, port);
		if (portText.empty() || error != std::errc{} || parsed != end || port == 0 || port > 65535) return std::nullopt;

		auto token = ReadFirstLine(TokenPath(a_directory, a_gamePort));
		if (!IsTokenText(token)) return std::nullopt;
		return LinkEndpoint{ static_cast<std::uint16_t>(port), std::move(token) };
	}

	std::optional<LinkEndpoint> ReadEndpointWithRetry(const std::filesystem::path& a_directory, std::uint16_t a_gamePort)
	{
		for (int attempt = 0; attempt < kLinkFileAttempts; ++attempt) {
			if (auto endpoint = ReadEndpoint(a_directory, a_gamePort)) return endpoint;
			if (attempt + 1 < kLinkFileAttempts) {
				std::this_thread::sleep_for(kLinkFileRetryDelay);
			}
		}
		return std::nullopt;
	}

	bool Send(const LinkEndpoint& a_endpoint, nlohmann::json a_request, nlohmann::json* a_reply)
	{
		if (a_endpoint.port == 0 || a_endpoint.token.empty()) return false;
		const SocketRuntime runtime;
		if (!runtime.ready) return false;
		const auto client = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (client == kInvalidSocket) return false;
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = htons(a_endpoint.port);
		bool ok = false;
		if (SetTimeouts(client) && ::connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) {
			a_request["adminToken"] = a_endpoint.token;
			if (SendAll(client, a_request.dump() + "\n")) {
				std::string response;
				std::array<char, 512> buffer{};
				const auto deadline = std::chrono::steady_clock::now() + kTotalReplyBudget;
				while (response.find('\n') == std::string::npos && response.size() < kMaximumResponseBytes) {
					const auto count = ::recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
					if (count <= 0) {
						if (count < 0 && WouldBlock() && std::chrono::steady_clock::now() < deadline) continue;
						break;
					}
					response.append(buffer.data(), static_cast<std::size_t>(count));
				}
				if (const auto newline = response.find('\n'); newline != std::string::npos) {
					const auto parsedResponse = nlohmann::json::parse(response.substr(0, newline), nullptr, false);
					if (a_reply != nullptr && parsedResponse.is_object()) {
						*a_reply = parsedResponse;
					}
					ok = parsedResponse.is_object() && parsedResponse.value("ok", false) == true;
				}
			}
		}
		CloseSocket(client);
		return ok;
	}

	std::optional<nlohmann::json> Request(const LinkEndpoint& a_endpoint, nlohmann::json a_request)
	{
		nlohmann::json reply;
		if (!Send(a_endpoint, std::move(a_request), &reply)) return std::nullopt;
		return reply.value("data", nlohmann::json(nullptr));
	}

	std::optional<SteamAdmission::HostPolicy> ReadHostPolicy(const LinkEndpoint& a_endpoint)
	{
		const auto data = Request(a_endpoint, nlohmann::json{ { "cmd", kPolicyCommand } });
		if (!data || !data->is_object()) return std::nullopt;
		const auto friendsOnly = data->find("friendsOnly");
		const auto passwordSet = data->find("serverPasswordSet");
		if (friendsOnly == data->end() || !friendsOnly->is_boolean() || passwordSet == data->end() || !passwordSet->is_boolean()) return std::nullopt;
		return SteamAdmission::HostPolicy{ friendsOnly->get<bool>(), passwordSet->get<bool>() };
	}

	Transport::SteamIdentityBinder MakeIdentityBinder(
		const std::filesystem::path& a_directory,
		std::uint16_t a_gamePort,
		std::uint64_t a_hostSteamId)
	{
		const auto endpoint = ReadEndpointWithRetry(a_directory, a_gamePort);
		if (!endpoint) return {};

		(void)Send(*endpoint, nlohmann::json{ { "cmd", kClearCommand } });
		if (SteamId::IsIndividual(a_hostSteamId)) {
			(void)Send(*endpoint, nlohmann::json{ { "cmd", kHostCommand }, { "steamId", std::to_string(a_hostSteamId) } });
		}
		return Transport::SteamIdentityBinder{
			.bind = [a_directory, a_gamePort](std::uint16_t a_localPort, std::uint64_t a_steamId, bool a_passwordRequired) {
				const auto current = ReadEndpointWithRetry(a_directory, a_gamePort);
				return current && SteamId::IsIndividual(a_steamId) &&
				       Send(*current, nlohmann::json{ { "cmd", kBindCommand }, { "port", a_localPort }, { "steamId", std::to_string(a_steamId) }, { "passwordRequired", a_passwordRequired } });
			},
			.unbind = [a_directory, a_gamePort](std::uint16_t a_localPort) {
				if (const auto current = ReadEndpoint(a_directory, a_gamePort)) {
					(void)Send(*current, nlohmann::json{ { "cmd", kUnbindCommand }, { "port", a_localPort } });
				}
			},
			.hostPolicy = ReadHostPolicy(*endpoint)
		};
	}

	Transport::SteamIdentityBinder MakeDefaultIdentityBinder(std::uint16_t a_gamePort, std::uint64_t a_hostSteamId)
	{
		const auto directory = Directory();
		if (!directory) return {};
		return MakeIdentityBinder(*directory, a_gamePort, a_hostSteamId);
	}
}

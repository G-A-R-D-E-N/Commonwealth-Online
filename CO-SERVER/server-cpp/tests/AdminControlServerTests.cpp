#include "AdminControlServer.h"

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

namespace
{
#ifdef _WIN32
	using NativeSocket = SOCKET;
	constexpr NativeSocket InvalidSocket = INVALID_SOCKET;
#else
	using NativeSocket = int;
	constexpr NativeSocket InvalidSocket = -1;
#endif

	void SetReceiveTimeout(std::uintptr_t a_socket, std::chrono::milliseconds a_timeout)
	{
#ifdef _WIN32
		const auto timeout = static_cast<DWORD>(a_timeout.count());
		setsockopt(
			static_cast<NativeSocket>(a_socket),
			SOL_SOCKET,
			SO_RCVTIMEO,
			reinterpret_cast<const char*>(&timeout),
			static_cast<int>(sizeof(timeout)));
#else
		timeval timeout{};
		timeout.tv_usec = static_cast<decltype(timeout.tv_usec)>(a_timeout.count() * 1000);
		setsockopt(static_cast<NativeSocket>(a_socket), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
	}

	void CloseSocket(NativeSocket a_socket)
	{
#ifdef _WIN32
		closesocket(a_socket);
#else
		close(a_socket);
#endif
	}

	NativeSocket ConnectLoopback(std::uint16_t a_port)
	{
		const auto client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		assert(client != InvalidSocket);

		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = htons(a_port);
		assert(connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
		return client;
	}

	nlohmann::json SendRequest(std::uint16_t a_port, nlohmann::json a_request)
	{
		const auto client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		assert(client != InvalidSocket);

		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = htons(a_port);
		assert(connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);

		const auto payload = a_request.dump() + "\n";
		std::size_t sent = 0;
		while (sent < payload.size()) {
#ifdef _WIN32
			const auto count = send(client, payload.data() + sent, static_cast<int>(payload.size() - sent), 0);
#else
			const auto count = send(client, payload.data() + sent, payload.size() - sent, MSG_NOSIGNAL);
#endif
			assert(count > 0);
			sent += static_cast<std::size_t>(count);
		}

		std::string line;
		std::array<char, 1024> buffer{};
		while (line.find('\n') == std::string::npos) {
#ifdef _WIN32
			const auto count = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
			const auto count = recv(client, buffer.data(), buffer.size(), 0);
#endif
			assert(count > 0);
			line.append(buffer.data(), static_cast<std::size_t>(count));
		}
		CloseSocket(client);
		line.resize(line.find('\n'));
		return nlohmann::json::parse(line);
	}
}

int main()
{
	using CommonwealthOnline::Server::AdminCommand;
	using CommonwealthOnline::Server::AdminControlServer;
	using Json = nlohmann::json;

	assert(AdminControlServer::TokenEquals("secret", "secret"));
	assert(!AdminControlServer::TokenEquals("secret", "Secret"));
	assert(!AdminControlServer::TokenEquals("short", "longer"));

	{
		AdminControlServer invalid{ 0, {} };
		std::string error;
		assert(!invalid.Start(error));
		assert(!error.empty());
	}

	{
		std::atomic_bool receiveCalled{ false };
		std::string error;
		AdminControlServer server{ 0, "test-admin-token", [&](std::uintptr_t, bool a_receive) {
			if (a_receive) {
				receiveCalled.store(true, std::memory_order_release);
				return false;
			}
			return true;
		} };
		assert(server.Start(error));
		const auto client = ConnectLoopback(server.LocalPort());
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (!receiveCalled.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		assert(receiveCalled.load(std::memory_order_acquire));
		server.Stop();
		CloseSocket(client);
	}

	{
		std::atomic_bool receiveCalled{ false };
		std::atomic_bool sendCalled{ false };
		std::string error;
		AdminControlServer server{ 0, "test-admin-token", [&](std::uintptr_t, bool a_receive) {
			if (a_receive) {
				receiveCalled.store(true, std::memory_order_release);
				return true;
			}
			sendCalled.store(true, std::memory_order_release);
			return false;
		} };
		assert(server.Start(error));
		const auto client = ConnectLoopback(server.LocalPort());
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (!sendCalled.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		assert(receiveCalled.load(std::memory_order_acquire));
		assert(sendCalled.load(std::memory_order_acquire));
		server.Stop();
		CloseSocket(client);
	}

	AdminControlServer server{ 0, "test-admin-token" };
	std::string error;
	assert(server.Start(error));
	assert(error.empty());
	assert(server.LocalPort() != 0);

	const auto unauthorized = SendRequest(server.LocalPort(), Json{
		{ "adminToken", "wrong-token" },
		{ "cmd", "ping" },
		{ "id", 3 }
	});
	assert(!unauthorized.value("ok", true));
	assert(unauthorized.value("id", 0) == 3);
	assert(unauthorized.value("error", std::string{}).find("Unauthorized") != std::string::npos);
	AdminCommand unexpected;
	assert(!server.TryPop(unexpected));

	Json authenticatedResponse;
	std::thread client([&] {
		authenticatedResponse = SendRequest(server.LocalPort(), Json{
			{ "adminToken", "test-admin-token" },
			{ "command", "  PiNg  " },
			{ "id", 7 }
		});
	});

	AdminCommand command;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (!server.TryPop(command) && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	assert(command.ticket != 0);
	assert(command.request.value("cmd", std::string{}) == "ping");
	assert(command.request.value("id", 0) == 7);
	assert(!command.request.contains("adminToken"));

	assert(server.Complete(command.ticket, AdminControlServer::Success(
		Json{ { "pong", true } },
		{},
		&command.request)));
	client.join();

	assert(authenticatedResponse.value("ok", false));
	assert(authenticatedResponse.value("id", 0) == 7);
	assert(authenticatedResponse.at("data").value("pong", false));
	assert(!server.Complete(command.ticket, Json::object()));

	const auto idle = ConnectLoopback(server.LocalPort());
	Json whileIdle;
	std::thread second([&] {
		whileIdle = SendRequest(server.LocalPort(), Json{
			{ "adminToken", "test-admin-token" },
			{ "cmd", "ping" },
			{ "id", 11 }
		});
	});
	AdminCommand concurrent;
	const auto concurrentDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (!server.TryPop(concurrent) && std::chrono::steady_clock::now() < concurrentDeadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	assert(concurrent.ticket != 0);
	assert(server.Complete(concurrent.ticket, AdminControlServer::Success({}, {}, &concurrent.request)));
	second.join();
	assert(whileIdle.value("ok", false));
	assert(whileIdle.value("id", 0) == 11);
	CloseSocket(idle);

	Json timedOutResponse;
	std::thread abandoned([&] {
		timedOutResponse = SendRequest(server.LocalPort(), Json{
			{ "adminToken", "test-admin-token" },
			{ "cmd", "kick" },
			{ "playerId", 4 },
			{ "id", 13 }
		});
	});
	abandoned.join();
	assert(!timedOutResponse.value("ok", true));
	assert(timedOutResponse.value("error", std::string{}).find("timed out") != std::string::npos);
	AdminCommand cancelled;
	assert(!server.TryPop(cancelled));

	Json racedResponse;
	std::thread raced([&] {
		racedResponse = SendRequest(server.LocalPort(), Json{
			{ "adminToken", "test-admin-token" },
			{ "cmd", "kick" },
			{ "playerId", 9 },
			{ "id", 17 }
		});
	});
	AdminCommand claimed;
	const auto claimDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (!server.TryPop(claimed) && std::chrono::steady_clock::now() < claimDeadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	assert(claimed.ticket != 0);
	assert(claimed.request.value("id", 0) == 17);
	std::this_thread::sleep_for(std::chrono::milliseconds(5200));
	assert(server.Complete(claimed.ticket, AdminControlServer::Success(
		Json{ { "kicked", 9 } },
		{},
		&claimed.request)));
	raced.join();
	assert(racedResponse.value("ok", false));
	assert(racedResponse.value("id", 0) == 17);
	assert(racedResponse.at("data").value("kicked", 0) == 9);

	server.Stop();
	assert(server.LocalPort() == 0);

	{
		std::atomic_bool clientAccepted{ false };
		AdminControlServer shutdownServer{ 0, "test-admin-token", [&](std::uintptr_t a_socket, bool a_receive) {
			if (a_receive) {
				SetReceiveTimeout(a_socket, std::chrono::seconds(5));
				clientAccepted.store(true, std::memory_order_release);
			}
			return true;
		} };
		std::string shutdownStartError;
		assert(shutdownServer.Start(shutdownStartError));
		const auto idleClient = ConnectLoopback(shutdownServer.LocalPort());
		const auto acceptDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (!clientAccepted.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < acceptDeadline) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		assert(clientAccepted.load(std::memory_order_acquire));
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

		std::atomic_bool stopped{ false };
		std::thread stopper([&] {
			shutdownServer.Stop();
			stopped.store(true, std::memory_order_release);
		});
		const auto stopDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (!stopped.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < stopDeadline) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		assert(stopped.load(std::memory_order_acquire));
		stopper.join();
		assert(shutdownServer.LocalPort() == 0);
		CloseSocket(idleClient);
	}

	{
		// constraint: the live settings commands are reachable over the loopback port
		std::vector<std::string> handled;
		AdminControlServer liveServer{ 0, "live-token", {}, [&handled](const Json& a_request) {
			handled.push_back(a_request.value("cmd", std::string{}));
			return AdminControlServer::Success(Json{ { "echo", a_request.value("cmd", std::string{}) } }, {}, &a_request);
		} };
		std::string liveStartError;
		assert(liveServer.Start(liveStartError));

		for (const auto* command : { "apply_settings", "server_config" }) {
			const auto refused = SendRequest(liveServer.LocalPort(), Json{
				{ "adminToken", "wrong-token" },
				{ "cmd", command },
				{ "document", Json{ { "enable_chat", false } } }
			});
			assert(!refused.value("ok", true));
			assert(refused.value("error", std::string{}).find("Unauthorized") != std::string::npos);

			const auto accepted = SendRequest(liveServer.LocalPort(), Json{
				{ "adminToken", "live-token" },
				{ "cmd", command },
				{ "document", Json{ { "enable_chat", false } } }
			});
			assert(accepted.value("ok", false));
			assert(accepted.at("data").value("echo", std::string{}) == command);
		}
		assert(handled.size() == 2);
		assert(handled.at(0) == "apply_settings");
		assert(handled.at(1) == "server_config");
		liveServer.Stop();
	}

	return 0;
}

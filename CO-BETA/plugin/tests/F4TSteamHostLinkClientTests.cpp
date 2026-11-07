#include "F4TSteamHostLinkClient.h"

#include "F4TSteamHostLink.h"

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>
#include <thread>
#include <vector>

namespace
{
	using Json = nlohmann::json;

	constexpr std::uint16_t kGamePort = 7777;
	constexpr std::uint64_t kHost = 76561198000000007ull;
	constexpr std::uint64_t kGuest = 76561198000000042ull;

#ifdef _WIN32
	using NativeSocket = SOCKET;
	constexpr NativeSocket kInvalid = INVALID_SOCKET;
	void Close(NativeSocket a_socket) { closesocket(a_socket); }
#else
	using NativeSocket = int;
	constexpr NativeSocket kInvalid = -1;
	void Close(NativeSocket a_socket) { close(a_socket); }
#endif

	class FakeLinkServer
	{
	public:
		explicit FakeLinkServer(bool a_reply, std::string a_policyReply = {}) : _reply(a_reply), _policyReply(std::move(a_policyReply))
		{
			_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			assert(_listen != kInvalid);
			sockaddr_in address{};
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			address.sin_port = 0;
			assert(bind(_listen, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
			assert(listen(_listen, 8) == 0);
#ifdef _WIN32
			int length = sizeof(address);
#else
			socklen_t length = sizeof(address);
#endif
			assert(getsockname(_listen, reinterpret_cast<sockaddr*>(&address), &length) == 0);
			port = ntohs(address.sin_port);
			_thread = std::thread([this] { Serve(); });
		}

		~FakeLinkServer()
		{
			_stop = true;
#ifndef _WIN32
			shutdown(_listen, SHUT_RDWR);
#endif
			Close(_listen);
			_thread.join();
		}

		std::vector<Json> Requests()
		{
			std::scoped_lock lock(_mutex);
			return _requests;
		}

		std::uint16_t port{};

	private:
		void Serve()
		{
			while (!_stop) {
				const auto client = accept(_listen, nullptr, nullptr);
				if (client == kInvalid) return;
				std::string line;
				std::array<char, 512> buffer{};
				while (line.find('\n') == std::string::npos) {
					const auto count = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
					if (count <= 0) break;
					line.append(buffer.data(), static_cast<std::size_t>(count));
				}
				bool policy = false;
				if (const auto newline = line.find('\n'); newline != std::string::npos) {
					std::scoped_lock lock(_mutex);
					_requests.push_back(Json::parse(line.substr(0, newline)));
					policy = _requests.back().value("cmd", "") == "steam_policy";
				}
				if (_reply) {
					const std::string response = policy && !_policyReply.empty() ? _policyReply : "{\"ok\":true}\n";
					(void)send(client, response.data(), static_cast<int>(response.size()), 0);
				}
				Close(client);
			}
		}

		bool _reply{};
		std::string _policyReply;
		std::atomic_bool _stop{ false };
		NativeSocket _listen{ kInvalid };
		std::thread _thread;
		std::mutex _mutex;
		std::vector<Json> _requests;
	};

	std::filesystem::path FreshDirectory(const char* a_name)
	{
		const auto directory = std::filesystem::temp_directory_path() / a_name;
		std::filesystem::remove_all(directory);
		std::filesystem::create_directories(directory);
		return directory;
	}

	void WriteLink(const std::filesystem::path& a_directory, const std::string& a_port, const std::string& a_token)
	{
		std::ofstream(F4T::SteamHostLink::PortPath(a_directory, kGamePort), std::ios::binary) << a_port;
		std::ofstream(F4T::SteamHostLink::TokenPath(a_directory, kGamePort), std::ios::binary) << a_token;
	}

	void TestReadEndpoint()
	{
		const auto directory = FreshDirectory("co-host-link-client-read");
		assert(!F4T::SteamHostLink::ReadEndpoint(directory, kGamePort));

		WriteLink(directory, "51234\r\n", "link-token\n");
		const auto endpoint = F4T::SteamHostLink::ReadEndpoint(directory, kGamePort);
		assert(endpoint && endpoint->port == 51234 && endpoint->token == "link-token");
		assert(!F4T::SteamHostLink::ReadEndpoint(directory, 7778));

		for (const char* port : { "0", "70000", "-1", "51234x", "" }) {
			WriteLink(directory, port, "link-token");
			assert(!F4T::SteamHostLink::ReadEndpoint(directory, kGamePort));
		}
		WriteLink(directory, "51234", "");
		assert(!F4T::SteamHostLink::ReadEndpoint(directory, kGamePort));
		WriteLink(directory, "51234", "has space");
		assert(!F4T::SteamHostLink::ReadEndpoint(directory, kGamePort));
		std::filesystem::remove_all(directory);
	}

	void TestNoLinkGivesEmptyBinder()
	{
		const auto directory = FreshDirectory("co-host-link-client-none");
		assert(F4T::SteamHostLink::MakeIdentityBinder(directory, kGamePort, kHost).IsEmpty());
		std::filesystem::remove_all(directory);
	}

	void TestBinderSendsAuthenticatedRequests()
	{
		FakeLinkServer server{ true, "{\"ok\":true,\"data\":{\"friendsOnly\":false,\"serverPasswordSet\":true}}\n" };
		const auto directory = FreshDirectory("co-host-link-client-send");
		WriteLink(directory, std::to_string(server.port), "link-token");

		const auto binder = F4T::SteamHostLink::MakeIdentityBinder(directory, kGamePort, kHost);
		assert(binder.IsComplete());
		assert(binder.hostPolicy && !binder.hostPolicy->friendsOnly && binder.hostPolicy->serverPasswordSet);
		assert(binder.bind(40000, kGuest, true));
		assert(!binder.bind(40001, 76561197960265728ull, false));
		binder.unbind(40000);

		const auto requests = server.Requests();
		assert(requests.size() == 5);
		for (const auto& request : requests) assert(request.at("adminToken") == "link-token");
		assert(requests[0].at("cmd") == "steam_clear");
		assert(requests[1].at("cmd") == "steam_host" && requests[1].at("steamId") == "76561198000000007");
		assert(requests[2].at("cmd") == "steam_policy");
		assert(requests[3].at("cmd") == "steam_bind" && requests[3].at("port") == 40000 && requests[3].at("steamId") == "76561198000000042" && requests[3].at("passwordRequired") == true);
		assert(requests[4].at("cmd") == "steam_unbind" && requests[4].at("port") == 40000);
		std::filesystem::remove_all(directory);
	}

	void TestSendCapturesReply()
	{
		FakeLinkServer server{ true };
		const auto directory = FreshDirectory("co-host-link-client-reply");
		WriteLink(directory, std::to_string(server.port), "link-token");
		const auto endpoint = F4T::SteamHostLink::ReadEndpoint(directory, kGamePort);
		assert(endpoint);

		Json reply;
		assert(F4T::SteamHostLink::Send(*endpoint, Json{ { "cmd", "world_mode" }, { "mode", "free-roam" } }, &reply));
		assert(reply.at("ok") == true);

		FakeLinkServer silent{ false };
		const auto silentDirectory = FreshDirectory("co-host-link-client-reply-silent");
		WriteLink(silentDirectory, std::to_string(silent.port), "link-token");
		const auto silentEndpoint = F4T::SteamHostLink::ReadEndpoint(silentDirectory, kGamePort);
		assert(silentEndpoint);
		Json missing;
		assert(!F4T::SteamHostLink::Send(*silentEndpoint, Json{ { "cmd", "world_mode" } }, &missing));
		assert(missing.is_null());
		std::filesystem::remove_all(silentDirectory);

		const auto requests = server.Requests();
		assert(requests.size() == 1);
		assert(requests[0].at("adminToken") == "link-token");
		assert(requests[0].at("cmd") == "world_mode" && requests[0].at("mode") == "free-roam");
		std::filesystem::remove_all(directory);
	}

	void TestBindFailsWithoutAcknowledgement()
	{
		FakeLinkServer server{ false };
		const auto directory = FreshDirectory("co-host-link-client-noack");
		WriteLink(directory, std::to_string(server.port), "link-token");
		const auto binder = F4T::SteamHostLink::MakeIdentityBinder(directory, kGamePort, 0);
		assert(binder.IsComplete() && !binder.hostPolicy);
		assert(!binder.bind(40000, kGuest, false));
		const auto requests = server.Requests();
		assert(requests.size() == 3);
		assert(requests[0].at("cmd") == "steam_clear");
		assert(requests[1].at("cmd") == "steam_policy");
		assert(requests[2].at("cmd") == "steam_bind" && requests[2].at("passwordRequired") == false);
		std::filesystem::remove_all(directory);
	}

	void TestPolicyNeedsWellFormedReply()
	{
		for (const std::string& reply : { std::string{}, std::string{ "{\"ok\":true,\"data\":{\"friendsOnly\":\"no\",\"serverPasswordSet\":true}}\n" } }) {
			FakeLinkServer server{ true, reply };
			const auto directory = FreshDirectory("co-host-link-client-policy");
			WriteLink(directory, std::to_string(server.port), "link-token");
			const auto binder = F4T::SteamHostLink::MakeIdentityBinder(directory, kGamePort, 0);
			assert(binder.IsComplete() && !binder.hostPolicy);
			std::filesystem::remove_all(directory);
		}
	}

	void TestBindRereadsTheLink()
	{
		FakeLinkServer first{ true };
		FakeLinkServer second{ true };
		const auto directory = FreshDirectory("co-host-link-client-reread");
		WriteLink(directory, std::to_string(first.port), "first-token");

		const auto binder = F4T::SteamHostLink::MakeIdentityBinder(directory, kGamePort, kHost);
		assert(binder.IsComplete());
		const auto firstRequests = first.Requests();
		assert(firstRequests.size() == 3);
		assert(firstRequests[0].at("adminToken") == "first-token");

		WriteLink(directory, std::to_string(second.port), "second-token");
		assert(binder.bind(40000, kGuest, false));

		const auto secondRequests = second.Requests();
		assert(secondRequests.size() == 1);
		assert(secondRequests.front().at("cmd") == "steam_bind");
		assert(secondRequests.front().at("adminToken") == "second-token");
		assert(first.Requests().size() == 3);
		std::filesystem::remove_all(directory);
	}

	void TestBindWaitsForALateLink()
	{
		FakeLinkServer server{ true };
		const auto directory = FreshDirectory("co-host-link-client-late");
		const auto port = server.port;
		WriteLink(directory, std::to_string(port), "late-token");
		const auto binder = F4T::SteamHostLink::MakeIdentityBinder(directory, kGamePort, 0);
		assert(binder.IsComplete());

		std::filesystem::remove(F4T::SteamHostLink::PortPath(directory, kGamePort));
		std::filesystem::remove(F4T::SteamHostLink::TokenPath(directory, kGamePort));
		std::thread writer([directory, port] {
			std::this_thread::sleep_for(std::chrono::milliseconds{ 50 });
			WriteLink(directory, std::to_string(port), "late-token");
		});

		assert(binder.bind(40000, kGuest, false));
		writer.join();

		const auto requests = server.Requests();
		assert(requests.size() == 3);
		assert(requests[0].at("cmd") == "steam_clear");
		assert(requests[1].at("cmd") == "steam_policy");
		assert(requests[2].at("cmd") == "steam_bind");
		assert(requests[2].at("adminToken") == "late-token");
		std::filesystem::remove_all(directory);
	}

	void TestBindFailsWhenServerIsGone()
	{
		std::uint16_t deadPort = 0;
		{
			FakeLinkServer server{ true };
			deadPort = server.port;
		}
		const auto directory = FreshDirectory("co-host-link-client-dead");
		WriteLink(directory, std::to_string(deadPort), "link-token");
		const auto started = std::chrono::steady_clock::now();
		const auto binder = F4T::SteamHostLink::MakeIdentityBinder(directory, kGamePort, kHost);
		assert(binder.IsComplete() && !binder.hostPolicy);
		assert(!binder.bind(40000, kGuest, false));
		assert(std::chrono::steady_clock::now() - started < std::chrono::seconds{ 10 });
		std::filesystem::remove_all(directory);
	}
}

int main()
{
#ifdef _WIN32
	WSADATA data{};
	assert(WSAStartup(MAKEWORD(2, 2), &data) == 0);
#endif
	TestReadEndpoint();
	TestNoLinkGivesEmptyBinder();
	TestBinderSendsAuthenticatedRequests();
	TestSendCapturesReply();
	TestBindRereadsTheLink();
	TestBindWaitsForALateLink();
	TestBindFailsWithoutAcknowledgement();
	TestPolicyNeedsWellFormedReply();
	TestBindFailsWhenServerIsGone();
#ifdef _WIN32
	WSACleanup();
#endif
	return 0;
}

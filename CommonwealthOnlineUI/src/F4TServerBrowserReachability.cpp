#include "pch.h"

#include "F4TServerBrowserReachability.h"

#include "F4TServerLive.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

namespace F4T::ServerBrowser::Reachability
{
	namespace
	{
		constexpr std::size_t kMaximumConcurrentChecks = 8;
		constexpr auto kCheckBudget = std::chrono::seconds(20);
		constexpr const char* kAnswering = "answering";
		constexpr const char* kNoAnswer = "no_answer";
		constexpr const char* kUnchecked = "unchecked";

		bool EnsureWinsock()
		{
			static const bool initialized = [] {
				WSADATA data{};
				return WSAStartup(MAKEWORD(2, 2), std::addressof(data)) == 0;
			}();
			return initialized;
		}

		bool ResolveAddress(const std::string& a_host, std::string& a_outAddress, std::string& a_outError)
		{
			auto host = a_host;
			if (host.size() > 2 && host.front() == '[' && host.back() == ']') {
				host = host.substr(1, host.size() - 2);
			}
			if (host.empty()) {
				a_outError = "the listing has no address";
				return false;
			}
			if (!EnsureWinsock()) {
				a_outError = "Winsock could not be initialized";
				return false;
			}

			addrinfo hints{};
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;

			addrinfo* results = nullptr;
			const auto status = getaddrinfo(host.c_str(), nullptr, std::addressof(hints), std::addressof(results));
			if (status != 0 || results == nullptr) {
				a_outError = std::format("could not resolve '{}'", host);
				return false;
			}

			std::string resolved;
			for (auto* entry = results; entry != nullptr; entry = entry->ai_next) {
				if (entry->ai_addr == nullptr) {
					continue;
				}
				char text[INET6_ADDRSTRLEN]{};
				if (entry->ai_family == AF_INET) {
					const auto* address = reinterpret_cast<const sockaddr_in*>(entry->ai_addr);
					if (inet_ntop(AF_INET, std::addressof(address->sin_addr), text, sizeof(text)) != nullptr) {
						resolved = text;
						break;
					}
				} else if (entry->ai_family == AF_INET6) {
					const auto* address = reinterpret_cast<const sockaddr_in6*>(entry->ai_addr);
					if (inet_ntop(AF_INET6, std::addressof(address->sin6_addr), text, sizeof(text)) != nullptr) {
						resolved = text;
						break;
					}
				}
			}
			freeaddrinfo(results);

			if (resolved.empty()) {
				a_outError = std::format("could not resolve '{}'", host);
				return false;
			}
			a_outAddress = std::move(resolved);
			return true;
		}

		struct ProbeResult
		{
			std::string state;
			std::string reason;
			int         milliseconds{};
		};

		ProbeResult Probe(const ServerEntry& a_entry, bool& a_pluginCannotCheck)
		{
			ProbeResult result{ .state = kUnchecked };
			if (a_entry.host.empty() || a_entry.port <= 0) {
				result.reason = "the listing has no address";
				return result;
			}

			std::string address;
			if (!ResolveAddress(a_entry.host, address, result.reason)) {
				result.state = kNoAnswer;
				return result;
			}

			nlohmann::json reply;
			std::string error;
			switch (F4T::ServerLive::ApplySettings(
				nlohmann::json{
					{ "action", "reachability" },
					{ "host", address },
					{ "port", a_entry.port } },
				reply,
				error)) {
			case F4T::ServerLive::LiveApply::Applied:
				break;
			case F4T::ServerLive::LiveApply::Unavailable:
				a_pluginCannotCheck = true;
				result.reason = "the running plugin cannot check servers";
				return result;
			case F4T::ServerLive::LiveApply::Refused:
				result.reason = error.empty() ? "no check could run" : error;
				return result;
			}

			if (reply.value("answering", false)) {
				result.state = kAnswering;
				result.milliseconds = (std::max)(0, reply.value("milliseconds", 0));
				return result;
			}
			result.state = kNoAnswer;
			result.reason = reply.value("reason", std::string{ "the server did not accept a connection" });
			return result;
		}
	}

	void Check(std::vector<ServerEntry>& a_servers, std::stop_token a_stop)
	{
		if (a_servers.empty()) {
			return;
		}

		const auto started = std::chrono::steady_clock::now();
		std::atomic_bool pluginCannotCheck{ false };
		std::atomic_bool budgetExpired{ false };
		std::atomic_size_t next{ 0 };
		std::mutex applyMutex;

		const auto worker = [&] {
			for (;;) {
				const auto index = next.fetch_add(1);
				if (index >= a_servers.size() || a_stop.stop_requested() ||
					pluginCannotCheck.load() || budgetExpired.load()) {
					return;
				}
				if (std::chrono::steady_clock::now() - started > kCheckBudget) {
					budgetExpired.store(true);
					return;
				}

				bool cannotCheck = false;
				const auto result = Probe(a_servers[index], cannotCheck);
				if (cannotCheck) {
					pluginCannotCheck.store(true);
				}
				{
					const std::scoped_lock lock(applyMutex);
					a_servers[index].reachability = result.state;
					a_servers[index].reachabilityReason = result.reason;
					a_servers[index].reachabilityMs = result.milliseconds;
				}
			}
		};

		const auto threadCount = (std::min)(kMaximumConcurrentChecks, a_servers.size());
		{
			std::vector<std::jthread> workers;
			workers.reserve(threadCount);
			for (std::size_t index = 0; index < threadCount; ++index) {
				workers.emplace_back(worker);
			}
		}

		const std::string reason = pluginCannotCheck.load() ?
			"the running plugin cannot check servers" :
			(budgetExpired.load() ? "not checked in this refresh" : "the check did not run");
		for (auto& entry : a_servers) {
			if (entry.reachability.empty()) {
				entry.reachability = kUnchecked;
				entry.reachabilityReason = reason;
			}
		}
	}
}

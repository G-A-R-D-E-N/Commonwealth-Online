#include "pch.h"

#include "F4TServerBrowserReachability.h"

#include "F4TServerBrowserBridgeInternal.h"
#include "F4THttpGet.h"
#include "F4TUiServerLive.h"
#include "SessionCallbacks.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
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
		constexpr std::size_t kMaximumAddressesPerHost = 8;
		constexpr auto kCheckBudget = std::chrono::seconds(20);
		constexpr auto kResolveBudget = std::chrono::milliseconds(4000);
		constexpr auto kResolvePoll = std::chrono::milliseconds(100);
		constexpr std::string_view kPluginCannotCheck = "the running plugin cannot check servers";
		constexpr std::string_view kNotChecked = "not checked in this refresh";
		constexpr std::string_view kNoAddress = "the listing has no address";
		constexpr std::string_view kNoConnection = "the server did not accept a connection";

		std::mutex        g_cacheMutex;
		ReachabilityCache g_cache;

		double MonotonicSeconds()
		{
			return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}


		bool EnsureWinsock()
		{
			static const bool initialized = [] {
				WSADATA data{};
				return WSAStartup(MAKEWORD(2, 2), std::addressof(data)) == 0;
			}();
			return initialized;
		}

		std::wstring ToWide(std::string_view a_text)
		{
			if (a_text.empty()) {
				return {};
			}
			const auto length = MultiByteToWideChar(CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()), nullptr, 0);
			std::wstring wide(static_cast<std::size_t>((std::max)(length, 0)), L'\0');
			if (length > 0) {
				MultiByteToWideChar(CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()), wide.data(), length);
			}
			return wide;
		}

		bool IsNumericHost(const std::wstring& a_host)
		{
			IN_ADDR ipv4{};
			IN6_ADDR ipv6{};
			return InetPtonW(AF_INET, a_host.c_str(), std::addressof(ipv4)) == 1 ||
			       InetPtonW(AF_INET6, a_host.c_str(), std::addressof(ipv6)) == 1;
		}

		std::string AddressText(const ADDRINFOEXW& a_entry)
		{
			char text[INET6_ADDRSTRLEN]{};
			if (a_entry.ai_family == AF_INET && a_entry.ai_addr != nullptr) {
				const auto* address = reinterpret_cast<const sockaddr_in*>(a_entry.ai_addr);
				if (inet_ntop(AF_INET, std::addressof(address->sin_addr), text, sizeof(text)) != nullptr) {
					return text;
				}
			} else if (a_entry.ai_family == AF_INET6 && a_entry.ai_addr != nullptr) {
				const auto* address = reinterpret_cast<const sockaddr_in6*>(a_entry.ai_addr);
				if (inet_ntop(AF_INET6, std::addressof(address->sin6_addr), text, sizeof(text)) != nullptr) {
					return text;
				}
			}
			return {};
		}

		// constraint: an overlapped resolve must be cancelled and awaited before cleanup
		bool ResolveAddresses(
			const std::string& a_host,
			std::chrono::milliseconds a_budget,
			std::stop_token a_stop,
			std::vector<std::string>& a_outAddresses,
			std::string& a_outError,
			bool& a_outCancelled)
		{
			a_outCancelled = false;
			auto host = TrimAsciiWhitespace(a_host);
			if (host.size() > 2 && host.front() == '[' && host.back() == ']') {
				host = TrimAsciiWhitespace(std::string_view{ host }.substr(1, host.size() - 2));
			}
			if (host.empty()) {
				a_outError = std::string{ kNoAddress };
				return false;
			}
			if (!EnsureWinsock()) {
				a_outError = "Winsock could not be initialized";
				return false;
			}
			const auto wide = ToWide(host);
			if (wide.empty()) {
				a_outError = std::format("could not resolve '{}'", host);
				return false;
			}
			if (IsNumericHost(wide)) {
				a_outAddresses.push_back(host);
				return true;
			}

			ADDRINFOEXW hints{};
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;

			const auto completeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (completeEvent == nullptr) {
				a_outError = std::format("could not resolve '{}'", host);
				return false;
			}
			struct Context
			{
				HANDLE       event{};
				OVERLAPPED   overlapped{};
				ADDRINFOEXW* results{};
			};
			auto context = std::make_unique<Context>();
			context->event = completeEvent;
			context->overlapped.hEvent = completeEvent;
			HANDLE cancelHandle = nullptr;
			const auto started = std::chrono::steady_clock::now();
			const auto status = GetAddrInfoExW(
				wide.c_str(),
				nullptr,
				NS_DNS,
				nullptr,
				std::addressof(hints),
				std::addressof(context->results),
				nullptr,
				std::addressof(context->overlapped),
				nullptr,
				std::addressof(cancelHandle));

			const auto releaseContext = [&context] {
				if (context->results != nullptr) {
					FreeAddrInfoExW(context->results);
					context->results = nullptr;
				}
				if (context->event != nullptr) {
					CloseHandle(context->event);
					context->event = nullptr;
				}
				context.reset();
			};

			bool cancelled = false;
			bool waitFailed = false;
			bool signalled = false;
			if (status == WSA_IO_PENDING) {
				const auto deadline = started + a_budget;
				for (;;) {
					const auto now = std::chrono::steady_clock::now();
					if (a_stop.stop_requested() || now >= deadline) {
						cancelled = true;
						break;
					}
					const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
					const auto wait = WaitForSingleObject(completeEvent, static_cast<DWORD>((std::min)(remaining, kResolvePoll).count()));
					if (wait == WAIT_OBJECT_0) {
						signalled = true;
						break;
					}
					if (wait == WAIT_FAILED) {
						cancelled = true;
						waitFailed = true;
						break;
					}
				}
				if (cancelled) {
					GetAddrInfoExCancel(std::addressof(cancelHandle));
				}
				if (!signalled && !waitFailed) {
					WaitForSingleObject(completeEvent, INFINITE);
				}
				if (waitFailed) {
					const auto abandonDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
					bool confirmedByPoll = false;
					while (std::chrono::steady_clock::now() < abandonDeadline) {
						if (GetAddrInfoExOverlappedResult(std::addressof(context->overlapped)) != WSAEINPROGRESS) {
							confirmedByPoll = true;
							break;
						}
						std::this_thread::sleep_for(kResolvePoll);
					}
					if (!confirmedByPoll) {
						// constraint: an unconfirmed overlapped resolve must outlive its stack frame
						static_cast<void>(context.release());
						a_outError = std::format("'{}' did not resolve within {} s", host, a_budget.count() / 1000);
						return false;
					}
				}
				const auto result = GetAddrInfoExOverlappedResult(std::addressof(context->overlapped));
				if (result != ERROR_SUCCESS || context->results == nullptr) {
					releaseContext();
					if (cancelled) {
						a_outCancelled = a_stop.stop_requested();
						a_outError = a_outCancelled ? std::string{ kNotChecked } :
							std::format("'{}' did not resolve within {} s", host, a_budget.count() / 1000);
					} else {
						a_outError = std::format("could not resolve '{}'", host);
					}
					return false;
				}
			} else if (status != NO_ERROR) {
				releaseContext();
				a_outError = std::format("could not resolve '{}'", host);
				return false;
			}

			for (auto* entry = context->results; entry != nullptr; entry = entry->ai_next) {
				auto text = AddressText(*entry);
				if (text.empty() || std::ranges::find(a_outAddresses, text) != a_outAddresses.end()) {
					continue;
				}
				a_outAddresses.push_back(std::move(text));
				if (a_outAddresses.size() >= kMaximumAddressesPerHost) {
					break;
				}
			}
			releaseContext();
			if (a_outAddresses.empty()) {
				a_outError = std::format("could not resolve '{}'", host);
				return false;
			}
			return true;
		}

		struct AddressProbe
		{
			bool               answering{};
			bool               pluginCannotCheck{};
			bool               refused{};
			int                milliseconds{};
			std::string        reason;
		};

		AddressProbe ProbeAddress(const std::string& a_address, int a_port)
		{
			AddressProbe probe;
			nlohmann::json reply;
			std::string error;
			switch (F4T::UiServerLive::ApplySettings(
				nlohmann::json{
					{ "action", "reachability" },
					{ "host", a_address },
					{ "port", a_port } },
				reply,
				error)) {
			case F4T::UiServerLive::LiveApply::Applied:
				break;
			case F4T::UiServerLive::LiveApply::Unavailable:
				probe.pluginCannotCheck = true;
				probe.reason = std::string{ kPluginCannotCheck };
				return probe;
			case F4T::UiServerLive::LiveApply::Refused:
				probe.refused = true;
				probe.reason = error.empty() ? "no check could run" : error;
				return probe;
			}
			probe.answering = reply.value("answering", false);
			if (probe.answering) {
				probe.milliseconds = (std::max)(0, reply.value("milliseconds", 0));
				return probe;
			}
			probe.reason = reply.value("reason", std::string{ kNoConnection });
			return probe;
		}

		ReachabilityResult Probe(
			const CheckTarget& a_target,
			std::stop_token a_stop,
			std::atomic_bool& a_pluginCannotCheck,
			std::chrono::steady_clock::time_point a_deadline)
		{
			ReachabilityResult result{ std::string{ ReachabilityState::kUnchecked }, {}, 0 };
			std::vector<std::string> addresses;
			std::string resolveError;
			bool cancelled = false;
			if (!ResolveAddresses(a_target.host, kResolveBudget, a_stop, addresses, resolveError, cancelled)) {
				result.reason = resolveError;
				if (!cancelled) {
					result.state = std::string{ ReachabilityState::kNoAnswer };
				}
				return result;
			}

			std::string lastReason;
			bool sawRealAnswer = false;
			for (const auto& address : addresses) {
				if (a_stop.stop_requested() || std::chrono::steady_clock::now() >= a_deadline) {
					result.reason = std::string{ kNotChecked };
					return result;
				}
				const auto probe = ProbeAddress(address, a_target.port);
				if (probe.pluginCannotCheck) {
					a_pluginCannotCheck.store(true);
					result.reason = std::string{ kPluginCannotCheck };
					return result;
				}
				if (probe.answering) {
					result.state = std::string{ ReachabilityState::kAnswering };
					result.reason.clear();
					result.milliseconds = probe.milliseconds;
					return result;
				}
				if (!probe.refused) {
					sawRealAnswer = true;
				}
				if (!probe.reason.empty()) {
					lastReason = probe.reason;
				}
			}
			if (!sawRealAnswer) {
				result.reason = lastReason.empty() ? std::string{ kNoConnection } : lastReason;
				return result;
			}
			result.state = std::string{ ReachabilityState::kNoAnswer };
			result.reason = lastReason.empty() ? std::string{ kNoConnection } : lastReason;
			return result;
		}
	}

	PreparedList Prepare(std::vector<ServerEntry>& a_servers)
	{
		PreparedList prepared;
		std::unordered_set<std::string> planned;
		const auto now = MonotonicSeconds();
		NormalizeListedEntries(a_servers);
		const std::scoped_lock lock(g_cacheMutex);
		for (std::size_t index = 0; index < a_servers.size(); ++index) {
			auto& entry = a_servers[index];
			if (entry.host.empty()) {
				entry.reachability = std::string{ ReachabilityState::kUnchecked };
				entry.reachabilityReason = std::string{ kNoAddress };
				entry.reachabilityMs = 0;
				continue;
			}
			const auto epoch = g_cache.Epoch(entry.host, entry.port);
			prepared.epochs.emplace_back(index, epoch);
			if (const auto cached = g_cache.Lookup(entry.host, entry.port, now)) {
				entry.reachability = cached->state;
				entry.reachabilityReason = cached->reason;
				entry.reachabilityMs = cached->milliseconds;
				continue;
			}
			entry.reachability = std::string{ ReachabilityState::kChecking };
			entry.reachabilityReason.clear();
			entry.reachabilityMs = 0;
			if (planned.insert(AddressKey(entry.host, entry.port)).second) {
				prepared.targets.push_back(CheckTarget{ entry.host, entry.port });
			}
		}
		return prepared;
	}

	void Run(const std::vector<CheckTarget>& a_targets, std::stop_token a_stop, const UpdateSink& a_sink)
	{
		if (a_targets.empty()) {
			return;
		}

		const auto started = std::chrono::steady_clock::now();
		std::atomic_bool pluginCannotCheck{ false };
		std::atomic_bool budgetExpired{ false };
		std::atomic_size_t next{ 0 };
		std::mutex sinkMutex;
		std::vector<std::atomic_bool> reported(a_targets.size());
		std::vector<std::uint64_t> epochs(a_targets.size());
		{
			const std::scoped_lock cacheLock(g_cacheMutex);
			for (std::size_t index = 0; index < a_targets.size(); ++index) {
				epochs[index] = g_cache.Epoch(a_targets[index].host, a_targets[index].port);
			}
		}

		const auto worker = [&] {
			for (;;) {
				const auto index = next.fetch_add(1);
				if (index >= a_targets.size() || a_stop.stop_requested() ||
					pluginCannotCheck.load() || budgetExpired.load()) {
					return;
				}
				if (std::chrono::steady_clock::now() - started > kCheckBudget) {
					budgetExpired.store(true);
					return;
				}
				ReachabilityResult result{ std::string{ ReachabilityState::kUnchecked }, std::string{ kNotChecked }, 0 };
				try {
					result = Probe(a_targets[index], a_stop, pluginCannotCheck, started + kCheckBudget);
				} catch (...) {
					detail::LogWarning(" Server reachability check failed unexpectedly; the server stays unchecked.");
				}
				reported[index].store(true);
				bool superseded = false;
				{
					const std::scoped_lock cacheLock(g_cacheMutex);
					superseded = g_cache.Epoch(a_targets[index].host, a_targets[index].port) != epochs[index];
					if (!superseded &&
						(result.state == ReachabilityState::kAnswering || result.state == ReachabilityState::kNoAnswer)) {
						g_cache.Record(a_targets[index].host, a_targets[index].port, result, MonotonicSeconds());
					}
				}
				if (superseded) {
					continue;
				}
				std::vector<ReachabilityUpdate> updates;
				updates.push_back(ReachabilityUpdate{
					a_targets[index].host,
					a_targets[index].port,
					epochs[index],
					std::move(result) });
				const std::scoped_lock sinkLock(sinkMutex);
				a_sink(std::move(updates));
			}
		};

		const auto threadCount = (std::min)(kMaximumConcurrentChecks, a_targets.size());
		{
			std::vector<std::jthread> workers;
			workers.reserve(threadCount);
			for (std::size_t index = 0; index < threadCount; ++index) {
				try {
					workers.emplace_back(worker);
				} catch (...) {
					break;
				}
			}
		}

		const std::string reason = pluginCannotCheck.load() ? std::string{ kPluginCannotCheck } :
			(a_stop.stop_requested() || budgetExpired.load()) ? std::string{ kNotChecked } :
			"the check did not run";
		std::vector<ReachabilityUpdate> remaining;
		for (std::size_t index = 0; index < a_targets.size(); ++index) {
			if (!reported[index].load()) {
				remaining.push_back(ReachabilityUpdate{
					a_targets[index].host,
					a_targets[index].port,
					epochs[index],
					ReachabilityResult{ std::string{ ReachabilityState::kUnchecked }, reason, 0 } });
			}
		}
		if (!remaining.empty()) {
			const std::scoped_lock sinkLock(sinkMutex);
			a_sink(std::move(remaining));
		}
	}

	bool IsCurrentEpoch(const std::string& a_host, int a_port, std::uint64_t a_epoch)
	{
		const std::scoped_lock lock(g_cacheMutex);
		return g_cache.IsCurrent(a_host, a_port, a_epoch);
	}

	void NoteAnswer(const std::string& a_host, int a_port, int a_milliseconds)
	{
		const std::scoped_lock lock(g_cacheMutex);
		g_cache.NoteAnswering(
			a_host,
			a_port,
			ReachabilityResult{ std::string{ ReachabilityState::kAnswering }, {}, (std::max)(0, a_milliseconds) },
			MonotonicSeconds());
	}

	std::optional<ReachabilityResult> CurrentResult(const std::string& a_host, int a_port)
	{
		const std::scoped_lock lock(g_cacheMutex);
		return g_cache.Lookup(a_host, a_port, MonotonicSeconds());
	}

	void ClearCached(const std::string& a_host, int a_port)
	{
		const std::scoped_lock lock(g_cacheMutex);
		g_cache.Clear(a_host, a_port);
	}
}

namespace F4T::ServerBrowser::detail
{
	void RefreshDirectory(
		const F4SE::TaskInterface* a_task,
		PRISMA_UI_API::IVPrismaUI11* a_api,
		const DispatchTicket& a_ticket,
		std::stop_token a_stop,
		const std::optional<ServerEntry>& a_currentServer)
	{
		std::string error;
		std::vector<ServerEntry> servers;
		bool success = false;

		const auto query = ServerBrowserData::QueryDirectoryUrls(
			kServerDirectoryUrls,
			[&](const char* url, bool& cancelled) {
				ServerBrowserData::DirectoryAttempt attempt;
				if (a_stop.stop_requested()) {
					cancelled = true;
					return attempt;
				}
				attempt.transportOk = F4T::Http::Get(url, attempt.body, attempt.transportError, 10000);
				cancelled = a_stop.stop_requested();
				return attempt;
			});
		if (query.cancelled) {
			FinishDirectoryDispatch(a_ticket);
			return;
		}
		servers = query.servers;
		error = query.error;
		success = query.success;

		if (success && a_currentServer && COUI::Session::IsConnected()) {
			for (auto& entry : servers) {
				if (a_currentServer->host == entry.host && a_currentServer->port == entry.port) {
					entry.ping = a_currentServer->ping;
					if (entry.status.empty()) {
						entry.status = "Open";
					}
				}
			}
		}

		auto prepared = success ? Reachability::Prepare(servers) : Reachability::PreparedList{};
		if (!DispatchDirectoryCompletion(
				a_task, a_api, a_ticket, std::move(servers), std::move(prepared.epochs), std::move(error), success)) {
			FinishDirectoryDispatch(a_ticket);
			LogWarning(" Dropped directory refresh result because no game-thread dispatcher is available.");
			return;
		}

		if (!prepared.targets.empty()) {
			Reachability::Run(prepared.targets, a_stop, [a_task, a_api, a_ticket](std::vector<ReachabilityUpdate> a_updates) {
				(void)DispatchReachabilityCompletion(a_task, a_api, a_ticket, std::move(a_updates));
			});
		}
		FinishDirectoryDispatch(a_ticket);
	}
}

#include "pch.h"

#include "F4TServerBrowserBridgeInternal.h"
#include "F4TServerBrowserReachability.h"
#include "F4SE/Interfaces.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace F4T::ServerBrowser::detail
{
	std::atomic_bool g_localScanRunning{ false };
	std::atomic_bool g_directoryRefreshRunning{ false };
	namespace
	{
		struct DirectoryRefreshCompletion
		{
			std::vector<ServerEntry> servers;
			std::vector<std::pair<std::size_t, std::uint64_t>> epochs;
			std::string error;
			bool success;
		};

		struct LocalScanCompletion
		{
			std::vector<ServerEntry> servers;
			std::string error;
			bool success;
		};

		struct ReachabilityCompletion
		{
			std::vector<ReachabilityUpdate> updates;
		};

		struct DispatchCompletion
		{
			DispatchTicket ticket;
			std::variant<DirectoryRefreshCompletion, LocalScanCompletion, ReachabilityCompletion> value;
		};

		std::recursive_mutex g_dispatchMutex;
		std::mutex g_workerMutex;
		std::jthread g_directoryWorker;
		std::jthread g_localWorker;
		std::shared_ptr<std::atomic_bool> g_directoryFinished;
		std::shared_ptr<std::atomic_bool> g_localFinished;

		struct RetiredWorker
		{
			std::jthread                      thread;
			std::shared_ptr<std::atomic_bool> finished;
		};

		std::vector<RetiredWorker>  g_retiredWorkers;
		std::vector<RetiredWorker>* g_abandonedWorkers = nullptr;
		std::unordered_map<std::uintptr_t, std::unique_ptr<DispatchCompletion>> g_dispatches;
		std::atomic_uintptr_t g_nextDispatchToken{ 1 };
		std::uint64_t g_dispatchGeneration{ 1 };
		std::uint64_t g_directoryGeneration{ 1 };
		bool g_dispatchStopped = false;

		bool WorkerFinished(const std::shared_ptr<std::atomic_bool>& a_finished)
		{
			return !a_finished || a_finished->load(std::memory_order_acquire);
		}

		// constraint: a worker is joined only after it reports finished
		void ReapWorkers(std::vector<RetiredWorker>& a_workers)
		{
			for (auto entry = a_workers.begin(); entry != a_workers.end();) {
				if (!WorkerFinished(entry->finished)) {
					++entry;
					continue;
				}
				if (entry->thread.joinable()) {
					entry->thread.join();
				}
				entry = a_workers.erase(entry);
			}
		}

		void ReapAllWorkers()
		{
			ReapWorkers(g_retiredWorkers);
			if (g_abandonedWorkers != nullptr) {
				ReapWorkers(*g_abandonedWorkers);
			}
		}

		void RetireWorker(std::jthread a_worker, std::shared_ptr<std::atomic_bool> a_finished)
		{
			if (!a_worker.joinable()) {
				return;
			}
			a_worker.request_stop();
			if (WorkerFinished(a_finished)) {
				a_worker.join();
				return;
			}
			g_retiredWorkers.push_back(RetiredWorker{ std::move(a_worker), std::move(a_finished) });
		}

		// constraint: an unfinished worker at shutdown is abandoned, never detached or awaited
		void AbandonWorkers(std::vector<RetiredWorker> a_workers)
		{
			if (a_workers.empty()) {
				return;
			}
			if (g_abandonedWorkers == nullptr) {
				g_abandonedWorkers = new std::vector<RetiredWorker>();
			}
			for (auto& worker : a_workers) {
				if (worker.thread.joinable()) {
					worker.thread.request_stop();
				}
				g_abandonedWorkers->push_back(std::move(worker));
			}
		}

		template <class T>
		std::uintptr_t RegisterCompletion(DispatchTicket a_ticket, T a_completion)
		{
			auto owned = std::make_unique<DispatchCompletion>(DispatchCompletion{ a_ticket, std::move(a_completion) });
			if (g_dispatchStopped) {
				return 0;
			}
			const auto token = g_nextDispatchToken.fetch_add(1);
			g_dispatches.emplace(token, std::move(owned));
			return token;
		}

		std::optional<DispatchCompletion> TakeCompletion(std::uintptr_t a_token)
		{
			const auto it = g_dispatches.find(a_token);
			if (it == g_dispatches.end()) {
				return std::nullopt;
			}
			auto completion = std::move(*it->second);
			g_dispatches.erase(it);
			return completion;
		}

		void ApplyReachabilityUpdates(const std::vector<ReachabilityUpdate>& a_updates)
		{
			auto payload = nlohmann::json::array();
			for (const auto& update : a_updates) {
				if (!Reachability::IsCurrentEpoch(update.host, update.port, update.epoch)) {
					continue;
				}
				g_data.SetReachability(update.host, update.port, update.result);
				auto item = nlohmann::json{
					{ "host", update.host },
					{ "port", update.port },
					{ "state", update.result.state }
				};
				if (!update.result.reason.empty()) {
					item["reason"] = update.result.reason;
				}
				if (update.result.milliseconds > 0) {
					item["milliseconds"] = update.result.milliseconds;
				}
				payload.push_back(std::move(item));
			}
			if (payload.empty()) {
				return;
			}
			DispatchEvent(nlohmann::json{
				{ "type", "serverReachabilityUpdated" },
				{ "updates", std::move(payload) }
			});
		}

		void CompleteDirectoryRefreshOnGameThread(void* a_userdata)
		{
			const auto token = reinterpret_cast<std::uintptr_t>(a_userdata);
			std::scoped_lock lock(g_dispatchMutex);
			auto completion = TakeCompletion(token);
			if (!completion || !IsDirectoryGenerationActive(completion->ticket)) {
				return;
			}
			auto& result = std::get<DirectoryRefreshCompletion>(completion->value);
			g_data.SetRefreshing(true);
			DispatchEvent(nlohmann::json{ { "type", "serverListRefreshStarted" } });
			CompleteDirectoryRefresh(std::move(result.servers), std::move(result.epochs), std::move(result.error), result.success);
		}

		void CompleteReachabilityOnGameThread(void* a_userdata)
		{
			const auto token = reinterpret_cast<std::uintptr_t>(a_userdata);
			std::scoped_lock lock(g_dispatchMutex);
			auto completion = TakeCompletion(token);
			if (!completion || !IsDirectoryGenerationActive(completion->ticket)) {
				return;
			}
			auto& result = std::get<ReachabilityCompletion>(completion->value);
			ApplyReachabilityUpdates(result.updates);
		}

		void CompleteLocalScanOnGameThread(void* a_userdata)
		{
			const auto token = reinterpret_cast<std::uintptr_t>(a_userdata);
			std::scoped_lock lock(g_dispatchMutex);
			auto completion = TakeCompletion(token);
			if (!completion || !IsDispatchGenerationActive(completion->ticket.generation)) {
				return;
			}
			auto& result = std::get<LocalScanCompletion>(completion->value);
			DispatchEvent(nlohmann::json{ { "type", "localScanStarted" } });
			CompleteLocalScan(std::move(result.servers), std::move(result.error), result.success);
		}
	}

	std::optional<std::uint64_t> BeginDispatch(std::atomic_bool& a_running)
	{
		std::scoped_lock lock(g_dispatchMutex);
		if (g_dispatchStopped || a_running.exchange(true)) {
			return std::nullopt;
		}
		return g_dispatchGeneration;
	}

	void FinishDispatch(std::atomic_bool& a_running, std::uint64_t a_generation)
	{
		std::scoped_lock lock(g_dispatchMutex);
		if (!g_dispatchStopped && a_generation == g_dispatchGeneration) {
			a_running.store(false);
		}
	}

	bool IsDispatchGenerationActive(std::uint64_t a_generation)
	{
		std::scoped_lock lock(g_dispatchMutex);
		return !g_dispatchStopped && a_generation == g_dispatchGeneration;
	}

	std::optional<DispatchTicket> BeginDirectoryDispatch()
	{
		std::scoped_lock lock(g_dispatchMutex);
		if (g_dispatchStopped) {
			return std::nullopt;
		}
		++g_directoryGeneration;
		g_directoryRefreshRunning.store(true);
		return DispatchTicket{ g_dispatchGeneration, g_directoryGeneration };
	}

	void FinishDirectoryDispatch(const DispatchTicket& a_ticket)
	{
		std::scoped_lock lock(g_dispatchMutex);
		if (!g_dispatchStopped && a_ticket.directoryGeneration == g_directoryGeneration) {
			g_directoryRefreshRunning.store(false);
		}
	}

	bool IsDirectoryGenerationActive(const DispatchTicket& a_ticket)
	{
		std::scoped_lock lock(g_dispatchMutex);
		return !g_dispatchStopped &&
		       a_ticket.generation == g_dispatchGeneration &&
		       a_ticket.directoryGeneration == g_directoryGeneration;
	}

	bool DispatchDirectoryCompletion(
		const F4SE::TaskInterface* a_task,
		PRISMA_UI_API::IVPrismaUI11* a_api,
		const DispatchTicket& a_ticket,
		std::vector<ServerEntry> a_servers,
		std::vector<std::pair<std::size_t, std::uint64_t>> a_epochs,
		std::string a_error,
		bool a_success)
	{
		std::scoped_lock lock(g_dispatchMutex);
		if (!IsDirectoryGenerationActive(a_ticket)) {
			return false;
		}
		if (a_task) {
			a_task->AddTask([ticket = a_ticket, servers = std::move(a_servers), epochs = std::move(a_epochs), error = std::move(a_error), a_success]() mutable {
				std::scoped_lock lock(g_dispatchMutex);
				if (IsDirectoryGenerationActive(ticket)) {
					CompleteDirectoryRefresh(std::move(servers), std::move(epochs), std::move(error), a_success);
				}
			});
			return true;
		}
		if (!a_api) {
			return false;
		}
		const auto token = RegisterCompletion(
			a_ticket,
			DirectoryRefreshCompletion{ std::move(a_servers), std::move(a_epochs), std::move(a_error), a_success });
		if (token != 0 && a_api->DispatchToGameThread(
				&CompleteDirectoryRefreshOnGameThread,
				reinterpret_cast<void*>(token))) {
			return true;
		}
		g_dispatches.erase(token);
		return false;
	}

	bool DispatchReachabilityCompletion(
		const F4SE::TaskInterface* a_task,
		PRISMA_UI_API::IVPrismaUI11* a_api,
		const DispatchTicket& a_ticket,
		std::vector<ReachabilityUpdate> a_updates)
	{
		std::scoped_lock lock(g_dispatchMutex);
		if (!IsDirectoryGenerationActive(a_ticket)) {
			return false;
		}
		if (a_task) {
			a_task->AddTask([ticket = a_ticket, updates = std::move(a_updates)]() mutable {
				std::scoped_lock lock(g_dispatchMutex);
				if (IsDirectoryGenerationActive(ticket)) {
					ApplyReachabilityUpdates(updates);
				}
			});
			return true;
		}
		if (!a_api) {
			return false;
		}
		const auto token = RegisterCompletion(
			a_ticket,
			ReachabilityCompletion{ std::move(a_updates) });
		if (token != 0 && a_api->DispatchToGameThread(
				&CompleteReachabilityOnGameThread,
				reinterpret_cast<void*>(token))) {
			return true;
		}
		g_dispatches.erase(token);
		return false;
	}

	bool DispatchLocalCompletion(
		const F4SE::TaskInterface* a_task,
		PRISMA_UI_API::IVPrismaUI11* a_api,
		std::uint64_t a_generation,
		std::vector<ServerEntry> a_servers,
		std::string a_error,
		bool a_success)
	{
		std::scoped_lock lock(g_dispatchMutex);
		if (!IsDispatchGenerationActive(a_generation)) {
			return false;
		}
		if (a_task) {
			a_task->AddTask([generation = a_generation, servers = std::move(a_servers), error = std::move(a_error), a_success]() mutable {
				std::scoped_lock lock(g_dispatchMutex);
				if (IsDispatchGenerationActive(generation)) {
					CompleteLocalScan(std::move(servers), std::move(error), a_success);
				}
			});
			return true;
		}
		if (!a_api) {
			return false;
		}
		const auto token = RegisterCompletion(
			DispatchTicket{ a_generation, 0 },
			LocalScanCompletion{ std::move(a_servers), std::move(a_error), a_success });
		if (token != 0 && a_api->DispatchToGameThread(
				&CompleteLocalScanOnGameThread,
				reinterpret_cast<void*>(token))) {
			return true;
		}
		g_dispatches.erase(token);
		return false;
	}

	void Activate()
	{
		std::scoped_lock lock(g_dispatchMutex);
		g_dispatchStopped = false;
	}

	void CancelDirectoryWorker()
	{
		std::scoped_lock lock(g_workerMutex);
		if (g_directoryWorker.joinable()) {
			g_directoryWorker.request_stop();
		}
	}

	void OwnDirectoryWorker(std::function<void(std::stop_token)> a_body)
	{
		auto finished = std::make_shared<std::atomic_bool>(false);
		std::jthread worker([body = std::move(a_body), finished](std::stop_token a_stopToken) {
			try {
				body(a_stopToken);
			} catch (...) {
				LogWarning(" Server browser worker ended after an unexpected error.");
			}
			finished->store(true, std::memory_order_release);
		});
		std::scoped_lock lock(g_workerMutex);
		ReapAllWorkers();
		auto previous = std::move(g_directoryWorker);
		auto previousFinished = std::move(g_directoryFinished);
		g_directoryWorker = std::move(worker);
		g_directoryFinished = std::move(finished);
		RetireWorker(std::move(previous), std::move(previousFinished));
	}

	void OwnLocalWorker(std::function<void(std::stop_token)> a_body)
	{
		auto finished = std::make_shared<std::atomic_bool>(false);
		std::jthread worker([body = std::move(a_body), finished](std::stop_token a_stopToken) {
			try {
				body(a_stopToken);
			} catch (...) {
				LogWarning(" Server browser worker ended after an unexpected error.");
			}
			finished->store(true, std::memory_order_release);
		});
		std::scoped_lock lock(g_workerMutex);
		ReapAllWorkers();
		auto previous = std::move(g_localWorker);
		auto previousFinished = std::move(g_localFinished);
		g_localWorker = std::move(worker);
		g_localFinished = std::move(finished);
		RetireWorker(std::move(previous), std::move(previousFinished));
	}

	void Shutdown()
	{
		{
			std::scoped_lock lock(g_dispatchMutex);
			g_dispatchStopped = true;
			++g_dispatchGeneration;
			++g_directoryGeneration;
			g_dispatches.clear();
			g_directoryRefreshRunning.store(false);
			g_localScanRunning.store(false);
			g_data.SetRefreshing(false);
			g_data.ClearCheckingReachability("not checked in this refresh");
		}

		std::vector<RetiredWorker> workers;
		{
			std::scoped_lock lock(g_workerMutex);
			ReapAllWorkers();
			const auto take = [&workers](std::jthread& a_thread, std::shared_ptr<std::atomic_bool>& a_finished) {
				if (!a_thread.joinable()) {
					return;
				}
				a_thread.request_stop();
				workers.push_back(RetiredWorker{ std::move(a_thread), std::move(a_finished) });
			};
			take(g_directoryWorker, g_directoryFinished);
			take(g_localWorker, g_localFinished);
			for (auto& retired : g_retiredWorkers) {
				if (retired.thread.joinable()) {
					retired.thread.request_stop();
				}
				workers.push_back(std::move(retired));
			}
			g_retiredWorkers.clear();
		}

		std::vector<RetiredWorker> abandoned;
		for (auto& worker : workers) {
			if (!WorkerFinished(worker.finished)) {
				abandoned.push_back(std::move(worker));
			} else if (worker.thread.joinable()) {
				worker.thread.join();
			}
		}
		std::scoped_lock lock(g_workerMutex);
		AbandonWorkers(std::move(abandoned));
	}
}

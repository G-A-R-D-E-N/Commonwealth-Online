#include "pch.h"

#include "F4TServerBrowserBridgeInternal.h"
#include "F4SE/Interfaces.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <variant>

namespace F4T::ServerBrowser::detail
{
	std::atomic_bool g_localScanRunning{ false };
	std::atomic_bool g_directoryRefreshRunning{ false };
	namespace
	{
		struct DirectoryRefreshCompletion
		{
			std::vector<ServerEntry> servers;
			std::string error;
			bool success;
		};

		struct LocalScanCompletion
		{
			std::vector<ServerEntry> servers;
			std::string error;
			bool success;
		};

		struct DispatchCompletion
		{
			std::uint64_t generation;
			std::variant<DirectoryRefreshCompletion, LocalScanCompletion> value;
		};

		std::recursive_mutex g_dispatchMutex;
		std::mutex g_workerMutex;
		std::jthread g_directoryWorker;
		std::jthread g_localWorker;
		std::unordered_map<std::uintptr_t, std::unique_ptr<DispatchCompletion>> g_dispatches;
		std::atomic_uintptr_t g_nextDispatchToken{ 1 };
		std::uint64_t g_dispatchGeneration{ 1 };
		bool g_dispatchStopped = false;

		template <class T>
		std::uintptr_t RegisterCompletion(std::uint64_t a_generation, T a_completion)
		{
			auto owned = std::make_unique<DispatchCompletion>(DispatchCompletion{ a_generation, std::move(a_completion) });
			if (g_dispatchStopped) {
				return 0;
			}
			const auto token = g_nextDispatchToken.fetch_add(1);
			g_dispatches.emplace(token, std::move(owned));
			return token;
		}

		void CompleteDirectoryRefreshOnGameThread(void* a_userdata)
		{
			const auto token = reinterpret_cast<std::uintptr_t>(a_userdata);
			std::scoped_lock lock(g_dispatchMutex);
			const auto it = g_dispatches.find(token);
			if (it == g_dispatches.end()) {
				return;
			}
			auto completion = std::move(*it->second);
			g_dispatches.erase(it);
			if (!IsDispatchGenerationActive(completion.generation)) {
				return;
			}
			auto& result = std::get<DirectoryRefreshCompletion>(completion.value);
			g_data.SetRefreshing(true);
			DispatchEvent(nlohmann::json{ { "type", "serverListRefreshStarted" } });
			CompleteDirectoryRefresh(std::move(result.servers), std::move(result.error), result.success);
		}

		void CompleteLocalScanOnGameThread(void* a_userdata)
		{
			const auto token = reinterpret_cast<std::uintptr_t>(a_userdata);
			std::scoped_lock lock(g_dispatchMutex);
			const auto it = g_dispatches.find(token);
			if (it == g_dispatches.end()) {
				return;
			}
			auto completion = std::move(*it->second);
			g_dispatches.erase(it);
			if (!IsDispatchGenerationActive(completion.generation)) {
				return;
			}
			auto& result = std::get<LocalScanCompletion>(completion.value);
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

	bool DispatchDirectoryCompletion(
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
					CompleteDirectoryRefresh(std::move(servers), std::move(error), a_success);
				}
			});
			return true;
		}
		if (!a_api) {
			return false;
		}
		const auto token = RegisterCompletion(
			a_generation,
			DirectoryRefreshCompletion{ std::move(a_servers), std::move(a_error), a_success });
		if (token != 0 && a_api->DispatchToGameThread(
				&CompleteDirectoryRefreshOnGameThread,
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
			a_generation,
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

	void OwnDirectoryWorker(std::jthread a_worker)
	{
		std::scoped_lock lock(g_workerMutex);
		g_directoryWorker = std::move(a_worker);
	}

	void OwnLocalWorker(std::jthread a_worker)
	{
		std::scoped_lock lock(g_workerMutex);
		g_localWorker = std::move(a_worker);
	}

	void Shutdown()
	{
		{
			std::scoped_lock lock(g_dispatchMutex);
			g_dispatchStopped = true;
			++g_dispatchGeneration;
			g_dispatches.clear();
			g_directoryRefreshRunning.store(false);
			g_localScanRunning.store(false);
			g_data.SetRefreshing(false);
		}

		std::jthread directoryWorker;
		std::jthread localWorker;
		{
			std::scoped_lock lock(g_workerMutex);
			directoryWorker = std::move(g_directoryWorker);
			localWorker = std::move(g_localWorker);
		}
		directoryWorker.request_stop();
		localWorker.request_stop();
	}
}

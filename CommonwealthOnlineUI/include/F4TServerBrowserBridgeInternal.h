#pragma once

#include "F4TServerBrowserData.h"
#include "PrismaUI_F4_API.h"

#include <nlohmann/json_fwd.hpp>

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace F4SE { class TaskInterface; }

namespace F4T::ServerBrowser::detail
{
	extern PRISMA_UI_API::IVPrismaUI4* g_api;
	extern PrismaView                    g_view;
	extern ServerBrowserData             g_data;
	extern std::atomic_bool              g_localScanRunning;
	extern std::atomic_bool              g_directoryRefreshRunning;

	bool CanDispatchToBrowser();
	void DispatchEvent(const nlohmann::json& a_event);
	void DispatchEventToView(const nlohmann::json& a_event);
	void PushRecentServersUpdated();
	void PushRetryAvailable();
	void PushServerListUpdated();
	void PushLocalServersUpdated();
	void PushUIThemeInternal();
	void PushConnectionStatusEvent();
	void PushSessionStateInternal(const char* a_stateJson);
	void PushCurrentServerInfoInternal();

	void RefreshServerList();
	void ScanLocalServers();
	void CompleteDirectoryRefresh(std::vector<ServerEntry> a_servers, std::string a_error, bool a_success);
	void CompleteLocalScan(std::vector<ServerEntry> a_servers, std::string a_error, bool a_success);
	std::optional<std::uint64_t> BeginDispatch(std::atomic_bool& a_running);
	void FinishDispatch(std::atomic_bool& a_running, std::uint64_t a_generation);
	bool IsDispatchGenerationActive(std::uint64_t a_generation);
	bool DispatchDirectoryCompletion(const F4SE::TaskInterface* a_task, PRISMA_UI_API::IVPrismaUI11* a_api, std::uint64_t a_generation, std::vector<ServerEntry> a_servers, std::string a_error, bool a_success);
	bool DispatchLocalCompletion(const F4SE::TaskInterface* a_task, PRISMA_UI_API::IVPrismaUI11* a_api, std::uint64_t a_generation, std::vector<ServerEntry> a_servers, std::string a_error, bool a_success);
	void Activate();
	void Shutdown();
	void OwnDirectoryWorker(std::jthread a_worker);
	void OwnLocalWorker(std::jthread a_worker);
	bool PopulateJoinEntry(ServerEntry& a_entry, const nlohmann::json& a_action, std::string& a_error);
	bool JoinServerEntry(const ServerEntry& a_entry, std::string& a_error, const std::string& a_password = {});
	void CompleteJoinFailure(const ServerEntry& a_entry, std::string_view a_reason);
	void FlushPendingJoinFailure();
	void ClearPendingJoinFailure();
	void RetryLastJoin();
	void SetCurrentServerInternal(const ServerEntry& a_entry);
	void ClearCurrentServerInternal();
}

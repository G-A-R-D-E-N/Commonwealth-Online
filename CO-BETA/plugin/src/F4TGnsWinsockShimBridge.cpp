#include "pch.h"

#include "F4TGnsWinsockShim.h"
%:include "F4TCompatibilityShim.h"
#include "F4TGnsComponentReceive.h"
#include "F4TGnsProtocolAdapter.h"
#include "F4TGnsSnapshotEnvelope.h"
#include "F4TGnsWinsockShimState.h"
#include "F4TGnsWinsockShimInternal.h"
#include "F4TClientCompatibility.h"
#include "F4TClientIdentity.h"
#include "F4TClientModpackProbe.h"
#include "F4TReliableOutbox.h"
#include "F4TServerProfileStore.h"
#include "F4TWorldPolicyEnforcer.h"

#include <Windows.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
	using namespace F4T::GnsWinsockShimState;

	using ConnectFailureKind = F4T::GnsWinsockShim::ConnectFailureKind;

	constexpr wchar_t kBridgeDllName[] = L"commonwealth_online_gns_bridge.dll";

	std::mutex g_bridgeMutex;
	BridgeApi g_bridgeApi{};

	void ModuleAnchor() noexcept {}

	std::filesystem::path GetCurrentModuleDirectory()
	{
		HMODULE module = nullptr;
		const auto moduleAddress = reinterpret_cast<LPCWSTR>(reinterpret_cast<std::uintptr_t>(&ModuleAnchor));
		if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				moduleAddress,
				std::addressof(module)) ||
			!module) {
			return {};
		}

		std::array<wchar_t, 32768> path{};
		const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
		if (length == 0 || length >= path.size()) {
			return {};
		}
		return std::filesystem::path{ std::wstring_view{ path.data(), length } }.parent_path();
	}

	template <class T>
	T ResolveExport(HMODULE a_module, const char* a_name) noexcept
	{
		return reinterpret_cast<T>(GetProcAddress(a_module, a_name));
	}

	bool TryLoadBridgeCandidate(const std::filesystem::path& a_path, BridgeApi& a_api)
	{
		if (!a_path.is_absolute()) {
			return false;
		}
		HMODULE module = LoadLibraryExW(a_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (!module) {
			return false;
		}

		BridgeApi candidate{};
		candidate.module = module;
		candidate.create = ResolveExport<ClientCreateFn>(module, "co_gns_client_create");
		candidate.destroy = ResolveExport<ClientDestroyFn>(module, "co_gns_client_destroy");
		candidate.poll = ResolveExport<ClientPollFn>(module, "co_gns_client_poll");
		candidate.send = ResolveExport<ClientSendFn>(module, "co_gns_client_send");
		candidate.disconnect = ResolveExport<ClientDisconnectFn>(module, "co_gns_client_disconnect");
		if (!candidate.create || !candidate.destroy || !candidate.poll || !candidate.send || !candidate.disconnect) {
			FreeLibrary(module);
			return false;
		}

		a_api = candidate;
		return true;
	}

	int FailurePriority(ConnectFailureKind a_kind) noexcept
	{
		switch (a_kind) {
		case ConnectFailureKind::LocalHandshakeFailure:
			return 3;
		case ConnectFailureKind::SessionRejected:
			return 2;
		case ConnectFailureKind::AdmissionRefused:
		default:
			return 1;
		}
	}
}

namespace F4T::GnsWinsockShim
{
	thread_local std::optional<ConnectFailure> g_currentThreadConnectFailure;

	BridgeApi* GetBridgeApi(std::string& a_error)
	{
		const std::scoped_lock lock(g_bridgeMutex);
		if (g_bridgeApi.module) {
			return std::addressof(g_bridgeApi);
		}

		std::vector<std::filesystem::path> candidates;
		if (const auto moduleDirectory = GetCurrentModuleDirectory(); !moduleDirectory.empty()) {
			candidates.push_back(moduleDirectory / kBridgeDllName);
		}
		std::error_code currentPathError;
		if (const auto currentPath = std::filesystem::current_path(currentPathError); !currentPathError) {
			candidates.push_back(currentPath / L"Data" / L"F4SE" / L"Plugins" / kBridgeDllName);
		}

		for (const auto& candidate : candidates) {
			if (TryLoadBridgeCandidate(candidate, g_bridgeApi)) {
				F4T::DebugLogging::Log("Networking", "loaded GNS transport bridge");
				return std::addressof(g_bridgeApi);
			}
		}

		a_error = std::format(
			"Could not load commonwealth_online_gns_bridge.dll or resolve its client transport exports (Win32 error {}).",
			GetLastError());
		return nullptr;
	}

	void PreserveThreadConnectFailure(ConnectFailure a_failure)
	{
		if (!g_currentThreadConnectFailure ||
			FailurePriority(a_failure.kind) > FailurePriority(g_currentThreadConnectFailure->kind)) {
			g_currentThreadConnectFailure = std::move(a_failure);
		}
	}

	void RecordConnectFailure(const std::shared_ptr<SocketState>& a_state, ConnectFailure a_failure)
	{
		const std::scoped_lock lock(a_state->failureMutex);
		if (!a_state->connectFailure ||
			FailurePriority(a_failure.kind) > FailurePriority(a_state->connectFailure->kind)) {
			a_state->connectFailure = std::move(a_failure);
		}
	}

	std::optional<ConnectFailure> TakeStateConnectFailure(const std::shared_ptr<SocketState>& a_state)
	{
		const std::scoped_lock lock(a_state->failureMutex);
		auto failure = std::move(a_state->connectFailure);
		a_state->connectFailure.reset();
		return failure;
	}

	bool ProbeServerReachability(
		std::string_view a_host,
		std::uint16_t a_port,
		int a_timeoutMilliseconds,
		int& a_latencyMilliseconds,
		std::string& a_error)
	{
		a_latencyMilliseconds = 0;
		a_error.clear();
		if (a_host.empty() || a_port == 0) {
			a_error = "A host and a port are required.";
			return false;
		}

		const auto hostText = std::string{ a_host };
		addrinfo hints{};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		addrinfo* results = nullptr;
		const auto portText = std::to_string(a_port);
		const auto resolveStatus = getaddrinfo(hostText.c_str(), portText.c_str(), std::addressof(hints), std::addressof(results));
		if (resolveStatus != 0 || results == nullptr) {
			a_error = std::format("Could not resolve '{}' ({})", hostText, gai_strerrorA(resolveStatus));
			return false;
		}
		std::string numericHost;
		for (auto* entry = results; entry != nullptr; entry = entry->ai_next) {
			char text[INET6_ADDRSTRLEN]{};
			if (entry->ai_family == AF_INET &&
				inet_ntop(AF_INET, std::addressof(reinterpret_cast<const sockaddr_in*>(entry->ai_addr)->sin_addr), text, sizeof(text)) != nullptr) {
				numericHost = text;
				break;
			}
			if (entry->ai_family == AF_INET6 &&
				inet_ntop(AF_INET6, std::addressof(reinterpret_cast<const sockaddr_in6*>(entry->ai_addr)->sin6_addr), text, sizeof(text)) != nullptr) {
				numericHost = text;
				break;
			}
		}
		freeaddrinfo(results);
		if (numericHost.empty()) {
			a_error = std::format("Could not resolve '{}' to an IP address.", hostText);
			return false;
		}

		std::string apiError;
		auto* api = GetBridgeApi(apiError);
		if (api == nullptr) {
			a_error = apiError.empty() ? "The GNS transport bridge is unavailable." : apiError;
			return false;
		}

		char createError[256]{};
		F4T::GnsWinsockShimState::GnsClientHandle handle{};
		if (api->create(numericHost.c_str(), a_port, std::addressof(handle), createError, sizeof(createError)) == 0 || handle == nullptr) {
			a_error = createError[0] ? std::string{ createError } : "The check connection could not start.";
			if (handle != nullptr) {
				api->destroy(handle);
			}
			return false;
		}

		const auto started = std::chrono::steady_clock::now();
		bool connected = false;
		std::string closeReason;
		for (;;) {
			const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
			if (elapsed >= a_timeoutMilliseconds) {
				break;
			}
			F4T::GnsWinsockShimState::GnsEvent event{};
			char payload[1]{};
			if (api->poll(handle, std::addressof(event), payload, sizeof(payload)) == 1) {
				if (event.type == F4T::GnsWinsockShimState::kEventConnected) {
					connected = true;
					break;
				}
				if (event.type == F4T::GnsWinsockShimState::kEventDisconnected) {
					closeReason = event.debug;
					break;
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		const auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
		api->destroy(handle);
		if (!connected) {
			a_error = closeReason.empty() ? std::format("No answer within {} ms.", a_timeoutMilliseconds) : closeReason;
			return false;
		}
		a_latencyMilliseconds = static_cast<int>(latency);
		return true;
	}
}

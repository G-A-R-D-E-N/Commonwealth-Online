#include "pch.h"

#include "SessionCallbacks.h"
#include "F4TRecentServers.h"
#include "F4TServerBrowserBridge.h"
#include "F4TServerBrowserBridgeInternal.h"
#include "F4TShareLog.h"
#include "F4TSteamActions.h"
#include "F4TWorldMode.h"

#include <nlohmann/json.hpp>

namespace F4T::ServerBrowser
{
	constexpr std::size_t kMaxPasswordLength = 128;

	void ServerBrowserBridge::Initialize(PRISMA_UI_API::IVPrismaUI4* a_api, PrismaView a_view)
	{
		detail::Shutdown();
		detail::Activate();
		detail::g_api = a_api;
		detail::g_view = a_view;
		detail::g_data.SetAllServers({});
		RecentServersStore::Load();
	}

	void ServerBrowserBridge::Reactivate(PRISMA_UI_API::IVPrismaUI4* a_api, PrismaView a_view)
	{
		detail::Activate();
		detail::g_api = a_api;
		detail::g_view = a_view;
	}

	void ServerBrowserBridge::Shutdown()
	{
		detail::Shutdown();
		detail::ClearPendingJoinFailure();
		detail::g_api = nullptr;
		detail::g_view = 0;
	}

	void ServerBrowserBridge::OnBrowserShown()
	{
		detail::PushUIThemeInternal();
		F4T::Steam::PushCapabilities();
		detail::DispatchEvent(nlohmann::json{ { "type", "menuOpened" } });
		detail::FlushPendingJoinFailure();
		detail::PushRetryAvailable();
		detail::PushRecentServersUpdated();
		detail::RefreshServerList();
	}

	void ServerBrowserBridge::OnBrowserHidden()
	{
		detail::DispatchEvent(nlohmann::json{ { "type", "menuClosed" } });
	}

	void ServerBrowserBridge::HandleAction(const nlohmann::json& a_action)
	{
		if (F4T::Steam::HandleAction(a_action)) {
			return;
		}

		const auto action = a_action.value("action", "");
		if (action.empty()) {
			return;
		}

		if (action == "refreshServerList") {
			detail::RefreshServerList();
			return;
		}

		if (action == "scanLocalServers") {
			detail::ScanLocalServers();
			return;
		}

		if (action == "setFilter") {
			auto filters = detail::g_data.GetFilters();
			const auto filterName = a_action.value("filterName", "");
			if (filterName == "search") {
				filters.search = a_action.value("value", "");
			} else if (filterName == "region") {
				filters.region = a_action.value("value", "Any Region");
			} else if (filterName == "mode") {
				filters.mode = a_action.value("value", "Any Mode");
			} else if (filterName == "worldspace") {
				filters.worldspace = a_action.value("value", "Any Worldspace");
			} else if (filterName == "ping") {
				filters.ping = a_action.value("value", "Any");
			} else if (filterName == "showFull" || filterName == "hasPassword" || filterName == "modded") {
				const auto value = a_action.value("value", false);
				if (filterName == "showFull") {
					filters.showFull = value;
				} else if (filterName == "hasPassword") {
					filters.hasPassword = value;
				} else {
					filters.modded = value;
				}
			}
			detail::g_data.SetFilters(filters);
			detail::PushServerListUpdated();
			return;
		}

		if (action == "resetFilters") {
			detail::g_data.ResetFilters();
			detail::PushServerListUpdated();
			return;
		}

		if (action == "toggleFavorite") {
			const auto serverId = a_action.value("serverId", "");
			if (!serverId.empty()) {
				detail::g_data.ToggleFavorite(serverId);
				detail::PushServerListUpdated();
			}
			return;
		}

		if (action == "switchTab") {
			const auto tabName = a_action.value("tabName", "");
			if (tabName == "local" || tabName == "steam-friends") {
				detail::ScanLocalServers();
			}
			return;
		}

		if (action == "joinServer") {
			const auto serverId = a_action.value("serverId", "");
			const auto* entry = detail::g_data.FindServer(serverId);
			if (!entry) {
				entry = detail::g_data.FindLocalServer(serverId);
			}
			if (!entry) {
				detail::DispatchEvent(nlohmann::json{
					{ "type", "joinFailed" },
					{ "serverId", serverId },
					{ "reason", "Server not found." }
				});
				return;
			}

			std::string error;
			if (!detail::JoinServerEntry(*entry, error)) {
				detail::CompleteJoinFailure(*entry, error);
			}
			return;
		}

		if (action == "openDirectConnect") {
			ServerEntry entry;
			entry.host = a_action.value("address", "127.0.0.1");
			entry.port = 7777;

			std::string error;
			if (!detail::PopulateJoinEntry(entry, a_action, error)) {
				entry.id = "direct";
				entry.name = "Direct Connect";
				detail::CompleteJoinFailure(entry, error);
				return;
			}

			entry.id = std::format("recent:{}:{}", entry.host, entry.port);
			entry.name = a_action.value("name", "");
			if (entry.name.empty()) {
				entry.name = std::format("{}:{}", entry.host, entry.port);
			}

			const auto password = a_action.value("password", "");
			entry.passworded = !password.empty();
			if (password.size() > kMaxPasswordLength) {
				detail::CompleteJoinFailure(entry, "Password must be 128 characters or fewer.");
				return;
			}
			if (!detail::JoinServerEntry(entry, error, password)) {
				detail::CompleteJoinFailure(entry, error);
			}
			return;
		}

		if (action == "joinWithPassword") {
			const auto serverId = a_action.value("serverId", "");
			ServerEntry entry{};
			const auto* found = detail::g_data.FindServer(serverId);
			if (!found) {
				found = detail::g_data.FindLocalServer(serverId);
			}
			const auto last = COUI::Session::GetLastJoin();
			if (found) {
				entry = *found;
			} else if (last && !serverId.empty() && last->id == serverId) {
				entry.id = last->id;
				entry.name = last->name;
				entry.host = last->host;
				entry.port = last->port;
			} else {
				detail::DispatchEvent(nlohmann::json{
					{ "type", "joinFailed" },
					{ "serverId", serverId },
					{ "reason", "Server not found." }
				});
				return;
			}
			entry.passworded = true;

			const auto password = a_action.value("password", "");
			if (password.size() > kMaxPasswordLength) {
				detail::CompleteJoinFailure(entry, "Password must be 128 characters or fewer.");
				return;
			}

			std::string error;
			if (!detail::JoinServerEntry(entry, error, password)) {
				detail::CompleteJoinFailure(entry, error);
			}
			return;
		}

		if (action == "retryLastJoin") {
			detail::RetryLastJoin();
			return;
		}

		if (action == "cancelJoin") {
			COUI::Session::Disconnect();
			return;
		}

		if (action == "shareLog") {
			(void)F4T::ShareLog::Share();
			return;
		}

		if (action == "closeBrowser") {
			return;
		}
	}

	void ServerBrowserBridge::ForwardControllerInput(const char* a_button, const char* a_phase)
	{
		if (!a_button || !a_phase) {
			return;
		}

		detail::DispatchEvent(nlohmann::json{
			{ "type", "controllerInput" },
			{ "button", a_button },
			{ "phase", a_phase }
		});
	}

	void ServerBrowserBridge::PushConnectionStatus()
	{
		detail::PushConnectionStatusEvent();
	}

	void ServerBrowserBridge::PushJoinProgress(const char* a_message)
	{
		if (a_message == nullptr || a_message[0] == '\0') {
			return;
		}
		detail::DispatchEvent(nlohmann::json{
			{ "type", "joinLoading" },
			{ "message", a_message }
		});
	}

	void ServerBrowserBridge::PushUITheme()
	{
		detail::PushUIThemeInternal();
	}

	void ServerBrowserBridge::PushSessionState(const char* a_stateJson)
	{
		detail::PushSessionStateInternal(a_stateJson);
	}

	void ServerBrowserBridge::SetCurrentServer(const ServerEntry& a_entry)
	{
		detail::SetCurrentServerInternal(a_entry);
		COUI::WorldMode::Set(a_entry.worldMode);
	}

	void ServerBrowserBridge::ClearCurrentServer()
	{
		detail::ClearCurrentServerInternal();
		COUI::WorldMode::Set({});
	}

	void ServerBrowserBridge::PushCurrentServerInfo()
	{
		detail::PushCurrentServerInfoInternal();
	}

	namespace detail
	{
		void PushSessionStateInternal(const char* a_stateJson)
		{
			if (a_stateJson == nullptr || a_stateJson[0] == '\0') {
				return;
			}
			nlohmann::json state;
			try {
				state = nlohmann::json::parse(a_stateJson);
			} catch (...) {
				return;
			}
			if (!state.is_object()) {
				return;
			}
			state["type"] = "sessionState";
			DispatchEventToView(state);
		}
	}
}

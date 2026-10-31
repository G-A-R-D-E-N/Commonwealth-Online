#include "pch.h"

#include "SessionCallbacks.h"
#include "F4THttpGet.h"
#include "F4TAlertSystem.h"
#include "F4TPrismaUI.h"
#include "F4TPrismaCapabilities.h"
#include "F4TRecentServers.h"
#include "F4TServerBrowserBridge.h"
#include "F4TServerBrowserBridgeInternal.h"
#include "F4TServerBrowserData.h"
#include "F4TServerBrowserReachability.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <format>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <unordered_set>

namespace F4T::ServerBrowser
{
	namespace detail
	{
		constexpr const char* kServerDirectoryUrls[] = { "https://oeajjfquvmdxqqtiotfa.supabase.co/functions/v1/server-directory", "https://commonwealth-online.com/servers/server.json" };
		PRISMA_UI_API::IVPrismaUI4* g_api = nullptr;
		PrismaView                    g_view = 0;
		ServerBrowserData             g_data;
		std::mutex                    g_currentServerMutex;
		std::optional<ServerEntry>    g_currentServer;
		std::optional<nlohmann::json> g_pendingJoinFailure;
		nlohmann::json LocalServersToJson()
		{
			nlohmann::json servers = nlohmann::json::array();
			for (const auto& entry : g_data.GetLocalServers()) {
				servers.push_back(entry.ToJson());
			}
			return servers;
		}

		std::string GetLogPrefix()
		{
			return std::string{COUI::Session::GetLogPrefix()} + " [BrowserBridge]";
		}

		void LogInfo(std::string_view a_message)
		{
			REX::INFO("{}{}", GetLogPrefix(), a_message);
		}

		void LogWarning(std::string_view a_message)
		{
			REX::WARN("{}{}", GetLogPrefix(), a_message);
		}
		bool CanDispatchToBrowser()
		{
			return g_api &&
			       g_view != 0 &&
			       g_api->IsValid(g_view) &&
			       !g_api->IsHidden(g_view) &&
			       F4T::PrismaUI::IsBrowserVisible();
		}

		void DispatchEventToView(const nlohmann::json& a_event)
		{
			if (!g_api || g_view == 0 || !g_api->IsValid(g_view)) {
				return;
			}

			const auto script = std::format("if(window.dispatchCoEvent){{window.dispatchCoEvent({});}}", a_event.dump());
			g_api->Invoke(g_view, script.c_str());
		}

		void DispatchEvent(const nlohmann::json& a_event)
		{
			if (!CanDispatchToBrowser()) {
				return;
			}

			DispatchEventToView(a_event);
		}

		void PushRecentServersUpdated()
		{
			// Recent list must reach JS even when the browser panel is closing after a join.
			DispatchEventToView(nlohmann::json{
				{ "type", "recentServersUpdated" },
				{ "servers", RecentServersStore::ToJson() }
			});
		}

		void PushServerListUpdated()
		{
			DispatchEvent(nlohmann::json{
				{ "type", "serverListUpdated" },
				{ "servers", g_data.ToAllServersJson() }
			});
		}

		void OnJoinFailureAlert(const char*, const char* a_actionId, void*)
		{
			if (a_actionId && std::string_view{ a_actionId } == "retry") {
				RetryLastJoin();
			}
		}

		void PushLocalServersUpdated()
		{
			DispatchEvent(nlohmann::json{
				{ "type", "localServersUpdated" },
				{ "servers", LocalServersToJson() }
			});
		}

		nlohmann::json ColorToJson(const RE::NiColor& a_color)
		{
			const auto toByte = [](float a_channel) {
				return static_cast<int>(std::clamp(a_channel * 255.0f, 0.0f, 255.0f));
			};

			return nlohmann::json{
				{ "r", toByte(a_color.r) },
				{ "g", toByte(a_color.g) },
				{ "b", toByte(a_color.b) }
			};
		}

		void PushUIThemeInternal()
		{
			if (!g_api || g_view == 0 || !g_api->IsValid(g_view)) {
				return;
			}

			const auto hudColor = RE::HUDMenuUtils::GetGameplayHUDColor();
			const auto hudBgColor = RE::HUDMenuUtils::GetGameplayHUDBackgroundColor();
			const auto themePayload = nlohmann::json{
				{ "hud", ColorToJson(hudColor) },
				{ "hudBackground", ColorToJson(hudBgColor) }
			};

			const auto script = std::format(
				"if(window.applyCoTheme){{window.applyCoTheme({});}}",
				themePayload.dump());
			g_api->Invoke(g_view, script.c_str());
		}

		void ClearCurrentServerInternal()
		{
			std::scoped_lock lock(g_currentServerMutex);
			g_currentServer.reset();
		}

		void SetCurrentServerInternal(const ServerEntry& a_entry)
		{
			std::scoped_lock lock(g_currentServerMutex);
			g_currentServer = a_entry;
		}

		void UpdateCurrentServerPlayers(int a_players)
		{
			std::scoped_lock lock(g_currentServerMutex);
			if (g_currentServer) {
				g_currentServer->players = a_players;
			}
		}

		std::optional<ServerEntry> SnapshotCurrentServer()
		{
			std::scoped_lock lock(g_currentServerMutex);
			return g_currentServer;
		}

		ServerEntry ResolveCurrentServerEntry()
		{
			if (const auto current = SnapshotCurrentServer()) {
				return *current;
			}

			const auto& recent = RecentServersStore::Get();
			if (!recent.empty()) {
				return recent.front();
			}

			return {};
		}

		int CountLivePlayers(const ServerEntry& a_entry)
		{
			std::string remoteJson;
			std::string error;
			if (COUI::Session::GetRemotePlayersMapJson(remoteJson, error)) {
				try {
					const auto parsed = nlohmann::json::parse(remoteJson.empty() ? "[]" : remoteJson);
					if (parsed.is_array()) {
						return static_cast<int>(parsed.size()) + 1;
					}
				} catch (...) {
					// Fall through to join-time snapshot.
				}
			}

			if (a_entry.players > 0) {
				return a_entry.players;
			}
			return 1;
		}

		nlohmann::json BuildCurrentServerInfoJson(
			bool              a_connected,
			const ServerEntry& a_entry,
			int               a_players,
			int               a_ping)
		{
			std::string name = a_entry.name;
			if (name.empty()) {
				if (!a_entry.host.empty()) {
					name = a_entry.port > 0
						? std::format("{}:{}", a_entry.host, a_entry.port)
						: a_entry.host;
				} else {
					name = "Connected";
				}
			}

			return nlohmann::json{
				{ "connected", a_connected },
				{ "name", name },
				{ "host", a_entry.host },
				{ "port", a_entry.port },
				{ "players", a_players },
				{ "maxPlayers", a_entry.maxPlayers },
				{ "ping", a_ping }
			};
		}

		void PushCurrentServerInfoInternal()
		{
			const bool connected = COUI::Session::IsConnected();
			if (!connected) {
				ServerBrowserBridge::ClearCurrentServer();
				F4T::PrismaUI::DispatchPauseMenuServerInfo(
					nlohmann::json{ { "connected", false } }.dump());
				return;
			}

			auto entry = ResolveCurrentServerEntry();
			const int players = CountLivePlayers(entry);
			entry.players = players;
			UpdateCurrentServerPlayers(players);

			F4T::PrismaUI::DispatchPauseMenuServerInfo(
				BuildCurrentServerInfoJson(true, entry, players, entry.ping).dump());
		}

		void PushConnectionStatusEvent()
		{
			const auto connected = COUI::Session::IsConnected();
			g_data.SetConnectionStatus(connected ? "Connected" : "Online");
			if (!connected) {
				ServerBrowserBridge::ClearCurrentServer();
			}

			DispatchEvent(nlohmann::json{
				{ "type", "connectionStatusChanged" },
				{ "status", g_data.GetConnectionStatus() },
				{ "serverCount", g_data.GetFilteredServers().size() }
			});

			if (CanDispatchToBrowser()) {
				const auto prefix = std::string{COUI::Session::GetLogPrefix()};
				const auto script = std::format(
					"if(window.setConnectionStatus){{window.setConnectionStatus({},{});}}",
					connected ? "true" : "false",
					nlohmann::json(prefix).dump());
				g_api->Invoke(g_view, script.c_str());
			}
		}

		void PushRetryAvailable()
		{
			DispatchEvent(nlohmann::json{
				{ "type", "retryAvailable" },
				{ "available", COUI::Session::HasLastJoin() }
			});
		}

		std::optional<std::uint16_t> ParsePortValue(const nlohmann::json& a_value)
		{
			if (a_value.is_number_unsigned() || a_value.is_number_integer()) {
				const auto port = a_value.get<std::int64_t>();
				if (port >= 1 && port <= 65535) {
					return static_cast<std::uint16_t>(port);
				}
				return std::nullopt;
			}

			if (a_value.is_string()) {
				const auto text = a_value.get<std::string>();
				std::uint32_t port = 0;
				const auto* begin = text.data();
				const auto* end = begin + text.size();
				const auto result = std::from_chars(begin, end, port);
				if (result.ec == std::errc{} && result.ptr == end && port >= 1 && port <= 65535) {
					return static_cast<std::uint16_t>(port);
				}
			}

			return std::nullopt;
		}

		bool PopulateJoinEntry(ServerEntry& a_entry, const nlohmann::json& a_action, std::string& a_error)
		{
			a_entry.host = a_action.value("address", a_entry.host);
			if (a_entry.host.empty()) {
				a_entry.host = "127.0.0.1";
			}

			if (a_action.contains("port")) {
				const auto port = ParsePortValue(a_action.at("port"));
				if (!port) {
					a_error = "Port must be a number between 1 and 65535.";
					return false;
				}
				a_entry.port = *port;
			} else if (a_entry.port <= 0) {
				a_entry.port = 7777;
			}

			return true;
		}

		void CompleteJoinSuccess(const ServerEntry& a_entry)
		{
			// Persist before UI dispatch — join hides the main menu, so the browser
			// visibility gate would drop joinSucceeded and localStorage never runs.
			RecentServersStore::Add(a_entry);
			ServerBrowserBridge::SetCurrentServer(a_entry);
			PushRecentServersUpdated();

			PushConnectionStatusEvent();
			DispatchEventToView(nlohmann::json{
				{ "type", "joinSucceeded" },
				{ "serverId", a_entry.id },
				{ "server", a_entry.ToJson() }
			});
			DispatchEventToView(nlohmann::json{
				{ "type", "uiSound" },
				{ "sound", "joinSuccess" }
			});
			LogInfo(std::format(" Joined server '{}'.", a_entry.name.empty() ? a_entry.host : a_entry.name));
		}

		void RestoreMainMenuAfterJoinFailure()
		{
			if (!F4T::PrismaUI::IsMainMenuVisible()) {
				F4T::PrismaUI::ShowMainMenu();
			}
			if (!F4T::PrismaUI::IsBrowserVisible()) {
				F4T::PrismaUI::ShowServerBrowser();
			}
		}

		void CompleteJoinFailure(const ServerEntry& a_entry, std::string_view a_reason)
		{
			COUI::Session::Disconnect();
			PushConnectionStatusEvent();
			const auto reasonText = std::string{ a_reason };
			const auto failureEvent = nlohmann::json{
				{ "type", "joinFailed" },
				{ "serverId", a_entry.id },
				{ "reason", reasonText }
			};

			if (F4T::PrismaUI::IsMainMenuVisible()) {
				RestoreMainMenuAfterJoinFailure();
				if (CanDispatchToBrowser()) {
					DispatchEventToView(failureEvent);
				} else {
					g_pendingJoinFailure = failureEvent;
				}
				DispatchEvent(nlohmann::json{
					{ "type", "uiSound" },
					{ "sound", "joinFailed" }
				});
				PushRetryAvailable();
			} else {
				const F4T::Alerts::AlertAction retryAction{ "retry", "Retry", "A" };
				const F4T::Alerts::AlertRequest alert{
					.id = "co-join-failure",
					.category = "MULTIPLAYER",
					.title = "Join failed",
					.message = reasonText.c_str(),
					.tone = "error",
					.durationMs = 0,
					.actions = &retryAction,
					.actionCount = 1,
				};
				F4T::Alerts::EnsureView();
				if (!F4T::Alerts::Show(alert, OnJoinFailureAlert)) {
					LogWarning(" Could not show in-game join failure alert.");
				}
			}
			LogWarning(std::format(" Join failed for '{}': {}", a_entry.name, a_reason));
		}

		void FlushPendingJoinFailure()
		{
			if (!g_pendingJoinFailure || !CanDispatchToBrowser()) {
				return;
			}

			DispatchEventToView(*g_pendingJoinFailure);
			g_pendingJoinFailure.reset();
			DispatchEvent(nlohmann::json{
				{ "type", "uiSound" },
				{ "sound", "joinFailed" }
			});
			PushRetryAvailable();
		}

		void ClearPendingJoinFailure()
		{
			g_pendingJoinFailure.reset();
		}

		std::string_view JoinFailureMessage(const char* a_joinError)
		{
			const std::string_view error = a_joinError ? a_joinError : "";
			if (error == "password_required") {
				return "This server needs a password.";
			}
			if (error == "password_invalid") {
				return "Wrong password.";
			}
			return error.empty() ? std::string_view{ "Join failed." } : error;
		}

		void OnJoinComplete(bool a_success, const char* a_joinError, void* a_userData)
		{
			auto* entry = static_cast<ServerEntry*>(a_userData);
			if (!entry) {
				return;
			}

			if (!a_success) {
				CompleteJoinFailure(*entry, JoinFailureMessage(a_joinError));
				delete entry;
				return;
			}

			CompleteJoinSuccess(*entry);
			F4T::PrismaUI::HideServerBrowser();
			delete entry;
		}

		bool JoinServerEntry(const ServerEntry& a_entry, std::string& a_error, const std::string& a_password)
		{
			if (a_entry.passworded && a_password.empty()) {
				DispatchEventToView(nlohmann::json{ { "type", "joinPasswordRequired" }, { "serverId", a_entry.id }, { "name", a_entry.name } });
				return true;
			}

			DispatchEvent(nlohmann::json{
				{ "type", "joinStarted" },
				{ "serverId", a_entry.id }
			});

			DispatchEvent(nlohmann::json{
				{ "type", "joinLoading" },
				{ "serverId", a_entry.id },
				{ "message", "Validating server..." }
			});

			auto* pendingEntry = new ServerEntry(a_entry);
			if (pendingEntry->host.empty()) {
				pendingEntry->host = "127.0.0.1";
			}
			if (pendingEntry->port <= 0) {
				pendingEntry->port = 7777;
			}

			COMMONWEALTH_ONLINE_UI_API::JoinRequest request{};
			request.id = pendingEntry->id.c_str();
			request.name = pendingEntry->name.c_str();
			request.host = pendingEntry->host.c_str();
			request.port = static_cast<std::uint16_t>(pendingEntry->port);
			request.passworded = pendingEntry->passworded;

			const bool started = a_entry.passworded
				? COUI::Session::JoinSessionWithPassword(request, a_password, OnJoinComplete, pendingEntry, a_error)
				: COUI::Session::JoinSession(request, OnJoinComplete, pendingEntry, a_error);

			if (!started) {
				delete pendingEntry;
				return false;
			}

			DispatchEvent(nlohmann::json{
				{ "type", "joinLoading" },
				{ "serverId", a_entry.id },
				{ "message", "Loading game..." }
			});

			if (g_api && g_view != 0 && g_api->IsValid(g_view)) {
				g_api->Invoke(g_view, "if(window.onSessionLoading){window.onSessionLoading();}");
			}

			// Hide the custom main menu overlay before the vanilla loading screen appears.
			F4T::PrismaUI::HideMainMenu();
			return true;
		}

		void RetryLastJoin()
		{
			const auto last = COUI::Session::GetLastJoin();
			ServerEntry entry{};
			if (last) {
				entry.id = last->id;
				entry.name = last->name;
				entry.host = last->host;
				entry.port = last->port;
				entry.passworded = last->passworded;
			}

			std::string error;
			if (entry.passworded && CanDispatchToBrowser()) {
				JoinServerEntry(entry, error);
				return;
			}

			DispatchEvent(nlohmann::json{
				{ "type", "joinStarted" },
				{ "serverId", entry.id }
			});
			DispatchEvent(nlohmann::json{
				{ "type", "joinLoading" },
				{ "serverId", entry.id },
				{ "message", "Reconnecting..." }
			});

			auto* retryEntry = new ServerEntry(entry);
			const bool started = COUI::Session::RetryLastJoin(OnJoinComplete, retryEntry, error);

			if (!started) {
				delete retryEntry;
				CompleteJoinFailure(entry, error.empty() ? "Retry failed." : error);
			}
		}

		void CompleteDirectoryRefresh(std::vector<ServerEntry> a_servers, std::string a_error, bool a_success)
		{
			if (a_success) {
				std::unordered_set<std::string> favoriteIds;
				for (const auto& entry : g_data.GetAllServers()) {
					if (entry.favorite && !entry.id.empty()) {
						favoriteIds.insert(entry.id);
					}
				}
				for (auto& entry : a_servers) {
					if (favoriteIds.contains(entry.id)) {
						entry.favorite = true;
					}
				}
				g_data.SetAllServers(std::move(a_servers));
			}

			g_data.SetRefreshing(false);
			PushServerListUpdated();
			PushConnectionStatusEvent();
			auto finishedEvent = nlohmann::json{
				{ "type", "serverListRefreshFinished" },
				{ "success", a_success }
			};
			if (!a_success && !a_error.empty()) {
				finishedEvent["error"] = ServerBrowserData::UserFacingDirectoryError(a_error);
			}
			DispatchEvent(std::move(finishedEvent));

			if (a_success) {
				LogInfo(std::format(" Server directory refreshed ({} server(s)).", g_data.GetAllServers().size()));
				if (!a_error.empty()) {
					LogWarning(std::format(" Server directory attempt failed: {}", a_error));
				}
			} else {
				LogWarning(std::format(" Server directory refresh failed: {}", a_error));
			}

			g_directoryRefreshRunning.store(false);
		}

		void RefreshServerList()
		{
			const auto generation = BeginDispatch(g_directoryRefreshRunning);
			if (!generation) {
				return;
			}
			const auto* taskInterface = F4SE::GetTaskInterface();
			auto* prismaV11 = F4T::PrismaCapabilities::GetV11();
			if (!taskInterface && !prismaV11) {
				FinishDispatch(g_directoryRefreshRunning, *generation);
				LogWarning(" Directory refresh not started because no game-thread dispatcher is available.");
				return;
			}
			if (taskInterface) {
				g_data.SetRefreshing(true);
				DispatchEvent(nlohmann::json{ { "type", "serverListRefreshStarted" } });
			}

			const auto currentServerSnapshot = SnapshotCurrentServer();
			OwnDirectoryWorker(std::jthread([taskInterface, prismaV11, generation = *generation, currentServerSnapshot](std::stop_token a_stopToken) {
				std::string error;
				std::vector<ServerEntry> servers;
				bool success = false;

				const auto query = ServerBrowserData::QueryDirectoryUrls(
					kServerDirectoryUrls,
					[&](const char* url, bool& cancelled) {
						ServerBrowserData::DirectoryAttempt attempt;
						if (a_stopToken.stop_requested()) {
							cancelled = true;
							return attempt;
						}
						attempt.transportOk = F4T::Http::Get(url, attempt.body, attempt.transportError, 10000);
						cancelled = a_stopToken.stop_requested();
						return attempt;
					});
				if (query.cancelled) {
					FinishDispatch(g_directoryRefreshRunning, generation);
					return;
				}
				servers = query.servers;
				error = query.error;
				success = query.success;

				if (success &&
					currentServerSnapshot &&
					COUI::Session::IsConnected()) {
					for (auto& entry : servers) {
						if (currentServerSnapshot->host == entry.host &&
							currentServerSnapshot->port == entry.port) {
							entry.ping = currentServerSnapshot->ping;
							if (entry.status.empty()) {
								entry.status = "Open";
							}
						}
					}
				}
				if (success) { Reachability::Check(servers, a_stopToken); }
				const auto serversCopy = std::move(servers);
				const auto errorCopy = std::move(error);

				if (DispatchDirectoryCompletion(
						taskInterface, prismaV11, generation, std::move(serversCopy), std::move(errorCopy), success)) {
					return;
				}

				FinishDispatch(g_directoryRefreshRunning, generation);
				LogWarning(" Dropped directory refresh result because no game-thread dispatcher is available.");
			}));
		}

		void CompleteLocalScan(std::vector<ServerEntry> a_servers, std::string a_error, bool a_success)
		{
			g_data.SetLocalServers(std::move(a_servers));
			PushLocalServersUpdated();

			auto finishedEvent = nlohmann::json{
				{ "type", "localScanFinished" },
				{ "success", a_success }
			};
			if (!a_error.empty()) {
				finishedEvent["error"] = a_error;
			}
			DispatchEvent(std::move(finishedEvent));

			if (a_success) {
				LogInfo(std::format(" LAN scan found {} local server(s).", g_data.GetLocalServers().size()));
			} else {
				LogWarning(std::format(" LAN scan found no servers. {}", a_error));
			}

			g_localScanRunning.store(false);
		}

		void ScanLocalServers()
		{
			const auto generation = BeginDispatch(g_localScanRunning);
			if (!generation) {
				return;
			}
			const auto* taskInterface = F4SE::GetTaskInterface();
			auto* prismaV11 = F4T::PrismaCapabilities::GetV11();
			if (!taskInterface && !prismaV11) {
				FinishDispatch(g_localScanRunning, *generation);
				LogWarning(" LAN scan not started because no game-thread dispatcher is available.");
				return;
			}
			if (taskInterface) {
				DispatchEvent(nlohmann::json{ { "type", "localScanStarted" } });
			}

			OwnLocalWorker(std::jthread([taskInterface, prismaV11, generation = *generation](std::stop_token a_stopToken) {
				std::string error;
				std::string jsonOut;
				std::vector<ServerEntry> localServers;

				const bool scanned = COUI::Session::ScanLocalServers(jsonOut, error);
				if (a_stopToken.stop_requested()) {
					FinishDispatch(g_localScanRunning, generation);
					return;
				}
				if (scanned) {
					try {
						const auto parsed = nlohmann::json::parse(jsonOut.empty() ? "[]" : jsonOut);
						if (parsed.is_array()) {
							localServers = ServerBrowserData::ParseDirectoryServers(parsed, error);
						} else {
							error = "LAN scan results must be an array.";
						}
					} catch (const std::exception& ex) {
						error = std::format("Failed to parse LAN scan results: {}", ex.what());
					}
				}

				const bool success = scanned && !localServers.empty() && error.empty();
				if (!success && error.empty()) {
					error = "No Commonwealth Online servers responded on the LAN. "
					        "Check that the host server is running and Windows Firewall allows the configured LAN discovery port.";
				}

				const auto serversCopy = std::move(localServers);
				const auto errorCopy = std::move(error);

				if (DispatchLocalCompletion(
						taskInterface, prismaV11, generation, std::move(serversCopy), std::move(errorCopy), success)) {
					return;
				}

				FinishDispatch(g_localScanRunning, generation);
				LogWarning(" Dropped LAN scan result because no game-thread dispatcher is available.");
			}));
		}

	}
}

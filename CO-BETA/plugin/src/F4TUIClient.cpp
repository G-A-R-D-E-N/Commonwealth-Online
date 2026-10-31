#include "pch.h"

#include "F4TUIClient.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TLanDiscovery.h"
#include "F4TModpackAutoFixClient.h"
#include "F4TDebugLogging.h"
#include "F4TCharacterSlotsClient.h"
#include "F4TChat.h"
#include "F4TFactions.h"
#include "F4TSettlementAccessClient.h"
#include "F4TSettlementCaptureClient.h"
#include "F4TNetworking.h"
#include "F4TRemotePlayerState.h"
#include "F4TSessionLaunch.h"
#include "F4TSessionSaveGuard.h"
#include "F4TServerProfileStore.h"
#include "F4TSteamSession.h"
#include "F4TUIClientJoin.h"
#include "F4TUIClientJoinErrors.h"
#include "F4TTextEntryMode.h"
#include "F4TVoiceUi.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace F4T::UIClient
{
	namespace
	{
		using namespace F4T::UIClient::JoinErrors;
		COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1* g_ui = nullptr;
		COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI5* g_uiWorld = nullptr;
		COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI3* g_uiFactions = nullptr;
		std::atomic_bool g_joinInProgress{ false };
		std::atomic_bool g_voluntaryExitRequested{ false };
		std::atomic_uint64_t g_joinGeneration{ 0 };
		std::atomic_uint64_t g_connectionOwnerToken{ 0 };
		std::atomic_uint64_t g_connectionTokenCounter{ 0 };
		std::mutex g_joinWorkerMutex;
		std::atomic_bool g_joinWorkerRunning{ false };
		// Never destroyed: a joinable std::thread destructor at process exit calls std::terminate.
		std::thread* g_joinWorker = nullptr;

		std::string GetLogPrefix()
		{
			return F4T::Networking::GetLocalPlayerLogPrefix() + " [UIClient]";
		}

		bool Callback_JoinSession(
			const COMMONWEALTH_ONLINE_UI_API::JoinRequest* a_request,
			std::optional<std::string>                     a_serverPassword,
			COMMONWEALTH_ONLINE_UI_API::JoinCompleteFn     a_onComplete,
			void*                                          a_userData,
			char*                                          a_errorBuf,
			std::size_t                                    a_errorBufSize)
		{
			if (!a_request || !a_request->host || !a_request->host[0]) {
				CopyError(a_errorBuf, a_errorBufSize, "Join request is missing a host.");
				return false;
			}
			std::lock_guard workerLock{ g_joinWorkerMutex };
			if (F4T::SessionSaveGuard::IsSessionExitPending()) {
				F4T::DebugLogging::Log("UIClient", "join refused: session exit save pending");
				CopyError(a_errorBuf, a_errorBufSize, "Saving the current session before disconnect; try again shortly.");
				return false;
			}
			if (F4T::Networking::IsConnectedToServer()) {
				F4T::DebugLogging::Log("UIClient", "[RT-VERIFY 410-E] join refused: already connected");
				CopyError(a_errorBuf, a_errorBufSize, "Disconnect from the current server before joining another.");
				return false;
			}
			if (g_joinWorkerRunning.load() || g_joinInProgress.exchange(true)) {
				CopyError(a_errorBuf, a_errorBufSize, "A server join is already in progress.");
				return false;
			}
			g_voluntaryExitRequested.store(false);

			const auto* taskInterface = F4SE::GetTaskInterface();
			if (!taskInterface) {
				g_joinInProgress.store(false);
				CopyError(a_errorBuf, a_errorBufSize, "Game task interface is unavailable.");
				return false;
			}

			const auto joinGeneration = g_joinGeneration.fetch_add(1) + 1;
			const auto attemptToken = g_connectionTokenCounter.fetch_add(1) + 1;
			const auto port = a_request->port > 0 ? a_request->port : static_cast<std::uint16_t>(7777);
			const std::string host{ a_request->host };
			const std::string name{ a_request->name ? a_request->name : "" };
			const std::string id{ a_request->id ? a_request->id : "" };
			F4T::DebugLogging::Log(
				"UIClient",
				"join requested host={}:{} generation={} id={} name={}",
				F4T::DebugLogging::SanitizeLogText(host, 128),
				port,
				joinGeneration,
				F4T::DebugLogging::SanitizeLogText(id, 96),
				F4T::DebugLogging::SanitizeLogText(name, 96));
			if (g_joinWorker) {
				g_joinWorker->join();
				delete g_joinWorker;
				g_joinWorker = nullptr;
			}
			g_joinWorkerRunning.store(true);
			auto worker = [host, port, name, id, joinGeneration, attemptToken, a_onComplete, a_userData, taskInterface, serverPassword = std::move(a_serverPassword)]() {
				std::string connectionError;
				std::optional<F4T::Networking::ConnectFailure> connectionFailure;
				const bool connected = F4T::Networking::ConnectAndValidateServer(
					host.c_str(), port, connectionError, &connectionFailure, serverPassword, id);
				if (connected) {
					g_connectionOwnerToken.store(attemptToken);
				}
				F4T::DebugLogging::Log(
					"UIClient",
					"join handshake host={}:{} connected={} error={}",
					F4T::DebugLogging::SanitizeLogText(host, 128),
					port,
					connected,
					connectionError.empty() ? std::string{ "<none>" } : F4T::DebugLogging::SanitizeLogText(connectionError));
				taskInterface->AddTask([host, port, name, id, serverPassword, a_onComplete, a_userData, joinGeneration, attemptToken, connected,
					connectionError = std::move(connectionError), connectionFailure = std::move(connectionFailure)]() mutable {
					if (g_joinGeneration.load() != joinGeneration) {
						if (connected && g_connectionOwnerToken.load() == attemptToken) {
							F4T::Networking::DisconnectFromLocalServer();
							g_connectionOwnerToken.store(0);
						}
						g_joinInProgress.store(false);
						if (a_onComplete) {
							a_onComplete(
								false,
								connected ? "Connection cancelled." : connectionError.c_str(),
								a_userData);
						}
						return;
					}
					if (!connected) {
						g_joinInProgress.store(false);
						const auto passwordMessage = connectionFailure ?
							PasswordRejectionMessage(connectionFailure->code) : std::string_view{};
						if (!passwordMessage.empty()) {
							connectionError = std::string{ passwordMessage };
						} else if (connectionFailure &&
							connectionFailure->kind == F4T::Networking::ConnectFailureKind::SessionRejected) {
							const auto serverKey = F4T::ServerProfileStore::ProfileServerKey(host, port, id);
							if (connectionFailure->code == "modpack_mismatch") {
								F4T::ModpackAutoFixClient::NoteEndpoint(serverKey, host, port, id, name, serverPassword);
							}
							const auto* modpackUrl =
								connectionFailure->modpackUrl.empty() ? nullptr : connectionFailure->modpackUrl.c_str();
							ShowHandshakeRejectionNotification(
								connectionFailure->code.c_str(), connectionFailure->detail.c_str(), serverKey.c_str(), modpackUrl);
							if (connectionError.empty()) {
								connectionError = connectionFailure->detail;
							}
						}
						if (a_onComplete) {
							a_onComplete(false, connectionError.c_str(), a_userData);
						}
						return;
					}
					if (!F4T::Networking::IsConnectedToServer()) {
						auto ownerToken = attemptToken;
						g_connectionOwnerToken.compare_exchange_strong(ownerToken, 0);
						g_joinInProgress.store(false);
						if (a_onComplete) {
							a_onComplete(false, "Connection ended before the join could complete.", a_userData);
						}
						return;
					}

					std::string launchError;
					F4T::SessionLaunch::JoinRequest launchRequest{ id, name, host, port, serverPassword };

					auto deferredCompletion = [joinGeneration, attemptToken, a_onComplete, a_userData](
						bool a_success,
						std::string_view a_reason) {
						if (g_joinGeneration.load() != joinGeneration) {
							F4T::DebugLogging::Log("UIClient", "dropped stale join completion generation={}", joinGeneration);
							return;
						}
						if (!a_success && g_connectionOwnerToken.load() == attemptToken && !F4T::SessionSaveGuard::IsSessionExitPending()) {
							F4T::Networking::DisconnectFromLocalServer();
							g_connectionOwnerToken.store(0);
						}
						g_joinInProgress.store(false);
						if (a_onComplete) {
							const std::string reason{ a_reason };
							a_onComplete(a_success, a_success ? "" : reason.c_str(), a_userData);
						}
					};

					const auto outcome = F4T::SessionLaunch::LaunchValidatedSession(
						launchRequest,
						launchError,
						std::move(deferredCompletion));
					const bool ok = outcome != F4T::SessionLaunch::LaunchOutcome::Failed;
					const bool pending = outcome == F4T::SessionLaunch::LaunchOutcome::Pending;
					F4T::DebugLogging::Log(
						"UIClient",
						"session launch generation={} ok={} pending={} error={}",
						joinGeneration,
						ok,
						pending,
						launchError.empty() ? std::string{ "<none>" } : F4T::DebugLogging::SanitizeLogText(launchError));

					if (pending) {
						return;
					}
					if (outcome == F4T::SessionLaunch::LaunchOutcome::Complete) {
						const bool superseded = g_joinGeneration.load() != joinGeneration;
						if (superseded || !F4T::Networking::IsConnectedToServer()) {
							if (g_connectionOwnerToken.load() == attemptToken) {
								F4T::Networking::DisconnectFromLocalServer();
								g_connectionOwnerToken.store(0);
							}
							g_joinInProgress.store(false);
							if (a_onComplete) {
								a_onComplete(
									false,
									superseded ? "Connection cancelled." : "Connection ended before the join could complete.",
									a_userData);
							}
							return;
						}
					}
					if (!ok) {
						F4T::Networking::DisconnectFromLocalServer();
						g_connectionOwnerToken.store(0);
					}
					g_joinInProgress.store(false);
					if (a_onComplete) {
						a_onComplete(ok, ok ? "" : launchError.c_str(), a_userData);
					}
				});
				g_joinWorkerRunning.store(false);
			};
			try {
				g_joinWorker = new std::thread(std::move(worker));
			} catch (const std::exception& a_error) {
				g_joinWorkerRunning.store(false);
				g_joinInProgress.store(false);
				F4T::DebugLogging::Log("UIClient", "join worker thread creation failed error={}", a_error.what());
				CopyError(a_errorBuf, a_errorBufSize, "Failed to start the server join worker.");
				return false;
			}

			return true;
		}

		bool Callback_JoinSession(const COMMONWEALTH_ONLINE_UI_API::JoinRequest* a_request,
			COMMONWEALTH_ONLINE_UI_API::JoinCompleteFn a_onComplete, void* a_userData, char* a_errorBuf, std::size_t a_errorBufSize)
		{
			return Callback_JoinSession(a_request, std::nullopt, a_onComplete, a_userData, a_errorBuf, a_errorBufSize);
		}

		bool Callback_PasswordJoin(const COMMONWEALTH_ONLINE_UI_API::JoinRequest* a_request, const char* a_password,
			COMMONWEALTH_ONLINE_UI_API::JoinCompleteFn a_onComplete, void* a_userData, char* a_errorBuf, std::size_t a_errorBufSize)
		{
			auto password = a_password ? std::optional<std::string>{ a_password } : std::nullopt;
			return Callback_JoinSession(a_request, std::move(password), a_onComplete, a_userData, a_errorBuf, a_errorBufSize);
		}

		void DisconnectNow();

		void Callback_Disconnect()
		{
			const auto* taskInterface = F4SE::GetTaskInterface();
			if (!taskInterface) {
				F4T::DebugLogging::Log("UIClient", "disconnect dropped: game task interface unavailable");
				return;
			}
			F4T::DebugLogging::Log("UIClient", "[RT-VERIFY 410-D] disconnect marshalled to game task");
			taskInterface->AddTask([]() {
				F4T::TextEntryMode::Reset();
				F4T::DebugLogging::Log("UIClient", "disconnect requested");
				if (g_joinInProgress.load() && !F4T::Networking::IsConnectedToServer()) {
					F4T::DebugLogging::Log("UIClient", "disconnect during a join before connect: no CO exit save [RT-VERIFY 410-D in-world]");
					DisconnectNow();
					return;
				}
				F4T::DebugLogging::Log("UIClient", "disconnect routed through the session exit save guard, join in progress {} [RT-VERIFY 410-D in-world]", g_joinInProgress.load());
				F4T::SessionSaveGuard::RunOrDeferSessionExit("disconnect requested", DisconnectNow);
			});
		}

		void DisconnectNow()
		{
			g_voluntaryExitRequested.store(true);
			F4T::SteamSession::CancelJoins();
			F4T::TextEntryMode::Reset();
			const bool cancelledPendingLaunch = F4T::SessionLaunch::CancelPendingJoin("Connection cancelled.");
			const bool cancelledDeferredLaunch = F4T::CharacterSlotsClient::CancelDeferredLaunch();
			g_joinGeneration.fetch_add(1);
			if (cancelledPendingLaunch || cancelledDeferredLaunch) {
				g_joinInProgress.store(false);
			}
			g_connectionOwnerToken.store(0);
			F4T::Networking::DisconnectFromLocalServer();
			PushConnectionStatus();
		}

		bool Callback_ConnectLocal(char* a_errorBuf, std::size_t a_errorBufSize)
		{
			if (g_joinInProgress.load()) {
				CopyError(a_errorBuf, a_errorBufSize, "A server join is already in progress.");
				return false;
			}

			if (F4T::SessionSaveGuard::IsSessionExitPending()) {
				F4T::DebugLogging::Log("UIClient", "local connect refused: session exit save pending");
				CopyError(a_errorBuf, a_errorBufSize, "Saving the current session before disconnect; try again shortly.");
				return false;
			}
			std::string error;
			if (!F4T::Networking::ConnectToLocalServer(error)) {
				CopyError(a_errorBuf, a_errorBufSize, error);
				return false;
			}
			g_voluntaryExitRequested.store(false);
			g_connectionOwnerToken.store(g_connectionTokenCounter.fetch_add(1) + 1);
			return true;
		}

		bool Callback_IsConnected()
		{
			return F4T::Networking::IsConnectedToServer();
		}

		bool Callback_ScanLocalServers(
			char*       a_jsonOut,
			std::size_t a_jsonOutSize,
			char*       a_errorBuf,
			std::size_t a_errorBufSize)
		{
			if (!a_jsonOut || a_jsonOutSize == 0) {
				CopyError(a_errorBuf, a_errorBufSize, "JSON output buffer is missing.");
				return false;
			}

			std::string error;
			const auto discovered = F4T::LanDiscovery::Scan(error);
			F4T::DebugLogging::Log(
				"UIClient",
				"LAN scan complete servers={} error={}",
				discovered.size(),
				error.empty() ? std::string{ "<none>" } : F4T::DebugLogging::SanitizeLogText(error));

			nlohmann::json servers = nlohmann::json::array();
			for (const auto& server : discovered) {
				servers.push_back(nlohmann::json{
					{ "id", std::format("local:{}:{}", server.host, server.port) },
					{ "name", server.name },
					{ "host", server.host },
					{ "port", server.port },
					{ "region", "Local" },
					{ "worldspace", "Commonwealth" },
					{ "mode", "Co-op" },
					{ "players", server.players },
					{ "maxPlayers", server.maxPlayers },
					{ "ping", server.ping },
					{ "status", server.players >= server.maxPlayers ? "Full" : "Open" },
					{ "modded", false },
					{ "passworded", false },
					{ "favorite", false },
					{ "description", server.description }
				});
			}

			const auto dumped = servers.dump();
			if (dumped.size() + 1 > a_jsonOutSize) {
				CopyError(a_errorBuf, a_errorBufSize, "LAN scan JSON exceeds output buffer.");
				return false;
			}

			std::memcpy(a_jsonOut, dumped.c_str(), dumped.size() + 1);

			if (discovered.empty() && !error.empty()) {
				CopyError(a_errorBuf, a_errorBufSize, error);
			}

			return true;
		}

		bool Callback_GetRemotePlayersMapJson(
			char*       a_jsonOut,
			std::size_t a_jsonOutSize,
			char*       a_errorBuf,
			std::size_t a_errorBufSize)
		{
			if (!a_jsonOut || a_jsonOutSize == 0) {
				CopyError(a_errorBuf, a_errorBufSize, "JSON output buffer is missing.");
				return false;
			}

			nlohmann::json players = nlohmann::json::array();
			for (const auto& remote : F4T::RemotePlayerState::GetRemotePlayerSnapshot()) {
				nlohmann::json player{
					{ "id", remote.playerId },
					{ "x", remote.x },
					{ "y", remote.y },
					{ "z", remote.z },
					{ "angleZ", remote.angleZ },
					{ "worldspaceId", remote.worldspaceId },
					{ "cellId", remote.cellId },
				};
				if (const auto memberName = F4T::Factions::MemberName(remote.playerId)) {
					player["faction"] = true;
					player["name"] = *memberName;
				}
				players.push_back(std::move(player));
			}

			const auto dumped = players.dump();
			if (dumped.size() + 1 > a_jsonOutSize) {
				CopyError(a_errorBuf, a_errorBufSize, "Remote players map JSON exceeds output buffer.");
				return false;
			}

			std::memcpy(a_jsonOut, dumped.c_str(), dumped.size() + 1);
			return true;
		}

		bool Callback_GetFactionStateJson(
			char*       a_jsonOut,
			std::size_t a_jsonOutSize,
			char*       a_errorBuf,
			std::size_t a_errorBufSize)
		{
			if (!a_jsonOut || a_jsonOutSize == 0) {
				CopyError(a_errorBuf, a_errorBufSize, "JSON output buffer is missing.");
				return false;
			}

			try {
				const auto state = F4T::CharacterSlotsClient::WithCharacterSlots(F4T::SettlementCaptureClient::WithCaptureState(F4T::SettlementAccessClient::WithBuildAccess(F4T::Factions::BuildStateJson())));
				if (state.size() + 1 > a_jsonOutSize) {
					CopyError(a_errorBuf, a_errorBufSize, "Faction state JSON exceeds output buffer.");
					return false;
				}

				std::memcpy(a_jsonOut, state.c_str(), state.size() + 1);
				return true;
			} catch (...) {
				CopyError(a_errorBuf, a_errorBufSize, "Faction state JSON could not be built.");
				return false;
			}
		}

		void Callback_SendFactionAction(const char* a_actionJson)
		{
			if (!a_actionJson) {
				return;
			}
			try {
				if (F4T::CharacterSlotsClient::HandleAction(a_actionJson)) return;
				if (F4T::SettlementAccessClient::HandleAction(a_actionJson)) {
					return;
				}
				if (F4T::SettlementCaptureClient::HandleAction(a_actionJson)) {
					return;
				}
				F4T::Factions::HandleAction(a_actionJson);
			} catch (...) {
				REX::WARN("{} Dropped a faction action after an internal error.", GetLogPrefix());
			}
		}

		const COMMONWEALTH_ONLINE_UI_API::FactionCallbacks kFactionCallbacks{
			Callback_GetFactionStateJson,
			Callback_SendFactionAction
		};

		void Callback_SendChatAction(const char* a_actionJson)
		{
			if (!a_actionJson) {
				return;
			}
			try {
				F4T::Chat::HandleAction(a_actionJson);
			} catch (...) {
				REX::WARN("{} Dropped a chat action after an internal error.", GetLogPrefix());
			}
		}

		const COMMONWEALTH_ONLINE_UI_API::ChatCallbacks kChatCallbacks{
			Callback_SendChatAction
		};

		const COMMONWEALTH_ONLINE_UI_API::SessionCallbacks kSessionCallbacks{
			Callback_JoinSession,
			Callback_Disconnect,
			Callback_ConnectLocal,
			Callback_IsConnected,
			Callback_ScanLocalServers
		};
	}

	bool RejoinServer(const char* a_host, std::uint16_t a_port, const char* a_name, const char* a_id)
	{
		COMMONWEALTH_ONLINE_UI_API::JoinRequest request{};
		request.id = a_id;
		request.name = a_name;
		request.host = a_host;
		request.port = a_port;
		char error[256]{};
		return Callback_JoinSession(&request, std::nullopt, nullptr, nullptr, error, sizeof(error));
	}

	void OnGameDataReady()
	{
		g_ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
			COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
		if (!g_ui) {
			REX::WARN(
				"{} CommonwealthOnlineUI.dll API not found. Install CommonwealthOnlineUI.dll "
				"(and PrismaUI_F4) for the custom UI.",
				GetLogPrefix());
			return;
		}

		if (g_ui->SetSessionCallbacks) {
			g_ui->SetSessionCallbacks(&kSessionCallbacks);
		}

		if (g_ui->SetRemotePlayersMapJsonCallback) {
			g_ui->SetRemotePlayersMapJsonCallback(Callback_GetRemotePlayersMapJson);
		}

		if (auto* uiPassword = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
				COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI3>();
			uiPassword && uiPassword->SetPasswordJoinCallback) {
			uiPassword->SetPasswordJoinCallback(Callback_PasswordJoin);
		}

		g_uiWorld = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
			COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI5>();
		F4T::SteamSession::Attach();

		g_uiFactions = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
			COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI3>();
		if (g_uiFactions && g_uiFactions->SetFactionCallbacks) {
			g_uiFactions->SetFactionCallbacks(&kFactionCallbacks);
		}

		auto* uiChat = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
			COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI3>();
		if (uiChat && uiChat->SetChatCallbacks) {
			uiChat->SetChatCallbacks(&kChatCallbacks);
		}

		F4T::VoiceUi::Register();

		if (g_ui->OnGameDataReady) {
			g_ui->OnGameDataReady();
		}

		F4T::SteamSession::FireLaunchJoin();

		REX::INFO("{} CommonwealthOnlineUI API ready.", GetLogPrefix());
	}

	void ShowMainMenu()
	{
		if (g_ui && g_ui->ShowMainMenu) {
			g_ui->ShowMainMenu();
		}
	}

	void HideMainMenu()
	{
		if (g_ui && g_ui->HideMainMenu) {
			g_ui->HideMainMenu();
		}
	}

	bool IsMainMenuVisible()
	{
		return g_ui && g_ui->IsMainMenuVisible && g_ui->IsMainMenuVisible();
	}

	void ShowServerBrowser()
	{
		if (g_ui && g_ui->ShowServerBrowser) {
			g_ui->ShowServerBrowser();
		}
	}

	void HideServerBrowser()
	{
		if (g_ui && g_ui->HideServerBrowser) {
			g_ui->HideServerBrowser();
		}
	}

	bool IsBrowserVisible()
	{
		return g_ui && g_ui->IsBrowserVisible && g_ui->IsBrowserVisible();
	}

	void DispatchMainMenuControllerInput(const char* a_button, const char* a_phase)
	{
		if (g_ui && g_ui->DispatchMainMenuControllerInput) {
			g_ui->DispatchMainMenuControllerInput(a_button, a_phase);
		}
	}

	bool IsCustomMenuInputCaptured()
	{
		return g_ui && g_ui->IsCustomMenuInputCaptured && g_ui->IsCustomMenuInputCaptured();
	}

	void PushConnectionStatus(const char* a_status)
	{
		if (g_ui && g_ui->PushConnectionStatus) {
			g_ui->PushConnectionStatus(a_status);
		}
	}

	void PushWorldMode(const char* a_mode)
	{
		const std::string mode = a_mode ? a_mode : "";
		F4T::DebugLogging::Log("UIClient", "world mode {}", mode);
		if (g_uiWorld && g_uiWorld->SetWorldMode) {
			g_uiWorld->SetWorldMode(mode.c_str());
		}
	}

	void NotifyFactionStateChanged()
	{
		if (g_uiFactions && g_uiFactions->NotifyFactionStateChanged) {
			g_uiFactions->NotifyFactionStateChanged();
		}
	}

	void ResetConnectionOwnership()
	{
		g_connectionOwnerToken.store(0);
	}

	void ClearJoinInProgress()
	{
		g_joinInProgress.store(false);
	}

	void NotifyTransportDisconnected(const char* a_reason)
	{
		F4T::SteamSession::StopTunnels();
		g_connectionOwnerToken.store(0);
		F4T::Factions::Reset();
		F4T::SettlementAccessClient::Reset();
		F4T::SettlementCaptureClient::Reset();
		F4T::CharacterSlotsClient::Reset();
		F4T::Chat::Reset();

		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			if (F4T::CharacterSlotsClient::CancelDeferredLaunch()) {
				g_joinInProgress.store(false);
			}
			return;
		}

		const std::string reasonCopy = a_reason ? a_reason : "Connection lost.";
		taskInterface->AddTask([reasonCopy]() {
			F4T::TextEntryMode::Reset();
			const bool cancelledPendingLaunch = F4T::SessionLaunch::CancelPendingJoin(reasonCopy);
			const bool cancelledDeferredLaunch = F4T::CharacterSlotsClient::CancelDeferredLaunch();
			g_joinGeneration.fetch_add(1);
			if (cancelledPendingLaunch || cancelledDeferredLaunch) {
				g_joinInProgress.store(false);
			}
			PushConnectionStatus();
			ShowDisconnectScreen("disconnected", reasonCopy.c_str());
		});
	}

	bool IsVoluntaryExitRequested()
	{
		return g_voluntaryExitRequested.load();
	}

	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1* GetCachedUI()
	{
		return g_ui;
	}

	void ShowOfflineSaveIgnoredNotification()
	{
		if (!g_ui || !g_ui->ShowAlert) {
			REX::WARN(
				"{} Cannot show offline save notification because the alert API is unavailable.",
				GetLogPrefix());
			return;
		}

		COMMONWEALTH_ONLINE_UI_API::AlertRequest request{};
		request.id = "offline-save-ignored";
		request.category = "save";
		request.title = "Offline Save Ignored";
		request.message = "A newer offline save was ignored for this server.";
		request.tone = "warning";
		request.durationMs = 12000;
		if (!g_ui->ShowAlert(&request, nullptr, nullptr, nullptr, 0)) {
			REX::WARN("{} Failed to show offline save notification.", GetLogPrefix());
		}
	}

	void ShowUnverifiedSaveNotification()
	{
		if (!g_ui || !g_ui->ShowAlert) {
			REX::WARN(
				"{} Cannot show unverified save notification because the alert API is unavailable.",
				GetLogPrefix());
			return;
		}

		COMMONWEALTH_ONLINE_UI_API::AlertRequest request{};
		request.id = "unverified-save";
		request.category = "save";
		request.title = "No verified multiplayer save";
		request.message = "Your newest save was not made on this server and no earlier server save matches. Loading it anyway.";
		request.tone = "warning";
		request.durationMs = 12000;
		if (!g_ui->ShowAlert(&request, nullptr, nullptr, nullptr, 0)) {
			REX::WARN("{} Failed to show unverified save notification.", GetLogPrefix());
		}
	}

	bool BeginJoin(const COMMONWEALTH_ONLINE_UI_API::JoinRequest* a_request, std::optional<std::string> a_serverPassword,
		COMMONWEALTH_ONLINE_UI_API::JoinCompleteFn a_onComplete, void* a_userData, char* a_errorBuf, std::size_t a_errorBufSize)
	{
		return Callback_JoinSession(a_request, std::move(a_serverPassword), a_onComplete, a_userData, a_errorBuf, a_errorBufSize);
	}
	void DisconnectFromSession()
	{
		Callback_Disconnect();
		F4T::SteamSession::StopTunnels();
	}
}

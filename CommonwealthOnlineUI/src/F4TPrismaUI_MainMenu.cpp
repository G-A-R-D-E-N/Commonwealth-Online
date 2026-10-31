#include "F4TPrismaUIInternal.h"
#include "F4TExitSaveGate.h"
#include "F4TVoiceBridge.h"
#include "F4TWorldMode.h"
#include "F4TServerSettingsBridge.h"

namespace F4T::PrismaUI::Internal
{
	void BindMainMenuDomHandlers(PrismaView a_view)
	{
		if (g_mainMenuBridgeReady.exchange(true)) return;
		g_api->RegisterConsoleCallback(
			a_view,
			[](PrismaView, PRISMA_UI_API::ConsoleMessageLevel a_level, const char* a_message) {
				if (!a_message) {
					return;
				}

				const auto prefix = GetLogPrefix() + " [MainMenu JS]";
				switch (a_level) {
				case PRISMA_UI_API::ConsoleMessageLevel::Error:
					REX::CRITICAL("{} {}", prefix, a_message);
					break;
				case PRISMA_UI_API::ConsoleMessageLevel::Warning:
					REX::WARN("{} {}", prefix, a_message);
					break;
				default:
					REX::INFO("{} {}", prefix, a_message);
					break;
				}
			});

		F4T::MainMenu::MainMenuBridge::Initialize();

		F4T::ServerBrowser::ServerBrowserBridge::Initialize(g_api, a_view);
		F4T::ServerBrowser::ServerBrowserBridge::PushUITheme();

		// Constraint: coAction is bound before settings init; a settings failure must not leave JS without a bridge.
		g_api->BindUIEvent(a_view, "coAction", [](const char* a_json) {
			if (!a_json) {
				return;
			}

			try {
				const auto action = nlohmann::json::parse(a_json);
				const auto actionName = action.value("action", "");

				if (actionName == "openView") {
					const auto viewName = action.value("view", "");
					if (viewName == "multiplayer") {
						ActivateEmbeddedBrowser("coAction:openView:multiplayer");
						return;
					}
				} else if (actionName == "closeBrowser") {
					HideServerBrowserInternal("coAction:closeBrowser");
					return;
				} else if (actionName == "openGameSettings") {
					LogInfo("openGameSettings requested from main menu (legacy). Prefer native settings API.");
					F4T::MainMenuVanillaSettings::OpenGameSettings();
					return;
				} else if (actionName == "quit") {
					const auto* taskInterface = F4SE::GetTaskInterface();
					if (!taskInterface) {
						LogWarning("Game task interface unavailable; cannot run main-menu quit.");
						return;
					}
					LogInfo("[MainMenu] quit marshalled to the game task");
					taskInterface->AddTask([]() {
						RequestQuitFromMainMenu();
					});
					return;
				} else if (actionName == "singlePlayer") {
					RequestSinglePlayerFromMainMenu();
					return;
				}

				if (actionName.starts_with("settings")) {
					F4T::Settings::SettingsBridge::HandleAction(action, g_mainMenuState.GetHandle());
					return;
				}

				if (actionName.starts_with("serverSettings")) {
					F4T::ServerSettings::HandleAction(action);
					return;
				}

				if (!actionName.empty()) {
					F4T::ServerBrowser::ServerBrowserBridge::HandleAction(action);
				}
			} catch (const std::exception& ex) {
				REX::WARN("{} [JS] Failed to parse coAction JSON: {}", GetLogPrefix(), ex.what());
			}
		});

		g_api->BindUIEvent(a_view, "coSettingsAction", [](const char* a_json) {
			if (!a_json) {
				return;
			}
			try {
				const auto action = nlohmann::json::parse(a_json);
				F4T::Settings::SettingsBridge::HandleAction(action, g_mainMenuState.GetHandle());
			} catch (const std::exception& ex) {
				REX::WARN("{} [JS] Failed to parse coSettingsAction JSON: {}", GetLogPrefix(), ex.what());
			}
		});

		F4T::Settings::SettingsBridge::Initialize(g_api);

		g_api->BindUIEvent(a_view, "closeBrowser", [](const char*) {
			HideServerBrowserInternal("BindUIEvent:closeBrowser");
		});

		g_api->BindUIEvent(a_view, "connectServer", [](const char*) {
			if (!COUI::Session::IsConnected()) {
				std::string error;
				if (!COUI::Session::ConnectLocal(error)) {
					REX::WARN("{} ConnectLocal failed: {}", COUI::Session::GetLogPrefix(), error);
				}
			}
			F4T::ServerBrowser::ServerBrowserBridge::PushConnectionStatus();
		});

		g_api->BindUIEvent(a_view, "disconnectServer", [](const char*) {
			const auto* taskInterface = F4SE::GetTaskInterface();
			if (!taskInterface) {
				LogWarning("Game task interface unavailable; cannot disconnect from server.");
				return;
			}

			taskInterface->AddTask([]() {
				COUI::Session::Disconnect();
				F4T::ServerBrowser::ServerBrowserBridge::PushConnectionStatus();
			});
		});

		F4T::ServerBrowser::ServerBrowserBridge::PushConnectionStatus();
	}

	void OnMainMenuDomReady(PrismaView a_view)
	{
		// Marshal onto the game thread; reject stale handles after recreate.
		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			if (!g_mainMenuState.MarkDomReady(a_view) || !g_api) {
				return;
			}
			TryFinalizeMainMenu();
			return;
		}

		taskInterface->AddTask([a_view]() {
			if (!g_mainMenuState.MarkDomReady(a_view) || !g_api) {
				return;
			}
			TryFinalizeMainMenu();
			F4T::VoiceBridge::AttachView(F4T::VoiceBridge::Surface::MainMenu, a_view);
		});
	}

	void CreateMainMenuView()
	{
		if (!IsMenuRuntimeReady()) return;
		const auto previousHandle = g_mainMenuState.GetHandle();
		const auto handle =
			g_mainMenuState.EnsureCreated(kMainMenuHtmlPath, OnMainMenuDomReady, g_prismaReady);
		if (handle != 0 && handle != previousHandle) {
			g_mainMenuBridgeReady = false;
			LogInfo(std::format(
				"[MainMenu] View created/recreated oldHandle={} newHandle={}; bridge readiness reset",
				previousHandle,
				handle));
		}
	}

	void ShowMainMenuInternal(std::string_view a_reason)
	{
		LogInfo(std::format("[MainMenu] Show requested reason={}", a_reason));
		ReconcileMainMenuOverlayState(a_reason);
	}
}

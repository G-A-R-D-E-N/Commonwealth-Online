#include "F4TPrismaUIInternal.h"
#include "F4TExitSaveGate.h"
#include "F4TVoiceBridge.h"
#include "F4TWorldMode.h"
#include "F4TServerSettingsBridge.h"


namespace F4T::PrismaUI::Internal
{
	F4T::ExitSave::Gate g_exitSaveGate;

	enum class PendingSessionQuit
	{
		kNone,
		kDesktop,
		kMainMenu
	};
	PendingSessionQuit g_pendingSessionQuit = PendingSessionQuit::kNone;
	bool g_pendingMainMenuWaitLogged = false;
	ULONGLONG g_pauseMenuQuitStartedAt = 0;

	bool ApplyPauseMenuState(std::string_view a_reason)
	{
		const auto result = g_pauseMenuState.ApplyState(a_reason);
		if (result.showRequested) {
			F4T::MenuInput::SetPauseMenuActive(true);
			F4T::PauseMenuInject::SetPauseMenuInputSuppressed(true);
			F4T::Alerts::SetMenuInteractionActive(true);
			if (!g_sessionLockoutActive) {
				SetVanillaPauseMenuSuppressed(true);
			}

			InitializeBrowserPointerMode();
			ApplyBrowserPointerMode(!g_menuPreferGamepad);

			if (g_api && g_pauseMenuState.IsHandleValid() && !g_sessionLockoutActive) {
				g_api->Invoke(
					g_pauseMenuState.GetHandle(),
					"if(window.onPauseMenuShown){window.onPauseMenuShown();}");
				F4T::ServerBrowser::ServerBrowserBridge::PushCurrentServerInfo();
				COUI::WorldMode::Push();
			}

			LogInfo(g_sessionLockoutActive
				? "Session lockout overlay shown via ApplyState."
				: "Pause menu overlay shown.");
		}

		if (result.hideRequested) {
			F4T::MenuInput::SetPauseMenuActive(false);
			F4T::PauseMenuInject::SetPauseMenuInputSuppressed(false);
			F4T::Alerts::SetMenuInteractionActive(
				g_mainMenuState.IsCurrentlyVisible() || g_browserCurrentlyVisible);
			SetVanillaPauseMenuSuppressed(false);
			F4T::Alerts::ClearPointerFocus("PauseMenuHidden");
			F4T::FactionView::Close("PauseMenuHidden");
			F4T::QuestView::Close("PauseMenuHidden");
			RequestNativeCursorRestore();
		}

		return result.applied;
	}

	void HideMainMenuInternal(std::string_view a_reason)
	{
		LogInfo(std::format("[MainMenu] Hide requested reason={}", a_reason));
		if (g_browserDesiredVisible || g_browserCurrentlyVisible) {
			DeactivateEmbeddedBrowser(std::string(a_reason) + ":withMainMenu");
		}

		g_mainMenuState.SetDesired(false, false);
		ApplyMainMenuState(a_reason);
	}

	void HidePauseMenuInternal(std::string_view a_reason)
	{
		if (g_sessionLockoutActive) {
			// Ban/kick lockout cannot be dismissed by pause-menu close paths.
			return;
		}

		LogInfo(std::format("[PauseMenu] Hide requested reason={}", a_reason));
		F4T::PauseMenuMap::StopStreaming();
		g_pauseMenuQuitInProgress = false;
		g_pauseMenuQuitStartedAt = 0;
		if (g_pendingSessionQuit == PendingSessionQuit::kMainMenu) {
			LogInfo(std::format("[PauseMenu] Pending main-menu quit cancelled by pause-menu close reason={} [RT-VERIFY 410-D]", a_reason));
			g_pendingSessionQuit = PendingSessionQuit::kNone;
		}
		g_pauseMenuState.SetDesired(false, false);
		g_pauseMenuState.SetFocusArgs(false, false);
		ApplyPauseMenuState(a_reason);
	}

	void RequestResumeFromPauseMenu()
	{
		if (g_pauseMenuQuitInProgress || g_sessionLockoutActive) {
			return;
		}

		LogInfo("Resume requested from pause menu.");

		F4T::PauseMenuMap::StopStreaming();

		// Hide our overlay immediately, but keep vanilla PauseMenu suppressed until the
		// engine closes it — otherwise the SWF can flash for a frame before kHide lands.
		g_pauseMenuState.SetDesired(false, false);
		g_pauseMenuState.SetFocusArgs(false, false);
		const auto result = g_pauseMenuState.ApplyState("ResumeFromPauseMenu");
		F4T::FactionView::QueueClose("ResumeFromPauseMenu");
		F4T::QuestView::QueueClose("ResumeFromPauseMenu");
		if (result.hideRequested || result.unfocusRequested) {
			F4T::MenuInput::SetPauseMenuActive(false);
			F4T::Alerts::SetMenuInteractionActive(
				g_mainMenuState.IsCurrentlyVisible() || g_browserCurrentlyVisible);
			F4T::Alerts::ClearPointerFocus("ResumeFromPauseMenu");
		}
		RequestNativeCursorRestore();

		auto* queue = RE::UIMessageQueue::GetSingleton();
		if (!queue) {
			LogWarning("UIMessageQueue unavailable; cannot close PauseMenu.");
			SetVanillaPauseMenuSuppressed(false);
			return;
		}

		queue->AddMessage(RE::PauseMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide);
	}

	void QueueDesktopQuit()
	{
		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			LogWarning("Game task interface unavailable; cannot queue desktop quit.");
			return;
		}

		taskInterface->AddTask([]() {
			auto* main = RE::Main::GetSingleton();
			if (!main) {
				REX::WARN("{} Main singleton unavailable; cannot quit game.", GetLogPrefix());
				return;
			}

			main->quitGame = true;
		});
	}

	void ClearSessionLockoutOverlay()
	{
		if (!g_sessionLockoutActive) {
			return;
		}

		g_sessionLockoutActive = false;
		g_sessionLockoutPending = false;
		if (g_api && g_pauseMenuState.IsHandleValid() && g_pauseMenuState.IsDomReady()) {
			g_api->Invoke(
				g_pauseMenuState.GetHandle(),
				"if(window.hideSessionLockout){window.hideSessionLockout();}");
		}
		LogInfo("Session lockout cleared; the main-menu quit owns the PauseMenu now.");
	}

	void BeginPauseMenuQuit(bool a_toMainMenu)
	{
		if (g_pauseMenuQuitInProgress) {
			return;
		}

		F4T::PauseMenuMap::StopStreaming();

		LogInfo(a_toMainMenu
			? "Quit to main menu requested from pause menu."
			: "Quit to desktop requested from pause menu.");

		g_pauseMenuQuitInProgress = true;
		g_pauseMenuQuitStartedAt = GetTickCount64();

		// Keep our overlay up to cover any vanilla flash during the transition.
		// HidePauseMenuInternal runs when PauseMenu actually closes.
		if (g_api && g_pauseMenuState.IsHandleValid()) {
			g_api->Invoke(
				g_pauseMenuState.GetHandle(),
				"if(window.onPauseMenuQuitStarted){window.onPauseMenuQuitStarted();}");
		}

		auto* ui = RE::UI::GetSingleton();
		auto pauseMenu = ui ? ui->GetMenu<RE::PauseMenu>() : nullptr;

		if (a_toMainMenu) {
			if (!pauseMenu) {
				LogWarning("PauseMenu instance unavailable; cannot queue quit to main menu.");
				g_pauseMenuQuitInProgress = false;
				g_pauseMenuQuitStartedAt = 0;
				return;
			}

			COUI::Session::Disconnect();
			if (COUI::Session::IsConnected()) {
				g_pendingSessionQuit = PendingSessionQuit::kMainMenu;
				g_pendingMainMenuWaitLogged = false;
				LogInfo("Quit to main menu waits for the session exit save.");
				return;
			}

			auto* main = RE::Main::GetSingleton();
			if (!main) {
				LogWarning("Main singleton unavailable; cannot return to the main menu.");
				g_pauseMenuQuitInProgress = false;
				g_pauseMenuQuitStartedAt = 0;
				return;
			}
			main->resetGame = true;

			g_pauseMenuState.SetDesired(true, true);
			g_pauseMenuState.SetFocusArgs(false, false);
			ApplyPauseMenuState("QuitToMainMenuCover");
			ClearSessionLockoutOverlay();
			LogInfo("Queued return to main menu (Main::resetGame).");
			return;
		}

		auto* saveMgr = RE::BGSSaveLoadManager::GetSingleton();
		const bool inSession = COUI::Session::IsConnected();
		LogInfo(std::format(
			"BeginPauseMenuQuit inputs [RT-VERIFY 410-A]: sessionConnected {}, saveMgr {:p}, savingAllowed {}",
			inSession,
			static_cast<const void*>(saveMgr),
			saveMgr ? saveMgr->savingAllowed : false));
		if (!inSession && saveMgr && saveMgr->savingAllowed) {
			g_exitSaveGate.Arm(GetTickCount64());
			saveMgr->QueueSaveLoadTask(RE::BGSSaveLoadManager::QUEUED_TASK::kSaveAndQuitToDesktop);
			LogInfo("BeginPauseMenuQuit branch offline: exit save gate armed; queued SaveAndQuitToDesktop; disconnect waits for kPostSaveGame.");
			return;
		}
		if (inSession) {
			LogInfo("BeginPauseMenuQuit branch connected: multiplayer session active; the CO exit save is queued and the quit waits for it.");
		} else if (!saveMgr) {
			LogWarning("BeginPauseMenuQuit branch no-save-manager: BGSSaveLoadManager unavailable; quitting without exit save.");
		} else {
			LogWarning("BeginPauseMenuQuit branch offline-blocked: saving is currently disallowed; quitting without exit save.");
		}

		COUI::Session::Disconnect();
		if (COUI::Session::IsConnected()) {
			g_pendingSessionQuit = PendingSessionQuit::kDesktop;
			LogInfo("Quit to desktop waits for the session exit save.");
			return;
		}

		QueueDesktopQuit();
	}

	void NotifyExitSavePosted()
	{
		g_exitSaveGate.OnPostSave(GetTickCount64());
	}

	void FailStuckPauseMenuQuit()
	{
		g_pauseMenuQuitInProgress = false;
		g_pauseMenuQuitStartedAt = 0;
		g_pendingSessionQuit = PendingSessionQuit::kNone;
		LogWarning("Pause menu quit watchdog: no result after 75s; clearing the quit.");
		if (!g_api || !g_pauseMenuState.IsHandleValid() || !g_pauseMenuState.IsDomReady()) {
			return;
		}

		const nlohmann::json message = "Quit did not finish. You are still in the game.";
		const auto script = std::format(
			"if(window.onPauseMenuQuitFailed){{window.onPauseMenuQuitFailed({});}}",
			message.dump());
		g_api->Invoke(g_pauseMenuState.GetHandle(), script.c_str());
	}

	void TickExitSaveDisconnect()
	{
		if (g_pendingSessionQuit != PendingSessionQuit::kNone && !COUI::Session::IsConnected()) {
			auto* ui = RE::UI::GetSingleton();
			auto pauseMenu = ui ? ui->GetMenu<RE::PauseMenu>() : nullptr;
			if (g_pendingSessionQuit == PendingSessionQuit::kMainMenu && !pauseMenu) {
				if (!g_pendingMainMenuWaitLogged) {
					g_pendingMainMenuWaitLogged = true;
					LogWarning("Session exit save settled while the pause menu is closed; main-menu quit waits for the pause menu to open.");
				}
			} else {
				const auto pending = g_pendingSessionQuit;
				g_pendingSessionQuit = PendingSessionQuit::kNone;
				LogInfo("Session exit save settled; continuing the quit.");
				if (pending == PendingSessionQuit::kMainMenu) {
					g_pauseMenuQuitInProgress = false;
					g_pauseMenuQuitStartedAt = 0;
					BeginPauseMenuQuit(true);
				} else {
					QueueDesktopQuit();
				}
			}
		}
		if (g_exitSaveGate.TakeDisconnect(GetTickCount64())) {
			LogInfo(g_exitSaveGate.PostSaveSeen()
				? "Exit save wait finished (post-save grace after kPostSaveGame); disconnecting."
				: "Exit save wait finished (timed out, no kPostSaveGame); disconnecting.");
			COUI::Session::Disconnect();
		}

		if (g_pauseMenuQuitInProgress && g_pauseMenuQuitStartedAt != 0) {
			const auto now = GetTickCount64();
			if (now - g_pauseMenuQuitStartedAt >= 75000) {
				const auto* main = RE::Main::GetSingleton();
				const bool quitHappened = main && (main->quitGame || main->resetGame);
				if (COUI::Session::IsConnected() || !quitHappened) {
					FailStuckPauseMenuQuit();
				}
			}
		}
	}

	void RequestQuitToMainMenuFromPause()
	{
		BeginPauseMenuQuit(true);
	}

	void RequestQuitToDesktopFromPause()
	{
		BeginPauseMenuQuit(false);
	}

	void RequestMainMenuFromSessionLockout()
	{
		if (!g_sessionLockoutActive) {
			return;
		}
		if (g_pendingSessionQuit == PendingSessionQuit::kMainMenu) {
			LogInfo("Return to main menu is already pending; ignoring the repeated lockout request.");
			return;
		}

		auto* queue = RE::UIMessageQueue::GetSingleton();
		if (!queue) {
			LogWarning("UIMessageQueue unavailable; cannot open PauseMenu for the lockout main-menu return.");
			return;
		}

		LogInfo("Return to main menu requested from the session lockout.");
		g_pendingSessionQuit = PendingSessionQuit::kMainMenu;
		g_pendingMainMenuWaitLogged = false;
		queue->AddMessage(RE::PauseMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow);
	}

	void RequestQuitFromMainMenu()
	{
		LogInfo("Quit requested from main menu.");

		HideMainMenuInternal("QuitFromMainMenu");
		COUI::Session::Disconnect();
		if (COUI::Session::IsConnected()) {
			g_pendingSessionQuit = PendingSessionQuit::kDesktop;
			LogInfo("Quit from main menu waits for the session exit save.");
			return;
		}

		QueueDesktopQuit();
	}

	void RequestSinglePlayerFromMainMenu()
	{
		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			LogWarning("Game task interface unavailable; cannot enter single-player mode.");
			return;
		}

		taskInterface->AddTask([]() {
			g_singlePlayerMode = true;
			HideMainMenuInternal("SinglePlayerFromMainMenu");
			ReconcileMainMenuOverlayState("SinglePlayerFromMainMenu");
		});
	}

	void BindPauseMenuDomHandlers(PrismaView a_view)
	{
		g_api->RegisterConsoleCallback(
			a_view,
			[](PrismaView, PRISMA_UI_API::ConsoleMessageLevel a_level, const char* a_message) {
				if (!a_message) {
					return;
				}

				const auto prefix = GetLogPrefix() + " [PauseMenu JS]";
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

		F4T::PauseMenu::PauseMenuBridge::Initialize();
		g_pauseMenuBridgeReady = true;

		g_api->BindUIEvent(a_view, "coAction", [](const char* a_json) {
			if (!a_json) {
				return;
			}

			try {
				const auto action = nlohmann::json::parse(a_json);
				const auto actionName = action.value("action", "");

				if (actionName == "resume" || actionName == "quitToMainMenu" || actionName == "quitToDesktop" || actionName == "lockoutMainMenu") {
					const auto* taskInterface = F4SE::GetTaskInterface();
					if (!taskInterface) {
						LogWarning("Game task interface unavailable; cannot run pause-menu action.");
						return;
					}
					LogInfo(std::format("[PauseMenu] coAction {} marshalled to the game task", actionName));
					taskInterface->AddTask([actionName]() {
						if (actionName == "resume") {
							RequestResumeFromPauseMenu();
						} else if (actionName == "quitToMainMenu") {
							RequestQuitToMainMenuFromPause();
						} else if (actionName == "lockoutMainMenu") {
							RequestMainMenuFromSessionLockout();
						} else {
							RequestQuitToDesktopFromPause();
						}
					});
					return;
				}
				if (actionName == "openView") {
					const auto viewName = action.value("view", "");
					if (viewName == "social") {
						F4T::FactionView::Open("coAction:openView:social");
					} else if (viewName == "quests") {
						F4T::QuestView::Open("coAction:openView:quests");
					}
					return;
				}
				if (actionName == "mapEnter") {
					// Flag only — BindUIEvent may run off the game thread.
					F4T::PauseMenuMap::StartStreaming();
					return;
				}
				if (actionName == "mapExit") {
					F4T::PauseMenuMap::StopStreaming();
					return;
				}
				if (actionName == "fastTravel") {
					const auto markerId = action.value("markerId", "");
					if (!markerId.empty()) {
						F4T::PauseMenuMap::RequestFastTravel(markerId);
					}
					return;
				}

				if (actionName.starts_with("settings")) {
					F4T::Settings::SettingsBridge::HandleAction(action, g_pauseMenuState.GetHandle());
					return;
				}
			} catch (const std::exception& ex) {
				REX::WARN("{} [JS] Failed to parse pause menu coAction JSON: {}", GetLogPrefix(), ex.what());
			}
		});

		g_api->BindUIEvent(a_view, "coSettingsAction", [](const char* a_json) {
			if (!a_json) {
				return;
			}
			try {
				const auto action = nlohmann::json::parse(a_json);
				F4T::Settings::SettingsBridge::HandleAction(action, g_pauseMenuState.GetHandle());
			} catch (const std::exception& ex) {
				REX::WARN("{} [JS] Failed to parse pause menu coSettingsAction JSON: {}", GetLogPrefix(), ex.what());
			}
		});

		F4T::Settings::SettingsBridge::Initialize(g_api);
	}

	void ReconcilePauseMenuAfterDomReady()
	{
		if (g_sessionLockoutPending || g_sessionLockoutActive) {
			ShowSessionLockoutInternal();
			return;
		}

		const auto* ui = RE::UI::GetSingleton();
		const bool pauseOpen = ui && ui->GetMenuOpen<RE::PauseMenu>();
		LogInfo(std::format(
			"[PauseMenu] DomReady reconcile FalloutPauseMenuOpen={} desiredVisible={}",
			pauseOpen,
			g_pauseMenuState.IsDesiredVisible()));

		if (pauseOpen) {
			g_pauseMenuState.SetDesired(true, true);
		} else if (!g_pauseMenuState.IsDesiredVisible()) {
			g_pauseMenuState.SetDesired(false, false);
		}

		g_pauseMenuState.SetFocusArgs(false, false);
		ApplyPauseMenuState("DomReady");
	}

	void OnPauseMenuDomReady(PrismaView a_view)
	{
		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			if (!g_pauseMenuState.MarkDomReady(a_view) || !g_api) {
				return;
			}
			BindPauseMenuDomHandlers(a_view);
			ReconcilePauseMenuAfterDomReady();
			return;
		}

		taskInterface->AddTask([a_view]() {
			if (!g_pauseMenuState.MarkDomReady(a_view) || !g_api) {
				return;
			}
			BindPauseMenuDomHandlers(a_view);
			F4T::VoiceBridge::AttachView(F4T::VoiceBridge::Surface::PauseMenu, a_view);
			ReconcilePauseMenuAfterDomReady();
		});
	}

	void CreatePauseMenuView()
	{
		g_pauseMenuState.EnsureCreated(kPauseMenuHtmlPath, OnPauseMenuDomReady, g_prismaReady);
	}

	void InvokeSessionLockoutScript()
	{
		if (!g_api || !g_pauseMenuState.IsHandleValid() || !g_pauseMenuState.IsDomReady()) {
			return;
		}

		const nlohmann::json codeJson = g_sessionLockoutCode;
		const nlohmann::json reasonJson = g_sessionLockoutReason;
		const auto script = std::format(
			"if(window.showSessionLockout){{window.showSessionLockout({}, {});}}",
			codeJson.dump(),
			reasonJson.dump());
		g_api->Invoke(g_pauseMenuState.GetHandle(), script.c_str());
	}

	void ShowSessionLockoutInternal()
	{
		F4T::FactionView::Close("SessionLockout");
		F4T::QuestView::Close("SessionLockout");
		g_sessionLockoutActive = true;
		g_sessionLockoutPending = true;

		g_pauseMenuState.SetDesired(true, true);
		// pauseGame=true so gameplay freezes under the lockout overlay.
		g_pauseMenuState.SetFocusArgs(true, false);

		if (!g_api) {
			LogWarning("[PauseMenu] Session lockout pending — Prisma API unavailable.");
			return;
		}

		if (!OpenPanel(PanelType::SessionLockout, "SessionLockout")) {
			return;
		}

		CreatePauseMenuView();
		if (!g_pauseMenuState.IsDomReady()) {
			LogInfo("[PauseMenu] Session lockout pending — DOM not ready");
			return;
		}

		g_sessionLockoutPending = false;
		ApplyPauseMenuState("SessionLockout");
		InvokeSessionLockoutScript();
		LogInfo(std::format(
			"Session lockout overlay shown (code={}).",
			g_sessionLockoutCode.empty() ? "unknown" : g_sessionLockoutCode));
	}

	void ShowPauseMenuInternal(std::string_view a_reason)
	{
		LogInfo(std::format("[PauseMenu] Fallout PauseMenu open / show reason={}", a_reason));

		if (g_pauseMenuQuitInProgress || g_sessionLockoutActive) {
			return;
		}

		g_pauseMenuState.SetDesired(true, true);
		g_pauseMenuState.SetFocusArgs(false, false);

		if (!g_api) {
			LogWarning("[PauseMenu] Show pending — Prisma API unavailable; leaving vanilla menu usable.");
			return;
		}

		if (!OpenPanel(PanelType::PauseMenu, a_reason)) {
			return;
		}

		CreatePauseMenuView();

		const auto* ui = RE::UI::GetSingleton();
		if (!ui || !ui->GetMenuOpen<RE::PauseMenu>()) {
			LogInfo("[PauseMenu] Show pending — Fallout PauseMenu not open");
			return;
		}

		ApplyPauseMenuState(a_reason);
	}

}

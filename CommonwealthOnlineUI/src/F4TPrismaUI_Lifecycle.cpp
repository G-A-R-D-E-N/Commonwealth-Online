
#include "F4TPrismaUIInternal.h"
#include "F4TCaptureHud.h"
#include "F4TVoiceBridge.h"

namespace F4T::PrismaUI::Internal
{
	void ShowServerBrowserInternal(std::string_view a_reason)
	{
		LogInfo(std::format("[ServerBrowser] Open requested reason={}", a_reason));
		g_browserDesiredVisible = true;
		if (g_singlePlayerMode) {
			RestoreMainMenuFromSinglePlayerInternal(a_reason);
		}

		if (!g_api || !g_menuServicesReady) {
			LogWarning("[ServerBrowser] Open pending — menu services not ready.");
			return;
		}

		// Embedded browser is a logical sub-panel of the MainMenu physical kPanel view.
		if (!OpenPanel(PanelType::ServerBrowser, a_reason)) {
			return;
		}

		// Ensure the shared MainMenu physical kPanel through the same readiness/lifecycle
		// reconciliation used by the normal menu.
		ReconcileMainMenuOverlayState(std::string(a_reason) + ":ensureMainMenu");

		ActivateEmbeddedBrowser(a_reason);

		if (g_api &&
			g_browserCurrentlyVisible &&
			g_mainMenuState.IsHandleValid() &&
			g_mainMenuState.IsDomReady()) {
			g_api->Invoke(
				g_mainMenuState.GetHandle(),
				"if(window.onBrowserShown){window.onBrowserShown();}");
		}
	}

	void ToggleMainMenu()
	{
		LogInfo("[MainMenu] Open/toggle requested through F9");
		if (g_singlePlayerMode) {
			RestoreMainMenuFromSinglePlayerInternal("F9Toggle");
			return;
		}
		if (g_mainMenuState.IsDesiredVisible() || g_mainMenuState.IsCurrentlyVisible()) {
			HideMainMenuInternal("F9Toggle");
			return;
		}

		ShowMainMenuInternal("F9Toggle");
	}

	void RestoreMainMenuFromSinglePlayerInternal(std::string_view a_reason)
	{
		g_singlePlayerMode = false;
		F4T::MainMenuLauncher::Hide(a_reason);
		ReconcileMainMenuOverlayState(a_reason);
	}

	void StartGameThreadTasks()
	{
		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			LogWarning("F4SE task interface unavailable; F9 main menu toggle disabled.");
			return;
		}

		if (!g_toggleTaskStarted) {
			F4T::F4SECapabilities::SchedulePermanentTask(taskInterface, F4SE::GetF4SEVersion(), []() {
				static bool lastToggleDown = false;
				if (!IsGameForeground()) {
					lastToggleDown = false;
					return;
				}
				const auto toggleDown = (GetAsyncKeyState(kToggleVirtualKey) & 0x8000) != 0;
				if (toggleDown && !lastToggleDown) {
					ToggleMainMenu();
				}
				lastToggleDown = toggleDown;
			});

			g_toggleTaskStarted = true;
		}

		if (!g_dumpStateTaskStarted) {
			F4T::F4SECapabilities::SchedulePermanentTask(taskInterface, F4SE::GetF4SEVersion(), []() {
				static bool lastDumpDown = false;
				if (!IsGameForeground()) {
					lastDumpDown = false;
					return;
				}
				const auto dumpDown = (GetAsyncKeyState(kDumpStateVirtualKey) & 0x8000) != 0;
				if (dumpDown && !lastDumpDown) {
					DumpAllViewStateInternal();
				}
				lastDumpDown = dumpDown;
			});
			g_dumpStateTaskStarted = true;
		}

		if (!g_chatKeyTaskStarted) {
			taskInterface->AddTaskPermanent([]() {
				static bool lastChatKeyDown = false;
				static bool lastEscapeDown = false;
				static bool lastChatOpen = false;
				if (!IsGameForeground()) {
					lastChatKeyDown = false;
					lastEscapeDown = false;
					return;
				}

				const auto chatOpen = F4T::ChatView::IsOpen();
				const auto chatKeyDown = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
				if (chatKeyDown && !lastChatKeyDown && !chatOpen && !lastChatOpen) {
					F4T::ChatView::Open("ChatHotkey");
				}
				lastChatKeyDown = chatKeyDown;
				lastChatOpen = chatOpen;

				if (!chatOpen) {
					lastEscapeDown = false;
					return;
				}
				const auto escapeDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
				if (escapeDown && !lastEscapeDown) {
					F4T::ChatView::Close("ChatEscape");
				}
				lastEscapeDown = escapeDown;
			});

			g_chatKeyTaskStarted = true;
		}

		if (!g_cursorRestoreTaskStarted) {
			F4T::F4SECapabilities::SchedulePermanentTask(taskInterface, F4SE::GetF4SEVersion(), []() {
				if (g_cursorRestoreFramesLeft <= 0) {
					return;
				}
				if (IsCustomMenuInputCaptured()) {
					return;
				}

				RestoreNativeGameCursor();
				g_cursorRestoreFramesLeft--;
				if (g_cursorRestoreFramesLeft == 0) {
					const auto* menuCursor = RE::MenuCursor::GetSingleton();
					const auto* ui = RE::UI::GetSingleton();
					const auto cursorMenu = ui ? ui->GetMenu<RE::CursorMenu>() : nullptr;
					LogInfo(std::format(
						"Native cursor restore done: registeredCursors={} forceOSCursorPos={} cursorMenuVisible={}",
						menuCursor ? menuCursor->registeredCursors : 0u,
						menuCursor ? menuCursor->forceOSCursorPos : false,
						cursorMenu && cursorMenu->uiMovie && cursorMenu->uiMovie->GetVisible()));
				}
			});

			g_cursorRestoreTaskStarted = true;
		}

		if (!g_menuWatchTaskStarted) {
			F4T::F4SECapabilities::SchedulePermanentTask(taskInterface, F4SE::GetF4SEVersion(), []() {
				TickExitSaveDisconnect();
				WatchMainMenuState();
				WatchPauseMenuState();
			});
			g_menuWatchTaskStarted = true;
		}

		F4T::F4SECapabilities::SchedulePermanentTask(taskInterface, F4SE::GetF4SEVersion(), []() {
			const bool foreground = IsGameForeground();
			if (foreground != g_gameForeground) {
				LogInfo(std::format("Game foreground changed {} -> {}", g_gameForeground, foreground));
				g_gameForeground = foreground;
				ResetBrowserPointerTracking();
				if (!foreground && g_browserCurrentlyVisible) {
					g_mainMenuState.SetDesiredFocused(false);
					g_mainMenuState.ApplyState("BrowserBackgrounded");
				}
				if (foreground &&
					(g_mainMenuState.IsCurrentlyVisible() ||
						g_browserCurrentlyVisible ||
						g_pauseMenuState.IsCurrentlyVisible())) {
					F4T::Alerts::ClearPointerFocus("ForegroundReacquired");
					ApplyBrowserPointerMode(g_menuMouseCursorVisible);
				}
			}

			F4T::MenuInput::PollGamepad();
			if (foreground && g_browserCurrentlyVisible) EnsureBrowserControllerActions();
			F4T::Alerts::PollHotkeys();
			F4T::Alerts::PollDemoHotkey();
			F4T::ChatView::Tick();
			F4T::VoiceBridge::Tick();
			F4T::CaptureHud::Tick();
			F4T::CharacterSelectView::Tick();
			UpdateBrowserPointerMode();
			UpdateAlertOverlayFocus();

			// Failed CreateView(0) is not retried every frame — TickDeferredCreates bounds retries.
			if (g_api && IsMenuRuntimeReady()) {
				TickDeferredCreates();

				// Re-apply desired state without per-frame Show spam: only when pending + ready.
				// OpenPanel re-checks external panels; block logging is deduped.
				if (g_mainMenuState.IsDesiredVisible() &&
					!g_mainMenuState.IsCurrentlyVisible() &&
					g_mainMenuState.IsDomReady()) {
					const auto requestedPanel =
						g_browserDesiredVisible ? PanelType::ServerBrowser : PanelType::MainMenu;
					if (OpenPanel(requestedPanel, "GameThreadPendingFlush")) {
						ApplyMainMenuState("GameThreadPendingFlush");
					}
				}
				if (g_pauseMenuState.IsDesiredVisible() &&
					!g_pauseMenuState.IsCurrentlyVisible() &&
					g_pauseMenuState.IsDomReady()) {
					if (g_sessionLockoutPending || g_sessionLockoutActive) {
						ShowSessionLockoutInternal();
					} else {
						const auto* ui = RE::UI::GetSingleton();
						if (ui && ui->GetMenuOpen<RE::PauseMenu>()) {
							if (OpenPanel(PanelType::PauseMenu, "GameThreadPendingFlush")) {
								ApplyPauseMenuState("GameThreadPendingFlush");
							}
						}
					}
				}
				if (g_browserDesiredVisible &&
					!g_browserCurrentlyVisible &&
					g_mainMenuState.IsCurrentlyVisible()) {
					if (OpenPanel(PanelType::ServerBrowser, "GameThreadPendingFlush")) {
						ActivateEmbeddedBrowser("GameThreadPendingFlush");
					}
				}
			}

			if (ShouldOverrideMenuCursorConstraints()) {
				// Re-assert every frame: the title-screen MainMenu re-applies its parallax cursor
				// constraints and OS-cursor forcing during AdvanceMovie, so we must keep clearing them.
				ReleaseMenuCursorConstraints();
				// Only suppress the native cursor in gamepad mode. In mouse mode Prisma Focus owns
				// the pointer — suppressing here every frame fought pad→mouse→pad switching.
				if (!g_menuMouseCursorVisible) {
					SuppressNativeGameCursor();
				}
			}

			static std::uint32_t hoverSyncCounter = 0;
			if (++hoverSyncCounter % 2 != 0) {
				return;
			}
			// Main menu doesn't need hover sync; only browser does
			if (g_browserCurrentlyVisible) {
				SyncBrowserHover();
			}
		});
	}
	}

namespace F4T::PrismaUI
{
	using namespace Internal;

	namespace
	{
		bool g_vanillaMessageBoxOpen{ false };
	}

	void OnGameDataReady()
	{
		const auto startMenuServices = [] {
			if (g_menuServicesReady) return;
			PrepareMenuRuntime();
			F4T::MainMenuInject::Install();
			F4T::PauseMenuInject::Install();
			InstallMainMenuCursorReleaseHook();
			InstallCursorApiHooks();
			RegisterMenuAdvanceSink();

			g_api = F4T::PrismaCapabilities::Acquire();
			if (!g_api) {
				return;
			}

			g_mainMenuState.SetApi(g_api);
			g_mainMenuState.SetFocusArgs(false, false);
			g_mainMenuState.SetDesiredRole(PRISMA_UI_API::ViewRole::kPanel);

			g_pauseMenuState.SetApi(g_api);
			g_pauseMenuState.SetFocusArgs(false, false);
			g_pauseMenuState.SetDesiredRole(PRISMA_UI_API::ViewRole::kPanel);

			g_standaloneBrowserState.SetApi(g_api);
			g_standaloneBrowserState.SetFocusArgs(false, false);
			g_standaloneBrowserState.SetDesiredRole(PRISMA_UI_API::ViewRole::kPanel);
			g_standaloneBrowserState.SetDesired(false, false);

			F4T::MenuInput::Register();
			StartGameThreadTasks();
			F4T::Alerts::Initialize(g_api);
			F4T::FactionView::Initialize();
			F4T::CharacterSelectView::Initialize();
			F4T::QuestView::Initialize();
			F4T::ChatView::Initialize();
			F4T::VoiceBridge::Initialize();
			F4T::CaptureHud::Initialize();
			F4T::DownedUi::Initialize();
			F4T::JoinOverlay::Initialize();
			F4T::MainMenuLauncher::Initialize(g_api);
			g_menuServicesReady = true;
			TryFinalizeMainMenu();
			ReconcileMainMenuOverlayState("PrismaApiAvailable");
		};
		if (const auto* tasks = F4SE::GetTaskInterface()) {
			tasks->AddTask(startMenuServices);
			return;
		}
		LogWarning("Game task interface unavailable; starting menu services inline");
		startMenuServices();
	}

	void ShowServerBrowser()
	{
		ShowServerBrowserInternal("PublicAPI");
	}

	void HideServerBrowser()
	{
		HideServerBrowserInternal("PublicAPI");
	}

	bool IsBrowserVisible()
	{
		return g_browserCurrentlyVisible;
	}

	bool IsBrowserControllerFocused()
	{
		const auto view = GetActiveBrowserView();
		return view != 0 && IsGameForeground() && F4T::PrismaCapabilities::HasV12() && g_api->HasFocus(view);
	}

	bool IsBrowserControllerInputOwned()
	{
		return IsBrowserControllerFocused() && AreBrowserControllerActionsReady();
	}

	void ShowMainMenu()
	{
		ShowMainMenuInternal("PublicAPI");
	}

	void HideMainMenu()
	{
		HideMainMenuInternal("PublicAPI");
	}

	void RestoreMainMenuFromSinglePlayer()
	{
		Internal::RestoreMainMenuFromSinglePlayerInternal("LauncherRestore");
	}

	bool IsMainMenuVisible()
	{
		return g_mainMenuState.IsCurrentlyVisible();
	}

	void ReconcileMainMenuOverlayState(std::string_view a_reason)
	{
		if (!g_menuServicesReady) {
			static bool s_loggedDeferred = false;
			if (!s_loggedDeferred) {
				s_loggedDeferred = true;
				LogInfo(std::format("[MainMenu] Reconcile deferred until menu services are ready reason={}", a_reason));
			}
			return;
		}
		const auto* ui = RE::UI::GetSingleton();
		const bool falloutMainMenuOpen = ui && ui->GetMenuOpen<RE::MainMenu>();
		const bool onMainPanel =
			falloutMainMenuOpen && F4T::MainMenuInject::IsMainMenuOnMainPanel(ui);
		g_mainMenuPanelEligible = onMainPanel;

		// Injection readiness is the minimum SWF/input integration gate. Prisma handle
		// and DOM readiness remain lifecycle gates in Controller::ApplyState.
		const bool overlayAllowed = falloutMainMenuOpen && onMainPanel && g_mainMenuOverlayReady && !g_vanillaMessageBoxOpen;
		const bool wantVisible = overlayAllowed && !g_singlePlayerMode;
		F4T::MainMenuLauncher::Reconcile(
			overlayAllowed && g_singlePlayerMode,
			std::string(a_reason) + ":launcher");
		const bool oldDesiredVisible = g_mainMenuState.IsDesiredVisible();
		const bool oldDesiredFocused = g_mainMenuState.IsDesiredFocused();

		if (wantVisible && g_api && g_prismaReady && !g_mainMenuState.IsHandleValid()) {
			CreateMainMenuView();
		}

		const auto handle = g_mainMenuState.GetHandle();
		const bool valid = g_mainMenuState.IsHandleValid();
		const bool domReady = g_mainMenuState.IsDomReady();
		const bool canDisplay =
			wantVisible && g_api != nullptr && handle != 0 && valid && domReady &&
			g_mainMenuBridgeReady;

		LogInfo(std::format(
			"[MainMenu] Reconcile reason={}\n"
			"  FalloutMainMenuOpen={}\n"
			"  onMainPanel={}\n"
			"  api={}\n"
			"  handle={}\n"
			"  valid={}\n"
			"  domReady={}\n"
			"  readyForOverlay={}\n"
			"  bridgeReady={}\n"
			"  canDisplayOverlay={}\n"
			"  desiredVisible={}\n"
			"  desiredFocused={}",
			a_reason,
			falloutMainMenuOpen,
			onMainPanel,
			g_api != nullptr,
			handle,
			valid,
			domReady,
			g_mainMenuOverlayReady,
			g_mainMenuBridgeReady.load(),
			canDisplay,
			oldDesiredVisible,
			oldDesiredFocused));

		if (oldDesiredVisible != wantVisible) {
			LogInfo(std::format(
				"[MainMenu] desiredVisible {} -> {} reason={}",
				oldDesiredVisible,
				wantVisible,
				a_reason));
		}
		if (oldDesiredFocused != wantVisible) {
			LogInfo(std::format(
				"[MainMenu] desiredFocused {} -> {} reason={}",
				oldDesiredFocused,
				wantVisible,
				a_reason));
		}

		g_mainMenuState.SetDesired(wantVisible, wantVisible);
		g_mainMenuState.SetFocusArgs(false, false);

		if (!wantVisible) {
			if (falloutMainMenuOpen && onMainPanel && !g_mainMenuOverlayReady) {
				constexpr std::string_view pendingReason = "WaitingForMainMenuInjection";
				if (g_lastMainMenuReadinessPending != pendingReason) {
					g_lastMainMenuReadinessPending = pendingReason;
					LogInfo(std::format(
						"[MainMenu] Overlay readiness pending reason={}",
						pendingReason));
				}
			} else {
				g_lastMainMenuReadinessPending.clear();
			}
			if ((!falloutMainMenuOpen || !onMainPanel) &&
				(g_browserDesiredVisible || g_browserCurrentlyVisible)) {
				DeactivateEmbeddedBrowser(std::string(a_reason) + ":mainMenuNotDisplayable");
			}
			if (handle != 0 || g_mainMenuState.IsCurrentlyVisible() ||
				g_mainMenuState.IsCurrentlyFocused()) {
				ApplyMainMenuState(a_reason);
			}
			return;
		}

		std::string pendingReason;
		if (!g_api) {
			pendingReason = "PrismaApiUnavailable";
		} else if (!g_prismaReady) {
			pendingReason = "WaitingForPrismaReady";
		} else if (handle == 0) {
			pendingReason = "MainMenuViewHandleZero";
		} else if (!valid) {
			pendingReason = "MainMenuViewInvalid";
		} else if (!domReady) {
			pendingReason = "WaitingForMainMenuDomReady";
		} else if (!g_mainMenuBridgeReady) {
			pendingReason = "WaitingForMainMenuBridge";
		}

		if (!pendingReason.empty()) {
			if (g_lastMainMenuReadinessPending != pendingReason) {
				g_lastMainMenuReadinessPending = pendingReason;
				LogInfo(std::format(
					"[MainMenu] Overlay readiness pending reason={}",
					pendingReason));
			}
			ApplyMainMenuState(a_reason);
			return;
		}

		const auto requestedPanel =
			g_browserDesiredVisible ? PanelType::ServerBrowser : PanelType::MainMenu;
		if (!OpenPanel(requestedPanel, a_reason)) {
			return;
		}

		g_lastMainMenuReadinessPending.clear();
		ApplyMainMenuState(a_reason);
		if (g_browserDesiredVisible && g_mainMenuState.IsCurrentlyVisible()) {
			ActivateEmbeddedBrowser(a_reason);
		}
	}

	void SetMainMenuOverlayReady(bool a_ready, std::string_view a_reason)
	{
		const bool oldReady = g_mainMenuOverlayReady;
		LogInfo(std::format(
			"[MainMenu] SetOverlayReady old={} new={} reason={}",
			oldReady,
			a_ready,
			a_reason));

		if (oldReady == a_ready) {
			// Calls are event-driven, not per-frame. Reconcile unchanged readiness as well
			// so catch-up/open/failure notifications cannot be lost.
			ReconcileMainMenuOverlayState(a_reason);
			return;
		}

		g_mainMenuOverlayReady = a_ready;
		ReconcileMainMenuOverlayState(a_reason);
	}

	void SetVanillaMessageBoxOpen(bool a_open)
	{
		if (g_vanillaMessageBoxOpen == a_open) {
			return;
		}
		g_vanillaMessageBoxOpen = a_open;
		LogInfo(std::format("[MainMenu] Vanilla MessageBoxMenu {}; CO overlay {}", a_open ? "opened" : "closed", a_open ? "steps aside" : "returns"));
		ReconcileMainMenuOverlayState(a_open ? "VanillaMessageBoxOpened" : "VanillaMessageBoxClosed");
	}

	void OnFalloutMainMenuOpened()
	{
		LogInfo("[MainMenu] Fallout MainMenu opened");
		g_vanillaMessageBoxOpen = false;
		ReconcileMainMenuOverlayState("FalloutMainMenuOpened");
	}

	void OnFalloutMainMenuClosed()
	{
		LogInfo("[MainMenu] Fallout MainMenu closed");
		g_singlePlayerMode = false;
		F4T::MainMenuLauncher::Hide("FalloutMainMenuClosed");
		if (g_browserDesiredVisible || g_browserCurrentlyVisible) {
			DeactivateEmbeddedBrowser("FalloutMainMenuClosed");
		}
		ReconcileMainMenuOverlayState("FalloutMainMenuClosed");
	}

	void DispatchMainMenuControllerInput(const char* a_button, const char* a_phase)
	{
		DispatchMainMenuControllerInputInternal(a_button, a_phase);
	}

	void ShowPauseMenu()
	{
		ShowPauseMenuInternal("PublicAPI");
	}

	void HidePauseMenu()
	{
		HidePauseMenuInternal("PublicAPI");
	}

	bool IsPauseMenuVisible()
	{
		return g_pauseMenuState.IsCurrentlyVisible();
	}

	void ShowSessionLockout(const char* a_code, const char* a_reason)
	{
		if ((g_sessionLockoutActive || g_sessionLockoutPending) && a_code && std::string_view{ a_code } == "disconnected") {
			return;
		}
		g_sessionLockoutCode = a_code ? a_code : "";
		g_sessionLockoutReason = a_reason ? a_reason : "";
		ShowSessionLockoutInternal();
	}

	void NotifySessionExitSave(const char* a_state, const char* a_message)
	{
		if (!g_api || !g_pauseMenuState.IsHandleValid() || !g_pauseMenuState.IsDomReady()) {
			return;
		}

		const nlohmann::json state = a_state ? a_state : "";
		const nlohmann::json message = a_message ? a_message : "";
		const auto script = std::format(
			"if(window.onSessionExitSave){{window.onSessionExitSave({{state:{},message:{}}});}}",
			state.dump(),
			message.dump());
		g_api->Invoke(g_pauseMenuState.GetHandle(), script.c_str());
	}

	void DispatchPauseMenuControllerInput(const char* a_button, const char* a_phase)
	{
		DispatchPauseMenuControllerInputInternal(a_button, a_phase);
	}

	void DispatchPauseMenuControllerAxes(float a_leftX, float a_leftY, float a_rightX, float a_rightY)
	{
		DispatchPauseMenuControllerAxesInternal(a_leftX, a_leftY, a_rightX, a_rightY);
	}

	void PreferGamepadPointerMode()
	{
		if (!g_mainMenuState.IsCurrentlyVisible() &&
			!g_browserCurrentlyVisible &&
			!g_pauseMenuState.IsCurrentlyVisible()) {
			return;
		}

		g_menuPreferGamepad = true;
		g_ignoreMouseUntil = std::chrono::steady_clock::now() + kGamepadMouseIgnore;

		if (g_menuMouseCursorVisible) {
			ApplyBrowserPointerMode(false);
		} else {
			SuppressNativeGameCursor();
		}

		// Re-baseline after Unfocus/suppress so residual cursor updates are not treated as a nudge.
		ResetBrowserPointerTracking();
	}

	void DispatchPauseMenuMapUpdate(std::string_view a_json)
	{
		DispatchPauseMenuMapUpdateInternal(a_json);
	}

	void DispatchPauseMenuServerInfo(std::string_view a_json)
	{
		DispatchPauseMenuServerInfoInternal(a_json);
	}

	bool IsCustomMenuInputCaptured()
	{
		return !g_vanillaMessageBoxOpen &&
			(g_mainMenuState.IsCurrentlyVisible() ||
				g_browserCurrentlyVisible ||
				g_pauseMenuState.IsCurrentlyVisible());
	}

	void DumpAllViewState()
	{
		DumpAllViewStateInternal();
	}
}

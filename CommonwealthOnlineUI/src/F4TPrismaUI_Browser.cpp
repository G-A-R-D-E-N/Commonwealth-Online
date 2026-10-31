#include "F4TPrismaUIInternal.h"

namespace F4T::PrismaUI::Internal
{
	std::atomic<PrismaView> g_browserControllerView{ 0 };
	std::atomic_bool g_browserControllerActionsBound{ false };
	int g_controllerRebindAttempts = 0;
	std::chrono::steady_clock::time_point g_controllerRebindAfter{};

	constexpr struct {
		const char* button;
		const char* action;
	} kBrowserControllerBindings[] = {
		{ "A", "co.select" }, { "B", "co.back" },
		{ "X", "co.refresh" }, { "Y", "co.favorite" },
		{ "LB", "co.prevTab" }, { "RB", "co.nextTab" },
		{ "DUp", "co.navUp" }, { "DDown", "co.navDown" },
		{ "DLeft", "co.navLeft" }, { "DRight", "co.navRight" },
	};

	void ClearBrowserControllerActions()
	{
		if (auto* api = F4T::PrismaCapabilities::GetV12(); api && api->IsValid(g_browserControllerView)) {
			api->ClearControllerActions(g_browserControllerView);
			api->SetViewOwnsEscape(g_browserControllerView, false);
			(void)PRISMA_UI_API::SetViewCursorPolicy(g_browserControllerView, PRISMA_UI_API::CursorPolicy::Default);
			F4T::PrismaViewState::ResetViewCursorPolicyCache();
		}
		g_browserControllerView = 0;
		g_browserControllerActionsBound = false;
		g_controllerRebindAttempts = 0;
		g_controllerRebindAfter = {};
	}

	void EnsureBrowserControllerActions()
	{
		auto* api = F4T::PrismaCapabilities::GetV12();
		const auto view = GetActiveBrowserView();
		if (!api || !api->IsValid(view)) return;
		if (view != g_browserControllerView) {
			ClearBrowserControllerActions();
			g_browserControllerView = view;
		}
		if (g_browserControllerActionsBound) {
			const auto state = api->GetControllerActionBridgeState(view);
			if (state == PRISMA_UI_API::ControllerActionBridgeState::Ready) {
				g_controllerRebindAttempts = 0;
			} else if (state == PRISMA_UI_API::ControllerActionBridgeState::Failed) {
				api->ClearControllerActions(view);
				g_browserControllerActionsBound = false;
				g_controllerRebindAfter = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
			}
			return;
		}
		if (g_controllerRebindAttempts >= 8 || std::chrono::steady_clock::now() < g_controllerRebindAfter) return;
		++g_controllerRebindAttempts;
		std::size_t bound = 0;
		for (const auto& binding : kBrowserControllerBindings) {
			if (api->BindControllerAction(view, binding.button, binding.action)) ++bound;
		}
		if (bound != std::size(kBrowserControllerBindings)) {
			api->ClearControllerActions(view);
			g_controllerRebindAfter = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
			LogWarning("Browser controller action binding failed; retry is bounded to eight attempts.");
			return;
		}
		api->SetViewOwnsEscape(view, true);
		g_browserControllerActionsBound = true;
	}

	bool AreBrowserControllerActionsReady()
	{
		auto* api = F4T::PrismaCapabilities::GetV12();
		return g_browserControllerActionsBound && api && g_browserControllerView == GetActiveBrowserView() &&
			api->GetControllerActionBridgeState(g_browserControllerView) == PRISMA_UI_API::ControllerActionBridgeState::Ready;
	}

	void InitializeBrowserPointerMode()
	{
		F4T::MenuInput::LogInputState();
		g_menuPreferGamepad = false;
		if (auto* api = F4T::PrismaCapabilities::GetV12()) {
			g_menuPreferGamepad = api->IsUsingGamepad();
			api->Invoke(GetActiveBrowserView(), std::format(
				"window.dispatchEvent(new CustomEvent('prisma-input-device-change',{{detail:{{gamepad:{},style:{}}}}}));",
				g_menuPreferGamepad ? "true" : "false", api->GetControllerStyle()).c_str());
		}
		g_ignoreMouseUntil = {};
		ResetBrowserPointerTracking();
	}

		void RestoreNativeGameCursor()
		{
			RestoreMenuCursorForceOSCursorPos();
			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return;
			}

			if (auto cursorMenu = ui->GetMenu<RE::CursorMenu>()) {
				if (cursorMenu->uiMovie) {
					cursorMenu->uiMovie->SetVisible(true);
				}
			}

			ui->RefreshCursor();

			if (ui->GetMenuOpen<RE::MainMenu>()) {
				if (auto* menuCursor = RE::MenuCursor::GetSingleton()) {
					if (menuCursor->registeredCursors == 0) {
						menuCursor->RegisterCursor();
						LogInfo(std::format("RegisterCursor: registeredCursors 0 -> {}", menuCursor->registeredCursors));
					}
				}
			}
		}

		void RequestNativeCursorRestore()
		{
			g_cursorRestoreFramesLeft = 4;
		}

		void SyncBrowserHover()
		{
			if (!IsGameForeground()) {
				return;
			}

			const auto activeView = GetActiveBrowserView();
			if (!g_browserCurrentlyVisible || !g_api || activeView == 0 || !g_api->IsValid(activeView)) {
				return;
			}

			if (!g_menuMouseCursorVisible) {
				return;
			}

			const auto* renderWindow = RE::BSGraphics::GetCurrentRendererWindow();
			if (!renderWindow || !renderWindow->hwnd) {
				return;
			}

			POINT cursor{};
			if (!::GetCursorPos(&cursor)) {
				return;
			}

			::ScreenToClient(reinterpret_cast<HWND>(renderWindow->hwnd), &cursor);

			const auto script = std::format(
				"if(window.CO_Hover&&window.CO_Hover.updateFromPoint){{window.CO_Hover.updateFromPoint({},{});}}",
				cursor.x,
				cursor.y);
			g_api->Invoke(activeView, script.c_str());
		}

		PrismaView GetActiveBrowserView()
		{
			if (!g_api) {
				return 0;
			}

			if (g_browserCurrentlyVisible &&
				g_mainMenuState.IsCurrentlyVisible() &&
				g_mainMenuState.IsDomReady() &&
				g_mainMenuState.IsHandleValid()) {
				return g_mainMenuState.GetHandle();
			}

			return 0;
		}

		void ActivateEmbeddedBrowser(std::string_view a_reason)
		{
			g_browserDesiredVisible = true;
			LogInfo(std::format("Embedded browser activate requested reason={}", a_reason));

			if (!g_mainMenuState.IsCurrentlyVisible()) {
				LogInfo("Embedded browser pending — MainMenu not currently visible");
				return;
			}

			if (!g_mainMenuState.IsDomReady() || !g_mainMenuState.IsHandleValid()) {
				LogInfo("Embedded browser pending — MainMenu not ready");
				return;
			}

			if (g_browserCurrentlyVisible) {
				return;
			}

			g_browserCurrentlyVisible = true;
			if (!F4T::PrismaCapabilities::HasV12()) {
				LogWarning("Browser controller input requires Prisma V12; keyboard and mouse remain available.");
			}
			EnsureBrowserControllerActions();
			F4T::MenuInput::SetBrowserActive(true);
			F4T::MainMenuInject::SetMainMenuInputSuppressed(true);
			F4T::ServerBrowser::ServerBrowserBridge::Reactivate(g_api, g_mainMenuState.GetHandle());

			InitializeBrowserPointerMode();
			ApplyBrowserPointerMode(!g_menuPreferGamepad);
			F4T::ServerBrowser::ServerBrowserBridge::OnBrowserShown();
			LogInfo(std::format("Embedded browser activated reason={}", a_reason));
		}

		void DeactivateEmbeddedBrowser(std::string_view a_reason)
		{
			const bool wasVisible = g_browserCurrentlyVisible;
			ClearBrowserControllerActions();
			g_browserDesiredVisible = false;
			g_browserCurrentlyVisible = false;
			F4T::PrismaViewState::ResetViewCursorPolicyCache();
			F4T::MenuInput::SetBrowserActive(false);
			ApplyBrowserPointerMode(g_menuMouseCursorVisible);
			F4T::MainMenuInject::SetMainMenuInputSuppressed(g_mainMenuState.IsCurrentlyVisible());

			if (wasVisible) {
				F4T::ServerBrowser::ServerBrowserBridge::OnBrowserHidden();
			}

			if (g_api &&
				g_mainMenuState.IsCurrentlyVisible() &&
				g_mainMenuState.IsDomReady() &&
				g_mainMenuState.IsHandleValid()) {
				g_api->Invoke(
					g_mainMenuState.GetHandle(),
					"if(window.onBrowserHidden){window.onBrowserHidden();}");
			}

			// Keep standalone browser view explicitly hidden/unfocused.
			g_standaloneBrowserState.SetDesired(false, false);
			g_standaloneBrowserState.ApplyState("DeactivateEmbeddedBrowser");
			RequestNativeCursorRestore();
			LogInfo(std::format("Embedded browser deactivated reason={}", a_reason));
		}

	void DispatchMainMenuControllerInputInternal(const char* a_button, const char* a_phase)
	{
		if (!g_mainMenuState.IsCurrentlyVisible() ||
			!g_mainMenuState.IsDomReady() ||
			!g_mainMenuState.IsHandleValid() ||
			!g_api) {
			return;
		}

		const auto script = std::format(
			"if(window.handleControllerInput){{window.handleControllerInput('{}', '{}');}}",
			a_button ? a_button : "",
			a_phase ? a_phase : "");
		g_api->Invoke(g_mainMenuState.GetHandle(), script.c_str());
	}

	void DispatchPauseMenuControllerInputInternal(const char* a_button, const char* a_phase)
	{
		if (F4T::FactionView::IsCurrentlyVisible() && !g_sessionLockoutActive) {
			F4T::FactionView::DispatchControllerInput(a_button, a_phase);
			return;
		}

		if (F4T::QuestView::IsCurrentlyVisible() && !g_sessionLockoutActive) {
			F4T::QuestView::DispatchControllerInput(a_button, a_phase);
			return;
		}

		if (!g_pauseMenuState.IsCurrentlyVisible() ||
			!g_pauseMenuState.IsDomReady() ||
			!g_pauseMenuState.IsHandleValid() ||
			!g_api) {
			return;
		}

		const auto script = std::format(
			"if(window.handleControllerInput){{window.handleControllerInput('{}', '{}');}}",
			a_button ? a_button : "",
			a_phase ? a_phase : "");
		g_api->Invoke(g_pauseMenuState.GetHandle(), script.c_str());
	}

	void DispatchPauseMenuControllerAxesInternal(float a_leftX, float a_leftY, float a_rightX, float a_rightY)
	{
		if (F4T::FactionView::IsCurrentlyVisible() && !g_sessionLockoutActive) {
			return;
		}

		if (F4T::QuestView::IsCurrentlyVisible() && !g_sessionLockoutActive) {
			return;
		}

		if (!g_pauseMenuState.IsCurrentlyVisible() ||
			!g_pauseMenuState.IsDomReady() ||
			!g_pauseMenuState.IsHandleValid() ||
			!g_api) {
			return;
		}

		const auto script = std::format(
			"if(window.handleControllerAxes){{window.handleControllerAxes({:.4f},{:.4f},{:.4f},{:.4f});}}",
			a_leftX,
			a_leftY,
			a_rightX,
			a_rightY);
		g_api->Invoke(g_pauseMenuState.GetHandle(), script.c_str());
	}

	void DispatchPauseMenuMapUpdateInternal(std::string_view a_json)
	{
		if (!g_pauseMenuState.IsCurrentlyVisible() ||
			!g_pauseMenuState.IsDomReady() ||
			!g_pauseMenuState.IsHandleValid() ||
			!g_api) {
			return;
		}

		if (a_json.empty()) {
			return;
		}

		// JSON is already a valid JS object literal when wrapped; escape only for embedding in quotes.
		std::string escaped;
		escaped.reserve(a_json.size() + 16);
		for (const char ch : a_json) {
			switch (ch) {
			case '\\':
				escaped += "\\\\";
				break;
			case '\'':
				escaped += "\\'";
				break;
			case '\n':
				escaped += "\\n";
				break;
			case '\r':
				escaped += "\\r";
				break;
			default:
				escaped += ch;
				break;
			}
		}

		const auto script = std::format(
			"if(window.onMapUpdate){{window.onMapUpdate(JSON.parse('{}'));}}",
			escaped);
		g_api->Invoke(g_pauseMenuState.GetHandle(), script.c_str());
	}

	void DispatchPauseMenuServerInfoInternal(std::string_view a_json)
	{
		if (!g_pauseMenuState.IsDomReady() ||
			!g_api ||
			!g_pauseMenuState.IsHandleValid()) {
			return;
		}

		if (a_json.empty()) {
			return;
		}

		// Payload is already JSON from nlohmann dump — safe as a JS object literal.
		const auto script = std::format(
			"if(window.setCurrentServerInfo){{window.setCurrentServerInfo({});}}",
			a_json);
		g_api->Invoke(g_pauseMenuState.GetHandle(), script.c_str());
	}

	void DispatchPauseMenuWorldModeInternal(std::string_view a_json)
	{
		if (!g_pauseMenuState.IsDomReady() ||
			!g_api ||
			!g_pauseMenuState.IsHandleValid()) {
			return;
		}

		if (a_json.empty()) {
			return;
		}

		const auto script = std::format(
			"if(window.setWorldModeInfo){{window.setWorldModeInfo({});}}",
			a_json);
		g_api->Invoke(g_pauseMenuState.GetHandle(), script.c_str());
	}

	const char* PanelTypeName(PanelType a_panel)
	{
		switch (a_panel) {
		case PanelType::MainMenu:
			return "MainMenu";
		case PanelType::ServerBrowser:
			return "ServerBrowser";
		case PanelType::PauseMenu:
			return "PauseMenu";
		case PanelType::SessionLockout:
			return "SessionLockout";
		case PanelType::None:
		default:
			return "None";
		}
	}

	PanelType GetActivePhysicalPanel()
	{
		if (g_sessionLockoutActive && g_pauseMenuState.IsCurrentlyVisible()) {
			return PanelType::SessionLockout;
		}
		if (g_pauseMenuState.IsCurrentlyVisible() || g_pauseMenuState.IsDesiredVisible()) {
			return PanelType::PauseMenu;
		}
		if (g_browserCurrentlyVisible || g_browserDesiredVisible) {
			return PanelType::ServerBrowser;
		}
		if (g_mainMenuState.IsCurrentlyVisible() || g_mainMenuState.IsDesiredVisible()) {
			return PanelType::MainMenu;
		}
		return PanelType::None;
	}

	bool IsExternalPanelBlocking(PrismaView a_ignoreView, PanelType a_requested, std::string_view a_reason)
	{
		// Clear temporary Alerts focus so widgets never look like an interactive panel.
		F4T::Alerts::ClearPointerFocus("ExternalPanelGate");

		std::string reason;
		bool blocked = false;

		if (F4T::PrismaCapabilities::HasV10()) {
			if (F4T::PrismaCapabilities::IsAnyExternalPanelVisible(a_ignoreView)) {
				const auto focused = F4T::PrismaCapabilities::TryGetFocusedView();
				reason = std::format(
					"V10 IsAnyPanelVisible(ignore={}) blocked {} (focusedView={}) reason={}",
					a_ignoreView,
					PanelTypeName(a_requested),
					focused,
					a_reason);
				blocked = true;
			}
		} else if (F4T::PrismaCapabilities::HasAnyActiveFocus()) {
			// V6 fallback: never use EnumerateViews / !IsHidden.
			// Prefer CommonwealthOnlineUI's own panel tracking. After clearing Alerts pointer
			// focus and ensuring our panels are not the focus holders, HasAnyActiveFocus may
			// indicate an external Prisma panel. Limitation: cannot distinguish widgets vs panels.
			const bool localOwnsFocus =
				(g_mainMenuState.IsCurrentlyFocused()) ||
				(g_pauseMenuState.IsCurrentlyFocused()) ||
				(a_ignoreView != 0 && g_api && g_api->HasFocus(a_ignoreView));
			if (!localOwnsFocus) {
				reason = std::format(
					"V6 HasAnyActiveFocus blocked {} (no local panel owns focus) reason={}",
					PanelTypeName(a_requested),
					a_reason);
				blocked = true;
			}
		}

		if (blocked) {
			// Log only when the block reason changes — avoid per-frame spam on pending flush.
			if (g_lastBlockedPanel != a_requested || g_lastPanelBlockReason != reason) {
				g_lastBlockedPanel = a_requested;
				g_lastPanelBlockReason = reason;
				LogWarning(reason);
			}
			return true;
		}

		if (g_lastBlockedPanel == a_requested) {
			LogInfo(std::format(
				"External panel block cleared for {} reason={}",
				PanelTypeName(a_requested),
				a_reason));
			g_lastBlockedPanel = PanelType::None;
			g_lastPanelBlockReason.clear();
		}
		return false;
	}

	void CloseConflictingPanels(PanelType a_requested, std::string_view a_reason)
	{
		static std::pair<PanelType, std::string> lastLogged{ PanelType::None, {} };
		const auto active = GetActivePhysicalPanel();
		const auto logTransition = [&] {
			const bool changed = lastLogged.first != active || lastLogged.second != a_reason;
			if (changed) lastLogged = { active, std::string(a_reason) };
			return changed;
		};

		// MainMenu <-> embedded ServerBrowser is a logical in-page transition on the same
		// physical MainMenu kPanel. Do not hide/unfocus the host Prisma view.
		const bool mainFamily =
			(active == PanelType::MainMenu || active == PanelType::ServerBrowser);
		const bool requestMainFamily =
			(a_requested == PanelType::MainMenu || a_requested == PanelType::ServerBrowser);
		if (mainFamily && requestMainFamily) {
			if (a_requested == PanelType::MainMenu && active == PanelType::ServerBrowser) {
				if (logTransition()) LogInfo(std::format(
					"Panel transition {} -> {} reason={}",
					PanelTypeName(active),
					PanelTypeName(a_requested),
					a_reason));
				DeactivateEmbeddedBrowser(std::string(a_reason) + ":panelTransition");
			} else if (a_requested == PanelType::ServerBrowser && active == PanelType::MainMenu) {
				if (logTransition()) LogInfo(std::format(
					"Panel transition {} -> {} reason={} (logical, same physical handle)",
					PanelTypeName(active),
					PanelTypeName(a_requested),
					a_reason));
			}
			return;
		}

		if (active == PanelType::None || active == a_requested) {
			return;
		}

		if (logTransition()) LogInfo(std::format(
			"Panel transition {} -> {} reason={}",
			PanelTypeName(active),
			PanelTypeName(a_requested),
			a_reason));

		if ((active == PanelType::ServerBrowser || active == PanelType::MainMenu) &&
			(a_requested == PanelType::PauseMenu || a_requested == PanelType::SessionLockout)) {
			HideMainMenuInternal(std::string(a_reason) + ":closeForPause");
		}

		if ((active == PanelType::PauseMenu || active == PanelType::SessionLockout) &&
			a_requested != PanelType::PauseMenu &&
			a_requested != PanelType::SessionLockout) {
			HidePauseMenuInternal(std::string(a_reason) + ":closeForOtherPanel");
		}
	}

	bool OpenPanel(PanelType a_requested, std::string_view a_reason)
	{
		CloseConflictingPanels(a_requested, a_reason);

		PrismaView ignoreView = 0;
		switch (a_requested) {
		case PanelType::MainMenu:
		case PanelType::ServerBrowser:
			ignoreView = g_mainMenuState.GetHandle();
			break;
		case PanelType::PauseMenu:
		case PanelType::SessionLockout:
			ignoreView = g_pauseMenuState.GetHandle();
			break;
		default:
			break;
		}

		if (IsExternalPanelBlocking(ignoreView, a_requested, a_reason)) {
			return false;
		}

		g_lastPanelBlockReason.clear();
		g_lastBlockedPanel = PanelType::None;
		return true;
	}

	void ArmCreateRetryOnce()
	{
		if (g_createRetryArmed) {
			return;
		}
		g_createRetryArmed = true;
		if (g_mainMenuState.DidCreateFail()) {
			g_mainMenuState.AllowCreateRetry();
		}
		if (g_pauseMenuState.DidCreateFail()) {
			g_pauseMenuState.AllowCreateRetry();
		}
		if (g_standaloneBrowserState.DidCreateFail()) {
			g_standaloneBrowserState.AllowCreateRetry();
		}
		F4T::Alerts::AllowCreateRetry();
		F4T::DownedUi::AllowCreateRetry();
		F4T::JoinOverlay::AllowCreateRetry();
		LogInfo("Armed single CreateView retry for failed views (Prisma lifecycle ready / controlled reinit).");
	}

	void TickDeferredCreates()
	{
		if (!g_api || !g_prismaReady) {
			return;
		}

		if (g_mainMenuState.DidCreateFail() ||
			g_pauseMenuState.DidCreateFail() ||
			g_standaloneBrowserState.DidCreateFail() ||
			F4T::DownedUi::DidCreateFail() ||
			F4T::JoinOverlay::DidCreateFail()) {
			ArmCreateRetryOnce();
		}

		if (!g_mainMenuState.IsHandleValid()) {
			if (!g_mainMenuState.DidCreateFail() || g_mainMenuState.CanRetryCreate()) {
				if (g_mainMenuState.GetHandle() != 0) {
					g_mainMenuState.InvalidateApplied();
				}
				CreateMainMenuView();
				if (g_mainMenuState.IsHandleValid()) {
					ReconcileMainMenuOverlayState("MainMenuViewCreated");
				}
			}
		}
		if (g_standaloneBrowserState.IsDesiredVisible() && !g_standaloneBrowserState.IsHandleValid()) {
			if (!g_standaloneBrowserState.DidCreateFail() || g_standaloneBrowserState.CanRetryCreate()) {
				if (g_standaloneBrowserState.GetHandle() != 0) {
					g_standaloneBrowserState.InvalidateApplied();
				}
				CreateServerBrowserView();
			}
		}
		static ULONGLONG s_secondaryGateStartMs = 0;
		static bool s_secondaryGateOpen = false;
		if (!s_secondaryGateOpen) {
			const auto now = GetTickCount64();
			if (s_secondaryGateStartMs == 0) {
				s_secondaryGateStartMs = now;
			}
			s_secondaryGateOpen = g_mainMenuState.IsDomReady() ||
				(g_mainMenuState.DidCreateFail() && !g_mainMenuState.CanRetryCreate()) ||
				now - s_secondaryGateStartMs >= 10000;
			if (!s_secondaryGateOpen) {
				return;
			}
		}
		if (!g_pauseMenuState.IsHandleValid()) {
			if (!g_pauseMenuState.DidCreateFail() || g_pauseMenuState.CanRetryCreate()) {
				if (g_pauseMenuState.GetHandle() != 0) {
					g_pauseMenuState.InvalidateApplied();
				}
				CreatePauseMenuView();
			}
		}
		F4T::MainMenuLauncher::EnsureView();
		F4T::Alerts::EnsureView();
		F4T::DownedUi::EnsureViews();
		F4T::JoinOverlay::EnsureView();
	}

	void HideServerBrowserInternal(std::string_view a_reason)
	{
		if (!g_browserCurrentlyVisible && !g_browserDesiredVisible) {
			return;
		}
		LogInfo(std::format("[ServerBrowser] Close requested reason={}", a_reason));
		DeactivateEmbeddedBrowser(a_reason);
	}

	void WatchMainMenuState()
	{
		const auto* ui = RE::UI::GetSingleton();
		const bool panelEligible =
			ui && ui->GetMenuOpen<RE::MainMenu>() &&
			F4T::MainMenuInject::IsMainMenuOnMainPanel(ui);
		if (panelEligible != g_mainMenuPanelEligible) {
			ReconcileMainMenuOverlayState("MainMenuPanelEligibilityChanged");
		}
	}

	void WatchPauseMenuState()
	{
		if (g_sessionLockoutActive) {
			// Lockout owns the pause overlay independently of vanilla PauseMenu.
			if (g_sessionLockoutPending && g_pauseMenuState.IsDomReady()) {
				ShowSessionLockoutInternal();
			}
			return;
		}

		if (!IsPauseMenuOverlayUp() && !g_pauseMenuQuitInProgress) {
			if (F4T::PauseMenuMap::IsStreaming()) {
				F4T::PauseMenuMap::StopStreaming();
			}
			// Engine QueueFastTravel must run after PauseMenu is fully closed.
			F4T::PauseMenuMap::TickDeferredFastTravel();
			return;
		}

		const auto* ui = RE::UI::GetSingleton();
		if (!ui || !ui->GetMenuOpen<RE::PauseMenu>()) {
			HidePauseMenuInternal("WatchPauseMenuClosed");
			F4T::PauseMenuMap::TickDeferredFastTravel();
			return;
		}

		F4T::PauseMenuMap::Tick();
		if (F4T::PauseMenuMap::ConsumeFastTravelCloseRequest()) {
			RequestResumeFromPauseMenu();
		}
	}

	bool ApplyMainMenuState(std::string_view a_reason)
	{
		static bool overlayEngaged = false;
		const auto result = g_mainMenuState.ApplyState(a_reason);
		if (result.showRequested) {
			overlayEngaged = true;
			// Only suppress vanilla after a successful Show request on a ready view.
			F4T::MenuInput::SetMainMenuActive(true);
			F4T::MainMenuInject::SetMainMenuInputSuppressed(true);
			F4T::Alerts::SetMenuInteractionActive(true);
			SetVanillaMainMenuSuppressed(true);

			if (const auto* ui = RE::UI::GetSingleton()) {
				if (auto mainMenu = ui->GetMenu<RE::MainMenu>()) {
					mainMenu->userEngaged = true;
					mainMenu->mainBinkShown = true;
				}
			}

			InitializeBrowserPointerMode();
			ApplyBrowserPointerMode(!g_menuPreferGamepad);

			if (g_api && g_mainMenuState.IsHandleValid()) {
				g_api->Invoke(
					g_mainMenuState.GetHandle(),
					"if(window.onMainMenuShown){window.onMainMenuShown();}");
			}

			if (g_browserDesiredVisible) {
				ActivateEmbeddedBrowser(std::string(a_reason) + ":autoEmbedBrowser");
			}

			LogInfo("Main menu overlay shown.");
		}

		if (result.hideRequested || (overlayEngaged && !g_mainMenuState.IsCurrentlyVisible())) {
			overlayEngaged = false;
			F4T::MenuInput::SetMainMenuActive(false);
			F4T::MainMenuInject::SetMainMenuInputSuppressed(false);
			F4T::Alerts::SetMenuInteractionActive(
				g_browserCurrentlyVisible || g_pauseMenuState.IsCurrentlyVisible());
			SetVanillaMainMenuSuppressed(false);
			F4T::Alerts::ClearPointerFocus("MainMenuHidden");
			RequestNativeCursorRestore();
		}

		return result.applied;
	}

}

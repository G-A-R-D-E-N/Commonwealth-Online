#include "F4TPrismaUIInternal.h"
#include "F4TInteractWheelView.h"
#include "F4TIatPatch.h"
#include "F4TVtableHookGuard.h"

namespace F4T::PrismaUI::Internal
{
		void MainMenuAdvanceMovieHook(RE::IMenu* a_this, float a_timeDelta, std::uint64_t a_time)
		{
			if (g_originalMainMenuAdvanceMovie != 0) {
				reinterpret_cast<MainMenuAdvanceMovieFn*>(g_originalMainMenuAdvanceMovie)(a_this, a_timeDelta, a_time);
			}

			if (UsesLegacyMenuReadiness(PRISMA_UI_API::GetRuntimeState())) {
				if (g_firstMenuAdvanceMs == 0) {
					g_firstMenuAdvanceMs = GetTickCount64();
					LogInfo("[Readiness] first MainMenu AdvanceMovie observed");
				} else if (const auto elapsedMs = GetTickCount64() - g_firstMenuAdvanceMs; !g_prismaReady && elapsedMs >= 250) {
					g_prismaReady = true;
					LogInfo(std::format("[Readiness] prismaReady after {} ms of MainMenu advance", elapsedMs));
				}
			}

			// MainMenu parallax updates MenuCursor bounds during AdvanceMovie; re-expand after vanilla runs.
			if (ShouldOverrideMenuCursorConstraints()) {
				ReleaseMenuCursorConstraints();
			}
		}

		class MenuAdvanceSink :
			public RE::BSTEventSink<RE::UIAdvanceMenusFunctionCompleteEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::UIAdvanceMenusFunctionCompleteEvent&,
				RE::BSTEventSource<RE::UIAdvanceMenusFunctionCompleteEvent>* a_source) override
			{
				(void)a_source;

				if (ShouldOverrideMenuCursorConstraints()) {
					ReleaseMenuCursorConstraints();
				}

				return RE::BSEventNotifyControl::kContinue;
			}
		};

		MenuAdvanceSink g_menuAdvanceSink;

		std::string GetLogPrefix()
		{
			return std::string{COUI::Session::GetLogPrefix()} + " [PrismaUI]";
		}

		void LogInfo(std::string_view a_message)
		{
			const auto message = GetLogPrefix() + " " + std::string(a_message);
			REX::INFO(std::string_view{ message });
		}

		void LogWarning(std::string_view a_message)
		{
			const auto message = GetLogPrefix() + " " + std::string(a_message);
			REX::WARN(std::string_view{ message });
		}

		void SetVanillaMainMenuSuppressed(bool a_suppress)
		{
			if (!g_api || g_vanillaMainMenuSuppressed == a_suppress) {
				return;
			}

			if (!g_api->SuppressVanillaMenu("MainMenu", a_suppress)) {
				LogWarning(std::format(
					"SuppressVanillaMenu(\"MainMenu\", {}) failed.",
					a_suppress));
				return;
			}

			g_vanillaMainMenuSuppressed = a_suppress;
			LogInfo(std::format(
				"Vanilla MainMenu SWF {}.",
				a_suppress ? "suppressed" : "restored"));
		}

		void SetVanillaPauseMenuSuppressed(bool a_suppress)
		{
			if (!g_api || g_vanillaPauseMenuSuppressed == a_suppress) {
				return;
			}

			if (!g_api->SuppressVanillaMenu("PauseMenu", a_suppress)) {
				LogWarning(std::format(
					"SuppressVanillaMenu(\"PauseMenu\", {}) failed.",
					a_suppress));
				return;
			}

			g_vanillaPauseMenuSuppressed = a_suppress;
			LogInfo(std::format(
				"Vanilla PauseMenu SWF {}.",
				a_suppress ? "suppressed" : "restored"));
		}

		void InstallMainMenuCursorReleaseHook()
		{
			if (g_mainMenuAdvanceHookInstalled) {
				return;
			}

			REL::Relocation<std::uintptr_t> vtable{ RE::MainMenu::VTABLE[0] };
			// source:CommonLib RE/I/IMenu.h AdvanceMovie slot 0x04.
			g_originalMainMenuAdvanceMovie = F4T::VtableHookGuard::TryWriteVfunc(
				vtable,
				0x04,
				REX::UNRESTRICTED_CAST<std::uintptr_t>(&MainMenuAdvanceMovieHook),
				"MainMenu AdvanceMovie");
			if (g_originalMainMenuAdvanceMovie != 0) {
				g_mainMenuAdvanceHookInstalled = true;
				LogInfo("Installed MainMenu AdvanceMovie hook for MenuCursor release.");
			} else {
				LogWarning("MainMenu AdvanceMovie hook failed to install.");
			}
		}

		void RegisterMenuAdvanceSink()
		{
			if (g_menuAdvanceSinkRegistered) {
				return;
			}

			if (auto* source = RE::UIAdvanceMenusFunctionCompleteEvent::GetEventSource()) {
				source->RegisterSink(&g_menuAdvanceSink);
				g_menuAdvanceSinkRegistered = true;
				LogInfo("Registered UIAdvanceMenusFunctionCompleteEvent sink for MenuCursor release.");
			}
		}

		void HideNativeCursorMenu()
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return;
			}

			if (auto cursorMenu = ui->GetMenu<RE::CursorMenu>()) {
				if (cursorMenu->uiMovie && cursorMenu->uiMovie->GetVisible()) {
					cursorMenu->uiMovie->SetVisible(false);
				}
			}
		}

		void SuppressNativeGameCursor()
		{
			HideNativeCursorMenu();

			if (auto* menuCursor = RE::MenuCursor::GetSingleton()) {
				if (menuCursor->registeredCursors > 0) {
					menuCursor->UnregisterCursor();
				}
			}
		}

	namespace
	{
		bool s_haveSavedForceOSCursorPos = false;
		bool s_savedForceOSCursorPos = false;
	}

	void RestoreMenuCursorForceOSCursorPos()
	{
		if (!s_haveSavedForceOSCursorPos) {
			return;
		}

		if (auto* menuCursor = RE::MenuCursor::GetSingleton()) {
			menuCursor->forceOSCursorPos = s_savedForceOSCursorPos;
		}
		s_haveSavedForceOSCursorPos = false;
	}

	void ReleaseMenuCursorConstraints()
	{
		auto* menuCursor = RE::MenuCursor::GetSingleton();
		if (!menuCursor) {
			return;
		}

		if (!s_haveSavedForceOSCursorPos) {
			s_savedForceOSCursorPos = menuCursor->forceOSCursorPos;
			s_haveSavedForceOSCursorPos = true;
		}
		menuCursor->forceOSCursorPos = false;

		menuCursor->ClearConstraints();

		const auto* renderWindow = RE::BSGraphics::GetCurrentRendererWindow();
		if (!renderWindow) {
			::ClipCursor(nullptr);
			return;
		}

		const auto width = static_cast<std::int32_t>(renderWindow->windowWidth);
		const auto height = static_cast<std::int32_t>(renderWindow->windowHeight);
		if (width <= 0 || height <= 0) {
			::ClipCursor(nullptr);
			return;
		}

		// Widen the raw cursor bounds to the full window so any residual clamping is a no-op.
		menuCursor->SetCursorConstraintsRaw(0, 0, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
		menuCursor->minCursorX = 0;
		menuCursor->minCursorY = 0;
		menuCursor->maxCursorX = width;
		menuCursor->maxCursorY = height;

		// Release any OS-level cursor clipping rectangle.
		::ClipCursor(nullptr);
	}

	// In mouse mode the title-screen game code repositions the OS cursor (SetCursorPos) and/or
	// clips it (ClipCursor) to a small centered box every frame — the "trampoline". This does not
	// happen with a gamepad connected because the mouse path never runs. Consuming the MainMenu's
	// own mouse-move vfuncs did not stop it, so the forcing originates from a more central engine
	// path. Rather than chase the exact function, we intercept SetCursorPos/ClipCursor in the game
	// executable's import table and make them no-ops while our overlay owns the pointer. We only
	// patch Fallout4.exe's IAT, so PrismaUI's own cursor rendering (a separate module) is untouched.

	[[nodiscard]] bool OverlayOwnsPointer()
	{
		return IsGameForeground() &&
			(g_mainMenuState.IsCurrentlyVisible() ||
			g_browserCurrentlyVisible ||
			g_pauseMenuState.IsCurrentlyVisible());
	}
	BOOL WINAPI SetCursorPosHook(int a_x, int a_y)
	{
		// While our overlay is up, ignore the game's attempts to move the OS cursor so the
		// Windows pointer stays where the user actually put it (no trampoline back to the box).
		if (OverlayOwnsPointer()) {
			return TRUE;
		}
		return g_realSetCursorPos ? g_realSetCursorPos(a_x, a_y) : TRUE;
	}

	BOOL WINAPI ClipCursorHook(const RECT* a_rect)
	{
		// While our overlay is up, never let the game clip the pointer to a sub-rect.
		if (OverlayOwnsPointer()) {
			return g_realClipCursor ? g_realClipCursor(nullptr) : TRUE;
		}
		return g_realClipCursor ? g_realClipCursor(a_rect) : TRUE;
	}

	void InstallCursorApiHooks()
	{
		if (g_cursorApiHooksInstalled) {
			return;
		}

		auto* gameModule = ::GetModuleHandleW(nullptr);
		if (!gameModule) {
			LogWarning("Cursor API hook: game module handle unavailable.");
			return;
		}

		g_realSetCursorPos = reinterpret_cast<SetCursorPosFn>(
			F4T::IatPatch::PatchIATEntry(gameModule, "USER32.dll", "SetCursorPos", reinterpret_cast<void*>(&SetCursorPosHook)));
		if (g_realSetCursorPos == nullptr) {
			LogWarning("Cursor API hook: SetCursorPos IAT entry not patched (missing or not the expected USER32 export); skipped.");
		}
		g_realClipCursor = reinterpret_cast<ClipCursorFn>(
			F4T::IatPatch::PatchIATEntry(gameModule, "USER32.dll", "ClipCursor", reinterpret_cast<void*>(&ClipCursorHook)));
		if (g_realClipCursor == nullptr) {
			LogWarning("Cursor API hook: ClipCursor IAT entry not patched (missing or not the expected USER32 export); skipped.");
		}

		if (g_realSetCursorPos == nullptr && g_realClipCursor == nullptr) {
			LogWarning("Cursor API hook: neither SetCursorPos nor ClipCursor found in game IAT.");
			return;
		}

		g_cursorApiHooksInstalled = true;
		LogInfo(std::format(
			"Installed game cursor API hooks (SetCursorPos={}, ClipCursor={}) to stop OS-cursor trampoline in the overlay.",
			g_realSetCursorPos != nullptr,
			g_realClipCursor != nullptr));
	}

	[[nodiscard]] PrismaView GetActiveMenuViewForPointer()
	{
		if (g_mainMenuState.IsCurrentlyVisible() && g_mainMenuState.IsHandleValid()) {
			return g_mainMenuState.GetHandle();
		}
		if (g_pauseMenuState.IsCurrentlyVisible() && g_pauseMenuState.IsHandleValid()) {
			return g_pauseMenuState.GetHandle();
		}
		return 0;
	}

	void ApplyMenuFocusForPointerMode(bool a_mouseCursorVisible)
	{
		if (F4T::FactionView::IsCurrentlyVisible() || F4T::QuestView::IsCurrentlyVisible() ||
			F4T::InteractWheelView::IsCurrentlyVisible() || F4T::CharacterSelectView::IsCurrentlyVisible()) {
			HideNativeCursorMenu();
			return;
		}

		// Focus routing must never Focus a hidden view. Controllers own Show/Hide;
		// pointer mode only adjusts desired focus on the already-visible active menu.
		F4T::PrismaViewState::Controller* active = nullptr;
		if (g_mainMenuState.IsCurrentlyVisible()) {
			active = &g_mainMenuState;
		} else if (g_pauseMenuState.IsCurrentlyVisible()) {
			active = &g_pauseMenuState;
		}

		if (!active) {
			F4T::Alerts::ClearPointerFocus("PointerModeNoMenu");
			return;
		}
		if (g_browserCurrentlyVisible) {
			F4T::PrismaViewState::SetViewCursorPolicyCached(active->GetHandle(), a_mouseCursorVisible
				? PRISMA_UI_API::CursorPolicy::Default : PRISMA_UI_API::CursorPolicy::Hidden);
		}

		if (a_mouseCursorVisible) {
			ReleaseMenuCursorConstraints();
			HideNativeCursorMenu();

			bool overAlerts = false;
			const auto* renderWindow = RE::BSGraphics::GetCurrentRendererWindow();
			if (renderWindow && renderWindow->hwnd) {
				POINT cursor{};
				if (::GetCursorPos(&cursor)) {
					::ScreenToClient(reinterpret_cast<HWND>(renderWindow->hwnd), &cursor);
					overAlerts = F4T::Alerts::IsPointerOverInteractive(cursor.x, cursor.y);
				}
			}

			if (overAlerts) {
				if (active->IsDesiredFocused() || active->IsCurrentlyFocused()) {
					active->SetDesiredFocused(false);
					active->ApplyState("PointerModeYieldToAlerts");
				}
				F4T::Alerts::FocusForPointer("PointerOverAlert");
			} else {
				F4T::Alerts::ClearPointerFocus("PointerOverMenu");
				if (!active->IsDesiredFocused() || !active->IsCurrentlyFocused()) {
					active->SetDesiredFocused(true);
					active->ApplyState("PointerModeMouse");
				}
			}
		} else {
			F4T::Alerts::ClearPointerFocus("PointerModeGamepad");
			if (g_browserCurrentlyVisible) {
				active->SetDesiredFocused(IsGameForeground());
				active->ApplyState("BrowserControllerFocus");
			} else if (active->IsDesiredFocused() || active->IsCurrentlyFocused()) {
				active->SetDesiredFocused(false);
				active->ApplyState("PointerModeGamepad");
			}
			SuppressNativeGameCursor();
		}
	}

	void ApplyBrowserPointerMode(bool a_mouseCursorVisible)
	{
		g_menuMouseCursorVisible = a_mouseCursorVisible;

		const auto activeView = GetActiveMenuViewForPointer();
		if (activeView != 0 && g_api) {
			ApplyMenuFocusForPointerMode(a_mouseCursorVisible);

			const auto* mode = a_mouseCursorVisible ? "mouse" : "gamepad";
			const auto script = std::format(
				"if(window.CO_App&&window.CO_App.setPointerMode){{window.CO_App.setPointerMode('{}');}}",
				mode);
			g_api->Invoke(activeView, script.c_str());
		} else if (!a_mouseCursorVisible) {
			SuppressNativeGameCursor();
		}
	}

	void UpdateAlertOverlayFocus()
	{
		if (!IsGameForeground()) {
			return;
		}

		if (!g_menuMouseCursorVisible ||
			(!g_mainMenuState.IsCurrentlyVisible() &&
				!g_browserCurrentlyVisible &&
				!g_pauseMenuState.IsCurrentlyVisible())) {
			return;
		}

		ApplyMenuFocusForPointerMode(true);
	}

		void ResetBrowserPointerTracking()
		{
			g_hasLastCursorPos = false;
			g_lastCursorPos = {};
		}

	void UpdateBrowserPointerMode()
	{
		if (!IsGameForeground()) {
			return;
		}
		if (g_browserCurrentlyVisible) {
			if (auto* api = F4T::PrismaCapabilities::GetV12()) {
				const bool mouse = !api->IsUsingGamepad();
				if (mouse != g_menuMouseCursorVisible) ApplyBrowserPointerMode(mouse);
			}
			return;
		}

		if (!g_mainMenuState.IsCurrentlyVisible() &&
			!g_browserCurrentlyVisible &&
			!g_pauseMenuState.IsCurrentlyVisible()) {
			return;
		}

		if (!g_menuPreferGamepad) {
			if (!g_menuMouseCursorVisible) {
				ApplyBrowserPointerMode(true);
			}
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

		const auto now = std::chrono::steady_clock::now();
		if (now < g_ignoreMouseUntil) {
			g_lastCursorPos = cursor;
			g_hasLastCursorPos = true;
			if (!g_menuMouseCursorVisible) {
				SuppressNativeGameCursor();
			}
			return;
		}

		const auto mouseButtonDown =
			(GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
			(GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0 ||
			(GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;

		bool mouseMoved = false;
		if (g_hasLastCursorPos) {
			const auto deltaX = std::abs(cursor.x - g_lastCursorPos.x);
			const auto deltaY = std::abs(cursor.y - g_lastCursorPos.y);
			mouseMoved = deltaX > kMouseModeSwitchSlop || deltaY > kMouseModeSwitchSlop;
		}

		g_lastCursorPos = cursor;
		g_hasLastCursorPos = true;

		if (!g_menuMouseCursorVisible && (mouseMoved || mouseButtonDown)) {
			ApplyBrowserPointerMode(true);
			return;
		}

		if (!g_menuMouseCursorVisible) {
			SuppressNativeGameCursor();
		}
}
}

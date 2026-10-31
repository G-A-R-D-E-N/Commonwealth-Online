#include "F4TPrismaUIInternal.h"

namespace F4T::PrismaUI::Internal
{
	RuntimeState g_runtimeState;

#define CO_STATE_REF(type, field, name) type& name = g_runtimeState.field
	CO_STATE_REF(PRISMA_UI_API::IVPrismaUI6*, api, g_api);
	CO_STATE_REF(F4T::PrismaViewState::Controller, mainMenuState, g_mainMenuState);
	CO_STATE_REF(F4T::PrismaViewState::Controller, pauseMenuState, g_pauseMenuState);
	CO_STATE_REF(F4T::PrismaViewState::Controller, standaloneBrowserState, g_standaloneBrowserState);
	CO_STATE_REF(bool, browserDesiredVisible, g_browserDesiredVisible);
	CO_STATE_REF(bool, browserCurrentlyVisible, g_browserCurrentlyVisible);
	CO_STATE_REF(std::string, lastPanelBlockReason, g_lastPanelBlockReason);
	CO_STATE_REF(PanelType, lastBlockedPanel, g_lastBlockedPanel);
	CO_STATE_REF(bool, createRetryArmed, g_createRetryArmed);
	CO_STATE_REF(std::atomic<bool>, mainMenuBridgeReady, g_mainMenuBridgeReady);
	CO_STATE_REF(bool, vanillaMainMenuSuppressed, g_vanillaMainMenuSuppressed);
	CO_STATE_REF(bool, mainMenuOverlayReady, g_mainMenuOverlayReady);
	CO_STATE_REF(bool, mainMenuPanelEligible, g_mainMenuPanelEligible);
	CO_STATE_REF(bool, singlePlayerMode, g_singlePlayerMode);
	CO_STATE_REF(std::string, lastMainMenuReadinessPending, g_lastMainMenuReadinessPending);
	CO_STATE_REF(bool, pauseMenuBridgeReady, g_pauseMenuBridgeReady);
	CO_STATE_REF(bool, vanillaPauseMenuSuppressed, g_vanillaPauseMenuSuppressed);
	CO_STATE_REF(bool, pauseMenuQuitInProgress, g_pauseMenuQuitInProgress);
	CO_STATE_REF(bool, sessionLockoutActive, g_sessionLockoutActive);
	CO_STATE_REF(bool, sessionLockoutPending, g_sessionLockoutPending);
	CO_STATE_REF(std::string, sessionLockoutCode, g_sessionLockoutCode);
	CO_STATE_REF(std::string, sessionLockoutReason, g_sessionLockoutReason);
	CO_STATE_REF(bool, toggleTaskStarted, g_toggleTaskStarted);
	CO_STATE_REF(bool, cursorRestoreTaskStarted, g_cursorRestoreTaskStarted);
	CO_STATE_REF(bool, menuWatchTaskStarted, g_menuWatchTaskStarted);
	CO_STATE_REF(bool, dumpStateTaskStarted, g_dumpStateTaskStarted);
	CO_STATE_REF(bool, chatKeyTaskStarted, g_chatKeyTaskStarted);
	CO_STATE_REF(int, cursorRestoreFramesLeft, g_cursorRestoreFramesLeft);
	CO_STATE_REF(bool, menuMouseCursorVisible, g_menuMouseCursorVisible);
	CO_STATE_REF(bool, menuPreferGamepad, g_menuPreferGamepad);
	CO_STATE_REF(bool, gameForeground, g_gameForeground);
	CO_STATE_REF(bool, hasLastCursorPos, g_hasLastCursorPos);
	CO_STATE_REF(POINT, lastCursorPos, g_lastCursorPos);
	CO_STATE_REF(std::chrono::steady_clock::time_point, ignoreMouseUntil, g_ignoreMouseUntil);
	CO_STATE_REF(bool, prismaReady, g_prismaReady);
	CO_STATE_REF(ULONGLONG, firstMenuAdvanceMs, g_firstMenuAdvanceMs);
	CO_STATE_REF(std::uintptr_t, originalMainMenuAdvanceMovie, g_originalMainMenuAdvanceMovie);
	CO_STATE_REF(bool, mainMenuAdvanceHookInstalled, g_mainMenuAdvanceHookInstalled);
	CO_STATE_REF(bool, menuAdvanceSinkRegistered, g_menuAdvanceSinkRegistered);
	CO_STATE_REF(Internal::SetCursorPosFn, realSetCursorPos, g_realSetCursorPos);
	CO_STATE_REF(Internal::ClipCursorFn, realClipCursor, g_realClipCursor);
	CO_STATE_REF(bool, cursorApiHooksInstalled, g_cursorApiHooksInstalled);
#undef CO_STATE_REF
}

namespace F4T::PrismaUI
{
	bool IsGameForeground()
	{
		const auto* renderWindow = RE::BSGraphics::GetCurrentRendererWindow();
		return renderWindow && renderWindow->hwnd &&
			::GetForegroundWindow() == reinterpret_cast<HWND>(renderWindow->hwnd);
	}

	void NotifyPostSaveGame()
	{
		Internal::NotifyExitSavePosted();
	}
}

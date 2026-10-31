#pragma once

#include "pch.h"
#include "DownedUiCallbacks.h"
#include "F4TAlertSystem.h"
#include "F4TJoinOverlayBridge.h"
#include "F4TMainMenuInject.h"
#include "F4TPauseMenuInject.h"
#include "F4TMenuInput.h"
#include "SessionCallbacks.h"
#include "F4TF4SEPermanentTask.h"
#include "F4TPrismaCapabilities.h"
#include "F4TPrismaUI.h"
#include "F4TPrismaViewState.h"
#include "F4TServerBrowserBridge.h"
#include "F4TSettingsBridge.h"
#include "F4TMainMenuBridge.h"
#include "F4TPauseMenuBridge.h"
#include "F4TPauseMenuMap.h"
#include "F4TMainMenuVanillaSettings.h"
#include "F4TMainMenuLauncher.h"
#include "F4TFactionView.h"
#include "F4TInteractWheelView.h"
#include "F4TCharacterSelectView.h"
#include "F4TQuestView.h"
#include "F4TChatView.h"
#include "F4TCharacterCreator.h"
#include "PrismaUI_F4_API.h"
#include "Scaleform/G/GFx_Movie.h"
#include "Scaleform/G/GFx_Value.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <nlohmann/json.hpp>
#include <string_view>

namespace F4T::PrismaUI::Internal
{
	using MainMenuAdvanceMovieFn = void(RE::IMenu*, float, std::uint64_t);
	using SetCursorPosFn = BOOL(WINAPI*)(int, int);
	using ClipCursorFn = BOOL(WINAPI*)(const RECT*);

	enum class PanelType { None, MainMenu, ServerBrowser, PauseMenu, SessionLockout, CharacterCreator };

	struct RuntimeState
	{
		PRISMA_UI_API::IVPrismaUI6* api = nullptr;
		F4T::PrismaViewState::Controller mainMenuState{ "MainMenu" };
		F4T::PrismaViewState::Controller pauseMenuState{ "PauseMenu" };
		F4T::PrismaViewState::Controller standaloneBrowserState{ "ServerBrowserStandalone" };
		bool browserDesiredVisible = false;
		bool browserCurrentlyVisible = false;
		std::string lastPanelBlockReason;
		PanelType lastBlockedPanel = PanelType::None;
		bool createRetryArmed = false;
		std::atomic<bool> mainMenuBridgeReady{ false };
		bool vanillaMainMenuSuppressed = false;
		bool mainMenuOverlayReady = false;
		bool mainMenuPanelEligible = false;
		bool singlePlayerMode = false;
		std::string lastMainMenuReadinessPending;
		bool pauseMenuBridgeReady = false;
		bool vanillaPauseMenuSuppressed = false;
		bool pauseMenuQuitInProgress = false;
		bool sessionLockoutActive = false;
		bool sessionLockoutPending = false;
		std::string sessionLockoutCode;
		std::string sessionLockoutReason;
		bool toggleTaskStarted = false;
		bool cursorRestoreTaskStarted = false;
		bool menuWatchTaskStarted = false;
		bool dumpStateTaskStarted = false;
		bool chatKeyTaskStarted = false;
		int cursorRestoreFramesLeft = 0;
		bool menuMouseCursorVisible = true;
		bool menuPreferGamepad = false;
		bool gameForeground = false;
		bool hasLastCursorPos = false;
		POINT lastCursorPos{};
		std::chrono::steady_clock::time_point ignoreMouseUntil{};
		bool prismaReady = false;
		ULONGLONG firstMenuAdvanceMs = 0;
		std::uintptr_t originalMainMenuAdvanceMovie = 0;
		bool mainMenuAdvanceHookInstalled = false;
		bool menuAdvanceSinkRegistered = false;
		SetCursorPosFn realSetCursorPos = nullptr;
		ClipCursorFn realClipCursor = nullptr;
		bool cursorApiHooksInstalled = false;
	};

	constexpr auto kServerBrowserHtmlPath = "CommonwealthOnline/browser/index.html";
	constexpr auto kMainMenuHtmlPath = "CommonwealthOnline/main-menu/index.html";
	constexpr auto kPauseMenuHtmlPath = "CommonwealthOnline/pause-menu/index.html";
	constexpr auto kToggleVirtualKey = 0x78;
	constexpr auto kDumpStateVirtualKey = 0x7A;
	constexpr auto kGamepadMouseIgnore = std::chrono::milliseconds(500);
	constexpr LONG kMouseModeSwitchSlop = 10;

#define CO_STATE_REF(type, name) extern type& name
	CO_STATE_REF(PRISMA_UI_API::IVPrismaUI6*, g_api);
	CO_STATE_REF(F4T::PrismaViewState::Controller, g_mainMenuState);
	CO_STATE_REF(F4T::PrismaViewState::Controller, g_pauseMenuState);
	CO_STATE_REF(F4T::PrismaViewState::Controller, g_standaloneBrowserState);
	CO_STATE_REF(bool, g_browserDesiredVisible);
	CO_STATE_REF(bool, g_browserCurrentlyVisible);
	CO_STATE_REF(std::string, g_lastPanelBlockReason);
	CO_STATE_REF(PanelType, g_lastBlockedPanel);
	CO_STATE_REF(bool, g_createRetryArmed);
	CO_STATE_REF(std::atomic<bool>, g_mainMenuBridgeReady);
	CO_STATE_REF(bool, g_vanillaMainMenuSuppressed);
	CO_STATE_REF(bool, g_mainMenuOverlayReady);
	CO_STATE_REF(bool, g_mainMenuPanelEligible);
	CO_STATE_REF(bool, g_singlePlayerMode);
	CO_STATE_REF(std::string, g_lastMainMenuReadinessPending);
	CO_STATE_REF(bool, g_pauseMenuBridgeReady);
	CO_STATE_REF(bool, g_vanillaPauseMenuSuppressed);
	CO_STATE_REF(bool, g_pauseMenuQuitInProgress);
	CO_STATE_REF(bool, g_sessionLockoutActive);
	CO_STATE_REF(bool, g_sessionLockoutPending);
	CO_STATE_REF(std::string, g_sessionLockoutCode);
	CO_STATE_REF(std::string, g_sessionLockoutReason);
	CO_STATE_REF(bool, g_toggleTaskStarted);
	CO_STATE_REF(bool, g_cursorRestoreTaskStarted);
	CO_STATE_REF(bool, g_menuWatchTaskStarted);
	CO_STATE_REF(bool, g_dumpStateTaskStarted);
	CO_STATE_REF(bool, g_chatKeyTaskStarted);
	CO_STATE_REF(int, g_cursorRestoreFramesLeft);
	CO_STATE_REF(bool, g_menuMouseCursorVisible);
	CO_STATE_REF(bool, g_menuPreferGamepad);
	CO_STATE_REF(bool, g_gameForeground);
	CO_STATE_REF(bool, g_hasLastCursorPos);
	CO_STATE_REF(POINT, g_lastCursorPos);
	CO_STATE_REF(std::chrono::steady_clock::time_point, g_ignoreMouseUntil);
	CO_STATE_REF(bool, g_prismaReady);
	CO_STATE_REF(ULONGLONG, g_firstMenuAdvanceMs);
	CO_STATE_REF(std::uintptr_t, g_originalMainMenuAdvanceMovie);
	CO_STATE_REF(bool, g_mainMenuAdvanceHookInstalled);
	CO_STATE_REF(bool, g_menuAdvanceSinkRegistered);
	CO_STATE_REF(SetCursorPosFn, g_realSetCursorPos);
	CO_STATE_REF(ClipCursorFn, g_realClipCursor);
	CO_STATE_REF(bool, g_cursorApiHooksInstalled);
#undef CO_STATE_REF

	void LogInfo(std::string_view);
	void LogWarning(std::string_view);
	std::string GetLogPrefix();
	void HideMainMenuInternal(std::string_view);
	void ShowMainMenuInternal(std::string_view);
	void RequestQuitFromMainMenu();
	void RequestSinglePlayerFromMainMenu();
	void HideServerBrowserInternal(std::string_view);
	void ShowServerBrowserInternal(std::string_view);
	void CreateMainMenuView();
	void CreateServerBrowserView();
	void CreatePauseMenuView();
	void HidePauseMenuInternal(std::string_view);
	void ShowPauseMenuInternal(std::string_view);
	void ShowSessionLockoutInternal();
	void InvokeSessionLockoutScript();
	void WatchPauseMenuState();
	void RequestResumeFromPauseMenu();
	void RequestQuitToMainMenuFromPause();
	void RequestQuitToDesktopFromPause();
	void BeginPauseMenuQuit(bool);
	void NotifyExitSavePosted();
	void TickExitSaveDisconnect();
	void SetVanillaPauseMenuSuppressed(bool);
	void SyncBrowserHover();
	void WatchMainMenuState();
	void SetVanillaMainMenuSuppressed(bool);
	PrismaView GetActiveBrowserView();
	void ActivateEmbeddedBrowser(std::string_view);
	void DeactivateEmbeddedBrowser(std::string_view);
	void ClearBrowserControllerActions();
	void EnsureBrowserControllerActions();
	bool AreBrowserControllerActionsReady();
	void InstallMainMenuCursorReleaseHook();
	bool IsMenuRuntimeReady();
	void TryFinalizeMainMenu();
	void BindMainMenuDomHandlers(PrismaView);
	extern std::atomic<bool> g_menuServicesReady;
	void RegisterMenuAdvanceSink();
	void ReleaseMenuCursorConstraints();
	void RestoreMenuCursorForceOSCursorPos();
	void InstallCursorApiHooks();
	void ReconcilePauseMenuAfterDomReady();
	void ApplyMenuFocusForPointerMode(bool);
	void DumpAllViewStateInternal();
	void RestoreMainMenuFromSinglePlayerInternal(std::string_view);
	bool ApplyMainMenuState(std::string_view);
	bool ApplyPauseMenuState(std::string_view);
	const char* PanelTypeName(PanelType);
	PanelType GetActivePhysicalPanel();
	bool IsExternalPanelBlocking(PrismaView, PanelType, std::string_view);
	void CloseConflictingPanels(PanelType, std::string_view);
	bool OpenPanel(PanelType, std::string_view);
	void TickDeferredCreates();
	bool UsesLegacyMenuReadiness(PRISMA_UI_API::RuntimeState a_state);
	void ArmCreateRetryOnce();
	void ApplyBrowserPointerMode(bool);
	void UpdateAlertOverlayFocus();
	void ResetBrowserPointerTracking();
	void UpdateBrowserPointerMode();
	void InitializeBrowserPointerMode();
	void RestoreNativeGameCursor();
	void SuppressNativeGameCursor();
	void RequestNativeCursorRestore();
	void DispatchMainMenuControllerInputInternal(const char*, const char*);
	void DispatchPauseMenuControllerInputInternal(const char*, const char*);
	void DispatchPauseMenuControllerAxesInternal(float, float, float, float);
	void DispatchPauseMenuMapUpdateInternal(std::string_view);
	void DispatchPauseMenuServerInfoInternal(std::string_view);
	void DispatchPauseMenuWorldModeInternal(std::string_view);

	inline bool IsMainMenuOverlayUp()
	{
		return g_mainMenuState.IsCurrentlyVisible() || g_mainMenuState.IsDesiredVisible();
	}

	inline bool IsPauseMenuOverlayUp()
	{
		return g_pauseMenuState.IsCurrentlyVisible() || g_pauseMenuState.IsDesiredVisible();
	}

	inline bool IsBrowserOverlayUp()
	{
		return g_browserCurrentlyVisible || g_browserDesiredVisible;
	}

	inline bool ShouldOverrideMenuCursorConstraints()
	{
		return F4T::PrismaUI::IsGameForeground() &&
			(g_mainMenuState.IsCurrentlyVisible() || g_browserCurrentlyVisible ||
				g_pauseMenuState.IsCurrentlyVisible() || F4T::CharacterCreator::IsOverlayUp());
	}
}

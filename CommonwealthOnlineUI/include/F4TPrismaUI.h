#pragma once

#include <string_view>

namespace F4T::PrismaUI
{
	void PrepareMenuRuntime();
	void OnGameDataReady();

	void ShowServerBrowser();
	void HideServerBrowser();
	bool IsBrowserVisible();
	bool IsBrowserControllerFocused();
	bool IsBrowserControllerInputOwned();

	void ShowMainMenu();
	void HideMainMenu();
	void RestoreMainMenuFromSinglePlayer();
	bool IsMainMenuVisible();
	void DispatchMainMenuControllerInput(const char* a_button, const char* a_phase);

	void OnFalloutMainMenuOpened();
	void OnFalloutMainMenuClosed();

	void SetMainMenuOverlayReady(bool a_ready, std::string_view a_reason);
	void ReconcileMainMenuOverlayState(std::string_view a_reason);
	void SetVanillaMessageBoxOpen(bool a_open);

	void ShowPauseMenu();
	void HidePauseMenu();
	bool IsPauseMenuVisible();
	void ShowSessionLockout(const char* a_code, const char* a_reason);
	void NotifySessionExitSave(const char* a_state, const char* a_message);
	void DispatchPauseMenuControllerInput(const char* a_button, const char* a_phase);
	void DispatchPauseMenuControllerAxes(float a_leftX, float a_leftY, float a_rightX, float a_rightY);
	void DispatchPauseMenuMapUpdate(std::string_view a_json);
	void DispatchPauseMenuServerInfo(std::string_view a_json);

	void PreferGamepadPointerMode();
	bool IsGameForeground();

	bool IsCustomMenuInputCaptured();

	void DumpAllViewState();
	void NotifyPostSaveGame();
}

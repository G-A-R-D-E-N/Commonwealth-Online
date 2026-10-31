#pragma once

#include "CommonwealthOnlineUI_API.h"
#include "F4TFactions.h"
#include <cstdint>
#include <string>

namespace F4T::UIClient
{
	void OnGameDataReady();
	std::string GetMultiplayerDisplayName();

	void ShowMainMenu();
	void HideMainMenu();
	bool IsMainMenuVisible();
	void ShowServerBrowser();
	void HideServerBrowser();
	bool IsBrowserVisible();
	void DispatchMainMenuControllerInput(const char* a_button, const char* a_phase);
	bool IsCustomMenuInputCaptured();
	void PushConnectionStatus(const char* a_status = nullptr);
	void PushWorldMode(const char* a_mode);
	void ResetConnectionOwnership();
	void ClearJoinInProgress();
	void NotifyFactionStateChanged();
	void NotifyTransportDisconnected(const char* a_reason = "Connection lost.");
	bool IsVoluntaryExitRequested();
	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1* GetCachedUI();
	void ShowSessionEndedNotification(const char* a_code, const char* a_reason);
	void ShowDisconnectScreen(const char* a_code, const char* a_reason);
	void ShowOfflineSaveIgnoredNotification();
	void ShowUnverifiedSaveNotification();
	void ShowHandshakeRejectionNotification(
		const char* a_code,
		const char* a_reason,
		const char* a_serverKey = nullptr,
		const char* a_modpackUrl = nullptr);
	void ClearRejectedResumeCredentialAndRetry(const char* a_serverKey = nullptr);
	bool RejoinServer(const char* a_host, std::uint16_t a_port, const char* a_name, const char* a_id);
}

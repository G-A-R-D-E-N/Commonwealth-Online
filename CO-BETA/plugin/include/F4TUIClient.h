#pragma once

#include "CommonwealthOnlineUI_API.h"
#include "F4TAdminClient.h"
#include "F4TFactions.h"
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

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
	std::uint64_t ShowSaveChoiceAlert(const std::string& a_message, bool a_canRetry, std::function<void(std::string_view)> a_onChoice);
	void DismissSaveChoiceAlert(std::uint64_t a_generation);
	void PushWorldMode(const char* a_mode);
	void ResetConnectionOwnership();
	void AdoptJoinConnectionGeneration(std::uint64_t a_generation);
	void ClearJoinInProgress();
	void NotifyFactionStateChanged();
	void OpenInteractWheel();
	void CloseInteractWheel();
	void PushInteractWheelState(const char* a_state_json);
	void PushInteractPrompt(const char* a_json);
	[[nodiscard]] bool IsInteractWheelVisible();
	void NotifyTransportDisconnected(const char* a_reason = "Connection lost.");
	bool IsVoluntaryExitRequested();
	bool IsJoinInProgress();
	std::uint64_t JoinGeneration();
	bool TryReconnectAfterTransportLoss();
	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1* GetCachedUI();
	void ShowSessionEndedNotification(const char* a_code, const char* a_reason);
	void ShowDisconnectScreen(const char* a_code, const char* a_reason);
	void ShowOfflineSaveIgnoredNotification();
	void ShowUnverifiedSaveNotification();
	void ShowStartingInventoryRecoveryNotification(const char* a_message);
	void ShowHandshakeRejectionNotification(
		const char* a_code,
		const char* a_reason,
		const char* a_serverKey = nullptr,
		const char* a_modpackUrl = nullptr);
	bool RejoinServer(const char* a_host, std::uint16_t a_port, const char* a_name, const char* a_id);
}

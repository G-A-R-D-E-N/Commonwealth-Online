#include "pch.h"

#include "F4TSessionCharacterSetupFlow.h"

#include "F4TConsoleGuard.h"
#include "F4TSessionWorldState.h"

#include "CommonwealthOnlineUI_API.h"
#include "RE/C/Console.h"
#include "RE/M/MainMenu.h"
#include "RE/Q/QuestFlag.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESQuest.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

namespace F4T::SessionCharacterSetupFlow
{
	namespace
	{
		constexpr std::string_view kCharacterSetupQuestEditorId = "CO_CharacterSetupQuest";
		constexpr std::uint32_t kMq102FormId = 0x0001CC2A;
		constexpr std::uint16_t kMq102PodExitStage = 2;
		constexpr const char* kContinueAlertId = "co-vanilla-start-continue";
		constexpr const char* kReturnToMenuAlertId = "co-vanilla-start-return";

		std::atomic<bool> g_onboardingIssued{ false };
		std::atomic<bool> g_continueIssued{ false };
		std::atomic<bool> g_continueChosen{ false };
		std::atomic<bool> g_alertWanted{ false };
		std::atomic<bool> g_alertNeedsShow{ false };

		void LogInfo(std::string_view a_message)
		{
			REX::INFO("[SessionCharacterSetupFlow] {}", a_message);
		}

		void LogWarning(std::string_view a_message)
		{
			REX::WARN("[SessionCharacterSetupFlow] {}", a_message);
		}

		void DismissContinueAlert()
		{
			auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
				COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
			if (ui && ui->DismissAlert) {
				ui->DismissAlert(kContinueAlertId);
			}
		}

		void OnContinueAlertResponse(const char*, const char* a_actionId, void*)
		{
			if (a_actionId && std::string_view{ a_actionId } == "continue") {
				g_alertWanted = false;
				g_continueChosen = true;
				return;
			}
			if (g_alertWanted) {
				g_alertNeedsShow = true;
			}
		}
	}

	void Reset()
	{
		g_onboardingIssued = false;
		g_continueIssued = false;
		g_continueChosen = false;
		g_alertWanted = false;
		g_alertNeedsShow = false;
	}

	bool StartVanillaNewGame()
	{
		auto* mainMenu = F4T::SessionWorldState::GetMainMenu();
		if (!mainMenu) {
			LogWarning("New game not started: the main menu is not open.");
			return false;
		}

		Scaleform::GFx::Value bgsCode;
		bool found = mainMenu->menuObj.IsObject() && mainMenu->menuObj.GetMember("BGSCodeObj", &bgsCode) && bgsCode.IsObject();
		if (!found && mainMenu->uiMovie) {
			found = mainMenu->uiMovie->GetVariable(&bgsCode, "BGSCodeObj") && bgsCode.IsObject();
		}
		if (!found || !bgsCode.Invoke("StartNewGame")) {
			LogWarning("New game not started: BGSCodeObj.StartNewGame is unavailable on the main menu.");
			return false;
		}
		LogInfo("Invoked BGSCodeObj.StartNewGame from the main menu.");
		return true;
	}

	bool BeginOnboarding()
	{
		if (g_onboardingIssued) {
			return true;
		}
		if (!RE::TESForm::GetFormByEditorID<RE::TESQuest>(RE::BSFixedString{ kCharacterSetupQuestEditorId })) {
			return false;
		}

		g_onboardingIssued = true;
		F4T::ConsoleGuard::ExecutePluginCommand("startquest CO_CharacterSetupQuest");
		F4T::ConsoleGuard::ExecutePluginCommand("cqf CO_CharacterSetupQuest BeginVanillaOnboarding");
		LogInfo("Started CO_CharacterSetupQuest and invoked BeginVanillaOnboarding.");
		return true;
	}

	bool ShowContinueAlert()
	{
		auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
			COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
		if (!ui || !ui->ShowAlert) {
			LogWarning("Cannot show the Continue prompt because the CO alert API is unavailable.");
			return false;
		}

		std::array<COMMONWEALTH_ONLINE_UI_API::AlertAction, 1> actions{ {
			{ "continue", "Skip to the vault (T)", "T" }
		} };

		COMMONWEALTH_ONLINE_UI_API::AlertRequest request{};
		request.id = kContinueAlertId;
		request.category = "Onboarding";
		request.title = "Character ready";
		request.message = "Continue to Vault 111 to join the server.";
		request.tone = "info";
		request.durationMs = 0;
		request.actions = actions.data();
		request.actionCount = actions.size();

		g_alertWanted = true;
		g_alertNeedsShow = false;
		if (!ui->ShowAlert(&request, &OnContinueAlertResponse, nullptr, nullptr, 0)) {
			LogWarning("Failed to show the Continue prompt.");
			return false;
		}
		return true;
	}

	bool PollContinueAlert()
	{
		if (g_continueChosen) {
			return true;
		}
		if (g_alertNeedsShow) {
			static_cast<void>(ShowContinueAlert());
		}
		return false;
	}

	void ShowReturnToMenuAlert()
	{
		auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
			COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
		if (!ui || !ui->ShowAlert) {
			LogWarning("Cannot show the return-to-menu prompt because the CO alert API is unavailable.");
			return;
		}

		COMMONWEALTH_ONLINE_UI_API::AlertRequest request{};
		request.id = kReturnToMenuAlertId;
		request.category = "Onboarding";
		request.title = "Could not start the new game";
		request.message = "Return to the main menu to join this server for the first time.";
		request.tone = "warning";
		if (!ui->ShowAlert(&request, nullptr, nullptr, nullptr, 0)) {
			LogWarning("Failed to show the return-to-menu prompt.");
		}
	}

	void ContinueToVault()
	{
		if (g_continueIssued) {
			return;
		}

		g_continueIssued = true;
		g_alertWanted = false;
		DismissContinueAlert();
		F4T::ConsoleGuard::ExecutePluginCommand("cqf CO_CharacterSetupQuest ContinueToVault");
		LogInfo("Invoked CO_CharacterSetupQuest ContinueToVault for the post-war cryo teleport.");
	}

	void Abort()
	{
		if (g_alertWanted) {
			g_alertWanted = false;
			DismissContinueAlert();
		}

		if (!g_onboardingIssued) {
			return;
		}

		F4T::ConsoleGuard::ExecutePluginCommand("cqf CO_CharacterSetupQuest AbortOnboarding");
		LogInfo("Invoked CO_CharacterSetupQuest AbortOnboarding to restore player state.");
	}

	bool IsPodExitSignal()
	{
		const auto* quest = RE::TESForm::GetFormByID<RE::TESQuest>(kMq102FormId);
		return quest && quest->currentStage >= kMq102PodExitStage;
	}

	bool IsComplete()
	{
		const auto* quest = RE::TESForm::GetFormByEditorID<RE::TESQuest>(
			RE::BSFixedString{ kCharacterSetupQuestEditorId });
		return quest && quest->currentStage >= 100;
	}

	void EnsureRunning()
	{
		auto* quest = RE::TESForm::GetFormByEditorID<RE::TESQuest>(
			RE::BSFixedString{ kCharacterSetupQuestEditorId });
		if (!quest) {
			LogWarning("CO_CharacterSetupQuest is not in the load order; quest sync has no script host.");
			return;
		}

		if ((quest->data.flags & static_cast<std::uint16_t>(RE::QuestFlag::kEnabled)) != 0) {
			F4T::ConsoleGuard::ExecutePluginCommand("cqf CO_CharacterSetupQuest Prime");
			LogInfo("Primed COQuestSyncScript on the running CO_CharacterSetupQuest.");
			return;
		}

		F4T::ConsoleGuard::ExecutePluginCommand("startquest CO_CharacterSetupQuest");
		LogInfo("Started CO_CharacterSetupQuest because it was not running.");
	}

}

#include "pch.h"

#include "F4TSessionLaunch.h"

#include "F4TIntroQuestProbe.h"
#include "F4TJoinFlow.h"
#include "F4TNetworking.h"
#include "F4TSessionCharacterSetupFlow.h"
#include "F4TSessionLaunchDiagnostics.h"
#include "F4TSessionLaunchInternal.h"
#include "F4TServerProfileStore.h"
#include "F4TSessionSaveFlow.h"
#include "F4TSessionSaveQueue.h"
#include "F4TSessionWorldState.h"

#include "RE/P/PlayerCharacter.h"

#include <chrono>
#include <format>
#include <string>
#include <string_view>

	using namespace F4T::SessionSaveFlow;
	using namespace F4T::SessionWorldState;
	using namespace F4T::SessionLaunchDiagnostics;

namespace F4T::SessionLaunch::Internal
{
	std::atomic<bool> g_newGameMessageSeen{ false };
	bool g_specialMenuSeen = false;
	std::chrono::steady_clock::time_point g_specialClosedSince{};
	int g_spawnPollFrameCounter = 0;
	int g_spawnWaitAttempts = 0;
	std::chrono::steady_clock::time_point g_waitDeadline{};
	int g_worldReadyPolls = 0;
	std::chrono::steady_clock::time_point g_continueAlertRefreshTime{};
	int g_continueAlertRetries = 0;

	void ArmWaitDeadline(std::chrono::seconds a_budget)
	{
		g_spawnWaitAttempts = 0;
		g_waitDeadline = std::chrono::steady_clock::now() + a_budget;
	}

	bool WaitDeadlinePassed()
	{
		return std::chrono::steady_clock::now() >= g_waitDeadline;
	}

	void QueueFirstTimeJoin(const std::string& a_serverKey, bool a_resumed)
	{
		g_pendingProfileBind = true;
		g_pendingProfileBindServerKey = a_serverKey;
		g_gameplayReadyReached = false;
		g_spawnPollFrameCounter = 0;
		ArmWaitDeadline(a_resumed ? kVaultExitBudget : kSpawnWaitBudget);
		g_worldReadyPolls = 0;
		g_profileSaveQueued = false;
		F4T::IntroQuestProbe::BeginFirstJoinTrace();
		if (a_resumed) {
			SetPhase(CharGenPhase::WaitingForGameplayWorldReady, "resumed saved vault character");
			LogInfo(std::format("Resumed vault onboarding for server {}.", a_serverKey));
			return;
		}
		SetPhase(CharGenPhase::StartingNewGame, "first-time server profile requires onboarding");
		LogInfo(std::format("Queued first-time vanilla start for server {}.", a_serverKey));
	}

	void TryFirstJoinFlowOnGameThread()
	{
		if (!IsFirstJoinInProgress()) {
			return;
		}

		F4T::IntroQuestProbe::Tick();

		if (!F4T::Networking::IsConnectedToServer()) {
			FailFirstTimeJoin("Connection ended before first-time onboarding completed.");
			return;
		}

		const auto* player = RE::PlayerCharacter::GetSingleton();

		if (g_charGenPhase == CharGenPhase::StartingNewGame) {
			if (!g_newGameMessageSeen || !player || IsLoadingScreenActive() ||
			    !F4T::SessionCharacterSetupFlow::BeginOnboarding()) {
				if (WaitDeadlinePassed()) {
					FailFirstTimeJoin("Timed out waiting for the new game to start from the main menu.");
				}
				return;
			}

			ArmWaitDeadline(kVanillaIntroBudget);
			SetPhase(CharGenPhase::VanillaIntro, "new game started; vanilla intro running");
			return;
		}

		if (g_charGenPhase == CharGenPhase::VanillaIntro) {
			if (IsMenuOpen("SPECIALMenu")) {
				g_specialMenuSeen = true;
				g_specialClosedSince = {};
				ArmWaitDeadline(kVanillaIntroBudget);
				return;
			}

			if (g_specialMenuSeen) {
				if (IsMenuOpen("MessageBoxMenu")) {
					g_specialClosedSince = {};
					return;
				}
				const auto now = std::chrono::steady_clock::now();
				if (g_specialClosedSince == std::chrono::steady_clock::time_point{}) {
					g_specialClosedSince = now;
					return;
				}
				if (now - g_specialClosedSince < std::chrono::seconds{ 1 }) {
					return;
				}
				if (!F4T::SessionCharacterSetupFlow::ShowContinueAlert()) {
					if (++g_continueAlertRetries < kContinueAlertRetryPolls) {
						return;
					}
					FailFirstTimeJoin("Could not show the Continue prompt after SPECIAL; the CO alert API is unavailable.");
					return;
				}
				ArmWaitDeadline(kContinueWaitBudget);
				g_continueAlertRefreshTime = std::chrono::steady_clock::now() + kContinueAlertRefreshInterval;
				SetPhase(CharGenPhase::WaitingForContinue, "SPECIAL menu closed; Continue prompt shown");
				return;
			}

			if (WaitDeadlinePassed()) {
				FailFirstTimeJoin("Timed out waiting for the vanilla intro to reach the SPECIAL menu.");
			}
			return;
		}

		if (g_charGenPhase == CharGenPhase::WaitingForContinue) {
			if (IsMenuOpen("MessageBoxMenu") || IsMenuOpen("SPECIALMenu")) {
				return;
			}
			if (F4T::SessionCharacterSetupFlow::PollContinueAlert()) {
				F4T::SessionCharacterSetupFlow::ContinueToVault();
				ArmWaitDeadline(kCharacterSetupBudget);
				F4T::SessionCharacterSetupFlow::ReleaseContinuePrompt();
				SetPhase(CharGenPhase::WaitingForPodExit, "Continue chosen; post-war cryo teleport started");
				return;
			}

			if (WaitDeadlinePassed()) {
				FailFirstTimeJoin("Timed out waiting for the Continue prompt to be chosen.");
				return;
			}
			if (std::chrono::steady_clock::now() >= g_continueAlertRefreshTime) {
				g_continueAlertRefreshTime = std::chrono::steady_clock::now() + kContinueAlertRefreshInterval;
				const auto shown = F4T::SessionCharacterSetupFlow::ShowContinueAlert();
				LogInfo(std::format("[OnbDbg] 3 s refresh: re-registered and re-showed Continue prompt (ok={})", shown));
				if (!shown) {
					LogWarning("Continue prompt refresh failed; the CO alert API is unavailable.");
				}
			}
			return;
		}

		if (g_charGenPhase == CharGenPhase::WaitingForPodExit) {
			if (F4T::SessionCharacterSetupFlow::IsComplete() && F4T::SessionCharacterSetupFlow::IsPodExitSignal()) {
				ArmWaitDeadline(kVaultExitBudget);
				g_worldReadyPolls = 0;
				F4T::SessionCharacterSetupFlow::ReleaseContinuePrompt();
				SetPhase(
					CharGenPhase::WaitingForGameplayWorldReady,
					"CO_CharacterSetupQuest reached stage 100 and MQ102 reached pod exit");
				if (player) {
					LogPlayerLocation("Vault teleport complete; waiting for the gameplay world", *player);
				}
				return;
			}

			if (WaitDeadlinePassed()) {
				FailFirstTimeJoin("Timed out waiting for the post-war cryo teleport and pod exit.");
			}
			return;
		}

		if (g_charGenPhase == CharGenPhase::WaitingForGameplayWorldReady) {
			++g_spawnWaitAttempts;
			if (!player || !IsGameplayWorldReady(*player)) {
				g_worldReadyPolls = 0;
				LogGameplayReadyPending(player);
				if (WaitDeadlinePassed()) {
					FailFirstTimeJoin(
						"Timed out waiting for a valid non-solo gameplay cell after the vault teleport.");
				}
				return;
			}

			LogGameplayReadyPending(player);
			if (++g_worldReadyPolls < kGameplayReadyStablePolls) {
				return;
			}

			const auto queued = QueueProfileSave();
			if (!queued) {
				if (WaitDeadlinePassed()) FailFirstTimeJoin("Survival readiness could not be confirmed. Disconnect and contact the server administrator.");
				return;
			}
			if (!*queued) {
				FailFirstTimeJoin("The first multiplayer save could not be queued: save manager unavailable or a session exit save is pending.");
				return;
			}
			g_gameplayReadyReached = true;
			ArmWaitDeadline(kSpawnWaitBudget);
			SetPhase(CharGenPhase::WaitingForProfileBind, "gameplay world remained stable across readiness polls");
			LogPlayerLocation("Gameplay-ready gate passed; enabling transform publication", *player);
			return;
		}

		if (g_charGenPhase == CharGenPhase::WaitingForProfileBind) {
			if (++g_spawnWaitAttempts == 1 || g_spawnWaitAttempts % kPendingLogIntervalPolls == 0) {
				LogInfo("Waiting for the first gameplay save to complete and bind the server profile.");
			}
			if (!g_firstJoinSettle.has_value() && WaitDeadlinePassed()) {
				FailFirstJoinAttempt("Timed out waiting for the first gameplay save to bind the server profile.");
			}
		}
	}

	F4T::SessionLaunch::LaunchOutcome StartFirstTimeJoin(
		const std::string& a_serverKey,
		F4T::SessionLaunch::JoinCompletion a_onComplete,
		std::string& a_error)
	{
		const auto slot = ServerProfileStore::ConnectedCharacterSlot();
		if (!ServerProfileStore::RecordFirstJoinHold(a_serverKey, 0, "", "First join started; save and inventory require reconciliation.", slot)) {
			g_activeProfileServerKey.clear();
			a_error = "Could not safely record first-join recovery. Contact the server administrator.";
			return F4T::SessionLaunch::LaunchOutcome::Failed;
		}
		g_newGameMessageSeen = false;
		if (!F4T::SessionCharacterSetupFlow::StartVanillaNewGame()) {
			g_activeProfileServerKey.clear();
			F4T::SessionCharacterSetupFlow::ShowReturnToMenuAlert();
			a_error = "Could not start a new game from the main menu. Use the Commonwealth Online server browser to join this server for the first time.";
			return F4T::SessionLaunch::LaunchOutcome::Failed;
		}
		g_pendingJoinCompletion = std::move(a_onComplete);
		QueueFirstTimeJoin(a_serverKey);
		F4T::JoinFlow::EndForFirstTimeJoin();
		return F4T::SessionLaunch::LaunchOutcome::Pending;
	}

	F4T::SessionLaunch::LaunchOutcome ResumeFirstTimeJoin(
		const std::string& a_serverKey,
		const std::string& a_resumeSave,
		F4T::SessionLaunch::JoinCompletion a_onComplete,
		std::string& a_error)
	{
		std::string loadError;
		if (!LoadSaveFromMainMenu(a_resumeSave, loadError)) {
			g_activeProfileServerKey.clear();
			g_activeProfileCharacterId.clear();
			a_error = loadError.empty() ? std::string{ "Your saved vault character could not be loaded. Your saves have not been changed." } : loadError;
			return F4T::SessionLaunch::LaunchOutcome::Failed;
		}
		g_pendingJoinCompletion = std::move(a_onComplete);
		QueueFirstTimeJoin(a_serverKey, true);
		F4T::JoinFlow::EndForFirstTimeJoin();
		return F4T::SessionLaunch::LaunchOutcome::Pending;
	}
}

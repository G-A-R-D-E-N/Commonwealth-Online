#include "pch.h"

#include "F4TSessionLaunch.h"
#include "F4TInventoryGrant.h"

#include "F4TCharacterSlotsClient.h"
#include "F4TClientIdentity.h"
#include "F4TFirstJoinAbandon.h"
#include "F4TDebugLogging.h"
#include "F4TDownedGlue.h"
#include "F4TIntroQuestProbe.h"
#include "F4TJoinFlow.h"
#include "F4TNetworking.h"
#include "F4TSessionCharacterSetupFlow.h"
#include "F4TSaveLineageSession.h"
#include "F4TSaveProfileLogic.h"
#include "F4TServerProfileStore.h"
#include "F4TSessionLaunchDiagnostics.h"
#include "F4TSessionLaunchInternal.h"
#include "F4TSessionSaveFlow.h"
#include "F4TSessionSaveGuard.h"
#include "F4TSessionSaveQueue.h"
#include "F4TSessionWorldState.h"
#include "F4TSoloCell.h"
#include "F4TSteamJoinHold.h"
#include "F4TTeleportOverlay.h"
#include "F4TUIClient.h"
#include "F4TVatsPolicy.h"

#include "RE/B/BGSSaveLoadManager.h"
#include "RE/L/LoadingMenu.h"
#include "RE/M/MainMenu.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESWorldSpace.h"
#include "RE/U/UI.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <optional>
#include <utility>
#include <vector>

using namespace F4T::SessionSaveFlow;
using namespace F4T::SessionWorldState;
using namespace F4T::SessionLaunchDiagnostics;

namespace F4T::SessionLaunch
{
	using namespace Internal;

	LaunchOutcome LaunchValidatedSession(
		const JoinRequest& a_request,
		std::string& a_error,
		JoinCompletion a_onComplete)
	{
		if (IsFirstJoinInProgress() || g_pendingProfileBind) {
			a_error = "A first-time session launch is already pending.";
			return LaunchOutcome::Failed;
		}
		F4T::SessionCharacterSetupFlow::Abort();
		F4T::JoinFlow::ReleaseVaultExitControls("join launch", true);
		ResetFirstJoinState();

		const auto host = a_request.host.empty() ? "127.0.0.1" : a_request.host;
		const auto port = a_request.port > 0 ? a_request.port : static_cast<std::uint16_t>(7777);
		const auto [profileHost, profilePort] = ServerProfileStore::ProfileEndpoint(host, port, a_request.id);
		const auto serverKey = ServerProfileStore::MakeServerKey(profileHost, profilePort);
		const auto resolvedServerKey = ServerProfileStore::ResolveServerKey(profileHost, profilePort);
		const auto profileKey = resolvedServerKey.value_or(serverKey);
		SetActiveServerKey("");
		SetActiveCharacterId("");
		ServerProfileStore::RepairFirstJoinBackup(profileKey);
		const bool onMainMenu = IsMainMenuOpen();
		const auto gameplaySave = ServerProfileStore::LookupSaveFile(profileKey);
		const auto resumeSave = ServerProfileStore::LookupOnboardingResumeSave(profileKey);
		const bool resumeOnboarding = F4T::FirstJoinAttempt::ShouldResumeOnboarding(
			onMainMenu,
			gameplaySave.has_value() && !IsFirstJoinSetupSave(*gameplaySave),
			resumeSave.has_value() && IsFirstJoinSetupSave(*resumeSave));
		if (F4T::CharacterSlotsClient::NeedsSelection()) {
			LogDebug("session launch deferred until character selection");
			F4T::CharacterSlotsClient::DeferLaunch(a_request, [request = a_request, onComplete = a_onComplete]() {
				if (!F4T::Networking::IsConnectedToServer()) {
					F4T::UIClient::ClearJoinInProgress();
					if (onComplete) {
						onComplete(false, "Connection cancelled.");
					}
					return;
				}
				std::string error;
				const auto outcome = LaunchValidatedSession(request, error, onComplete);
				if (outcome == LaunchOutcome::Pending || !onComplete) {
					return;
				}
				if (outcome == LaunchOutcome::Failed) {
					LogWarning(std::format("Deferred session launch for {}:{} failed: {}", request.host, request.port, error));
					onComplete(false, error);
				} else if (outcome == LaunchOutcome::Complete && F4T::Networking::IsConnectedToServer()) {
					onComplete(true, "");
				} else {
					onComplete(false, "Connection ended before the join could complete.");
				}
			}, a_onComplete);
			F4T::UIClient::PushConnectionStatus("Select a character to continue.");
			return LaunchOutcome::Pending;
		}
		if (!resumeOnboarding && ServerProfileStore::HasFirstJoinHold(profileKey)) {
			return F4T::FirstJoinAbandon::BeginForSaveLessHold(profileKey, std::move(a_onComplete), a_error);
		}

		const bool inGame = RE::PlayerCharacter::GetSingleton() != nullptr;
		const bool hasProfile = resolvedServerKey.has_value();
		if (ServerProfileStore::HasUnconfirmedCharacterBinding(profileKey)) {
			a_error = "The selected character has no confirmed local save binding. Create and select a fresh character to continue. Existing save files have not been changed.";
			return LaunchOutcome::Failed;
		}
		LogDebug(std::format(
			"join host={} port={} profileKey={} hasProfile={} onMainMenu={} inGame={}",
			host,
			port,
			profileKey,
			hasProfile,
			onMainMenu,
			inGame));

		if (!onMainMenu && !inGame) {
			a_error = "Game is not ready to launch a session.";
			return LaunchOutcome::Failed;
		}

		if (!onMainMenu && !hasProfile) {
			a_error = "Join this server from the Commonwealth Online server browser to create a new character.";
			return LaunchOutcome::Failed;
		}

		if (resumeOnboarding) {
			bool approved = false;
			if (!F4T::InventoryGrant::CheckLaunchSave(std::nullopt, approved)) {
				a_error = "Starting inventory needs recovery. Contact the server administrator before creating a character.";
				return LaunchOutcome::Failed;
			}
			LogInfo(std::format("Server profile '{}' has a saved vault character '{}'; resuming onboarding.", profileKey, *resumeSave));
			SetActiveServerKey(profileKey);
			SetActiveCharacterId("");
			return ResumeFirstTimeJoin(profileKey, *resumeSave, std::move(a_onComplete), a_error);
		}

		if (!hasProfile) {
			bool approved = false;
			if (!F4T::InventoryGrant::CheckLaunchSave(std::nullopt, approved)) {
				a_error = "Starting inventory needs recovery. Contact the server administrator before creating a character.";
				return LaunchOutcome::Failed;
			}
		}
		if (hasProfile) {
			auto saveFile = ServerProfileStore::LookupSaveFile(profileKey);
			if (saveFile && IsFirstJoinSetupSave(*saveFile)) {
				LogWarning(std::format(
					"Server profile '{}' is bound to onboarding save '{}'; treating onboarding as incomplete.",
					profileKey,
					*saveFile));
				saveFile.reset();
			}
			if (!saveFile) {
				bool approved = false;
				if (!F4T::InventoryGrant::CheckLaunchSave(std::nullopt, approved)) {
					a_error = "Starting inventory needs recovery. Contact the server administrator before creating a character.";
					return LaunchOutcome::Failed;
				}
				if (!onMainMenu) {
					a_error = "Server profile has no gameplay-ready associated save file. Join this server from the Commonwealth Online server browser to complete onboarding.";
					return LaunchOutcome::Failed;
				}

				LogWarning(std::format(
					"Server profile '{}' has no gameplay-ready bound save; running first-time flow.",
					profileKey));
				SetActiveServerKey(profileKey);
				SetActiveCharacterId("");
				return StartFirstTimeJoin(profileKey, std::move(a_onComplete), a_error);
			}

			std::string profileCharacterId = ServerProfileStore::LookupCharacterId(profileKey).value_or("");
			LogDebug(std::format(
				"profile save={} characterId={}",
				*saveFile,
				profileCharacterId.empty() ? "<legacy>" : profileCharacterId));
			if (profileCharacterId.empty()) {
				if (const auto parsedCharacterId = SaveProfileLogic::ExtractCharacterId(*saveFile)) {
					profileCharacterId = *parsedCharacterId;
				}
			}

			const bool isAdmin = F4T::ClientIdentity::IsConnectedServerAdmin();
			auto joinSave = ResolveJoinSave(profileKey, *saveFile, profileCharacterId, isAdmin);
			if (!joinSave.allowed && joinSave.verifiedHead && g_useVerifiedSave) {
				LogWarning(std::format("Joining server {} with the last verified save '{}' as the player chose; the newer save '{}' is untouched.", profileKey, *joinSave.verifiedHead, joinSave.saveFile.value_or("")));
				joinSave.saveFile = joinSave.verifiedHead;
				joinSave.allowed = true;
			}
			if (!joinSave.allowed && joinSave.continuation && !g_suppressContinuation && F4T::Networking::IsConnectedToServer()) {
				F4T::UIClient::PushConnectionStatus("Verifying your newest save with the server.");
				BeginContinuationWait(a_request, std::move(a_onComplete), profileKey, *joinSave.continuation);
				return LaunchOutcome::Pending;
			}
			if (!joinSave.allowed && joinSave.verifiedHead && F4T::Networking::IsConnectedToServer() &&
				BeginSaveChoice(a_request, a_onComplete, profileKey, joinSave.reason, *joinSave.verifiedHead, joinSave.continuation.has_value())) {
				return LaunchOutcome::Pending;
			}
			if (!joinSave.allowed) {
				LogWarning(std::format("Join refused for server {}: save '{}' is not usable on this PC: {}", profileKey, *saveFile, joinSave.reason));
				a_error = joinSave.reason;
				return LaunchOutcome::Failed;
			}
			if (joinSave.saveFile && *joinSave.saveFile != *saveFile) {
				LogInfo(std::format(
					"Updating associated save from '{}' to '{}' for server {}.",
					*saveFile,
					*joinSave.saveFile,
					profileKey));
				saveFile = joinSave.saveFile;
				if (!ServerProfileStore::BindSaveFile(profileKey, *saveFile)) {
					LogWarning(std::format(
						"Failed to persist the updated save binding for server {}; this session will still "
						"load '{}', but the rebind will be retried on the next join.",
						profileKey,
						*saveFile));
				}
			}

			std::string loadError;
			bool loadStarted = false;

			if (onMainMenu) {
				loadStarted = LoadSaveFromMainMenu(*saveFile, loadError);
				if (!loadStarted && IsMissingAssociatedSaveError(loadError)) {
					const auto latestSave = ResolveJoinSave(profileKey, *saveFile, profileCharacterId, isAdmin);
					if (!latestSave.allowed) {
						LogWarning(std::format("Join refused for server {}: save '{}' is not usable on this PC after reload: {}", profileKey, *saveFile, latestSave.reason));
						a_error = latestSave.reason;
						return LaunchOutcome::Failed;
					}
					if (latestSave.saveFile && *latestSave.saveFile != *saveFile) {
						LogInfo(std::format(
							"Retrying load with latest character save '{}' (previous '{}').",
							*latestSave.saveFile,
							*saveFile));
						saveFile = latestSave.saveFile;
						if (!ServerProfileStore::BindSaveFile(profileKey, *saveFile)) {
							LogWarning(std::format(
								"Failed to persist the retried save binding for server {}; this session will "
								"still load '{}', but the rebind will be retried on the next join.",
								profileKey,
								*saveFile));
						}
						loadStarted = LoadSaveFromMainMenu(*saveFile, loadError);
					}
				}
				if (!loadStarted && IsMissingAssociatedSaveError(loadError)) {
					LogWarning(std::format(
						"Associated save '{}' for server '{}' is missing; refusing replacement character creation.",
						*saveFile,
						profileKey));
					a_error = "Your associated save was not found. Keep your existing saves and contact the server administrator for recovery.";
					return LaunchOutcome::Failed;
				}
			} else {
				loadStarted = LoadSaveInGame(*saveFile, loadError);
				if (!loadStarted && IsMissingAssociatedSaveError(loadError)) {
					a_error = std::format(
						"Associated save '{}' was not found. Keep your existing saves and contact the server administrator for recovery.",
						*saveFile);
					return LaunchOutcome::Failed;
				}
			}

			if (!loadStarted) {
				a_error = loadError;
				return LaunchOutcome::Failed;
			}

			SetActiveServerKey(profileKey);
			SetActiveCharacterId(profileCharacterId);
			if (const auto loadedCharacterId = SaveProfileLogic::ExtractCharacterId(*saveFile)) {
				SetActiveCharacterId(*loadedCharacterId);
			}
			if (!ServerProfileStore::TouchProfile(profileKey)) {
				LogWarning(std::format(
					"Failed to persist the last-joined timestamp for server {}; the gameplay load is "
					"unaffected.",
					profileKey));
			}
			LogInfo(std::format("Loading associated save '{}' for server {}.", *saveFile, profileKey));
			return LaunchOutcome::Complete;
		}

		if (!onMainMenu) {
			a_error = "Use the Commonwealth Online server browser to join this server for the first time.";
			return LaunchOutcome::Failed;
		}

		LogDebug(std::format("no profile found; starting first-time flow for {}", serverKey));
		SetActiveServerKey(serverKey);
		SetActiveCharacterId("");
		return StartFirstTimeJoin(serverKey, std::move(a_onComplete), a_error);
	}

	bool CancelPendingJoin(std::string_view a_reason)
	{
		if (CancelContinuationWait(a_reason.empty() ? "Connection cancelled." : a_reason)) {
			return true;
		}
		if (F4T::FirstJoinAbandon::CancelPending()) {
			return true;
		}
		if (!IsFirstJoinInProgress() && !g_pendingProfileBind) {
			return false;
		}

		CompletePendingJoin(false, a_reason.empty() ? "Connection cancelled." : a_reason);
		return true;
	}

	bool CanPublishGameplayTransform()
	{
		if (g_charGenPhase == CharGenPhase::None || g_charGenPhase == CharGenPhase::Complete) {
			return true;
		}
		return g_charGenPhase == CharGenPhase::WaitingForProfileBind && g_gameplayReadyReached;
	}

	bool IsFirstJoinOnboarding()
	{
		return IsFirstJoinInProgress();
	}

	bool IsFirstJoinSaveInFlight()
	{
		return g_profileSaveQueued || g_firstJoinAttempt.has_value() || g_firstJoinSettle.has_value();
	}

	std::string ActiveProfileServerKey()
	{
		return g_activeProfileServerKey;
	}

	std::string ActiveCharacterId()
	{
		const std::scoped_lock lock(g_activeProfileMutex);
		return g_activeProfileCharacterId;
	}

	std::string ActiveServerKey()
	{
		const std::scoped_lock lock(g_activeProfileMutex);
		return g_activeProfileServerKey;
	}

	void Update()
	{
		F4T::FirstJoinAbandon::Tick();
		TickContinuationWait();
		F4T::JoinFlow::Tick();
		F4T::TeleportOverlay::Tick();
		F4T::Steam::SetJoinHeld(!F4T::JoinFlow::HasLeftVault());
		if (!IsFirstJoinInProgress() && !g_pendingProfileBind) {
			return;
		}

		TryCompleteFirstJoinSettle();

			if (++g_spawnPollFrameCounter % kSpawnPollIntervalFrames != 0) {
			return;
		}

		TryFirstJoinFlowOnGameThread();
	}
	void OnF4SEMessage(F4SE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}

		switch (a_msg->type) {
		case F4SE::MessagingInterface::kPostSaveGame: {
			const auto savedName = F4T::SaveLineageSession::ReadPostSaveGameName(*a_msg);
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask([]() {
					F4T::Downed::OnWorldSaved();
				});
			}
			const auto onboardingSave = ResolveBindablePostSaveName(savedName);
			if (F4T::FirstJoinAttempt::ShouldRecordOnboardingResume(
					g_pendingProfileBind, IsPastPodExit(), IsFirstJoinSetupSave(savedName), savedName) &&
				!ServerProfileStore::RecordOnboardingResume(g_pendingProfileBindServerKey, savedName)) {
				LogWarning(std::format("Could not record vault resume save '{}'.", savedName));
			}
			TryCompletePendingProfileBind(onboardingSave);
			if (g_pendingProfileBind) break;
			std::string activeServerKey;
			std::string activeCharacterId;
			{
				const std::scoped_lock lock(g_activeProfileMutex);
				activeServerKey = g_activeProfileServerKey;
				activeCharacterId = g_activeProfileCharacterId;
			}
			F4T::SaveLineageSession::TryUpdateActiveProfileBinding(activeServerKey, activeCharacterId, g_pendingProfileBind, onboardingSave, savedName, F4T::SessionSaveGuard::TakeLateExitSave());
			break;
		}
		case F4SE::MessagingInterface::kNewGame:
			g_newGameMessageSeen = true;
			break;
		case F4SE::MessagingInterface::kPostLoadGame:
		case F4SE::MessagingInterface::kGameLoaded:
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask([]() {
					F4T::VatsPolicy::OnGameLoaded();
					if (IsFirstJoinInProgress()) {
						TryFirstJoinFlowOnGameThread();
					} else {
						F4T::SessionCharacterSetupFlow::EnsureRunning();
					}
				});
			}
			if (a_msg->type == F4SE::MessagingInterface::kGameLoaded) {
				if (const auto* taskInterface = F4SE::GetTaskInterface()) {
					taskInterface->AddTask([]() {
						F4T::Downed::OnWorldLoaded();
					});
				}
			}
			break;
		default:
			break;
		}
	}
}

#include "pch.h"

#include "F4TSessionLaunch.h"

#include "F4TCharacterSlotsClient.h"
#include "F4TClientIdentity.h"
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
		if (IsFirstJoinInProgress() || g_pendingProfileBind) {
			a_error = "A first-time session launch is already pending.";
			return LaunchOutcome::Failed;
		}
		ResetFirstJoinState();

		const auto host = a_request.host.empty() ? "127.0.0.1" : a_request.host;
		const auto port = a_request.port > 0 ? a_request.port : static_cast<std::uint16_t>(7777);
		const auto [profileHost, profilePort] = ServerProfileStore::ProfileEndpoint(host, port, a_request.id);
		const auto serverKey = ServerProfileStore::MakeServerKey(profileHost, profilePort);
		const auto resolvedServerKey = ServerProfileStore::ResolveServerKey(profileHost, profilePort);
		const auto profileKey = resolvedServerKey.value_or(serverKey);
		g_activeProfileServerKey.clear();
		g_activeProfileCharacterId.clear();

		const bool onMainMenu = IsMainMenuOpen();
		const bool inGame = RE::PlayerCharacter::GetSingleton() != nullptr;
		const bool hasProfile = resolvedServerKey.has_value();
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
			a_error = "Return to the main menu to create a new character for this server.";
			return LaunchOutcome::Failed;
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
				if (!onMainMenu) {
					a_error = "Server profile has no gameplay-ready associated save file. Return to the main menu to complete onboarding.";
					return LaunchOutcome::Failed;
				}

				LogWarning(std::format(
					"Server profile '{}' has no gameplay-ready bound save; running first-time flow.",
					profileKey));
				g_activeProfileServerKey = profileKey;
				g_activeProfileCharacterId.clear();
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
			const auto joinSave = ResolveJoinSave(profileKey, *saveFile, profileCharacterId, isAdmin);
			if (!joinSave.allowed) {
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
						"Associated save '{}' for server '{}' is missing; running first-time flow.",
						*saveFile,
						profileKey));
					g_activeProfileServerKey = profileKey;
					g_activeProfileCharacterId.clear();
					return StartFirstTimeJoin(profileKey, std::move(a_onComplete), a_error);
				}
			} else {
				loadStarted = LoadSaveInGame(*saveFile, loadError);
				if (!loadStarted && IsMissingAssociatedSaveError(loadError)) {
					a_error = std::format(
						"Associated save '{}' was not found. Return to the main menu to create a replacement save for this server.",
						*saveFile);
					return LaunchOutcome::Failed;
				}
			}

			if (!loadStarted) {
				a_error = loadError;
				return LaunchOutcome::Failed;
			}

			g_activeProfileServerKey = profileKey;
			g_activeProfileCharacterId = profileCharacterId;
			if (const auto loadedCharacterId = SaveProfileLogic::ExtractCharacterId(*saveFile)) {
				g_activeProfileCharacterId = *loadedCharacterId;
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
			a_error = "Return to the main menu to join this server for the first time.";
			return LaunchOutcome::Failed;
		}

		LogDebug(std::format("no profile found; starting first-time flow for {}", serverKey));
		g_activeProfileServerKey = serverKey;
		g_activeProfileCharacterId.clear();
		return StartFirstTimeJoin(serverKey, std::move(a_onComplete), a_error);
	}

	bool CancelPendingJoin(std::string_view a_reason)
	{
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

	bool CanApplyServerCorrection()
	{
		return g_charGenPhase == CharGenPhase::None || g_charGenPhase == CharGenPhase::Complete;
	}

	void Update()
	{
		F4T::JoinFlow::Tick();
		if (!IsFirstJoinInProgress() && !g_pendingProfileBind) {
			return;
		}

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
			TryCompletePendingProfileBind(onboardingSave);
			if (onboardingSave && g_pendingProfileBind && IsPastPodExit()) {
				const auto onboardingCharacterId = BindableCharacterId(*onboardingSave);
				if (!onboardingCharacterId) {
					break;
				}
				F4T::SaveLineageSession::TryUpdateActiveProfileBinding(g_pendingProfileBindServerKey, *onboardingCharacterId, false, onboardingSave, savedName);
				break;
			}
			F4T::SaveLineageSession::TryUpdateActiveProfileBinding(g_activeProfileServerKey, g_activeProfileCharacterId, g_pendingProfileBind, onboardingSave, savedName);
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

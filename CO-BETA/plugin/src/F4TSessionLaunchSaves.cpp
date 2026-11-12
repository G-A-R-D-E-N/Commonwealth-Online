#include "pch.h"

#include "F4TSessionLaunch.h"

#include "F4TNetworking.h"
#include "F4TReplicationRuntime.h"
#include "F4TSurvivalEnforcer.h"
#include "F4TSurvivalPolicyClient.h"
#include "F4TSessionWorldState.h"

#include "RE/B/BGSSaveLoadManager.h"
#include "RE/P/PlayerCharacter.h"
#include "F4TSaveLineageSession.h"
#include "F4TSaveProfileLogic.h"
#include "F4TServerProfileStore.h"
#include "F4TSessionLaunchDiagnostics.h"
#include "F4TSessionLaunchInternal.h"
#include "F4TSessionSaveGuard.h"
#include "F4TSessionSaveQueue.h"
#include "F4TUIClient.h"

#include <chrono>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

	using namespace F4T::SessionLaunchDiagnostics;

namespace F4T::SessionLaunch::Internal
{
	bool g_profileSaveQueued = false;

	std::optional<F4T::FirstJoinAttempt::Scope> FirstJoinScope()
	{
		const auto connection = F4T::Networking::GetConnectionGeneration();
		const auto session = F4T::ReplicationRuntime::CurrentGeneration();
		const auto policyRevision = F4T::SurvivalPolicy::g_clientState.ReadyRevision(connection, session);
		if (!policyRevision) {
			return std::nullopt;
		}
		return F4T::FirstJoinAttempt::Scope{ connection, session, F4T::SurvivalPolicy::g_clientState.LoadEpoch(), *policyRevision };
	}

	std::optional<bool> QueueProfileSave()
	{
		const auto connection = F4T::Networking::GetConnectionGeneration();
		const auto session = F4T::ReplicationRuntime::CurrentGeneration();
		auto* player = RE::PlayerCharacter::GetSingleton();
		const bool loaded = player && F4T::SessionWorldState::IsGameplayWorldReady(*player);
		std::optional<bool> queued;
		F4T::Networking::TryRunForConnectionGeneration(connection, [&] {
			if (session != F4T::ReplicationRuntime::CurrentGeneration()) return;
			const bool admitted = F4T::SurvivalEnforcer::Tick(connection, session, loaded, [&] {
				const auto scope = FirstJoinScope();
				auto* saveLoadManager = RE::BGSSaveLoadManager::GetSingleton();
				if (F4T::SessionSaveGuard::IsSessionExitPending() || !scope || !saveLoadManager) {
					queued = false;
					return;
				}
				F4T::FirstJoinAttempt::Token token{ F4T::FirstJoinAttempt::NextId(), *scope };
				token.baselineMostRecentSave = saveLoadManager->mostRecentSaveGame ? saveLoadManager->mostRecentSaveGame : "";
				token.baselineLastSaveTime = saveLoadManager->lastSaveTime;
				if (!F4T::SessionSaveQueue::StartQueuedSave(F4T::SessionSaveGuard::ArmOwner::kFirstJoin, "queued first-join save", token.id)) {
					queued = false;
					return;
				}
				g_firstJoinAttempt = std::move(token);
				g_profileSaveQueued = true;
				queued = true;
			});
			if (!admitted && queued == true) {
				F4T::SessionSaveQueue::StopQueue(F4T::SessionSaveGuard::ArmOwner::kFirstJoin);
				F4T::SessionSaveGuard::DisarmInternalSave("Survival admission changed");
				g_profileSaveQueued = false;
				g_firstJoinAttempt.reset();
				queued.reset();
			}
		});
		return queued;
	}

	std::optional<std::string> BindableCharacterId(const std::string& a_savedName)
	{
		const auto characterId = SaveProfileLogic::ExtractCharacterId(a_savedName);
		if (!characterId || characterId->empty()) {
			LogWarning(std::format(
				"Refusing to bind the server profile to save '{}': the save name carries no character id.",
				a_savedName));
			return std::nullopt;
		}
		return characterId;
	}

	void FailFirstJoinAttempt(std::string_view a_reason)
	{
		LogWarning(std::format("First-join save attempt failed: {}", a_reason));
		if (!g_pendingProfileBindServerKey.empty()) {
			const auto attemptId = g_firstJoinAttempt ? g_firstJoinAttempt->id : std::uint64_t{};
			const auto saveName = g_firstJoinSettle ? g_firstJoinSettle->saveName : std::string{};
			const auto slot = saveName.empty() ? ServerProfileStore::ConnectedCharacterSlot() : std::nullopt;
			if (!ServerProfileStore::RecordFirstJoinHold(g_pendingProfileBindServerKey, attemptId, saveName, std::string{ a_reason }, slot)) {
				LogWarning("Could not record the first-join hold in the server profile store.");
			}
		}
		SetActiveServerKey("");
		FailFirstTimeJoin("Your first save could not be confirmed. Disconnect, keep your saves, and contact the server administrator before rejoining.");
	}

}

namespace F4T::SessionLaunch
{
	using namespace Internal;
	using namespace F4T::SessionLaunchDiagnostics;

	void TryCompletePendingProfileBind(const std::optional<std::string>& a_savedName)
	{
		if (!a_savedName ||
			g_pendingProfileBindServerKey.empty() ||
			!g_pendingProfileBind ||
			!g_profileSaveQueued ||
			!F4T::Networking::IsConnectedToServer() ||
			g_charGenPhase != CharGenPhase::WaitingForProfileBind) {
			return;
		}

		const auto characterId = BindableCharacterId(*a_savedName);
		const auto scope = FirstJoinScope();
		if (!characterId || !scope || !g_firstJoinAttempt ||
			!F4T::FirstJoinAttempt::CallbackAccepted(
				*g_firstJoinAttempt,
				*scope,
				g_firstJoinSettle.has_value(),
				F4T::SessionWorldState::IsFirstJoinSetupSave(*a_savedName),
				*a_savedName,
				*characterId)) {
			FailFirstJoinAttempt("the gameplay save callback did not match the first-join attempt");
			return;
		}

		g_firstJoinSettle = FirstJoinSettle{ *a_savedName, *characterId, std::chrono::steady_clock::now() + kFirstJoinSettleBudget };
		LogInfo(std::format("First-join save '{}' completed; waiting for it to settle before binding server {}.", *a_savedName, g_pendingProfileBindServerKey));
	}

}

namespace F4T::SessionLaunch::Internal
{
	void TryCompleteFirstJoinSettle()
	{
		if (!g_firstJoinSettle || !g_firstJoinAttempt) {
			return;
		}
		const auto settle = *g_firstJoinSettle;
		const auto attempt = *g_firstJoinAttempt;
		const auto scope = FirstJoinScope();
		if (!scope || !F4T::FirstJoinAttempt::SameScope(attempt.scope, *scope)) {
			FailFirstJoinAttempt("the connection, session, survival load or policy revision changed before the first save settled");
			return;
		}
		if (F4T::FirstJoinAttempt::SettleExpired(settle.deadline, std::chrono::steady_clock::now())) {
			FailFirstJoinAttempt("the first gameplay save did not settle and attest in time");
			return;
		}
		const auto entry = F4T::SessionSaveQueue::FirstJoinEntry();
		if (F4T::FirstJoinAttempt::EntryPending(attempt, entry)) {
			return;
		}
		if (!F4T::FirstJoinAttempt::EntryAccepted(attempt, entry)) {
			FailFirstJoinAttempt("the engine entry for the first gameplay save did not match the attempt");
			return;
		}
		if (settle.sha256Hex.empty()) {
			const auto sha256 = F4T::SaveLineageSession::HashSaveForRecovery(settle.saveName);
			if (!sha256) {
				return;
			}
			g_firstJoinSettle->sha256Hex = *sha256;
			F4T::SaveLineageSession::ExpectFirstJoinAttestation(settle.saveName, *sha256);
			if (!F4T::SaveLineageSession::AttestFirstJoinSave(g_pendingProfileBindServerKey, settle.saveName, *sha256)) {
				FailFirstJoinAttempt("the first gameplay save could not be sent for attestation");
			}
			return;
		}
		const auto outcome = F4T::SaveLineageSession::FirstJoinAttestationOutcome();
		if (outcome == F4T::SaveLineageSession::AttestationOutcome::Pending) {
			return;
		}
		if (outcome == F4T::SaveLineageSession::AttestationOutcome::Rejected) {
			FailFirstJoinAttempt("the server did not accept the first gameplay save attestation");
			return;
		}
		const auto sha256 = F4T::SaveLineageSession::HashSaveForRecovery(settle.saveName);
		if (!sha256 || *sha256 != settle.sha256Hex) {
			FailFirstJoinAttempt("the first gameplay save changed after attestation");
			return;
		}
		if (!F4T::Networking::IsConnectedToServer()) {
			FailFirstJoinAttempt("the server connection closed before the first gameplay save was bound");
			return;
		}
		const auto serverKey = g_pendingProfileBindServerKey;
		if (!ServerProfileStore::BindSaveFileClearingHold(serverKey, settle.saveName)) {
			FailFirstJoinAttempt("the first gameplay save could not be bound to the server profile");
			return;
		}
		SetActiveServerKey(serverKey);
		SetActiveCharacterId(settle.characterId);
		CompletePendingJoin(true, "the first gameplay save is attested and bound to the server profile");
	}
}

namespace F4T::SessionLaunch::Internal
{
	namespace
	{
		struct ContinuationWait
		{
			JoinRequest request;
			JoinCompletion onComplete;
			std::string serverKey;
			F4T::SaveLineageSession::ContinuationPlan plan;
			std::chrono::steady_clock::time_point deadline{};
		};

		struct SaveChoiceWait
		{
			JoinRequest request;
			JoinCompletion onComplete;
			std::string serverKey;
			std::string reason;
			std::string verifiedHead;
			std::uint64_t serial{ 0 };
			std::uint64_t alertGeneration{ 0 };
		};

		std::optional<ContinuationWait> g_continuationWait;
		std::optional<SaveChoiceWait> g_saveChoiceWait;
		std::uint64_t g_saveChoiceSerial = 0;
	}

	bool g_suppressContinuation = false;
	bool g_useVerifiedSave = false;

	static void RunJoinAgain(const JoinRequest& a_request, const JoinCompletion& a_onComplete)
	{
		std::string error;
		const auto launch = LaunchValidatedSession(a_request, error, a_onComplete);
		if (launch == LaunchOutcome::Pending || !a_onComplete) {
			return;
		}
		if (launch == LaunchOutcome::Failed) {
			a_onComplete(false, error);
		} else if (F4T::Networking::IsConnectedToServer()) {
			a_onComplete(true, "");
		} else {
			a_onComplete(false, "Connection ended before the join could complete.");
		}
	}

	bool BeginSaveChoice(const JoinRequest& a_request, JoinCompletion a_onComplete, const std::string& a_serverKey, const std::string& a_reason, const std::string& a_verifiedHead, bool a_canRetry)
	{
		CancelContinuationWait("Another join replaced this one.");
		const auto serial = ++g_saveChoiceSerial;
		const auto savedAt = F4T::SaveProfileLogic::FormatSaveTimestamp(a_verifiedHead);
		const auto message = std::format(
			"{} continue from your last verified save{}. Your newer save stays in your save folder. Progress made after that save will not be in this session.",
			a_canRetry ? "Your newest save could not be verified. You can retry, or" : "Your newest save could not be verified, so it cannot be loaded on this server. You can",
			savedAt ? std::format(" (saved {})", *savedAt) : std::string{});
		g_saveChoiceWait = SaveChoiceWait{ a_request, std::move(a_onComplete), a_serverKey, a_reason, a_verifiedHead, serial };
		g_saveChoiceWait->request.serverPassword.reset();
		const auto alertGeneration = F4T::UIClient::ShowSaveChoiceAlert(message, a_canRetry, [serial](std::string_view a_action) {
			if (!g_saveChoiceWait || g_saveChoiceWait->serial != serial) {
				return;
			}
			auto wait = std::move(*g_saveChoiceWait);
			g_saveChoiceWait.reset();
			if (a_action != "retry" && a_action != "use_verified") {
				LogInfo(std::format("The save choice for server {} was dismissed.", wait.serverKey));
				if (wait.onComplete) {
					wait.onComplete(false, wait.reason);
				}
				return;
			}
			g_useVerifiedSave = a_action == "use_verified";
			LogWarning(std::format("The player chose '{}' for server {} (verified save '{}').", a_action, wait.serverKey, wait.verifiedHead));
			RunJoinAgain(wait.request, wait.onComplete);
			g_useVerifiedSave = false;
		});
		if (alertGeneration == 0) {
			g_saveChoiceWait.reset();
			return false;
		}
		if (g_saveChoiceWait && g_saveChoiceWait->serial == serial) {
			g_saveChoiceWait->alertGeneration = alertGeneration;
		}
		return true;
	}

	bool CancelContinuationWait(std::string_view a_reason)
	{
		JoinCompletion onComplete;
		if (g_continuationWait) {
			onComplete = std::move(g_continuationWait->onComplete);
			g_continuationWait.reset();
		} else if (g_saveChoiceWait) {
			onComplete = std::move(g_saveChoiceWait->onComplete);
			F4T::UIClient::DismissSaveChoiceAlert(g_saveChoiceWait->alertGeneration);
			g_saveChoiceWait.reset();
		} else {
			return false;
		}
		if (onComplete) {
			onComplete(false, a_reason);
		}
		return true;
	}

	void BeginContinuationWait(const JoinRequest& a_request, JoinCompletion a_onComplete, const std::string& a_serverKey, const F4T::SaveLineageSession::ContinuationPlan& a_plan)
	{
		CancelContinuationWait("Another join replaced this one.");
		g_continuationWait = ContinuationWait{ a_request, std::move(a_onComplete), a_serverKey, a_plan, std::chrono::steady_clock::now() + kContinuationWaitBudget };
		g_continuationWait->request.serverPassword.reset();
		if (!F4T::SaveLineageSession::RequestContinuationAttestation(a_serverKey, a_plan)) {
			LogWarning(std::format("Could not ask the server to verify '{}'.", a_plan.saveFile));
			g_continuationWait->deadline = std::chrono::steady_clock::now();
			return;
		}
		LogInfo(std::format("Asked the server to verify '{}' as the continuation of '{}'.", a_plan.saveFile, a_plan.parentSaveFile));
	}

	void TickContinuationWait()
	{
		if (g_saveChoiceWait && !F4T::Networking::IsConnectedToServer()) {
			CancelContinuationWait("Connection ended before the join could complete.");
		}
		if (!g_continuationWait) {
			return;
		}
		const auto outcome = F4T::SaveLineageSession::FirstJoinAttestationOutcome();
		const bool connected = F4T::Networking::IsConnectedToServer();
		const bool expired = std::chrono::steady_clock::now() >= g_continuationWait->deadline;
		if (connected && !expired && outcome == F4T::SaveLineageSession::AttestationOutcome::Pending) {
			return;
		}
		auto wait = std::move(*g_continuationWait);
		g_continuationWait.reset();
		const bool accepted = connected && outcome == F4T::SaveLineageSession::AttestationOutcome::Accepted &&
		                      F4T::SaveLineageSession::CompleteContinuationAttestation(wait.serverKey, wait.plan);
		if (accepted) {
			LogInfo(std::format("The server verified '{}'; running the join again.", wait.plan.saveFile));
		} else {
			LogWarning(std::format("The server did not verify '{}' (connected={}, timedOut={}); the save stays unattested.", wait.plan.saveFile, connected, expired));
		}
		g_suppressContinuation = true;
		RunJoinAgain(wait.request, wait.onComplete);
		g_suppressContinuation = false;
	}
}

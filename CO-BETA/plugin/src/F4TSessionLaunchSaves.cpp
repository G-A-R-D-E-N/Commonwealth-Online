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

#include <chrono>
#include <cstdint>
#include <format>
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

#include "pch.h"

#include "F4TSessionLaunch.h"

#include "F4TIntroQuestProbe.h"
#include "F4TSessionCharacterSetupFlow.h"
#include "F4TSessionLaunchDiagnostics.h"
#include "F4TSessionLaunchInternal.h"
#include "F4TSessionSaveGuard.h"
#include "F4TSessionSaveQueue.h"
#include "F4TUIClient.h"

#include <chrono>
#include <format>
#include <string>
#include <string_view>

	using namespace F4T::SessionLaunchDiagnostics;

namespace F4T::SessionLaunch::Internal
{
	std::string g_pendingProfileBindServerKey;
	std::string g_activeProfileServerKey;
	std::string g_activeProfileCharacterId;
	F4T::SessionLaunch::JoinCompletion g_pendingJoinCompletion;
	CharGenPhase g_charGenPhase = CharGenPhase::None;
	bool g_pendingProfileBind = false;
	bool g_gameplayReadyReached = false;

	void SetPhase(CharGenPhase a_phase, std::string_view a_reason)
	{
		if (g_charGenPhase == a_phase) {
			return;
		}
		LogInfo(std::format(
			"Onboarding phase {} -> {}: {}",
			PhaseName(g_charGenPhase),
			PhaseName(a_phase),
			a_reason));
		g_charGenPhase = a_phase;
	}

	void ClearFirstJoinWorkingState(std::string_view a_reason)
	{
		F4T::SessionSaveGuard::DisarmInternalSave(a_reason);
		F4T::SessionSaveQueue::StopQueue(F4T::SessionSaveGuard::ArmOwner::kFirstJoin);
		F4T::IntroQuestProbe::EndFirstJoinTrace();
		F4T::SessionCharacterSetupFlow::Reset();
		g_pendingProfileBind = false;
		g_pendingProfileBindServerKey.clear();
		g_gameplayReadyReached = false;
		g_specialMenuSeen = false;
		g_spawnPollFrameCounter = 0;
		ArmWaitDeadline(kSpawnWaitBudget);
		g_worldReadyPolls = 0;
		g_profileSaveQueued = false;
		g_profileSaveRequeued = false;
		g_continueAlertRetries = 0;
	}

	void ResetFirstJoinState()
	{
		ClearFirstJoinWorkingState("first-join state reset");
		g_pendingJoinCompletion = {};
		g_charGenPhase = CharGenPhase::None;
	}

	void CompletePendingJoin(bool a_success, std::string_view a_reason)
	{
		if (!a_success) {
			F4T::SessionCharacterSetupFlow::Abort();
		}

		auto completion = std::move(g_pendingJoinCompletion);
		g_pendingJoinCompletion = {};
		if (a_success) {
			SetPhase(CharGenPhase::Complete, a_reason);
		} else {
			SetPhase(CharGenPhase::Failed, a_reason);
		}
		ClearFirstJoinWorkingState(a_reason);

		if (completion) {
			completion(a_success, a_reason);
		}
	}

	void FailFirstTimeJoin(std::string_view a_reason)
	{
		LogWarning(a_reason);
		CompletePendingJoin(false, a_reason);
		F4T::UIClient::ShowDisconnectScreen("disconnected", std::string{ a_reason }.c_str());
	}

	bool IsPastPodExit()
	{
		return g_charGenPhase == CharGenPhase::WaitingForGameplayWorldReady ||
		       g_charGenPhase == CharGenPhase::WaitingForProfileBind;
	}

	bool IsFirstJoinInProgress()
	{
		return g_charGenPhase != CharGenPhase::None &&
		       g_charGenPhase != CharGenPhase::Complete &&
		       g_charGenPhase != CharGenPhase::Failed;
	}

}

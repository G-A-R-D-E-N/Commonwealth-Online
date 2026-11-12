#pragma once

#include "F4TSessionLaunch.h"
#include "F4TSessionLaunchDiagnostics.h"
#include "F4TFirstJoinAttempt.h"
#include "F4TSaveLineageSession.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace RE
{
	class PlayerCharacter;
}

namespace F4T::SessionLaunch::Internal
{

inline constexpr int kSpawnPollIntervalFrames = 15;
inline constexpr auto kSpawnWaitBudget = std::chrono::seconds{ 150 };
inline constexpr auto kVanillaIntroBudget = std::chrono::minutes{ 30 };
inline constexpr auto kContinueWaitBudget = std::chrono::minutes{ 30 };
inline constexpr auto kVaultExitBudget = std::chrono::minutes{ 30 };
inline constexpr auto kContinueAlertRefreshInterval = std::chrono::seconds{ 3 };
inline constexpr int kContinueAlertRetryPolls = 5;
inline constexpr int kGameplayReadyStablePolls = 10;
inline constexpr auto kCharacterSetupBudget = std::chrono::seconds{ 300 };
inline constexpr int kPendingLogIntervalPolls = 40;
inline constexpr auto kFirstJoinSettleBudget = std::chrono::seconds{ 30 };
inline constexpr auto kContinuationWaitBudget = std::chrono::seconds{ 10 };

struct FirstJoinSettle
{
	std::string saveName;
	std::string characterId;
	std::chrono::steady_clock::time_point deadline{};
	std::string sha256Hex;
};

extern std::string g_pendingProfileBindServerKey;
extern std::string g_activeProfileServerKey;
extern std::string g_activeProfileCharacterId;
extern std::mutex g_activeProfileMutex;
void SetActiveServerKey(const std::string& a_serverKey);
void SetActiveCharacterId(const std::string& a_characterId);
extern F4T::SessionLaunch::JoinCompletion g_pendingJoinCompletion;
extern SessionLaunchDiagnostics::CharGenPhase g_charGenPhase;
extern bool g_pendingProfileBind;
extern bool g_gameplayReadyReached;
extern std::atomic<bool> g_newGameMessageSeen;
extern bool g_specialMenuSeen;
extern std::chrono::steady_clock::time_point g_specialClosedSince;
extern int g_spawnPollFrameCounter;
extern int g_spawnWaitAttempts;
extern std::chrono::steady_clock::time_point g_waitDeadline;
extern int g_worldReadyPolls;
extern bool g_profileSaveQueued;
extern std::optional<F4T::FirstJoinAttempt::Token> g_firstJoinAttempt;
extern std::optional<FirstJoinSettle> g_firstJoinSettle;
extern std::chrono::steady_clock::time_point g_continueAlertRefreshTime;
extern int g_continueAlertRetries;
extern bool g_suppressContinuation;
extern bool g_useVerifiedSave;

void SetPhase(SessionLaunchDiagnostics::CharGenPhase a_phase, std::string_view a_reason);
void LogPlayerLocation(std::string_view a_label, const RE::PlayerCharacter& a_player);
void LogGameplayReadyPending(const RE::PlayerCharacter* a_player);
void ArmWaitDeadline(std::chrono::seconds a_budget);
bool WaitDeadlinePassed();
void ClearFirstJoinWorkingState(std::string_view a_reason);
void ResetFirstJoinState();
void QueueFirstTimeJoin(const std::string& a_serverKey, bool a_resumed = false);
void CompletePendingJoin(bool a_success, std::string_view a_reason);
void FailFirstTimeJoin(std::string_view a_reason);
void FailFirstJoinAttempt(std::string_view a_reason);
std::optional<bool> QueueProfileSave();
bool IsPastPodExit();
bool IsFirstJoinInProgress();
void TryFirstJoinFlowOnGameThread();
void TryCompleteFirstJoinSettle();
LaunchOutcome StartFirstTimeJoin(
	const std::string& a_serverKey,
	JoinCompletion a_onComplete,
	std::string& a_error);
LaunchOutcome ResumeFirstTimeJoin(
	const std::string& a_serverKey,
	const std::string& a_resumeSave,
	JoinCompletion a_onComplete,
	std::string& a_error);
std::optional<std::string> BindableCharacterId(const std::string& a_savedName);
void BeginContinuationWait(const JoinRequest& a_request, JoinCompletion a_onComplete, const std::string& a_serverKey, const F4T::SaveLineageSession::ContinuationPlan& a_plan);
void TickContinuationWait();
bool CancelContinuationWait(std::string_view a_reason);
bool BeginSaveChoice(const JoinRequest& a_request, JoinCompletion a_onComplete, const std::string& a_serverKey, const std::string& a_reason, const std::string& a_verifiedHead, bool a_canRetry);
}

#pragma once

#include "F4TSessionLaunch.h"
#include "F4TSessionLaunchDiagnostics.h"

#include <atomic>
#include <chrono>
#include <cstdint>
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

extern std::string g_pendingProfileBindServerKey;
extern std::string g_activeProfileServerKey;
extern std::string g_activeProfileCharacterId;
extern F4T::SessionLaunch::JoinCompletion g_pendingJoinCompletion;
extern SessionLaunchDiagnostics::CharGenPhase g_charGenPhase;
extern bool g_pendingProfileBind;
extern bool g_gameplayReadyReached;
extern std::atomic<bool> g_newGameMessageSeen;
extern bool g_specialMenuSeen;
extern int g_spawnPollFrameCounter;
extern int g_spawnWaitAttempts;
extern std::chrono::steady_clock::time_point g_waitDeadline;
extern int g_worldReadyPolls;
extern bool g_profileSaveQueued;
extern bool g_profileSaveRequeued;
extern std::chrono::steady_clock::time_point g_continueAlertRefreshTime;
extern int g_continueAlertRetries;

void SetPhase(SessionLaunchDiagnostics::CharGenPhase a_phase, std::string_view a_reason);
void LogPlayerLocation(std::string_view a_label, const RE::PlayerCharacter& a_player);
void LogGameplayReadyPending(const RE::PlayerCharacter* a_player);
void ArmWaitDeadline(std::chrono::seconds a_budget);
bool WaitDeadlinePassed();
void ClearFirstJoinWorkingState(std::string_view a_reason);
void ResetFirstJoinState();
void QueueFirstTimeJoin(const std::string& a_serverKey);
void CompletePendingJoin(bool a_success, std::string_view a_reason);
void FailFirstTimeJoin(std::string_view a_reason);
bool QueueProfileSave();
bool IsPastPodExit();
bool IsFirstJoinInProgress();
void TryFirstJoinFlowOnGameThread();
LaunchOutcome StartFirstTimeJoin(
	const std::string& a_serverKey,
	JoinCompletion a_onComplete,
	std::string& a_error);
std::optional<std::string> BindableCharacterId(const std::string& a_savedName);
}

#pragma once

#include "F4TDownedEngine.h"
#include "F4TDownedGlue.h"
#include "F4TDownedUi.h"
#include "F4TDownedWire.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TProxyActorController.h"
#include "F4TRemotePlayerState.h"

#include "RE/P/PlayerCharacter.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace F4T::Downed::GlueInternal
{
	using F4T::Downed::DecodeStatus;
	using F4T::Downed::DownedEvent;
	using F4T::Downed::EventKind;
	using F4T::Downed::Phase;
	using F4T::Downed::RemotePoseEntry;
	using F4T::Downed::RespawnPoint;

	using RemoteSnapshot = std::vector<F4T::RemotePlayerState::RemotePlayerState>;

	inline constexpr std::string_view kLogComponent{ "Downed" };
	inline constexpr double kRemotePoseRetrySeconds = 1.0;
	inline constexpr double kChannelProgressStep = 0.02;
	inline constexpr std::size_t kMaximumPendingEvents = 64;

	inline F4T::Downed::State g_state;
	inline F4T::Downed::ReviveHold g_reviveHold;
	inline std::atomic<F4T::Downed::IDownedUiHost*> g_uiHost{ nullptr };
	inline std::atomic<F4T::Downed::IDownedTransport*> g_transport{ nullptr };
	inline std::atomic<bool> g_connectionResetPending{ false };

	inline bool g_poseApplied = false;
	inline bool g_overlayVisible = false;
	inline bool g_respawnMenuVisible = false;
	inline bool g_transportInstalled = false;
	inline double g_nextPoseRetrySeconds = 0.0;
	inline double g_nextRespawnRequestSeconds = 0.0;
	inline std::uint32_t g_respawnRequestAttempts = 0;
	inline std::optional<std::int32_t> g_lastOverlayRemainingMs;
	inline std::optional<bool> g_lastOverlayRescuerNearby;
	inline std::optional<RespawnPoint> g_pendingRespawnPoint;
	inline std::optional<std::uint32_t> g_pendingRespawnFriendId;
	// Constraint: only a checkpoint the server accepted may be offered as a point.
	inline std::optional<F4T::Downed::Checkpoint> g_checkpoint;

	inline F4T::Downed::ReviveChannelRunner g_channel;
	inline double g_lastChannelProgress{ -1.0 };
	inline bool g_localCanStimpak{ false };
	inline std::unordered_map<std::uint32_t, bool> g_remoteCanStimpak;

	inline std::unordered_map<std::uint32_t, RemotePoseEntry> g_remotePoses;

	inline std::mutex g_pendingMutex;
	inline std::deque<DownedEvent> g_pendingEvents;


	void EnterDownedPhase(double a_nowSeconds);
	void EnterDeadPhase(double a_nowSeconds);
	void LeaveIncapacitated(bool a_restoreHealth);
	void HideOverlay();
	void HideRespawnMenu();
	void SyncLocalCanStimpak(bool a_downed);
	void ReapplyDownedPose(double a_nowSeconds);
	void HandleEngineLethalDamage(double a_nowSeconds);
	void EndSession();
	void ResetSession();
	void InstallTransport();
	RespawnPoint FallbackRespawnPoint();
	RespawnPoint AutomaticRespawnPoint();
	void RecordCheckpoint(const char* a_why);
	void RequestRespawn(RespawnPoint a_point, std::optional<std::uint32_t> a_friendId);
	std::vector<F4T::Downed::RespawnFriend> CollectFriends();
	void ShowRespawnMenu();
	void RetryPendingRespawn(double a_nowSeconds);
	void ApplyRespawnGrantEvent(const DownedEvent& a_event);
	void ApplyRespawnChoiceEvent(const DownedEvent& a_event);
	bool IsRescuerNearby(const RemoteSnapshot& a_snapshot);
	std::optional<std::uint32_t> FindReviveTarget(const RemoteSnapshot& a_snapshot);
	void UpdateReviveInteraction(const RemoteSnapshot& a_snapshot, double a_nowSeconds);
	std::string TargetName(std::uint32_t a_playerId, const RemoteSnapshot& a_snapshot);
	void ClearReviveChannel();
	void UpdateReviveChannel(const RemoteSnapshot& a_snapshot, double a_nowSeconds);
	void UpdateRemotePoses(const RemoteSnapshot& a_snapshot, double a_nowSeconds);
	void UpdateOverlay(const RemoteSnapshot& a_snapshot, double a_nowSeconds);
	void ApplyReviveStartedEvent(const DownedEvent& a_event, double a_nowSeconds);
	void ApplyReviveCancelledEvent(const DownedEvent& a_event);
	void QueueEvent(DownedEvent a_event);
	std::deque<DownedEvent> DrainEvents();
	void RestoreEvents(std::deque<DownedEvent>& a_events);
	void ApplyDownedStateEvent(const DownedEvent& a_event, double a_nowSeconds);
	void ApplyRevivedEvent(const DownedEvent& a_event);
	void ApplyDiedEvent(double a_nowSeconds);
	void ApplyPlayerDiedEvent(const DownedEvent& a_event, double a_nowSeconds);
	void ApplyHeldItemDroppedEvent(const DownedEvent& a_event);
	void ApplyPendingEvents(double a_nowSeconds);
}


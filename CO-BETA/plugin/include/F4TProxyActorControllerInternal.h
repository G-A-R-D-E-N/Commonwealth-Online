// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission.
#pragma once

#include <chrono>
#include <vector>

#include "F4TNetworking.h"
#include "F4TPowerArmorSync.h"
#include "F4TProxyPathPredicates.h"
#include "F4TDebugLogging.h"
#include "F4TGnsSnapshotEnvelope.h"
#include "F4TProxyAnimationSync.h"
#include "F4TProxyEquipmentDebounce.h"
#include "F4TProxyPuppet.h"
#include "F4TProxySneakAction.h"
#include "F4TRemotePlayerState.h"
#include "F4RemoteActionComponent.h"
#include "F4TInterpolationSystem.h"
#include "F4TProxyEquipmentStacks.h"
#include "F4TProxyMovementSmoothing.h"
#include "F4TProxySnapshotMovement.h"
#include "F4TProxyGameAPI.h"
#include "F4TSoloCell.h"
#include "F4TVtableGuard.h"
#include "F4TWeaponClassification.h"

#include "RE/A/ActorEquipManager.h"
#include "RE/A/ActorValue.h"
#include "RE/B/bhkCharacterController.h"
#include "RE/B/BIPED_OBJECT.h"
#include "RE/B/BipedAnim.h"
#include "RE/B/BGSObjectInstance.h"
#include "RE/A/AIProcess.h"
#include "RE/C/Console.h"
#include "RE/H/hkVector4.h"
#include "RE/I/IAnimationGraphManagerHolder.h"
#include "RE/N/NEW_REFR_DATA.h"
#include "RE/M/MiddleHighProcessData.h"
#include "RE/P/ProcessLists.h"
#include "RE/R/RESET_3D_FLAGS.h"
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESObjectARMO.h"
#include "RE/T/TESObjectWEAP.h"
#include "RE/T/TaskQueueInterface.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <mutex>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace F4T::ProxyActorController::detail
{
	constexpr auto kProxyEditorId = "F4TProxyRemotePlayer01REF";
	constexpr auto kProxyBaseActorEditorId = "COPlayerProxy";
	constexpr auto kFemaleProxyBaseActorEditorId = "COPlayerProxyFemale";
	constexpr auto kTestPluginName = "CommonwealthOnline.esp";
	constexpr RE::TESFormID kProxyFallbackLocalFormId = 0x0020A2;
	constexpr std::uint32_t kDebugCodsworthBaseFormId = 0x000179FF;

	constexpr float kPlaceAtMeProxySettleSeconds = 2.0F;
	constexpr float kPlaceAtMeProxyCandidateSearchSeconds = 5.0F;
	constexpr auto kDebugPlaceAtMeSpawnDetectionRadius = 700.0F;
	constexpr auto kWarningLogInterval = 5s;
	constexpr auto kRuntimeProxyVisibilityDiagnosticLogInterval = 2s;
	constexpr auto kProxyNeutralizationInterval = 2s;
	constexpr auto kNotInTestCellLogInterval = 30s;

	using RemotePlayer = F4T::RemotePlayerState::RemotePlayerState;
	using RemoteEquippedItem = F4T::RemotePlayerState::RemoteEquippedItem;
	using RemoteAppearance = F4T::RemotePlayerState::RemoteAppearance;

	enum class ActiveProxyKind
	{
		kRuntimeSpawned,
		kPlacedFallback
	};

	enum class RuntimeActorBaseSource
	{
		kCustomProxy,
		kVanillaDiagnosticNpc
	};

	enum class PlaceAtMeBackedProxySpawnState
	{
		kNotStarted,
		kCustomProxyAttempting,
		kCodsworthFallbackAttempting,
		kSettling,
		kPromoted,
		kFailed
	};


	enum class ProxyLifecycleState
	{
		kHeldNoRemote,
		kActive,
		kHeldDisconnected,
		kHeldLeftCell,
		kReusable,
		kSpawnFailed
	};

	enum class ProxyMovementDiagnosticMode
	{
		kRestore,
		kSmooth,
		kSnap
	};

	enum class ProxyNeutralizationReason
	{
		kSpawnPromoted,
		kSlotReassigned,
		kActiveMaintenance,
		kHolding
	};

	enum class ProxyAnimationSpeedBucket
	{
		kIdle,
		kSlow,
		kWalk,
		kRun,
		kSprint
	};

	struct ProxyActorSlot
	{
		std::uint32_t remotePlayerId{};
		std::uint32_t holdingPositionIndex{};
		RE::ActorHandle proxyHandle;
		RE::ActorHandle pendingSpawnHandle;
		ProxyLifecycleState lifecycleState{ ProxyLifecycleState::kHeldNoRemote };
		std::optional<ProxyLifecycleState> holdReason;
		std::optional<std::uint32_t> previousRemotePlayerId;
		PlaceAtMeBackedProxySpawnState spawnState{ PlaceAtMeBackedProxySpawnState::kNotStarted };
		RuntimeActorBaseSource spawnSource{ RuntimeActorBaseSource::kCustomProxy };
		std::uint32_t spawnBaseFormId{ 0 };
		std::unordered_set<std::uint32_t> preexistingCandidateRefs;
		std::optional<RE::NiPoint3> initialRemoteSpawnPosition;
		std::chrono::steady_clock::time_point candidateSearchStartTime{};
		std::chrono::steady_clock::time_point settleStartTime{};
		std::chrono::steady_clock::time_point heldSinceTime{};
		std::chrono::steady_clock::time_point lastPositionSyncTime{};
		std::chrono::steady_clock::time_point unresolvedHandleSinceTime{};
		std::chrono::steady_clock::time_point lastSmoothMoveTime{};
		double lastApproachSampleSeconds{ 0.0 };
		float puppetGraphSpeed{ -1.0F };
		F4T::ProxyMovementSmoothing::State movementSmoothing{};
		float puppetDirection{ 0.0F };
		RE::NiPoint3 targetPosition{};
		float targetHeading{ 0.0F };
		bool spawnAttempted{ false };
		bool spawnAttemptInProgress{ false };
		bool spawnSucceeded{ false };
		bool spawnFailed{ false };
		bool restoredForCurrentState{ false };
		bool firstSmoothedMovementLogged{ false };
		bool remoteStateMovedLogged{ false };
		bool reusableLogged{ false };
		bool visibilityRefreshPending{ true };
		bool visibilityRefreshLogged{ false };
		bool proxyObservedLoaded3D{ false };
		bool proxyPending3DRecovery{ false };
		bool proxy3DResetQueued{ false };
		bool proxy3DRecoverySettling{ false };
		std::chrono::steady_clock::time_point proxy3DRecoverySettleStartTime{};
		bool headMeshRefreshAttempted{ false };
		bool headMeshPendingStable{ false };
		std::optional<bool> lastObservedProxyVisible;
		bool skipSmoothFrameOnce{ false };
		std::optional<std::uint32_t> cachedProxyFormId;
		RE::NiPointer<RE::Actor> cachedProxyActor;
		bool hasLoggedInitialAnimationState{ false };
		std::uint64_t equipmentCleanupLogCount{ 0 };
		std::size_t equipmentCleanupLogState{ 0 };
		std::optional<bool> lastObservedMovingState;
		std::optional<bool> lastObservedSprintingState;
		std::optional<bool> lastObservedSneakingState;
		F4T::ProxySneakAction::SneakRequestState sneakRequest{};
		std::optional<bool> lastObservedJumpingState;
		std::optional<bool> lastObservedWeaponDrawnState;
		std::optional<std::string> lastObservedMovementType;
		std::optional<std::string> lastObservedRemoteCellId;
		std::optional<ProxyAnimationSpeedBucket> lastObservedMovementSpeedBucket;
		bool hasAppliedEquipmentState{ false };
		std::vector<RemoteEquippedItem> lastAppliedEquipmentItems;
		std::vector<RemoteEquippedItem> lastSeenEquipmentItems;
		F4T::ProxyEquipmentDebounce::State equipmentDebounce;
		F4T::PowerArmorSync::PowerArmorState lastAppliedPowerArmorState;
		std::uint32_t appliedPowerArmorFrameFormId{ 0 };
		F4T::PowerArmorSync::PlacedPowerArmorFrame placedPowerArmorFrame;
		const RE::ExtraDataList* syncedModdedWeaponExtra{ nullptr };
		std::uint32_t syncedModdedWeaponFormId{ 0 };
		// Constraint: holds only stacks this client added; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
		F4T::ProxyEquipmentStacks::Ledger syncedEquipmentStacks;
		std::chrono::steady_clock::time_point weaponGraphSettleUntil{};
		bool hasAppliedAppearanceState{ false };
		RemoteAppearance lastAppliedAppearance;
		std::optional<std::string> lastAppliedCharacterName;
		std::optional<RemotePlayer> lastRemotePlayerSnapshot;
		std::uint32_t lastProcessedActionSequence{ 0 };
		F4T::ProxyAnimationSync::ProxyAppliedAnimationState appliedAnimationState{};

		F4T::RemoteActionComponent::RemoteActionQueue actionQueue;

		F4T::ProxySnapshotMovement::State snapshotMovement{};
	};

	inline RE::ObjectRefHandle g_proxyHandle;
	inline RE::ObjectRefHandle g_runtimeProxyHandle;
	inline bool g_runtimeProxySpawnAttempted = false;
	inline bool g_runtimeProxySpawnAttemptInProgress = false;
	inline bool g_runtimeProxySpawnSucceeded = false;
	inline bool g_runtimeProxySpawnFailed = false;
	inline bool g_remoteStateMovedLogged = false;
	inline bool g_firstSmoothedMovementLogged = false;
	inline ProxyLifecycleState g_proxyLifecycleState = ProxyLifecycleState::kHeldNoRemote;
	inline std::optional<ProxyLifecycleState> g_proxyHoldReason;
	inline std::optional<std::uint32_t> g_proxyHoldPlayerId;
	inline std::optional<ActiveProxyKind> g_proxyHoldKind;
	inline std::optional<std::uint32_t> g_representedPlayerId;
	inline RuntimeActorBaseSource g_runtimeProxyActorBaseSource = RuntimeActorBaseSource::kCustomProxy;
	inline std::unordered_map<std::uint32_t, ProxyActorSlot> g_proxySlots;
	inline std::optional<std::uint32_t> g_runtimeProxySpawnAttemptPlayerId;
	inline std::size_t g_slotWindowScans = 0;
	inline std::size_t g_slotWindowSuppressedLogs = 0;
	inline std::size_t g_slotWindowCandidateLogLines = 0;
	inline std::vector<std::pair<std::uint32_t, bool>> g_slotWindowLoggedCandidates;

	RE::ActorHandle GetProxyActorHandle(RE::Actor& a_actor);
	RE::Actor* FindActorInProcessListsByFormId(std::uint32_t a_formId);
	void LogInfoWithLocalPlayerPrefix(std::string_view a_message);
	void LogWarningWithLocalPlayerPrefix(std::string_view a_message);
	bool ShouldLog(std::string a_key, std::chrono::steady_clock::duration a_interval);
	void LogThrottledWarning(
		const std::string& a_key,
		std::string_view a_message,
		std::chrono::steady_clock::duration a_interval = kWarningLogInterval);
	void LogThrottledInfo(
		const std::string& a_key,
		std::string_view a_message,
		std::chrono::steady_clock::duration a_interval = kWarningLogInterval);
	const char* GetProxyNeutralizationReasonLabel(ProxyNeutralizationReason a_reason);
	std::string FormatPosition(const RE::NiPoint3& a_position);
	float GetDistance(const RE::NiPoint3& a_left, const RE::NiPoint3& a_right);
	void DiagnosticProxyAIState(
		RE::Actor& a_proxy,
		std::string_view a_label);
	void NeutralizeProxyActor(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RE::PlayerCharacter& a_player,
		ProxyNeutralizationReason a_reason);
	const char* GetActiveProxySourceForLog(ActiveProxyKind a_kind);
	const char* GetActiveProxyDescription(ActiveProxyKind a_kind);
	const char* GetRuntimeActorBaseSourceLabel(RuntimeActorBaseSource a_source);
	void LogRuntimeActorBaseSourceIfNeeded(RuntimeActorBaseSource a_source);
	const char* GetProxyMovementDiagnosticModeName(ProxyMovementDiagnosticMode a_mode);
	std::string GetActiveProxyLogName(ActiveProxyKind a_kind);
	void MoveProxyToHoldingPosition(
		RE::Actor& a_proxy,
		ProxyLifecycleState a_reason,
		std::optional<std::uint32_t> a_playerId = std::nullopt,
		ActiveProxyKind a_proxyKind = ActiveProxyKind::kPlacedFallback);
	bool ShouldSnapRemoteMovement(std::string_view a_movementType);
	void ResetRemoteMovementStateTracking();
	bool HasEditorId(const RE::TESForm& a_form, std::string_view a_editorId);
	bool IsCkPlacedProxyReference(
		const RE::TESObjectREFR& a_ref,
		const RE::Actor& a_actor,
		std::optional<RE::TESFormID> a_proxyBaseFormId);
	std::string GetEditorIdForLog(const RE::TESForm& a_form);
	RE::TESNPC* ResolveCodsworthBaseByAbsoluteFormId();
	RE::TESNPC* ResolveProxyActorBaseByPluginLocalFormId(
		const char* a_editorId,
		RE::TESFormID a_localFormId);
	RE::TESNPC* ResolveProxyActorBaseByPluginLocalFormId();
	RE::TESNPC* ResolveFemaleProxyActorBaseByPluginLocalFormId();
	RE::TESNPC* ResolveProxyActorBaseForSex(bool a_wantFemale);
	RE::TESNPC* ResolveProxyActorBase();
	std::optional<bool> GetDesiredProxyIsFemale(const RemotePlayer& a_remotePlayer);
	bool IsCustomPlayerProxyBase(const RE::TESNPC& a_baseActor);
	bool IsCustomPlayerProxyActor(const RE::Actor& a_actor);
	std::optional<std::uint32_t> ParseHexFormId(const std::string& a_formId);
	std::optional<std::uint32_t> GetLocalWorldspaceId(
		const RE::PlayerCharacter& a_player,
		const RE::TESObjectCELL& a_parentCell);
	bool IsRemotePlayerInSameLocation(
		const RemotePlayer& a_remotePlayer,
		const RE::PlayerCharacter& a_player,
		const RE::TESObjectCELL& a_parentCell);
	bool IsProxyInSamePlaySpace(
		const RE::TESObjectCELL& a_proxyCell,
		const RE::TESObjectCELL& a_localCell);
	bool IsValidProxyActor(const RE::Actor& a_proxy, const RE::TESObjectCELL& a_expectedCell, const RE::PlayerCharacter& a_player);
	bool IsRuntimeProxyRef(const RE::TESObjectREFR& a_ref);
	void MarkRuntimeProxySpawnFailed(std::string_view a_reason);
	bool ProxyHasLoaded3D(const RE::Actor& a_proxy);
	bool Proxy3DReadyForPresentation(const RE::Actor& a_proxy);
	bool ShouldRetainProxyInPriorLocalCell(
		std::uint32_t a_playerId,
		const RE::TESObjectCELL& a_proxyCell,
		RE::TESObjectCELL& a_currentCell,
		const RE::PlayerCharacter& a_player);
	std::uint32_t GetProxyVisibilityUpdate3DFlags();
	void QueueProxyUpdate3D(RE::Actor& a_proxy, std::uint32_t a_flags);
	void RefreshProxyAfterEquipmentChange(ProxyActorSlot& a_slot, RE::Actor& a_proxy);
	void ResetSlotEquipmentTracking(ProxyActorSlot& a_slot, bool a_forgetTrackedStacks = false);
	void ResetSlotAppearanceTracking(ProxyActorSlot& a_slot);
	void ResetSlotDisplayNameTracking(ProxyActorSlot& a_slot);
	void ApplyProxyDisplayNameFromRemoteState(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer);
	void ClearSlotEquipmentIfApplied(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		std::string_view a_reason);
	struct ModdedWeaponEquip
	{
		bool equipped{ false };
		RE::TBO_InstanceData* instanceData{ nullptr };
		const RE::ExtraDataList* extra{ nullptr };
	};
	ModdedWeaponEquip EquipSyncedWeaponWithMods(
		RE::ActorEquipManager& a_equipManager,
		RE::Actor& a_proxy,
		RE::TESObjectWEAP& a_weapon,
		const std::vector<F4T::RemotePlayerState::RemoteItemMod>& a_mods,
		std::string_view a_logKey);
	void RemoveTrackedModdedStack(ProxyActorSlot& a_slot, RE::Actor& a_proxy);
	void LogReceivedEquipment(const RemotePlayer& a_remotePlayer);
	void ApplyProxyEquipmentFromRemoteState(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer);
	void ApplyProxyPowerArmorFromRemoteState(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer);
	void ApplyProxyAppearanceFromRemoteState(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer);
	bool ProxyHeadNodesMissing(const RE::Actor& a_proxy);
	void EnsureProxyHeadMesh(ProxyActorSlot& a_slot, RE::Actor& a_proxy);
	void EnsureRuntimeProxyVisibility(ProxyActorSlot& a_slot, RE::Actor& a_proxy, std::string_view a_context);
	void MaintainRuntimeProxyFrustumVisibility(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RE::PlayerCharacter& a_player);
	void MaintainRuntimeProxy3DState(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer,
		const RE::PlayerCharacter& a_player,
		bool a_sameCellPassed);
	bool IsProxyPoolActorAssigned(const RE::Actor& a_actor);
	const char* GetPlaceAtMeBackedProxySourceLabel(RuntimeActorBaseSource a_source);
	bool IsPlaceAtMeBackedProxyAttempting(PlaceAtMeBackedProxySpawnState a_state);
	bool ShouldUseCustomProxyBaseForPlaceAtMeBackedSpawn();
	bool ShouldFallbackToCodsworthForPlaceAtMeBackedSpawn();
	void LogPlaceAtMeBackedProxySettleDiagnostic(
		RE::Actor& a_actor,
		const RE::PlayerCharacter& a_player,
		float a_settleElapsedSeconds,
		float a_settleRemainingSeconds);
	bool HasValidRemoteTransform(const RemotePlayer& a_remotePlayer);
	std::vector<RemotePlayer> GetSortedRemotePlayerSnapshot();
	RE::NiPoint3 GetRuntimeProxyVisibleTargetPosition(
		ProxyActorSlot& a_slot,
		const RemotePlayer& a_remotePlayer,
		const RE::PlayerCharacter& a_player);
	void SetSlotInitialRemoteSpawnTransform(ProxyActorSlot& a_slot, const RemotePlayer& a_remotePlayer);
	void CaptureSlotInitialRemoteSpawnTransform(ProxyActorSlot& a_slot, const RemotePlayer& a_remotePlayer);
	bool TryBeginSlotCodsworthFallback(
		ProxyActorSlot& a_slot,
		RE::TESObjectCELL& a_currentCell,
		const RE::PlayerCharacter& a_player);
	bool TryBeginSlotPlaceAtMeSpawn(
		ProxyActorSlot& a_slot,
		RE::TESObjectCELL& a_currentCell,
		const RE::PlayerCharacter& a_player,
		const RemotePlayer& a_remotePlayer);
	RE::Actor* TryIsolateSlotPlaceAtMeCandidate(
		ProxyActorSlot& a_slot,
		RE::TESObjectCELL& a_currentCell,
		const RE::PlayerCharacter& a_player,
		std::size_t& a_newCandidateCount);
	void MarkSlotSpawnFailed(ProxyActorSlot& a_slot);
	void StartSlotPlaceAtMeSettle(ProxyActorSlot& a_slot, RE::Actor& a_actor, std::size_t a_newCandidateCount);
	void PromoteSlotPlaceAtMeProxy(ProxyActorSlot& a_slot, RE::Actor& a_actor);
	bool MaintainSlotPlaceAtMeSpawn(
		ProxyActorSlot& a_slot,
		RE::TESObjectCELL& a_currentCell,
		const RE::PlayerCharacter& a_player,
		const RemotePlayer* a_remotePlayer = nullptr);
	RE::Actor* ResolveProxy(RE::TESObjectCELL& a_currentCell, const RE::PlayerCharacter& a_player);
	void ApplyRemoteMovementStateToProxy(RE::Actor& a_proxy, const RemotePlayer& a_remotePlayer);
	void LogProxyMovementDiagnostic(
		const RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer,
		const RE::PlayerCharacter& a_player,
		ActiveProxyKind a_proxyKind,
		bool a_sameCellPassed,
		ProxyMovementDiagnosticMode a_movementMode);
	void RestoreProxyForRemotePlayer(
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer,
		const RE::PlayerCharacter& a_player,
		ActiveProxyKind a_proxyKind,
		bool a_sameCellPassed);
	void MoveProxyToRemotePlayer(
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer,
		const RE::PlayerCharacter& a_player,
		ActiveProxyKind a_proxyKind,
		bool a_sameCellPassed,
		ProxyActorSlot& a_slot);
	void ClearSlotCachedProxyActor(ProxyActorSlot& a_slot);
	RE::Actor* TryResolveSlotProxy(
		ProxyActorSlot& a_slot,
		const RE::TESObjectCELL& a_currentCell,
		const RE::PlayerCharacter& a_player);
	void ResetSlotForRespawn(ProxyActorSlot& a_slot);
	void ResetSlotMovementState(ProxyActorSlot& a_slot);
	void ResetSlotAppliedAnimationState(ProxyActorSlot& a_slot);
	void ClearSlotProxyAnimationIfApplied(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		std::string_view a_reason);
	void ResetSlotObservationState(ProxyActorSlot& a_slot);
	void ResetSlotSneakObservationState(ProxyActorSlot& a_slot, std::string_view a_reason);
	void ReleaseAllSlotProxiesInOldCell(
		std::uint32_t a_oldCellId,
		RE::TESObjectCELL& a_currentCell,
		const RE::PlayerCharacter& a_player);
	void MarkSlotHeld(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		ProxyLifecycleState a_reason);
	void CorrectHeldSlotPositions(RE::TESObjectCELL& a_parentCell, const RE::PlayerCharacter& a_player);
	void MarkReusableSlots(
		RE::TESObjectCELL& a_parentCell,
		const RE::PlayerCharacter& a_player);
	void RestoreSlotProxyForRemotePlayer(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer,
		const RE::PlayerCharacter& a_player,
		bool a_sameCellPassed);
	void UpdateProxyAnimationStateDebug(
		ProxyActorSlot& a_slot,
		const RemotePlayer& a_remotePlayer,
		RE::Actor& a_proxy);
	void MoveSlotProxyToRemotePlayer(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer,
		const RE::PlayerCharacter& a_player,
		bool a_sameCellPassed);
	void HoldDisconnectedSlots(
		const std::unordered_set<std::uint32_t>& a_seenRemotePlayerIds,
		RE::TESObjectCELL& a_parentCell,
		const RE::PlayerCharacter& a_player);
	void HoldFallbackIfRepresentedPlayerDisconnected(
		const std::unordered_set<std::uint32_t>& a_seenRemotePlayerIds,
		RE::TESObjectCELL& a_parentCell,
		const RE::PlayerCharacter& a_player);
	bool TryUpdatePlacedFallbackForRemotePlayer(
		const RemotePlayer& a_remotePlayer,
		RE::TESObjectCELL& a_parentCell,
		const RE::PlayerCharacter& a_player,
		bool a_sameCellPassed,
		ProxyActorSlot& a_slot);
	void SyncProxyAnimationFromMotion(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		float a_measuredSpeed);
	void ApplySmoothFrameMovement();
}

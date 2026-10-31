#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace RE
{
	class TESObjectREFR;
}

namespace F4T::ProxySaveSafety
{
	enum class TickGate
	{
		kRun,
		kLoadingMenu,
		kMainMenu,
		kSaveLoadBusy
	};

	struct TickGateInputs
	{
		bool loadingMenuOpen{ false };
		bool mainMenuOpen{ false };
		bool saveLoadBusy{ false };
	};

	[[nodiscard]] inline TickGate EvaluateTickGate(const TickGateInputs& a_inputs)
	{
		if (a_inputs.loadingMenuOpen) {
			return TickGate::kLoadingMenu;
		}
		if (a_inputs.mainMenuOpen) {
			return TickGate::kMainMenu;
		}
		if (a_inputs.saveLoadBusy) {
			return TickGate::kSaveLoadBusy;
		}
		return TickGate::kRun;
	}

	[[nodiscard]] inline const char* GetTickGateLabel(TickGate a_gate)
	{
		switch (a_gate) {
		case TickGate::kLoadingMenu:
			return "loading menu open";
		case TickGate::kMainMenu:
			return "main menu open";
		case TickGate::kSaveLoadBusy:
			return "save or load task busy";
		case TickGate::kRun:
			break;
		}
		return "running";
	}

	enum class SlotSaveCleanup
	{
		kNone,
		kDropPendingSpawn,
		kDropLiveProxy
	};

	[[nodiscard]] inline SlotSaveCleanup DecideSlotSaveCleanup(bool a_hasLiveProxy, bool a_hasPendingSpawn)
	{
		if (a_hasLiveProxy) {
			return SlotSaveCleanup::kDropLiveProxy;
		}
		if (a_hasPendingSpawn) {
			return SlotSaveCleanup::kDropPendingSpawn;
		}
		return SlotSaveCleanup::kNone;
	}

	[[nodiscard]] inline constexpr bool IsPlacedFallbackProxy(
		bool a_isCkPlacedProxyReference,
		bool a_isCreated) noexcept
	{
		return a_isCkPlacedProxyReference && !a_isCreated;
	}

	[[nodiscard]] inline constexpr bool IsProtectedPlacedCodsworth(
		bool a_isCreated,
		bool a_isCodsworthBase) noexcept
	{
		return !a_isCreated && a_isCodsworthBase;
	}

	[[nodiscard]] inline constexpr bool IsCreatedFallbackBaseProxy(
		bool a_isCreated,
		bool a_isFallbackBase,
		bool a_isNpcSyncPuppet,
		bool a_isCoStandIn) noexcept
	{
		return a_isCreated && a_isFallbackBase && a_isCoStandIn && !a_isNpcSyncPuppet;
	}

	enum class SavedProxySweep
	{
		kKeep,
		kDelete
	};

	[[nodiscard]] inline SavedProxySweep DecideSavedProxySweep(
		bool a_isProxyActor,
		bool a_isDeleted,
		bool a_isOwnedByLiveSlot,
		bool a_isPlacedFallbackProxy,
		bool a_isCreatedFallbackBaseActor)
	{
		if (!(a_isProxyActor || a_isCreatedFallbackBaseActor) ||
			a_isDeleted || a_isOwnedByLiveSlot || a_isPlacedFallbackProxy) {
			return SavedProxySweep::kKeep;
		}
		return SavedProxySweep::kDelete;
	}

	enum class ProxyRetireAction
	{
		kSkip,
		kHoldPlacedFallback,
		kDelete
	};

	[[nodiscard]] inline constexpr ProxyRetireAction DecideProxyRetire(
		bool a_isPlacedFallbackProxy,
		bool a_isCustomPlayerProxyActor,
		bool a_isTrackedRuntimeProxy) noexcept
	{
		if (a_isPlacedFallbackProxy) {
			return ProxyRetireAction::kHoldPlacedFallback;
		}
		if (!a_isCustomPlayerProxyActor && !a_isTrackedRuntimeProxy) {
			return ProxyRetireAction::kSkip;
		}
		return ProxyRetireAction::kDelete;
	}

	[[nodiscard]] inline bool IsSaveStartFallback(
		bool a_saveLoadThreadBusy,
		bool a_loadingMenuOpen,
		bool a_loadInProgress)
	{
		return a_saveLoadThreadBusy && !a_loadingMenuOpen && !a_loadInProgress;
	}

	enum class SweepSource
	{
		kPlayerCell,
		kProcessLists
	};

	inline constexpr std::array<SweepSource, 2> kSweepSourcePriority{
		SweepSource::kPlayerCell,
		SweepSource::kProcessLists
	};

	template <class TVisit>
	inline void VisitSweepSourcesInPriorityOrder(TVisit&& a_visit)
	{
		for (const auto source : kSweepSourcePriority) {
			a_visit(source);
		}
	}

	[[nodiscard]] inline constexpr bool ShouldRunAnotherSweepPass(
		std::size_t a_newCandidatesThisPass,
		bool a_collectionCapped) noexcept
	{
		return a_collectionCapped && a_newCandidatesThisPass > 0;
	}

	enum class SaveTeardownRoute
	{
		kRunInline,
		kQueueGameThreadTask
	};

	[[nodiscard]] inline constexpr bool IsSaveTeardownOnGameThread(
		std::uint32_t a_currentThreadId,
		std::uint32_t a_gameThreadId) noexcept
	{
		return a_gameThreadId != 0 && a_currentThreadId == a_gameThreadId;
	}

	[[nodiscard]] inline constexpr SaveTeardownRoute DecideSaveTeardownRoute(
		std::uint32_t a_currentThreadId,
		std::uint32_t a_gameThreadId) noexcept
	{
		return IsSaveTeardownOnGameThread(a_currentThreadId, a_gameThreadId) ?
		           SaveTeardownRoute::kRunInline :
		           SaveTeardownRoute::kQueueGameThreadTask;
	}

	[[nodiscard]] inline constexpr bool ShouldBumpSessionGeneration(
		bool a_connected,
		bool a_bumpedSinceLastGeneration) noexcept
	{
		return a_connected && !a_bumpedSinceLastGeneration;
	}

	inline constexpr std::size_t kMaximumStandInFormIds = 4096;
	inline constexpr std::size_t kMaximumWindowCandidateLogLines = 64;

	[[nodiscard]] inline constexpr bool IsValidStandInRecordLength(std::uint32_t a_length) noexcept
	{
		return a_length % sizeof(std::uint32_t) == 0 && a_length <= kMaximumStandInFormIds * sizeof(std::uint32_t);
	}

	[[nodiscard]] inline constexpr bool IsSuccessfulLoadMessageData(std::uintptr_t a_messageData) noexcept
	{
		return a_messageData != 0;
	}

	[[nodiscard]] inline constexpr bool ShouldKeepStandIn(bool a_resolved, bool a_created, bool a_deleted) noexcept
	{
		return !a_resolved || (a_created && !a_deleted);
	}

	[[nodiscard]] inline constexpr bool IsUniqueStandInCandidate(std::size_t a_newCandidates) noexcept
	{
		return a_newCandidates == 1;
	}

	[[nodiscard]] inline constexpr bool CanRecordStandIn(std::size_t a_currentCount) noexcept
	{
		return a_currentCount < kMaximumStandInFormIds;
	}

	[[nodiscard]] inline constexpr bool ShouldLogPeriodic(std::size_t a_occurrence) noexcept
	{
		return a_occurrence <= 5 || a_occurrence % 300 == 0;
	}

	void NoteGameThreadId() noexcept;
	void DestroyProxyActor(RE::TESObjectREFR& a_ref);
	bool IsProxyTickBlocked();
	void Tick();
	void PrepareForSave();
	void AfterLoadSweep(bool a_loadSucceeded, bool a_clearStandIns);
	void RecordStandIn(std::uint32_t a_formId);
	void ForgetStandIn(std::uint32_t a_formId);
	std::vector<std::uint32_t> StandInFormIdsForSave();
	void LoadStandInFormIds(std::vector<std::uint32_t> a_formIds);
	void ClearStandInFormIds();
	std::size_t StandInFormIdCount();
	bool IsStandIn(std::uint32_t a_formId);
	void PruneStandInFormIds();
	bool IsSaveTeardownPending();
}

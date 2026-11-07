#include "pch.h"

#include "F4TSessionSaveQueue.h"

#include "F4TDebugLogging.h"
#include "F4TNetworking.h"
#include "F4TPowerArmorSync.h"
#include "F4TSessionSaveGuardPolicy.h"
#include "F4TSessionWorldState.h"

#include "RE/B/BGSSaveLoadManager.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <mutex>
#include <optional>
#include <utility>

namespace F4T::SessionSaveQueue
{
	namespace
	{
		using namespace SessionSaveGuard;

		// TODO-verify(#407) source:docs/code-notes/CO-BETA-plugin.md
		constexpr auto kQueuedSaveTask = RE::BGSSaveLoadManager::QUEUED_TASK::kForceSave;
		constexpr auto kQueueAcceptWindow = std::chrono::milliseconds(1500);

		struct QueueState
		{
			bool wanted{ false };
			std::uint32_t stablePolls{ 0 };
			std::uint32_t attempts{ 0 };
			std::optional<std::chrono::steady_clock::time_point> queuedAt{};
		};

		std::mutex g_queueMutex{};
		QueueState g_quitQueue{};
		QueueState g_firstJoinQueue{};
		std::atomic_bool g_saveQueueOutstanding{ false };
		FirstJoinAttempt::Entry g_firstJoinEntry{};

		template <class... Args>
		void Trace(std::format_string<Args...> a_format, Args&&... a_args) noexcept
		{
			try {
				F4T::DebugLogging::Log("SessionSaveQueue", a_format, std::forward<Args>(a_args)...);
			} catch (...) {
			}
		}

		[[nodiscard]] QueueState& StateFor(ArmOwner a_owner) noexcept
		{
			return a_owner == ArmOwner::kQuit ? g_quitQueue : g_firstJoinQueue;
		}

		[[nodiscard]] bool QueueStopped(ArmOwner a_owner) noexcept
		{
			if (a_owner == ArmOwner::kQuit) {
				return SessionSaveGuard::IsSessionExitLanded();
			}
			return SessionSaveGuard::IsSessionExitPending();
		}
	}

	bool StartQueuedSave(ArmOwner a_owner, std::string_view a_reason, std::uint64_t a_attemptId)
	{
		if (!RE::BGSSaveLoadManager::GetSingleton()) {
			return false;
		}
		{
			std::scoped_lock lock{ g_queueMutex };
			StateFor(a_owner) = QueueState{ .wanted = true };
			if (a_owner == ArmOwner::kFirstJoin) {
				g_firstJoinEntry = FirstJoinAttempt::Entry{ a_attemptId, false, 0 };
			}
		}
		SessionSaveGuard::ArmInternalSave(a_reason, a_owner);
		Trace(
			"[RT-VERIFY 571] {} save: queue waits for {} consecutive original IsSavingAllowed polls, task {}",
			SessionSaveGuard::OwnerName(a_owner),
			kStablePollsBeforeQueue,
			static_cast<std::int32_t>(kQueuedSaveTask));
		return true;
	}

	void StopQueue(ArmOwner a_owner)
	{
		std::scoped_lock lock{ g_queueMutex };
		StateFor(a_owner) = QueueState{};
		if (!g_quitQueue.wanted && !g_quitQueue.queuedAt &&
			!g_firstJoinQueue.wanted && !g_firstJoinQueue.queuedAt) {
			F4T::PowerArmorSync::GetFrameSaveGate().CancelPendingSave();
		}
	}

	void PumpQueue(ArmOwner a_owner)
	{
		if (!F4T::Networking::IsConnectedToServer() || QueueStopped(a_owner)) {
			StopQueue(a_owner);
			return;
		}
		auto* saveLoadManager = RE::BGSSaveLoadManager::GetSingleton();
		if (!saveLoadManager) {
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		{
			std::scoped_lock lock{ g_queueMutex };
			auto& state = StateFor(a_owner);
			if (state.queuedAt && now - *state.queuedAt >= kQueueAcceptWindow) {
				state.queuedAt.reset();
				state.stablePolls = 0;
				state.wanted = RetryQueuedSave(a_owner, state.attempts);
				if (!state.wanted && !g_quitQueue.wanted && !g_quitQueue.queuedAt &&
					!g_firstJoinQueue.wanted && !g_firstJoinQueue.queuedAt) {
					F4T::PowerArmorSync::GetFrameSaveGate().CancelPendingSave();
				}
				Trace(
					"[RT-VERIFY 571] {} save: no SaveGameImpl entry {}ms after queue attempt {}/{}, {}",
					SessionSaveGuard::OwnerName(a_owner),
					kQueueAcceptWindow.count(),
					state.attempts,
					kMaxQueueAttempts,
					state.wanted ? "re-queue after stable polls" : "no attempts left, the save runs to its deadline");
			}
			if (!state.wanted) {
				return;
			}
		}
		const auto allowed = SessionSaveGuard::OriginalIsSavingAllowed(saveLoadManager);
		std::uint32_t attempt{ 0 };
		{
			std::scoped_lock lock{ g_queueMutex };
			auto& state = StateFor(a_owner);
			if (!state.wanted) {
				return;
			}
			state.stablePolls = NextStablePollCount(state.stablePolls, allowed);
			if (!QueueReady(state.stablePolls, state.attempts)) {
				return;
			}
			attempt = ++state.attempts;
			state.wanted = false;
			state.queuedAt = now;
		}
		Trace(
			"[RT-VERIFY 571] {} save: {} stable polls, queue attempt {}/{}, savingAllowed {}, PauseMenu open {}, main menu open {} [RT-VERIFY 410-F]",
			SessionSaveGuard::OwnerName(a_owner),
			kStablePollsBeforeQueue,
			attempt,
			kMaxQueueAttempts,
			saveLoadManager->savingAllowed,
			F4T::SessionWorldState::IsMenuOpen("PauseMenu"),
			F4T::SessionWorldState::IsMainMenuOpen());
		g_saveQueueOutstanding.store(true, std::memory_order_release);
		saveLoadManager->QueueSaveLoadTask(kQueuedSaveTask);
	}

	void NoteEntered(ArmOwner a_owner, bool a_result)
	{
		std::scoped_lock lock{ g_queueMutex };
		auto& state = StateFor(a_owner);
		state.queuedAt.reset();
		if (a_owner == ArmOwner::kFirstJoin) {
			g_firstJoinEntry.result = a_result;
		}
		if (a_result) {
			if (a_owner == ArmOwner::kFirstJoin) {
				++g_firstJoinEntry.count;
			}
			state.wanted = false;
			return;
		}
		if (a_owner == ArmOwner::kQuit && !SessionSaveGuard::IsSessionExitPending()) {
			return;
		}
		state.wanted = RetryQueuedSave(a_owner, state.attempts);
		state.stablePolls = 0;
		if (!state.wanted && !g_quitQueue.wanted && !g_quitQueue.queuedAt &&
			!g_firstJoinQueue.wanted && !g_firstJoinQueue.queuedAt) {
			F4T::PowerArmorSync::GetFrameSaveGate().CancelPendingSave();
		}
		if (a_owner == ArmOwner::kFirstJoin) return;
		SessionSaveGuard::ArmInternalSave(
			a_owner == ArmOwner::kQuit ? "quit save refused by engine" : "first-join save refused by engine",
			a_owner);
		Trace(
			"[RT-VERIFY 571] {} save refused by engine: SaveGameImpl returned false, arm re-established, attempts {}/{}, {}",
			SessionSaveGuard::OwnerName(a_owner),
			state.attempts,
			kMaxQueueAttempts,
			state.wanted ? "re-queue after stable polls" : "no attempts left");
	}

	bool TakeOutstanding()
	{
		return g_saveQueueOutstanding.exchange(false, std::memory_order_acq_rel);
	}

	FirstJoinAttempt::Entry FirstJoinEntry()
	{
		std::scoped_lock lock{ g_queueMutex };
		return g_firstJoinEntry;
	}
}

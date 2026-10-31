#include "pch.h"

#include "F4TSessionSaveGuard.h"
#include "F4THookThreadTable.h"

#include "F4TDebugLogging.h"
#include "F4TExecutingRuntime.h"
#include "F4TFlyOver.h"
#include "F4TInlineHookGuard.h"
#include "F4TNetworking.h"
#include "F4TSaveLineageSession.h"
#include "F4TSessionExitSaveNotice.h"
#include "F4TSessionSaveGuardPolicy.h"
#include "F4TSessionSaveQueue.h"
#include "F4TSessionWorldState.h"
#include "F4TVtableGuard.h"

#include "RE/B/BGSSaveLoadManager.h"
#include "RE/S/SendHUDMessage.h"

#include <safetyhook.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <intrin.h>
#include <mutex>
#include <string_view>
#include <utility>

namespace F4T::SessionSaveGuard
{
	namespace
	{
		using SaveGameImplFn = bool (*)(
			RE::BGSSaveLoadManager*,
			const char*,
			std::int32_t,
			std::uint32_t,
			bool);
		using IsSavingAllowedFn = bool (*)(const RE::BGSSaveLoadManager*);

		constexpr std::uintptr_t kSaveGameImplOffsetOg = 0xced1d0;
		constexpr std::uintptr_t kSaveGameImplOffsetNg = 0xb77a30;
		constexpr std::uintptr_t kSaveGameImplOffsetAe = 0xbf9330;
		constexpr std::uintptr_t kIsSavingAllowedOffsetOg = 0xcee3d0;
		constexpr std::uintptr_t kIsSavingAllowedOffsetNg = 0xb78f10;
		constexpr std::uintptr_t kIsSavingAllowedOffsetAe = 0xbfb3a0;

		std::atomic<ArmOwner> g_internalSaveOwner{ ArmOwner::kNone };
		std::atomic<SaveGameImplFn> g_originalSaveGameImpl{ nullptr };
		std::atomic<IsSavingAllowedFn> g_originalIsSavingAllowed{ nullptr };
		std::atomic_bool g_blockNoticeLogged{ false };
		constexpr auto kIsSavingAllowedNoticeDebounce = std::chrono::milliseconds(2000);
		std::mutex g_isSavingAllowedNoticeMutex{};
		std::optional<std::chrono::steady_clock::time_point> g_isSavingAllowedNoticeShownAt{};
		std::atomic<std::uint32_t> g_isSavingAllowedCalls{ 0 };
		std::atomic<std::uint32_t> g_isSavingAllowedBlockedWhileSessionCalls{ 0 };
		std::atomic_bool g_isSavingAllowedLastAllowed{ true };
		std::array<std::uintptr_t, 32> g_refusedCallerRvas{};
		std::size_t g_refusedCallerRvaCount{ 0 };
		std::mutex g_refusedCallerRvaMutex{};
		constexpr auto kSessionExitSaveTimeout = std::chrono::milliseconds(15000);
		constexpr auto kSessionExitPostSaveGrace = std::chrono::milliseconds(1500);
		std::mutex g_sessionExitMutex{};
		std::function<void()> g_sessionExitProceed{};
		std::chrono::steady_clock::time_point g_sessionExitDeadline{};
		std::uint64_t g_sessionExitConnectionGeneration{ 0 };
		std::optional<std::chrono::steady_clock::time_point> g_sessionExitPostSaveAt{};
		std::atomic_bool g_sessionExitPending{ false };
		std::atomic_bool g_sessionExitLanded{ false };
		std::atomic_bool g_queuedQuitSaveRan{ false };
		bool g_sessionExitAwaitingArm{ false };
		bool g_sessionExitRetried{ false };
		constexpr auto kOrphanRefusalWindow = std::chrono::seconds(30);
		std::atomic_bool g_refuseOrphanQuitSave{ false };
		std::chrono::steady_clock::time_point g_orphanQuitSaveSetAt{};
		std::uint64_t g_orphanQuitSaveGeneration{ 0 };
		constexpr std::string_view kQueuedSaveNamePrefix{ "quicksave" };
		thread_local bool t_armedSaveInProgress{ false };
		safetyhook::InlineHook g_saveGameImplHook{};
		safetyhook::InlineHook g_isSavingAllowedHook{};

		template <class... Args>
		void Trace(std::format_string<Args...> a_format, Args&&... a_args) noexcept
		{
			try {
				F4T::DebugLogging::Log("SessionSaveGuard", a_format, std::forward<Args>(a_args)...);
			} catch (...) {
			}
		}

		[[nodiscard]] bool SessionActive() noexcept
		{
			return F4T::Networking::IsConnectedToServer();
		}

		[[nodiscard]] bool InternalSaveAllowed() noexcept
		{
			return g_internalSaveOwner.load(std::memory_order_acquire) != ArmOwner::kNone;
		}

		[[nodiscard]] std::uintptr_t ModuleRva(std::uintptr_t a_address) noexcept
		{
			return a_address - REX::FModule::GetExecutingModule().GetBaseAddress();
		}

		void ShowBlockedNotice(std::string_view a_source)
		{
			RE::SendHUDMessage::ShowHUDMessage(kSaveBlockedNotice.data(), nullptr, true, true);
			Trace("blocked save: HUD notice '{}' shown from {}", kSaveBlockedNotice, a_source);
			if (!g_blockNoticeLogged.exchange(true, std::memory_order_acq_rel)) {
				REX::WARN("SessionSaveGuard: player save blocked while a multiplayer session is active");
			}
		}

		bool ShowDebouncedBlockedNotice(std::string_view a_source)
		{
			const auto now = std::chrono::steady_clock::now();
			{
				std::scoped_lock lock{ g_isSavingAllowedNoticeMutex };
				if (g_isSavingAllowedNoticeShownAt && now - *g_isSavingAllowedNoticeShownAt < kIsSavingAllowedNoticeDebounce) {
					return false;
				}
				g_isSavingAllowedNoticeShownAt = now;
			}
			ShowBlockedNotice(a_source);
			return true;
		}

		[[nodiscard]] bool AtVerifiedOffset(std::uintptr_t a_address, std::uintptr_t a_expected) noexcept
		{
			return ModuleRva(a_address) == a_expected;
		}

		[[nodiscard]] std::optional<std::uintptr_t> ExpectedSaveGameImplOffset()
		{
			const auto target = F4T::ExecutingRuntime::Target();
			if (!target) {
				return std::nullopt;
			}
			return F4T::ExecutingRuntime::RvaFor(
				*target, kSaveGameImplOffsetOg, kSaveGameImplOffsetNg, kSaveGameImplOffsetAe);
		}

		[[nodiscard]] std::optional<std::uintptr_t> ExpectedIsSavingAllowedOffset()
		{
			const auto target = F4T::ExecutingRuntime::Target();
			if (!target) {
				return std::nullopt;
			}
			return F4T::ExecutingRuntime::RvaFor(
				*target, kIsSavingAllowedOffsetOg, kIsSavingAllowedOffsetNg, kIsSavingAllowedOffsetAe);
		}

		[[nodiscard]] bool FirstRefusalFromCaller(std::uintptr_t a_callerRva)
		{
			std::scoped_lock lock{ g_refusedCallerRvaMutex };
			for (std::size_t i = 0; i < g_refusedCallerRvaCount; ++i) {
				if (g_refusedCallerRvas[i] == a_callerRva) {
					return false;
				}
			}
			if (g_refusedCallerRvaCount == g_refusedCallerRvas.size()) {
				return false;
			}
			g_refusedCallerRvas[g_refusedCallerRvaCount++] = a_callerRva;
			return true;
		}

		// TODO-verify(#407) source:docs/code-notes/CO-BETA-plugin.md
		[[nodiscard]] bool IsQueuedSaveName(std::string_view a_fileName) noexcept
		{
			const auto separator = a_fileName.find_last_of("\\/");
			const auto name = separator == std::string_view::npos ? a_fileName : a_fileName.substr(separator + 1);
			return name.empty() || (name.size() >= kQueuedSaveNamePrefix.size() &&
				_strnicmp(name.data(), kQueuedSaveNamePrefix.data(), kQueuedSaveNamePrefix.size()) == 0);
		}

		void ArmOrphanQuitSaveRefusal(std::uint64_t a_generation)
		{
			std::scoped_lock lock{ g_sessionExitMutex };
			g_orphanQuitSaveSetAt = std::chrono::steady_clock::now();
			g_orphanQuitSaveGeneration = a_generation;
			g_refuseOrphanQuitSave.store(true, std::memory_order_release);
			Trace("orphan refusal armed for the queued quit save, window {}s", kOrphanRefusalWindow.count());
		}

		[[nodiscard]] bool ConsumeOrphanQuitSaveRefusal(bool a_session, std::string_view a_fileName)
		{
			if (!g_refuseOrphanQuitSave.load(std::memory_order_acquire)) {
				return false;
			}
			std::scoped_lock lock{ g_sessionExitMutex };
			const bool newSession = a_session && F4T::Networking::GetConnectionGeneration() != g_orphanQuitSaveGeneration;
			const bool expired = std::chrono::steady_clock::now() - g_orphanQuitSaveSetAt >= kOrphanRefusalWindow;
			if (newSession || expired) {
				g_refuseOrphanQuitSave.store(false, std::memory_order_release);
				Trace("orphan refusal cleared: {}", newSession ? "new session connected" : "window elapsed");
				return false;
			}
			if (!IsQueuedSaveName(a_fileName)) {
				Trace("[RT-VERIFY 410-C] save inside the orphan window let through: file '{}' is not a queued save name", F4T::DebugLogging::SanitizeLogText(a_fileName));
				return false;
			}
			return g_refuseOrphanQuitSave.exchange(false, std::memory_order_acq_rel);
		}

		[[nodiscard]] bool TakeDeferredQuitArm()
		{
			std::scoped_lock lock{ g_sessionExitMutex };
			if (!g_sessionExitAwaitingArm || InternalSaveAllowed()) {
				return false;
			}
			g_sessionExitAwaitingArm = false;
			return true;
		}

		void ClearInternalSaveIfOwner(ArmOwner a_observed, std::string_view a_reason)
		{
			auto expected = a_observed;
			if (g_internalSaveOwner.compare_exchange_strong(expected, ArmOwner::kNone, std::memory_order_acq_rel)) {
				Trace("{}: cleared arm owner {}", a_reason, OwnerName(a_observed));
				if (a_observed == ArmOwner::kFirstJoin) {
					ArmOrphanQuitSaveRefusal(F4T::Networking::GetConnectionGeneration());
				}
				return;
			}
			Trace("{}: arm owner is now {}, expected {}; arm left in place", a_reason, OwnerName(expected), OwnerName(a_observed));
		}

		void StartQuitSave(std::string_view a_reason)
		{
			F4T::FlyOver::Restore(F4T::FlyOver::ExitReason::Disconnect);
			{
				std::scoped_lock lock{ g_sessionExitMutex };
				if (g_refuseOrphanQuitSave.exchange(false, std::memory_order_acq_rel)) {
					Trace("[RT-VERIFY 410-C] quit save: orphan refusal cleared before arming the quit");
				}
				g_sessionExitDeadline = std::chrono::steady_clock::now() + kSessionExitSaveTimeout;
				g_queuedQuitSaveRan.store(false, std::memory_order_release);
			}
			if (!F4T::SessionSaveQueue::StartQueuedSave(ArmOwner::kQuit, a_reason)) {
				Trace("quit save: no save manager for '{}'", a_reason);
				return;
			}
			Trace("quit save: armed for '{}'", a_reason);
		}

		bool SaveGameImplDetour(
			RE::BGSSaveLoadManager* a_self,
			const char* a_fileName,
			std::int32_t a_deviceID,
			std::uint32_t a_outputStats,
			bool a_compress)
		{
			F4T::HookThread::Note(F4T::HookThread::Id::SaveGameImpl);
			// TODO-verify(#407)
			const auto callerRva = ModuleRva(reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
			const auto original = g_originalSaveGameImpl.load(std::memory_order_acquire);
			const auto session = SessionActive();
			const auto armed = InternalSaveAllowed();
			const auto fileName = a_fileName ? std::string_view{ a_fileName } : std::string_view{};
			const auto queuedName = IsQueuedSaveName(fileName);
			// TODO-verify(#407)
			Trace(
				"SaveGameImpl entered: caller rva {:#x}, this {:p}, file '{}', device {}, stats {:#x}, compress {}, session {}, armed {}, queued name {} [RT-VERIFY 410-A] [RT-VERIFY 410-C]",
				callerRva,
				static_cast<const void*>(a_self),
				F4T::DebugLogging::SanitizeLogText(fileName),
				a_deviceID,
				a_outputStats,
				a_compress,
				session,
				armed,
				queuedName);
			if (ConsumeOrphanQuitSaveRefusal(session, fileName)) {
				Trace("[RT-VERIFY 410-C] orphan queued save refused, file '{}'", F4T::DebugLogging::SanitizeLogText(fileName));
				return false;
			}
			const auto queuedSignature = fileName.empty() && a_deviceID == -1 && a_outputStats == 0 && a_compress;
			const auto queued = queuedSignature && F4T::SessionSaveQueue::TakeOutstanding();
			const auto owner = session && queued ? g_internalSaveOwner.exchange(ArmOwner::kNone, std::memory_order_acq_rel) : ArmOwner::kNone;
			if (session && armed && owner == ArmOwner::kNone) {
				Trace("[RT-VERIFY 410-C] armed internal save not consumed: file '{}' is not a queued save call or no queued save is outstanding, arm owner {}", F4T::DebugLogging::SanitizeLogText(fileName), OwnerName(g_internalSaveOwner.load(std::memory_order_acquire)));
			}
			const bool consumed = owner != ArmOwner::kNone;
			if (owner == ArmOwner::kQuit && g_sessionExitPending.load(std::memory_order_acquire)) {
				g_queuedQuitSaveRan.store(true, std::memory_order_release);
				Trace("[RT-VERIFY 410-C] queued quit save executed, session {}", session);
			}
			if (DecidePlayerSave(session, consumed) == Decision::kBlock) {
				ShowBlockedNotice("SaveGameImpl");
				Trace("SaveGameImpl decision BLOCK: session active, no internal save armed");
				return false;
			}
			Trace(
				"SaveGameImpl decision ALLOW: file '{}', {}, arm owner {}",
				F4T::DebugLogging::SanitizeLogText(fileName),
				consumed ? "armed internal save, arm consumed" : "no multiplayer session",
				OwnerName(owner));
			const auto outerArmedSave = t_armedSaveInProgress;
			t_armedSaveInProgress = consumed || outerArmedSave;
			const auto result = original(a_self, a_fileName, a_deviceID, a_outputStats, a_compress);
			t_armedSaveInProgress = outerArmedSave;
			Trace("SaveGameImpl original returned {}", result);
			if (owner != ArmOwner::kNone) {
				F4T::SessionSaveQueue::NoteEntered(owner, result);
			}
			if (owner == ArmOwner::kQuit && result && g_sessionExitPending.load(std::memory_order_acquire)) {
				bool graceRestarted{ false };
				{
					std::scoped_lock lock{ g_sessionExitMutex };
					g_sessionExitLanded.store(true, std::memory_order_release);
					if (g_sessionExitPostSaveAt) {
						g_sessionExitPostSaveAt = std::chrono::steady_clock::now();
						graceRestarted = true;
					}
				}
				Trace("quit save: landed result={} file '{}' [RT-VERIFY 410-C]", result, F4T::DebugLogging::SanitizeLogText(fileName));
				if (graceRestarted) {
					Trace("quit save: kPostSaveGame arrived before landing; grace {}ms restarts at landing [RT-VERIFY 410-C landed-means-written]", kSessionExitPostSaveGrace.count());
				}
			}
			return result;
		}

		bool IsSavingAllowedDetour(const RE::BGSSaveLoadManager* a_self)
		{
			F4T::HookThread::Note(F4T::HookThread::Id::IsSavingAllowed);
			// TODO-verify(#407)
			const auto callerRva = ModuleRva(reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
			const auto original = g_originalIsSavingAllowed.load(std::memory_order_acquire);
			const auto session = SessionActive();
			const auto armed = InternalSaveAllowed() || t_armedSaveInProgress;
			const auto allowed = DecidePlayerSave(session, armed) == Decision::kAllow;
			const auto callCount = g_isSavingAllowedCalls.fetch_add(1, std::memory_order_relaxed) + 1;
			const auto changed = g_isSavingAllowedLastAllowed.exchange(allowed, std::memory_order_relaxed) != allowed;
			const bool result = allowed && original(a_self);
			if (changed || callCount <= 5 || callCount % 300 == 0) {
				Trace(
					"IsSavingAllowed call {} (caller rva {:#x}): session {}, armed {}, decision {}, returns {}{}",
					callCount,
					callerRva,
					session,
					armed,
					allowed ? "allow" : "block",
					result,
					changed ? " (state changed)" : "");
			}
			// TODO-verify(#407)
			if (session && !allowed) {
				const auto blockedCount = g_isSavingAllowedBlockedWhileSessionCalls.fetch_add(1, std::memory_order_relaxed) + 1;
				const bool firstFromCaller = FirstRefusalFromCaller(callerRva);
				const auto noticeShown = ShowDebouncedBlockedNotice("IsSavingAllowed");
				if (firstFromCaller || blockedCount <= 5 || blockedCount % 300 == 0) {
					Trace(
						"IsSavingAllowed BLOCK while session active (HUD notice {}): caller rva {:#x}, block call {}{} [RT-VERIFY 410-B2 caller]",
						noticeShown ? "shown" : "debounced",
						callerRva,
						blockedCount,
						firstFromCaller ? ", first refusal from this caller" : "");
				}
			}
			return result;
		}

		template <class Fn>
		bool InstallHook(
			std::uintptr_t a_target,
			std::uintptr_t a_expectedOffset,
			void* a_detour,
			std::atomic<Fn>& a_original,
			safetyhook::InlineHook& a_hook,
			const char* a_label)
		{
			if (a_hook) {
				REX::INFO("SessionSaveGuard: {} hook already installed; skipped", a_label);
				return true;
			}

			if (!AtVerifiedOffset(a_target, a_expectedOffset)) {
				REX::WARN(
					"SessionSaveGuard: {} hook not installed: {:#x} is not the verified entry {:#x}",
					a_label,
					ModuleRva(a_target),
					a_expectedOffset);
				return false;
			}

			const auto text = REX::FModule::GetExecutingModule().GetSection(".text");
			const F4T::VtableGuard::AddressRange codeSection{ text.GetAddress(), text.GetAddress() + text.GetSize() };
			const auto verdict = F4T::InlineHookGuard::CheckTarget(
				a_target, F4T::InlineHookGuard::kMinPrologueBytes, codeSection);
			if (verdict != F4T::InlineHookGuard::InstallVerdict::kOk) {
				REX::WARN(
					"SessionSaveGuard: {} hook not installed at {:#x}: {}",
					a_label,
					ModuleRva(a_target),
					F4T::InlineHookGuard::Describe(verdict));
				return false;
			}

			try {
				auto created = safetyhook::InlineHook::create(
					reinterpret_cast<void*>(a_target),
					a_detour,
					safetyhook::InlineHook::StartDisabled);
				if (!created) {
					REX::WARN("SessionSaveGuard: {} hook create failed at {:#x}", a_label, ModuleRva(a_target));
					return false;
				}
				a_original.store(created->original<Fn>(), std::memory_order_release);
				a_hook = std::move(*created);
				if (const auto enabled = a_hook.enable(); !enabled) {
					REX::WARN("SessionSaveGuard: {} hook enable failed at {:#x}", a_label, ModuleRva(a_target));
					a_hook.reset();
					a_original.store(nullptr, std::memory_order_release);
					return false;
				}
				REX::INFO("SessionSaveGuard: {} hook installed at {:#x}", a_label, ModuleRva(a_target));
				return true;
			} catch (...) {
				REX::WARN("SessionSaveGuard: {} hook threw while installing at {:#x}", a_label, ModuleRva(a_target));
				return false;
			}
		}
	}

	void ArmInternalSave(std::string_view a_reason, ArmOwner a_owner)
	{
		auto previous = g_internalSaveOwner.load(std::memory_order_acquire);
		do {
			if (previous == ArmOwner::kQuit && a_owner != ArmOwner::kQuit) {
				Trace("[RT-VERIFY 410-C] ArmInternalSave refused: reason '{}', owner {} would overwrite the live quit arm (owner {})", a_reason, OwnerName(a_owner), OwnerName(previous));
				return;
			}
		} while (!g_internalSaveOwner.compare_exchange_weak(previous, a_owner, std::memory_order_acq_rel));
		Trace("ArmInternalSave: reason '{}', owner {}, was armed {} (previous owner {})", a_reason, OwnerName(a_owner), previous != ArmOwner::kNone, OwnerName(previous));
	}

	std::string_view OwnerName(ArmOwner a_owner)
	{
		switch (a_owner) {
		case ArmOwner::kFirstJoin:
			return "first-join";
		case ArmOwner::kQuit:
			return "quit";
		case ArmOwner::kNone:
			break;
		}
		return "none";
	}

	void DisarmInternalSave(std::string_view a_reason)
	{
		ClearInternalSaveIfOwner(ArmOwner::kFirstJoin, a_reason);
	}

	void RunOrDeferSessionExit(std::string_view a_reason, std::function<void()> a_proceed)
	{
		auto* saveLoadManager = RE::BGSSaveLoadManager::GetSingleton();
		if (!SessionActive() || !saveLoadManager) {
			Trace("quit save: skipped for '{}', no live session or save manager; proceeding now", a_reason);
			a_proceed();
			return;
		}
		if (F4T::SessionWorldState::IsMainMenuOpen()) {
			Trace("quit save: skipped for '{}', main menu open so no game world is loaded; proceeding now", a_reason);
			a_proceed();
			return;
		}
		bool deferArm{ false };
		{
			std::scoped_lock lock{ g_sessionExitMutex };
			if (!AcceptsSessionExitRequest(static_cast<bool>(g_sessionExitProceed))) {
				Trace("quit save: already waiting; request '{}' not queued", a_reason);
				return;
			}
			g_sessionExitProceed = std::move(a_proceed);
			g_sessionExitDeadline = std::chrono::steady_clock::now() + kSessionExitSaveTimeout;
			g_sessionExitConnectionGeneration = F4T::Networking::GetConnectionGeneration();
			g_sessionExitPostSaveAt.reset();
			g_sessionExitRetried = false;
			deferArm = InternalSaveAllowed();
			g_sessionExitAwaitingArm = deferArm;
		}
		g_sessionExitLanded.store(false, std::memory_order_release);
		g_sessionExitPending.store(true, std::memory_order_release);
		F4T::SessionExitSaveNotice::ShowSaving();
		if (deferArm) {
			Trace("quit save: '{}' waiting for the armed internal save to clear; queued save follows", a_reason);
			return;
		}
		StartQuitSave(a_reason);
	}

	void NotePostSaveGame()
	{
		std::scoped_lock lock{ g_sessionExitMutex };
		if (!g_sessionExitPending.load(std::memory_order_acquire) || g_sessionExitPostSaveAt) {
			return;
		}
		g_sessionExitPostSaveAt = std::chrono::steady_clock::now();
		Trace("kPostSaveGame seen while the quit save is pending, landed {}; grace {}ms starts, quit still waits for landed [RT-VERIFY 410-C landed-means-written]", g_sessionExitLanded.load(std::memory_order_acquire), kSessionExitPostSaveGrace.count());
	}

	bool IsSessionExitPending()
	{
		return g_sessionExitPending.load(std::memory_order_acquire);
	}

	bool IsSessionExitLanded()
	{
		return g_sessionExitLanded.load(std::memory_order_acquire);
	}

	bool OriginalIsSavingAllowed(const RE::BGSSaveLoadManager* a_manager)
	{
		const auto original = g_originalIsSavingAllowed.load(std::memory_order_acquire);
		return original ? original(a_manager) : true;
	}

	void Install()
	{
		const auto saveOffset = ExpectedSaveGameImplOffset();
		const auto allowOffset = ExpectedIsSavingAllowedOffset();
		if (!saveOffset || !allowOffset) {
			REX::WARN("SessionSaveGuard: runtime is not a verified build; save hooks not installed");
			return;
		}

		const auto saveOk = InstallHook(
			RE::ID::BGSSaveLoadManager::SaveGameImpl.address(),
			*saveOffset,
			REX::UNRESTRICTED_CAST<void*>(&SaveGameImplDetour),
			g_originalSaveGameImpl,
			g_saveGameImplHook,
			"SaveGameImpl");
		const auto allowOk = InstallHook(
			RE::ID::BGSSaveLoadManager::IsSavingAllowed.address(),
			*allowOffset,
			REX::UNRESTRICTED_CAST<void*>(&IsSavingAllowedDetour),
			g_originalIsSavingAllowed,
			g_isSavingAllowedHook,
			"IsSavingAllowed");
		REX::INFO("SessionSaveGuard: install result: SaveGameImpl {}, IsSavingAllowed {}", saveOk ? "ok" : "failed", allowOk ? "ok" : "failed");
		if (saveOk != allowOk) {
			REX::WARN("SessionSaveGuard: save block is one-sided: {} hook missing", saveOk ? "IsSavingAllowed" : "SaveGameImpl");
		}
	}

	void Tick()
	{
		if (!SessionActive()) {
			g_blockNoticeLogged.store(false, std::memory_order_relaxed);
		}
		F4T::SessionSaveQueue::PumpQueue(ArmOwner::kFirstJoin);

		if (!g_sessionExitPending.load(std::memory_order_acquire)) {
			return;
		}
		if (TakeDeferredQuitArm()) {
			Trace("quit save: earlier internal save cleared; arming the deferred quit save");
			StartQuitSave("deferred quit save");
		}
		std::function<void()> proceed{};
		std::string_view outcome{};
		bool landed{ false };
		bool postSaveSeen{ false };
		bool orphanPossible{ false };
		bool retry{ false };
		ArmOwner observedOwner{ ArmOwner::kNone };
		std::uint64_t capturedGeneration{ 0 };
		{
			std::scoped_lock lock{ g_sessionExitMutex };
			landed = g_sessionExitLanded.load(std::memory_order_acquire);
			postSaveSeen = g_sessionExitPostSaveAt.has_value();
			capturedGeneration = g_sessionExitConnectionGeneration;
			if (landed && postSaveSeen &&
				std::chrono::steady_clock::now() >= *g_sessionExitPostSaveAt + kSessionExitPostSaveGrace &&
				!F4T::SaveLineageSession::HasPendingRecord()) {
				outcome = "landed";
			} else if (!SessionActive()) {
				outcome = "session ended";
			} else if (std::chrono::steady_clock::now() >= g_sessionExitDeadline) {
				outcome = "timeout";
			}
			if (!outcome.empty()) {
				observedOwner = g_internalSaveOwner.load(std::memory_order_acquire);
				orphanPossible = !landed && !g_sessionExitAwaitingArm && !g_queuedQuitSaveRan.load(std::memory_order_acquire);
				g_sessionExitAwaitingArm = false;
				retry = outcome == "timeout" && !landed && !g_sessionExitRetried && SessionActive() &&
					capturedGeneration == F4T::Networking::GetConnectionGeneration();
				if (retry) {
					g_sessionExitRetried = true;
					g_sessionExitPostSaveAt.reset();
				} else {
					proceed = std::move(g_sessionExitProceed);
					g_sessionExitProceed = nullptr;
					g_sessionExitPending.store(false, std::memory_order_release);
					F4T::SessionSaveQueue::StopQueue(ArmOwner::kQuit);
				}
			}
		}
		if (retry) {
			ClearInternalSaveIfOwner(observedOwner, "quit save timed out; retrying");
			Trace("quit save: retrying after timeout (landed {}, kPostSaveGame seen {}), arm cleared, quit save re-armed", landed, postSaveSeen);
			StartQuitSave("quit save retry");
			F4T::SessionExitSaveNotice::ShowRetrying();
			return;
		}
		if (!proceed) {
			F4T::SessionSaveQueue::PumpQueue(ArmOwner::kQuit);
			return;
		}
		if (orphanPossible) {
			ArmOrphanQuitSaveRefusal(capturedGeneration);
		}
		const auto generation = F4T::Networking::GetConnectionGeneration();
		if (generation != capturedGeneration) {
			if (outcome != "landed") {
				ClearInternalSaveIfOwner(observedOwner, "quit save: connection changed");
			}
			Trace("quit save: {} but the connection generation changed {}->{}; continuation skipped", outcome, capturedGeneration, generation);
			if (outcome == "session ended") {
				F4T::SessionExitSaveNotice::ShowFailed("The connection closed before your session was saved.");
			} else {
				F4T::SessionExitSaveNotice::Dismiss();
			}
			return;
		}
		if (outcome == "landed") {
			Trace("quit save: landed and kPostSaveGame grace {}ms elapsed, proceeding with quit/disconnect [RT-VERIFY 410-C landed-means-written]", kSessionExitPostSaveGrace.count());
			F4T::SessionExitSaveNotice::ShowSaved();
		} else if (outcome == "timeout") {
			ClearInternalSaveIfOwner(observedOwner, "quit save timed out");
			Trace("quit save: timeout after {}ms (landed {}, kPostSaveGame seen {}), disarmed; proceeding with quit/disconnect", kSessionExitSaveTimeout.count(), landed, postSaveSeen);
			Trace("quit save: the queued save is not cancelled (no cancel path in CommonLib BGSSaveLoadManager); it runs unarmed, and the next queued-name save within the orphan window is refused once, it is not tied to the queued save [RT-VERIFY 410-C]");
			F4T::SessionExitSaveNotice::ShowFailed(landed ? "Your session was saved, but the server did not confirm it in time." : "The save did not finish in time. Progress since your last save may be lost.");
		} else {
			ClearInternalSaveIfOwner(observedOwner, "session ended during quit save");
			Trace("quit save: session ended before the save landed (landed {}, kPostSaveGame seen {}), disarmed; proceeding with quit/disconnect", landed, postSaveSeen);
			F4T::SessionExitSaveNotice::ShowFailed("The connection closed before your session was saved.");
		}
		proceed();
	}
}

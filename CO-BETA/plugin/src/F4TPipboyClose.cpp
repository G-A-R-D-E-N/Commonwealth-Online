#include "pch.h"

#include "F4TPipboyClose.h"

#include "F4TDebugLogging.h"
#include "F4TExecutingRuntime.h"
#include "F4TVtableGuard.h"

#include "RE/B/BSAnimationGraphEvent.h"
#include "RE/B/BSInputEventUser.h"
#include "RE/B/BSTEvent.h"
#include "RE/F/FavoritesManager.h"
#include "RE/M/MenuControls.h"
#include "RE/P/PipboyManager.h"
#include "RE/U/UI.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace F4T::PipboyClose
{
	namespace
	{
		constexpr std::size_t kProcessEventSlot{ 1 };
		constexpr std::size_t kShouldHandleEventSlot{ 1 };
		constexpr std::chrono::milliseconds kStateLifetime{ 250 };
		// Constraint: slot 1 must hold exactly these Pip-Boy event handler RVAs (RE Docs local/GardenEngine-re-answers/evidence-2026-10-23/mainmenu-save2/r15-audit/scripts/r13_c21820_check.py); UNVERIFIED, so the install refuses on a mismatch.
		constexpr std::uintptr_t kOgEventRva{ 0xC21820 };
		constexpr std::uintptr_t kNgEventRva{ 0xAE0430 };
		constexpr std::uintptr_t kAeEventRva{ 0xB525B0 };

		using ProcessEventFn = RE::BSEventNotifyControl (*)(RE::PipboyManager*, const RE::BSAnimationGraphEvent&, RE::BSTEventSource<RE::BSAnimationGraphEvent>*);
		using ShouldHandleFn = bool (*)(RE::BSInputEventUser*, const RE::InputEvent*);

		std::uintptr_t g_originalProcessEvent{ 0 };
		std::uintptr_t g_originalPipboyHandler{ 0 };
		std::uintptr_t g_originalFavorites{ 0 };
		bool g_sinkDone{ false };
		bool g_handlerDone{ false };
		bool g_favoritesDone{ false };
		bool g_unsupportedLogged{ false };
		std::optional<F4T::ClientCompatibility::BuildTarget> g_target;
		bool g_targetResolved{ false };
		std::atomic<bool> g_inForceClose{ false };
		std::atomic<bool> g_session{ false };
		std::atomic<bool> g_inputGate{ false };
		std::atomic<std::int64_t> g_stateStampNs{ 0 };
		std::atomic<std::uint32_t> g_openedEvents{ 0 };
		std::atomic<std::uint32_t> g_closedEvents{ 0 };
		std::atomic<std::uint32_t> g_forceAttempts{ 0 };
		std::atomic<std::uint32_t> g_suppressed{ 0 };

		struct ForceCloseGuard
		{
			~ForceCloseGuard() { g_inForceClose = false; }
		};

		std::int64_t NowNs()
		{
			return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		// Constraint: the state must be trusted only while Tick keeps refreshing it, so a stopped tick can never leave the gameplay Pip-Boy key dead.
		bool StateFresh()
		{
			return NowNs() - g_stateStampNs.load() < std::chrono::duration_cast<std::chrono::nanoseconds>(kStateLifetime).count();
		}

		bool InputGateActive()
		{
			return g_inputGate && StateFresh();
		}

		RE::BSEventNotifyControl ProcessEventHook(RE::PipboyManager* a_this, const RE::BSAnimationGraphEvent& a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source)
		{
			const char* name = a_event.tag.c_str();
			const std::string_view tag{ name ? name : "" };
			const auto opened = tag == "pipboyOpened";
			const auto closingBefore = a_this->pipboyClosing;
			const auto closed = tag == "pipboyClosed" || (tag == "PowerArmorClose" && closingBefore);
			if (opened) {
				++g_openedEvents;
			}
			if (closed) {
				++g_closedEvents;
			}
			const auto result = reinterpret_cast<ProcessEventFn>(g_originalProcessEvent)(a_this, a_event, a_source);
			if (opened || closed) {
				F4T::DebugLogging::Log("PipboyDiag", "anim event {}: managerClosing={} session={}", tag, a_this->pipboyClosing, g_session && StateFresh());
			}
			if (closed && g_session && StateFresh() && a_this->pipboyClosing) {
				ForceClose("close event left the close pending");
			}
			return result;
		}

		bool PipboyHandlerShouldHandleHook(RE::BSInputEventUser* a_this, const RE::InputEvent* a_event)
		{
			if (InputGateActive()) {
				++g_suppressed;
				return false;
			}
			return reinterpret_cast<ShouldHandleFn>(g_originalPipboyHandler)(a_this, a_event);
		}

		bool FavoritesShouldHandleHook(RE::BSInputEventUser* a_this, const RE::InputEvent* a_event)
		{
			if (InputGateActive()) {
				++g_suppressed;
				return false;
			}
			return reinterpret_cast<ShouldHandleFn>(g_originalFavorites)(a_this, a_event);
		}

		void InstallSlot(std::string_view a_name, std::uintptr_t a_vtableAddress, std::size_t a_slot, std::uintptr_t a_replacement, std::uintptr_t& a_original, std::uintptr_t a_expectedRva)
		{
			REL::Relocation<std::uintptr_t> table{ a_vtableAddress };
			const auto module = REX::FModule::GetExecutingModule();
			const auto text = module.GetSection(".text");
			const auto rdata = module.GetSection(".rdata");

			VtableGuard::SlotRequest request{};
			request.vtableAddress = table.address();
			request.slotIndex = a_slot;
			request.replacement = a_replacement;
			request.codeSection = { text.GetAddress(), text.GetAddress() + text.GetSize() };
			request.vtableSection = { rdata.GetAddress(), rdata.GetAddress() + rdata.GetSize() };
			request.slotValue = VtableGuard::ReadSlot(request);

			const auto verdict = VtableGuard::VerifySlot(request);
			const auto chainedOk = a_expectedRva == 0 && verdict == VtableGuard::SlotVerdict::kSlotNotExecutable;
			if (verdict != VtableGuard::SlotVerdict::kOk && !chainedOk) {
				REX::CRITICAL("Pip-Boy {} hook NOT installed ({}).", a_name, VtableGuard::Describe(verdict));
				return;
			}
			if (a_expectedRva != 0 && !VtableGuard::SlotMatchesExpectedRva(request.slotValue, module.GetBaseAddress(), a_expectedRva)) {
				REX::CRITICAL("Pip-Boy {} hook NOT installed: slot {} is not this runtime's expected function.", a_name, a_slot);
				return;
			}

			a_original = request.slotValue;
			table.write_vfunc(a_slot, a_replacement);
			const auto slotAddress = request.vtableAddress + (a_slot * sizeof(std::uintptr_t));
			if (!VtableGuard::WriteSucceeded(*reinterpret_cast<const std::uintptr_t*>(slotAddress), a_replacement)) {
				REX::CRITICAL("Pip-Boy {} hook FAILED to install: function table slot did not take the write.", a_name);
				return;
			}
			F4T::DebugLogging::Log("PipboyDiag", "{} hook installed on slot {}", a_name, a_slot);
		}

		std::uintptr_t LiveTable(const void* a_object)
		{
			return *reinterpret_cast<const std::uintptr_t*>(a_object);
		}
	}

	void Install()
	{
		if (g_sinkDone && g_handlerDone && g_favoritesDone) {
			return;
		}
		if (g_unsupportedLogged) {
			return;
		}
		if (!g_targetResolved) {
			g_targetResolved = true;
			g_target = F4T::ExecutingRuntime::Target();
		}
		const auto target = g_target;
		if (!target) {
			g_unsupportedLogged = true;
			REX::CRITICAL("Pip-Boy close handshake hooks NOT installed (runtime is not OG 1.10.163, NG 1.10.984 or AE 1.11.240).");
			return;
		}
		if (!g_sinkDone) {
			if (const auto* manager = RE::PipboyManager::GetSingleton()) {
				g_sinkDone = true;
				InstallSlot("anim event sink", LiveTable(manager), kProcessEventSlot, REX::UNRESTRICTED_CAST<std::uintptr_t>(&ProcessEventHook), g_originalProcessEvent,
					F4T::ExecutingRuntime::RvaFor(*target, kOgEventRva, kNgEventRva, kAeEventRva));
			}
		}
		if (!g_handlerDone) {
			const auto* controls = RE::MenuControls::GetSingleton();
			if (controls && controls->pipboyHandler) {
				g_handlerDone = true;
				InstallSlot("gameplay handler gate", LiveTable(controls->pipboyHandler), kShouldHandleEventSlot, REX::UNRESTRICTED_CAST<std::uintptr_t>(&PipboyHandlerShouldHandleHook), g_originalPipboyHandler, 0);
			}
		}
		if (!g_favoritesDone) {
			if (auto* favorites = RE::FavoritesManager::GetSingleton()) {
				g_favoritesDone = true;
				InstallSlot("favorites gate", LiveTable(static_cast<RE::BSInputEventUser*>(favorites)), kShouldHandleEventSlot, REX::UNRESTRICTED_CAST<std::uintptr_t>(&FavoritesShouldHandleHook), g_originalFavorites, 0);
			}
		}
	}

	void ForceClose(std::string_view a_reason)
	{
		if (g_inForceClose.exchange(true)) {
			return;
		}
		ForceCloseGuard guard;
		auto* manager = RE::PipboyManager::GetSingleton();
		if (!manager) {
			F4T::DebugLogging::Log("PipboyDiag", "force close skipped ({}): no Pip-Boy manager", a_reason);
			return;
		}
		const auto attempt = ++g_forceAttempts;
		const auto wasClosing = manager->pipboyClosing;
		F4T::DebugLogging::Log("PipboyDiag", "force close #{} ({}): managerClosing={}", attempt, a_reason, wasClosing);
		if (wasClosing || attempt > 1) {
			manager->OnPipboyClosed();
		} else {
			manager->PlayPipboyCloseAnim(true);
		}
	}

	void SetState(bool a_session, bool a_inputGate)
	{
		g_session = a_session;
		g_inputGate = a_inputGate;
		g_stateStampNs = NowNs();
	}

	void OnMenuEdge()
	{
		g_openedEvents = 0;
		g_closedEvents = 0;
		g_forceAttempts = 0;
		g_suppressed = 0;
	}

	bool ClosePending()
	{
		const auto* manager = RE::PipboyManager::GetSingleton();
		return manager && manager->pipboyClosing;
	}

	void LogState(std::string_view a_when, bool a_inputLayerHeld)
	{
		const auto* ui = RE::UI::GetSingleton();
		const auto* manager = RE::PipboyManager::GetSingleton();
		F4T::DebugLogging::Log("PipboyDiag",
			"{}: menuMode={} managerOpening={} managerClosing={} closeQueued={} raising={} openedEvents={} closedEvents={} inputLayerHeld={} handlerGate={} suppressedEvents={}",
			a_when,
			ui ? ui->menuMode : 0u,
			manager && manager->pipboyOpening,
			manager && manager->pipboyClosing,
			manager && manager->pipboyMenuCloseQueued,
			manager && manager->pipboyRaising,
			g_openedEvents.load(),
			g_closedEvents.load(),
			a_inputLayerHeld,
			InputGateActive(),
			g_suppressed.load());
	}
}

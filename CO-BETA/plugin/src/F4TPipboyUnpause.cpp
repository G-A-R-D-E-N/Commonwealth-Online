#include "pch.h"

#include "F4TPipboyUnpause.h"

#include "F4TDebugLogging.h"
#include "F4TExecutingRuntime.h"
#include "F4TNetworking.h"
#include "F4TPipboyClose.h"
#include "F4TPipboyPauseLatch.h"
#include "F4TRemotePlayerState.h"
#include "F4TSessionLaunch.h"
#include "F4TVtableGuard.h"

#include "RE/B/BSInputEnableLayer.h"
#include "RE/B/BSInputEnableManager.h"
#include "RE/B/BSSpinLock.h"
#include "RE/B/BSTSmartPointer.h"
#include "RE/P/PipboyMenu.h"
#include "RE/U/UI.h"
#include "RE/U/UIMenuEntry.h"
#include "RE/U/UI_MENU_FLAGS.h"
#include "RE/U/UI_MESSAGE_RESULTS.h"
#include "RE/U/UI_MESSAGE_TYPE.h"
#include "RE/U/UIMessage.h"
#include "RE/U/UserEvents.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace F4T::PipboyUnpause
{
	namespace
	{
		constexpr std::size_t kProcessMessageSlot{ 3 };
		constexpr std::array kLockedUserEvents{ RE::UserEvents::USER_EVENT_FLAG::kMovement, RE::UserEvents::USER_EVENT_FLAG::kFighting };

		using ProcessMessageFn = RE::UI_MESSAGE_RESULTS (*)(RE::PipboyMenu*, RE::UIMessage&);

		ProcessMessageFn g_originalProcessMessage{ nullptr };
		RE::BSTSmartPointer<RE::BSInputEnableLayer> g_inputLayer;
		bool g_installAttempted{ false };
		PipboyPauseLatch::State g_latch;
		bool g_pipboyOpen{ false };
		std::chrono::steady_clock::time_point g_openSince{};
		std::chrono::steady_clock::time_point g_lastForcedClose{};
		constexpr std::chrono::seconds kOnboardingStuckAfter{ 90 };
		constexpr std::chrono::seconds kOnboardingHardCap{ 300 };
		std::chrono::steady_clock::time_point g_openedAt{};
		constexpr std::chrono::seconds kForcedCloseRetry{ 5 };
		constexpr std::chrono::seconds kCloseStuckAfter{ 4 };
		std::atomic<bool> g_forceRequested{ false };
		constexpr std::chrono::seconds kDiagFirst{ 1 };
		constexpr std::chrono::seconds kDiagLongOpen{ 10 };
		constexpr std::chrono::seconds kDiagEvery{ 5 };
		bool g_closeRequested{ false };
		std::chrono::steady_clock::time_point g_closeRequestedAt{};
		bool g_firstDiagLogged{ false };
		std::chrono::steady_clock::time_point g_lastDiag{};

		constexpr std::uintptr_t kOgRva{ 0xB935C0 };
		constexpr std::uintptr_t kNgRva{ 0xA728F0 };
		constexpr std::uintptr_t kAeRva{ 0xAE4090 };

		struct XInputPacket
		{
			DWORD packet;
			WORD  buttons;
			BYTE  rest[10];
		};
		using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XInputPacket*);

		// Constraint: counts only new key, mouse-button or gamepad-button presses while the game window has focus, never which input, so held keys and stick drift cannot keep a stuck Pip-Boy open.
		bool PlayerInputSeen()
		{
			static XInputGetStateFn getState = [] {
				const auto module = LoadLibraryExW(L"xinput1_4.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
				return module ? reinterpret_cast<XInputGetStateFn>(GetProcAddress(module, "XInputGetState")) : nullptr;
			}();
			static std::array<bool, 255> wasDown{};
			static std::array<WORD, 4> lastButtons{};
			DWORD foregroundProcess = 0;
			GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
			const bool focused = foregroundProcess == GetCurrentProcessId();
			bool seen = false;
			for (int vk = 1; vk < 255; ++vk) {
				const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
				seen = seen || (down && !wasDown[vk]);
				wasDown[vk] = down;
			}
			if (getState) {
				for (DWORD i = 0; i < lastButtons.size(); ++i) {
					XInputPacket state{};
					if (getState(i, &state) == ERROR_SUCCESS) {
						seen = seen || (state.buttons != 0 && state.buttons != lastButtons[i]);
						lastButtons[i] = state.buttons;
					}
				}
			}
			return focused && seen;
		}

		RE::UIMenuEntry::Create_t* g_originalCreate{ nullptr };

		bool InSession()
		{
			return F4T::Networking::IsConnectedToServer() && F4T::RemotePlayerState::GetAssignedPlayerId().has_value();
		}

		bool InMultiplayerSession()
		{
			// Constraint: first-join onboarding keeps the vanilla paused Pip-Boy handout, so the unpause and input lock must not apply.
			return InSession() && !F4T::SessionLaunch::IsFirstJoinOnboarding();
		}

		void NoteMessage(const RE::PipboyMenu* a_menu, const RE::UIMessage& a_message, RE::UI_MESSAGE_RESULTS a_result)
		{
			if (a_message.type.get() != RE::UI_MESSAGE_TYPE::kHide) {
				return;
			}
			if (!g_closeRequested) {
				g_closeRequestedAt = std::chrono::steady_clock::now();
				g_closeRequested = true;
			}
			F4T::PipboyClose::LogState(std::format("hide message: hasData={} result={} onStack={}", a_message.QData() != nullptr, static_cast<int>(a_result), a_menu->OnStack()), g_inputLayer.get() != nullptr);
		}

		RE::UI_MESSAGE_RESULTS ProcessMessageHook(RE::PipboyMenu* a_this, RE::UIMessage& a_message)
		{
			const auto result = g_originalProcessMessage(a_this, a_message);
			NoteMessage(a_this, a_message, result);
			if (a_message.type.get() == RE::UI_MESSAGE_TYPE::kShow) {
				const auto clearPausesGame = PipboyPauseLatch::OnShow(g_latch, a_this->OnStack(), InMultiplayerSession());
				F4T::DebugLogging::Log("PipboyUnpause", "PipboyMenu show: onStack={} clearPausesGame={}", a_this->OnStack(), clearPausesGame);
				if (clearPausesGame) {
					a_this->menuFlags.reset(RE::UI_MENU_FLAGS::kPausesGame);
				}
			}
			return result;
		}

		// Constraint: the freeze-frame and blur flags must be cleared at construction, before the show message captures the background.
		RE::IMenu* PipboyCreateHook(const RE::UIMessage& a_message)
		{
			const auto menu = g_originalCreate(a_message);
			if (menu && InMultiplayerSession()) {
				menu->menuFlags.reset(RE::UI_MENU_FLAGS::kFreezeFrameBackground);
				menu->menuFlags.reset(RE::UI_MENU_FLAGS::kUsesBlurredBackground);
			}
			return menu;
		}

		void InstallCreateHook(RE::UI* a_ui)
		{
			static bool attempted = false;
			if (attempted) {
				return;
			}
			const RE::BSAutoWriteLock lock{ RE::UI::GetMenuMapRWLock() };
			const auto it = a_ui->menuMap.find(RE::BSFixedString{ RE::PipboyMenu::MENU_NAME });
			if (it == a_ui->menuMap.end() || !it->second.create) {
				return;
			}
			attempted = true;
			g_originalCreate = it->second.create;
			it->second.create = &PipboyCreateHook;
			F4T::DebugLogging::Log("PipboyUnpause", "PipboyMenu create hook installed");
		}

		// Constraint: restore only after the pump removed the menu (OnStack false) so the engine's hide decrement still sees the flag cleared.
		void RestorePauseFlag(const RE::UI* a_ui)
		{
			const auto menu = a_ui->GetMenu<RE::PipboyMenu>();
			if (PipboyPauseLatch::OnTick(g_latch, menu != nullptr, menu && menu->OnStack())) {
				menu->menuFlags.set(RE::UI_MENU_FLAGS::kPausesGame);
			}
		}

		void LockInput()
		{
			if (g_inputLayer) {
				return;
			}
			auto* manager = RE::BSInputEnableManager::GetSingleton();
			if (!manager || !manager->AllocateNewLayer(g_inputLayer, "CommonwealthOnline_Pipboy")) {
				return;
			}
			for (const auto flag : kLockedUserEvents) {
				manager->EnableUserEvent(g_inputLayer->layerID, flag, false, RE::UserEvents::SENDER_ID::kScript);
			}
		}

		void InstallProcessMessageHook()
		{
			const auto target = F4T::ExecutingRuntime::Target();
			if (!target) {
				REX::CRITICAL("Pip-Boy unpause hook NOT installed (runtime is not OG 1.10.163, NG 1.10.984 or AE 1.11.240).");
				return;
			}
			REL::Relocation<std::uintptr_t> table{ RE::VTABLE::PipboyMenu[0] /* E:/RE-work/co-pipboy/pipboy-unpause-static-facts.md */ };
			const auto module = REX::FModule::GetExecutingModule();
			const auto text = module.GetSection(".text");
			const auto rdata = module.GetSection(".rdata");

			VtableGuard::SlotRequest request{};
			request.vtableAddress = table.address();
			request.slotIndex = kProcessMessageSlot;
			request.replacement = REX::UNRESTRICTED_CAST<std::uintptr_t>(&ProcessMessageHook);
			request.codeSection = { text.GetAddress(), text.GetAddress() + text.GetSize() };
			request.vtableSection = { rdata.GetAddress(), rdata.GetAddress() + rdata.GetSize() };
			request.slotValue = VtableGuard::ReadSlot(request);

			const auto verdict = VtableGuard::VerifySlot(request);
			if (verdict != VtableGuard::SlotVerdict::kOk) {
				REX::CRITICAL("Pip-Boy unpause hook NOT installed ({}).", VtableGuard::Describe(verdict));
				return;
			}
			if (!VtableGuard::SlotMatchesExpectedRva(request.slotValue, module.GetBaseAddress(), F4T::ExecutingRuntime::RvaFor(*target, kOgRva, kNgRva, kAeRva))) {
				REX::CRITICAL("Pip-Boy unpause hook NOT installed: slot 3 is not this runtime's PipboyMenu::ProcessMessage.");
				return;
			}

			g_originalProcessMessage = reinterpret_cast<ProcessMessageFn>(request.slotValue);
			table.write_vfunc(kProcessMessageSlot, request.replacement);
			const auto slotAddress = request.vtableAddress + (kProcessMessageSlot * sizeof(std::uintptr_t));
			if (!VtableGuard::WriteSucceeded(*reinterpret_cast<const std::uintptr_t*>(slotAddress), request.replacement)) {
				REX::CRITICAL("Pip-Boy unpause hook FAILED to install: function table slot did not take the write.");
				return;
			}
			F4T::DebugLogging::Log("PipboyUnpause", "PipboyMenu ProcessMessage hook installed on slot {}", kProcessMessageSlot);
		}
	}

	void Install()
	{
		if (g_installAttempted) {
			return;
		}
		g_installAttempted = true;
		InstallProcessMessageHook();
	}

	// Constraint: a Pip-Boy left open this long during first-join onboarding is a stuck handout, so it is force-closed to give the player a way out.
	static void CloseIfOnboardingStuck(bool a_open)
	{
		const auto now = std::chrono::steady_clock::now();
		if (!a_open || !F4T::SessionLaunch::IsFirstJoinOnboarding() || (now - g_openSince < kOnboardingStuckAfter && now - g_openedAt < kOnboardingHardCap) || now - g_lastForcedClose < kForcedCloseRetry) {
			return;
		}
		g_lastForcedClose = now;
		F4T::DebugLogging::Log("PipboyUnpause", "Pip-Boy idle for {}s during first-join onboarding; forcing it closed", std::chrono::duration_cast<std::chrono::seconds>(now - g_openSince).count());
		CloseForDownedPlayer();
	}

	// Constraint: a close that was requested but left the Pip-Boy open this long means the engine's animation-event close never completed, in any connected session.
	static void CloseIfCloseStuck(bool a_open)
	{
		const auto now = std::chrono::steady_clock::now();
		if (!a_open || !InSession()) {
			return;
		}
		if (!g_closeRequested && F4T::PipboyClose::ClosePending()) {
			g_closeRequestedAt = now;
			g_closeRequested = true;
		}
		if (!g_closeRequested || now - g_closeRequestedAt < kCloseStuckAfter || now - g_lastForcedClose < kForcedCloseRetry) {
			return;
		}
		g_lastForcedClose = now;
		F4T::DebugLogging::Log("PipboyUnpause", "Pip-Boy still open {}s after a close was requested; forcing it closed", std::chrono::duration_cast<std::chrono::seconds>(now - g_closeRequestedAt).count());
		CloseForDownedPlayer();
	}

	static void RunPendingForceClose(bool a_open)
	{
		if (!g_forceRequested.exchange(false)) {
			return;
		}
		if (!a_open) {
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		if (!g_closeRequested) {
			g_closeRequestedAt = now;
			g_closeRequested = true;
		}
		g_lastForcedClose = now;
		F4T::PipboyClose::ForceClose("close requested");
	}

	static void LogOpenDiagnostics(bool a_open)
	{
		const auto now = std::chrono::steady_clock::now();
		if (!a_open) {
			return;
		}
		const auto openFor = now - g_openedAt;
		if (!g_firstDiagLogged && openFor >= kDiagFirst) {
			g_firstDiagLogged = true;
			F4T::PipboyClose::LogState("open +1s", g_inputLayer.get() != nullptr);
		}
		if (openFor > kDiagLongOpen && now - g_lastDiag >= kDiagEvery) {
			g_lastDiag = now;
			F4T::PipboyClose::LogState(std::format("open {}s", std::chrono::duration_cast<std::chrono::seconds>(openFor).count()), g_inputLayer.get() != nullptr);
		}
	}

	void Tick()
	{
		auto* ui = RE::UI::GetSingleton();
		if (ui) {
			InstallCreateHook(ui);
			F4T::PipboyClose::Install();
			RestorePauseFlag(ui);
		}
		const auto isOpen = ui && ui->GetMenuOpen<RE::PipboyMenu>();
		if (isOpen != g_pipboyOpen) {
			g_pipboyOpen = isOpen;
			g_openedAt = std::chrono::steady_clock::now();
			g_openSince = std::chrono::steady_clock::now();
			g_closeRequested = false;
			g_firstDiagLogged = false;
			g_lastDiag = g_openedAt;
			F4T::DebugLogging::Log("PipboyUnpause", "PipboyMenu {} (multiplayer={})", isOpen ? "opened" : "closed", InMultiplayerSession());
			if (!isOpen) {
				F4T::PipboyClose::LogState("closed", g_inputLayer.get() != nullptr);
			}
			F4T::PipboyClose::OnMenuEdge();
			if (isOpen) {
				F4T::PipboyClose::LogState("open", g_inputLayer.get() != nullptr);
			}
		}
		if (isOpen && InMultiplayerSession()) {
			LockInput();
		} else {
			g_inputLayer.reset();
		}
		if (isOpen && F4T::SessionLaunch::IsFirstJoinOnboarding() && PlayerInputSeen()) {
			g_openSince = std::chrono::steady_clock::now();
		}
		LogOpenDiagnostics(isOpen);
		CloseIfOnboardingStuck(isOpen);
		CloseIfCloseStuck(isOpen);
		RunPendingForceClose(isOpen);
		const auto gateOn = isOpen && InMultiplayerSession() && !g_closeRequested;
		F4T::PipboyClose::SetState(InSession(), gateOn);
	}

	void CloseForDownedPlayer()
	{
		const auto* ui = RE::UI::GetSingleton();
		if (ui && ui->GetMenuOpen<RE::PipboyMenu>()) {
			g_forceRequested = true;
		}
	}
}

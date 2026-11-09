#include "pch.h"

#include "F4TPipboyUnpause.h"

#include "F4TDebugLogging.h"
#include "F4TExecutingRuntime.h"
#include "F4TNetworking.h"
#include "F4TPipboyPauseLatch.h"
#include "F4TRemotePlayerState.h"
#include "F4TSessionLaunch.h"
#include "F4TVtableGuard.h"

#include "RE/B/BSInputEnableLayer.h"
#include "RE/B/BSInputEnableManager.h"
#include "RE/B/BSTSmartPointer.h"
#include "RE/P/PipboyMenu.h"
#include "RE/U/UI.h"
#include "RE/U/UI_MENU_FLAGS.h"
#include "RE/U/UI_MESSAGE_RESULTS.h"
#include "RE/U/UI_MESSAGE_TYPE.h"
#include "RE/U/UIMessage.h"
#include "RE/U/UIMessageQueue.h"
#include "RE/U/UserEvents.h"

#include <windows.h>

#include <array>
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

		bool InMultiplayerSession()
		{
			// Constraint: first-join onboarding keeps the vanilla paused Pip-Boy handout, so the unpause and input lock must not apply.
			return F4T::Networking::IsConnectedToServer() && F4T::RemotePlayerState::GetAssignedPlayerId().has_value() && !F4T::SessionLaunch::IsFirstJoinOnboarding();
		}

		RE::UI_MESSAGE_RESULTS ProcessMessageHook(RE::PipboyMenu* a_this, RE::UIMessage& a_message)
		{
			const auto result = g_originalProcessMessage(a_this, a_message);
			if (a_message.type.get() == RE::UI_MESSAGE_TYPE::kShow) {
				const auto clearPausesGame = PipboyPauseLatch::OnShow(g_latch, a_this->OnStack(), InMultiplayerSession());
				F4T::DebugLogging::Log("PipboyUnpause", "PipboyMenu show: onStack={} clearPausesGame={}", a_this->OnStack(), clearPausesGame);
				if (clearPausesGame) {
					a_this->menuFlags.reset(RE::UI_MENU_FLAGS::kPausesGame);
				}
			}
			return result;
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

	void Tick()
	{
		const auto* ui = RE::UI::GetSingleton();
		if (ui) {
			RestorePauseFlag(ui);
		}
		const auto isOpen = ui && ui->GetMenuOpen<RE::PipboyMenu>();
		if (isOpen != g_pipboyOpen) {
			g_pipboyOpen = isOpen;
			g_openedAt = std::chrono::steady_clock::now();
			g_openSince = std::chrono::steady_clock::now();
			F4T::DebugLogging::Log("PipboyUnpause", "PipboyMenu {} (multiplayer={})", isOpen ? "opened" : "closed", InMultiplayerSession());
		}
		if (isOpen && InMultiplayerSession()) {
			LockInput();
		} else {
			g_inputLayer.reset();
		}
		if (isOpen && F4T::SessionLaunch::IsFirstJoinOnboarding() && PlayerInputSeen()) {
			g_openSince = std::chrono::steady_clock::now();
		}
		CloseIfOnboardingStuck(isOpen);
	}

	void CloseForDownedPlayer()
	{
		const auto* ui = RE::UI::GetSingleton();
		auto* queue = RE::UIMessageQueue::GetSingleton();
		if (ui && queue && ui->GetMenuOpen<RE::PipboyMenu>()) {
			queue->AddMessage(RE::BSFixedString{ RE::PipboyMenu::MENU_NAME }, RE::UI_MESSAGE_TYPE::kHide);
		}
	}
}

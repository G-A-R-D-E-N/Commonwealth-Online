#include "pch.h"

#include "F4TMapKey.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TDebugLogging.h"
#include "F4TInteractWheel.h"
#include "F4TNetworking.h"
#include "F4TRemotePlayerState.h"
#include "F4TVtableGuard.h"

#include "RE/B/BSInputEventUser.h"
#include "RE/B/ButtonEvent.h"
#include "RE/M/MenuControls.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace F4T::MapKey
{
	namespace
	{
		constexpr std::string_view kQuickMapUserEvent{ "QuickMap" };
		// RE offset source:commonlibf4-nomad/include/RE/B/BSInputEventUser.h slot 8, include/RE/M/MenuControls.h pipboyHandler.
		constexpr std::size_t kOnButtonEventSlot{ 8 };

		using OnButtonEventFn = void (*)(RE::BSInputEventUser*, const RE::ButtonEvent*);

		std::uintptr_t g_originalOnButtonEvent{ 0 };
		// Must run on the game thread (per-frame permanent task).
		bool g_installAttempted{ false };

		bool ConsumeQuickMap(const RE::ButtonEvent& a_event)
		{
			const auto coWindowOpen = F4T::InteractWheel::CoWindowOpen();
			const auto connected = F4T::Networking::IsConnectedToServer() && F4T::RemotePlayerState::GetAssignedPlayerId().has_value();
			if (!coWindowOpen && !connected) {
				return false;
			}
			auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI19>();
			if (!ui || !ui->HandlePauseMenuMapKey) {
				return false;
			}
			if (a_event.QJustPressed()) {
				ui->HandlePauseMenuMapKey(!coWindowOpen && !F4T::InteractWheel::VanillaMenuOpen(false));
			}
			return true;
		}

		void OnButtonEventHook(RE::BSInputEventUser* a_this, const RE::ButtonEvent* a_event)
		{
			if (a_event && a_event->QUserEvent() == kQuickMapUserEvent && ConsumeQuickMap(*a_event)) {
				return;
			}
			reinterpret_cast<OnButtonEventFn>(g_originalOnButtonEvent)(a_this, a_event);
		}

		void InstallOnButtonEventHook(std::uintptr_t a_vtableAddress)
		{
			REL::Relocation<std::uintptr_t> vtable{ a_vtableAddress };
			const auto module = REX::FModule::GetExecutingModule();
			const auto text = module.GetSection(".text");
			const auto rdata = module.GetSection(".rdata");

			VtableGuard::SlotRequest request{};
			request.vtableAddress = vtable.address();
			request.slotIndex = kOnButtonEventSlot;
			request.replacement = REX::UNRESTRICTED_CAST<std::uintptr_t>(&OnButtonEventHook);
			request.codeSection = { text.GetAddress(), text.GetAddress() + text.GetSize() };
			request.vtableSection = { rdata.GetAddress(), rdata.GetAddress() + rdata.GetSize() };
			request.slotValue = VtableGuard::ReadSlot(request);

			const auto verdict = VtableGuard::VerifySlot(request);
			if (verdict != VtableGuard::SlotVerdict::kOk && verdict != VtableGuard::SlotVerdict::kSlotNotExecutable) {
				REX::CRITICAL("Map key hook NOT installed ({}).", VtableGuard::Describe(verdict));
				return;
			}

			g_originalOnButtonEvent = request.slotValue;
			vtable.write_vfunc(kOnButtonEventSlot, request.replacement);
			const auto slotAddress = request.vtableAddress + (kOnButtonEventSlot * sizeof(std::uintptr_t));
			if (!VtableGuard::WriteSucceeded(*reinterpret_cast<const std::uintptr_t*>(slotAddress), request.replacement)) {
				REX::CRITICAL("Map key hook FAILED to install: vtable slot did not take the write.");
				return;
			}
			F4T::DebugLogging::Log("MapKey", "QuickMap hook installed on PipboyHandler vtable slot {}", kOnButtonEventSlot);
		}
	}

	void Install()
	{
		if (g_installAttempted) {
			return;
		}
		const auto* controls = RE::MenuControls::GetSingleton();
		if (!controls || !controls->pipboyHandler) {
			return;
		}
		g_installAttempted = true;
		const auto vtableAddress = *reinterpret_cast<const std::uintptr_t*>(controls->pipboyHandler);
		InstallOnButtonEventHook(vtableAddress);
	}
}

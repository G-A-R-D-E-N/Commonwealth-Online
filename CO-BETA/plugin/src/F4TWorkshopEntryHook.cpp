#include "pch.h"

#include "F4TWorkshopEntryHook.h"
#include "F4THookThreadTable.h"

#include "F4TDebugLogging.h"
#include "F4TInlineHook.h"
#include "F4TNetworking.h"
#include "F4TRuntimeSelect.h"
#include "F4TSettlementCaptureClient.h"
#include "F4TUIClient.h"
#include "F4TWorkshopReplicationClient.h"
#include "RE/P/PlayerCharacter.h"

#include <array>
#include <cstdint>
#include <format>
#include <intrin.h>
#include <span>
#include <string_view>

namespace F4T::WorkshopEntryHook
{
	namespace
	{
		bool s_installAttempted = false;

		void LogLine(std::string_view a_message)
		{
			F4T::DebugLogging::Write("WorkshopEntry", a_message);
		}

		bool OpenFactionWindow()
		{
			static auto* api = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI25>();
			return api && api->OpenWorldFactionWindow && api->OpenWorldFactionWindow();
		}

		inline constexpr REL::ID kWorkshopEntry{ 171771, 2195134, 2195134 } /* E:/RE-work/co-workbench/workshop-entry-hook.md */;
		inline constexpr REL::ID kStartNearbyWorkshop{ 937340, 2194880, 2194880 } /* E:/RE-work/co-workbench/startnearbyworkshop-verification.md */;
		constexpr std::uintptr_t kStartNearbyEntryReturnOffset{ 0x41 }; // RE: E:/RE-work/Tools/steamless-work Fallout4.163.exe.unpacked.exe, E8 call at +0x3C into the entry
		constexpr std::array<std::uint8_t, 15> kPrologueOg{
			0x40, 0x55, 0x53, 0x56, 0x57, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x58, 0x48, 0x8B, 0xD9
		};
		constexpr std::array<std::uint8_t, 17> kPrologueNgAe{
			0x48, 0x8B, 0xC4, 0x55, 0x41, 0x56, 0x48, 0x8D, 0x68, 0xA1, 0x48, 0x81, 0xEC, 0x98, 0x00, 0x00, 0x00
		};

		using WorkshopEntryFn = void (*)(RE::TESObjectREFR*);

		F4T::InlineHook::Result s_entryHook{};

		void WorkshopEntryDetour(RE::TESObjectREFR* a_workbench) noexcept
		{
			F4T::HookThread::Note(F4T::HookThread::Id::WorkshopEntry);
			if (!s_entryHook.hook) {
				return;
			}
			const auto fromNearbyPrompt = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == kStartNearbyWorkshop.address() + kStartNearbyEntryReturnOffset;
			if (a_workbench) {
				try {
					auto* player = RE::PlayerCharacter::GetSingleton();
					if (!fromNearbyPrompt && F4T::WorkshopReplicationClient::SubmitCampStore(*a_workbench, player && player->IsSneaking())) {
						return;
					}
					if (F4T::Networking::IsConnectedToServer() &&
						F4T::SettlementCaptureClient::CannotBuildAtWorkbench(*a_workbench)) {
						if (OpenFactionWindow()) {
							return;
						}
						LogLine("settlement window refused; vanilla workshop applies");
					}
				} catch (...) {
				}
			}
			s_entryHook.hook->original<WorkshopEntryFn>()(a_workbench);
		}
	}

	void Install()
	{
		if (s_installAttempted) {
			return;
		}
		s_installAttempted = true;
		const auto module = REX::FModule::GetExecutingModule();
		const auto version = module.GetFileVersion();
		if (version != F4T::RuntimeSelect::VerifiedVersion()) {
			LogLine(std::format(
				"workshop entry hook not installed: runtime {} is not verified (expected {}); vanilla workshop applies",
				version.string(),
				F4T::RuntimeSelect::VerifiedVersion().string()));
			return;
		}
		const auto text = module.GetSection(".text");
		F4T::InlineHook::Request request{};
		request.target = kWorkshopEntry.address();
		request.codeSection = { text.GetAddress(), text.GetAddress() + text.GetSize() };
		request.expectedPrologue = F4T::RuntimeSelect::IsOG()
			? std::span<const std::uint8_t>{ kPrologueOg }
			: std::span<const std::uint8_t>{ kPrologueNgAe };
		request.destination = REX::UNRESTRICTED_CAST<void*>(&WorkshopEntryDetour);
		F4T::InlineHook::Install(request, LogLine, s_entryHook);
	}
}

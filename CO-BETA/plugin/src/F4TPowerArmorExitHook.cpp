#include "pch.h"

#include "F4TPowerArmorExitHook.h"
#include "F4THookThreadTable.h"

#include "F4TDebugLogging.h"
#include "F4TInlineHook.h"
#include "F4TRuntimeSelect.h"

#include "RE/A/Actor.h"

#include <array>
#include <cstdint>
#include <format>
#include <span>
#include <string_view>

namespace F4T::PowerArmorExitHook
{
	namespace
	{
		bool s_installAttempted = false;

		void LogLine(std::string_view a_message)
		{
			F4T::DebugLogging::Write("PowerArmorExit", a_message);
		}

		inline constexpr REL::ID kSwitchFromPowerArmorStart{ 550299, 2219450, 2219450 } /* E:/RE-work/F4RR-main-20261023/RE-Work/subsystems/commonwealth-online-re/co-pr363-answers/findings.jsonl */;
		constexpr std::array<std::uint8_t, 22> kExpectedPrologueOg{
			0x48, 0x8B, 0xC4, 0x55, 0x53, 0x41, 0x56, 0x48, 0x8D, 0x68, 0xA8,
			0x48, 0x81, 0xEC, 0x40, 0x01, 0x00, 0x00, 0x48, 0x89, 0x70, 0xE0
		};
		constexpr std::array<std::uint8_t, 22> kExpectedPrologueNgAe{
			0x48, 0x8B, 0xC4, 0x55, 0x53, 0x41, 0x56, 0x48, 0x8D, 0x68, 0xB8,
			0x48, 0x81, 0xEC, 0x30, 0x01, 0x00, 0x00, 0x48, 0x89, 0x70, 0xE0
		};

		using SwitchFromPowerArmorStartFn = void (*)(RE::Actor*);

		F4T::InlineHook::Result s_exitHook{};

		void SwitchFromPowerArmorStartDetour(RE::Actor* a_actor) noexcept
		{
			F4T::HookThread::Note(F4T::HookThread::Id::PowerArmorExit);
			s_exitHook.hook->original<SwitchFromPowerArmorStartFn>()(a_actor);
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
				"power armor exit hook not installed: runtime {} is not verified (expected {})",
				version.string(),
				F4T::RuntimeSelect::VerifiedVersion().string()));
			return;
		}
		const auto text = module.GetSection(".text");
		F4T::InlineHook::Request request{};
		request.target = kSwitchFromPowerArmorStart.address();
		request.codeSection = { text.GetAddress(), text.GetAddress() + text.GetSize() };
		request.expectedPrologue = F4T::RuntimeSelect::IsOG()
			? std::span<const std::uint8_t>{ kExpectedPrologueOg }
			: std::span<const std::uint8_t>{ kExpectedPrologueNgAe };
		request.destination = REX::UNRESTRICTED_CAST<void*>(&SwitchFromPowerArmorStartDetour);
		F4T::InlineHook::Install(request, LogLine, s_exitHook);
	}
}

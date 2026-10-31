#include "pch.h"

#include "F4TProjectileLaunchHook.h"
#include "F4THookThreadTable.h"

#include "F4TDebugLogging.h"
#include "F4TInlineHook.h"
#include "F4TProjectileAim.h"
#include "F4TRangedCombatCapture.h"
#include "F4TShotReplay.h"

#include "RE/P/PlayerCharacter.h"
#include "RE/P/ProjectileLaunchData.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string_view>

namespace F4T::ProjectileLaunchHook
{
	namespace
	{
		bool s_installAttempted = false;

		void LogLine(std::string_view a_message)
		{
			F4T::DebugLogging::Write("ProjectileLaunch", a_message);
		}

// source:RE-Work/subsystems/commonwealth-online-re/ranged-hit-identity-co56/README.md (section 10, #219 answer)
#if defined(F4T_TARGET_OG)
		inline constexpr REL::ID kLaunchDispatch{ 1452334 };
		inline constexpr REL::Version kVerifiedVersion{ 1, 10, 163 };
		constexpr std::array<std::uint8_t, 20> kExpectedPrologue{
			0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x4B, 0x08, 0x53, 0x57, 0x41,
			0x54, 0x41, 0x56, 0x48, 0x81, 0xEC, 0xA8, 0x00, 0x00, 0x00
		};
#elif defined(F4T_TARGET_NG) || defined(F4T_TARGET_AE)
		inline constexpr REL::ID kLaunchDispatch{ 2236958 };
	#if defined(F4T_TARGET_NG)
		inline constexpr REL::Version kVerifiedVersion{ 1, 10, 984 };
	#else
		inline constexpr REL::Version kVerifiedVersion{ 1, 11, 240 };
	#endif
		constexpr std::array<std::uint8_t, 15> kExpectedPrologue{
			0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x57
		};
#else
	#error "A Commonwealth Online runtime target is required"
#endif

		static_assert(offsetof(RE::ProjectileLaunchData, zAngle) == 0x4C);
		static_assert(offsetof(RE::ProjectileLaunchData, xAngle) == 0x50);

		using LaunchDispatchFn = std::uint32_t* (*)(std::uint32_t*, RE::ProjectileLaunchData*);

		F4T::InlineHook::Result s_launchHook{};

		// May run off the main thread and re-entrantly; stack-only state.
		// source:RE-Work/subsystems/commonwealth-online-re/co245-ranged/README.md (section 11 item 1)
		std::uint32_t* LaunchDispatchDetour(std::uint32_t* a_handleOut, RE::ProjectileLaunchData* a_launch) noexcept
		{
			F4T::HookThread::Note(F4T::HookThread::Id::LaunchDispatch);
			if (!s_launchHook.hook) {
				return a_handleOut;
			}
			const auto original = s_launchHook.hook->original<LaunchDispatchFn>();
			auto* const returned = original(a_handleOut, a_launch);
			if (!a_handleOut || !a_launch) {
				return returned;
			}
			const auto handle = *a_handleOut;
			auto* localPlayer = RE::PlayerCharacter::GetSingleton();
			if (!a_launch->shooter || !localPlayer || a_launch->shooter != localPlayer || handle == 0) {
				return returned;
			}
			try {
				F4T::RangedCombatCapture::NoteLocalProjectileLaunch(handle, std::chrono::steady_clock::now());
				F4T::ProjectileAim::QueueAimForSend(F4T::ShotReplay::DirectionFromLaunchAngles(
					static_cast<double>(a_launch->zAngle),
					static_cast<double>(a_launch->xAngle)));
			} catch (...) {
			}
			return returned;
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
		if (version != kVerifiedVersion) {
			LogLine(std::format(
				"launch hook not installed: runtime {} is not verified (expected {}); strict ranged rule applies",
				version.string(),
				kVerifiedVersion.string()));
			return;
		}
		const auto text = module.GetSection(".text");
		F4T::InlineHook::Request request{};
		request.target = kLaunchDispatch.address();
		request.codeSection = { text.GetAddress(), text.GetAddress() + text.GetSize() };
		request.expectedPrologue = kExpectedPrologue;
		request.destination = REX::UNRESTRICTED_CAST<void*>(&LaunchDispatchDetour);
		F4T::InlineHook::Install(request, LogLine, s_launchHook);
	}
}

// License: GPL-3.0 with additional terms (FatherLamb); ported from FathersFallout client-ng/src/sync/FrameLimiter.cpp.
#include "pch.h"

#include "F4TFrameLimiter.h"
#include "F4TFrameLimiterPacing.h"

#include "F4TDebugLogging.h"
#include "F4TInlineHook.h"

#include "RE/P/PlayerCharacter.h"
#include "RE/U/UI.h"

#include <Windows.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>
#include <timeapi.h>

namespace F4T::FrameLimiter
{
	namespace
	{
		constexpr std::int64_t kTargetFps = 60;
		constexpr std::int64_t kPeriodNs = 1'000'000'000 / kTargetFps;
		constexpr std::int64_t kSpinNs = 1'000'000;
		constexpr std::int64_t kDoubleIdleNs = 4'000'000;

		// Must be 15 bytes: a 14-byte jump would cut the instruction at offset 0x0B.
#if defined(F4T_TARGET_OG)
		inline constexpr REL::ID kOnIdle{ 633524 };
		constexpr std::array<std::uint8_t, 15> kExpectedPrologue{
			0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40
		};
#elif defined(F4T_TARGET_NG) || defined(F4T_TARGET_AE)
		inline constexpr REL::ID kOnIdle{ 2228917 };
		constexpr std::array<std::uint8_t, 15> kExpectedPrologue{
			0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x10, 0x48, 0x89, 0x78, 0x18
		};
#else
	#error "A Commonwealth Online runtime target is required"
#endif

		using OnIdleFn = void (*)(void*);

		F4T::InlineHook::Result s_hook{};
		bool s_installAttempted = false;
		HANDLE s_timer = nullptr;
		std::int64_t s_anchorNs = 0;
		std::int64_t s_lastIdleNs = 0;
		bool s_doubleIdleLogged = false;

		void LogLine(std::string_view a_message)
		{
			F4T::DebugLogging::Write("FrameLimiter", a_message);
		}

		std::int64_t NowNs()
		{
			return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		void WaitUntil(std::int64_t a_deadlineNs)
		{
			std::int64_t blockNs = 0;
			while ((blockNs = F4T::FrameLimiterPacing::BlockNs(a_deadlineNs - NowNs(), kSpinNs)) > 0) {
				LARGE_INTEGER due{};
				due.QuadPart = -(blockNs / 100);
				if (!s_timer || !SetWaitableTimerEx(s_timer, &due, 0, nullptr, nullptr, nullptr, 0) ||
				    WaitForSingleObject(s_timer, INFINITE) != WAIT_OBJECT_0) {
					Sleep(1);
				}
			}
			while (NowNs() < a_deadlineNs) {
				YieldProcessor();
			}
		}

		bool Capped()
		{
			const auto* ui = RE::UI::GetSingleton();
			const bool loading = ui && ui->GetMenuOpen("LoadingMenu");
			const bool mainMenu = ui && ui->GetMenuOpen("MainMenu");
			return F4T::FrameLimiterPacing::ShouldCap(RE::PlayerCharacter::GetSingleton() != nullptr, loading, mainMenu);
		}

		void Pace()
		{
			if (!Capped()) {
				s_anchorNs = NowNs();
				return;
			}
			WaitUntil(s_anchorNs + kPeriodNs);
			s_anchorNs = F4T::FrameLimiterPacing::NextAnchorNs(s_anchorNs, kPeriodNs, NowNs());
		}

		void OnIdleDetour(void* a_main) noexcept
		{
			if (!s_hook.hook) {
				return;
			}
			const auto now = NowNs();
			// TODO-verify(#568): OnIdle may run more than once per frame; source:E:/GardenEngine/re-answers/2026-10-27-fs-p2-onidle-prologue.md
			if (!s_doubleIdleLogged && s_lastIdleNs != 0 && now - s_lastIdleNs < kDoubleIdleNs) {
				s_doubleIdleLogged = true;
				try {
					LogLine("OnIdle ran twice under 4 ms apart");
				} catch (...) {
				}
			}
			s_lastIdleNs = now;
			Pace();
			s_hook.hook->original<OnIdleFn>()(a_main);
		}
	}

	void Install()
	{
		if (s_installAttempted) {
			return;
		}
		s_installAttempted = true;
		s_timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
		if (!s_timer) {
			timeBeginPeriod(1);
		}
		F4T::DebugLogging::Log("FrameLimiter", "cap {} FPS; hi-res timer: {}", kTargetFps, s_timer != nullptr);
		const auto text = REX::FModule::GetExecutingModule().GetSection(".text");
		F4T::InlineHook::Request request{};
		request.target = kOnIdle.address();
		request.codeSection = { text.GetAddress(), text.GetAddress() + text.GetSize() };
		request.expectedPrologue = kExpectedPrologue;
		request.destination = REX::UNRESTRICTED_CAST<void*>(&OnIdleDetour);
		F4T::InlineHook::Install(request, LogLine, s_hook);
	}
}

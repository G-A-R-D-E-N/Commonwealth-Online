#include "pch.h"
// address source:commonlibf4-nomad

#include "F4TProjectileLaunchHook.h"
#include "F4THookThreadTable.h"

#include "F4TDebugLogging.h"
#include "F4TCombatTicketStore.h"
#include "F4TInlineHook.h"
#include "F4TRuntimeSelect.h"
#include "F4TProjectileAim.h"
#include "F4TRangedCombatCapture.h"
#include "F4TReplicationRuntime.h"
#include "F4TShotReplay.h"

#include "RE/P/PlayerCharacter.h"
#include "RE/P/ProjectileLaunchData.h"
#include "RE/T/TESObjectWEAP.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <intrin.h>
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
		inline constexpr REL::ID kLaunchDispatch{ 1452334, 2236958, 2236958 };
		constexpr std::array<std::uint8_t, 15> kFirePrologueOg{
			0x44, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x41, 0x54, 0x41, 0x55
		};
		constexpr std::array<std::uint8_t, 16> kFirePrologueNgAe{
			0x4C, 0x89, 0x4C, 0x24, 0x20, 0x48, 0x89, 0x54, 0x24, 0x10, 0x48, 0x89, 0x4C, 0x24, 0x08, 0x55
		};
		constexpr std::array<std::uint8_t, 5> kFireLaunchOg{ 0xE8, 0xDD, 0xC8, 0xC7, 0x00 };
		constexpr std::array<std::uint8_t, 5> kFireLaunchNg{ 0xE8, 0x65, 0x61, 0x99, 0x00 };
		constexpr std::array<std::uint8_t, 5> kFireLaunchAe{ 0xE8, 0x45, 0x81, 0x9C, 0x00 };
		constexpr std::array<std::uint8_t, 20> kExpectedPrologueOg{
			0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x4B, 0x08, 0x53, 0x57, 0x41,
			0x54, 0x41, 0x56, 0x48, 0x81, 0xEC, 0xA8, 0x00, 0x00, 0x00
		};
		constexpr std::array<std::uint8_t, 15> kExpectedPrologueNgAe{
			0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x57
		};

		static_assert(offsetof(RE::ProjectileLaunchData, zAngle) == 0x4C);
		static_assert(offsetof(RE::ProjectileLaunchData, xAngle) == 0x50);

		using LaunchDispatchFn = std::uint32_t* (*)(std::uint32_t*, RE::ProjectileLaunchData*);
		using FireFn = void (*)(const void*, RE::TESObjectREFR*, std::uint32_t, void*, void*);

		F4T::InlineHook::Result s_launchHook{};
		F4T::InlineHook::Result s_fireHook{};
		std::atomic<LaunchDispatchFn> s_launchOriginal{};
		std::atomic<bool> s_launchReady{};
		std::atomic<FireFn> s_fireOriginal{};
		std::atomic<bool> s_fireReady{};
		std::atomic<std::uintptr_t> s_fireLaunchReturn{};

		void PublishLaunchOriginal(void* a_original)
		{
			s_launchOriginal.store(reinterpret_cast<LaunchDispatchFn>(a_original), std::memory_order_release);
		}

		void PublishFireOriginal(void* a_original)
		{
			s_fireOriginal.store(reinterpret_cast<FireFn>(a_original), std::memory_order_release);
		}

		void FireDetour(const void* a_weapon, RE::TESObjectREFR* a_shooter, std::uint32_t a_equipIndex, void* a_ammo, void* a_extra)
		{
			const auto original = s_fireOriginal.load(std::memory_order_acquire);
			if (!s_fireReady.load(std::memory_order_acquire)) {
				original(a_weapon, a_shooter, a_equipIndex, a_ammo, a_extra);
				return;
			}
			const auto generation = a_shooter == RE::PlayerCharacter::GetSingleton() ? F4T::ReplicationRuntime::CurrentGeneration() : 0;
			F4T::CombatTicketStore::ShotScope scope(generation);
			const auto capturedAt = std::chrono::steady_clock::now();
			bool tracked = false;
			try {
				tracked = F4T::RangedCombatCapture::BeginRangedShot(generation, scope.Shot());
			} catch (...) {
			}
			original(a_weapon, a_shooter, a_equipIndex, a_ammo, a_extra);
			if (tracked && scope.HasLaunch()) {
				try {
					F4T::RangedCombatCapture::QueueRangedShot(generation, scope.Shot(), capturedAt);
				} catch (...) {
				}
			}
		}

		// May run off the main thread and re-entrantly; stack-only state.
		// source:RE-Work/subsystems/commonwealth-online-re/co245-ranged/README.md (section 11 item 1)
		std::uint32_t* LaunchDispatchDetour(std::uint32_t* a_handleOut, RE::ProjectileLaunchData* a_launch) noexcept
		{
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			const bool ready = s_launchReady.load(std::memory_order_acquire);
			auto* const fireScope = ready ? F4T::CombatTicketStore::ShotScope::Current() : nullptr;
			const auto original = s_launchOriginal.load(std::memory_order_acquire);
			const auto launchedAt = std::chrono::steady_clock::now();
			auto* const returned = original(a_handleOut, a_launch);
			if (!ready) return returned;
			F4T::HookThread::Note(F4T::HookThread::Id::LaunchDispatch);
			if (!a_handleOut || !a_launch) {
				return returned;
			}
			const auto handle = *a_handleOut;
			auto* localPlayer = RE::PlayerCharacter::GetSingleton();
			if (!a_launch->shooter || !localPlayer || a_launch->shooter != localPlayer || handle == 0) {
				return returned;
			}
			try {
				if (fireScope && caller == s_fireLaunchReturn.load(std::memory_order_acquire)) {
					fireScope->MarkLaunched();
					F4T::RangedCombatCapture::NoteShotLaunch(fireScope->Generation(), fireScope->Shot(), handle, launchedAt);
				}
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
		if (version != F4T::RuntimeSelect::VerifiedVersion()) {
			LogLine(std::format(
				"launch hook not installed: runtime {} is not verified (expected {}); strict ranged rule applies",
				version.string(),
				F4T::RuntimeSelect::VerifiedVersion().string()));
			return;
		}
		const auto text = module.GetSection(".text");
		F4T::InlineHook::Request request{};
		request.target = kLaunchDispatch.address();
		request.codeSection = { text.GetAddress(), text.GetAddress() + text.GetSize() };
		request.expectedPrologue = F4T::RuntimeSelect::IsOG() ? std::span<const std::uint8_t>{ kExpectedPrologueOg } : std::span<const std::uint8_t>{ kExpectedPrologueNgAe };
		request.destination = REX::UNRESTRICTED_CAST<void*>(&LaunchDispatchDetour);
		request.publishOriginal = PublishLaunchOriginal;
		request.requiredRelocatedBytes = F4T::RuntimeSelect::IsOG() ? 7 : 5;
		request.retainAfterEnableFailure = true;
		F4T::InlineHook::Install(request, LogLine, s_launchHook);
		s_launchReady.store(s_launchHook.verdict == F4T::InlineHookGuard::InstallVerdict::kOk, std::memory_order_release);
		if (!s_launchReady.load(std::memory_order_acquire)) return;
		const auto fire = RE::ID::TESObjectWEAP::Fire.address();
		const bool isOg = F4T::RuntimeSelect::IsOG();
		const bool isNg = REX::FModule::GetRuntimeIndex() == REX::FModule::Runtime::kNG;
		const auto call = fire + (isOg ? 0x4EEU : 0x6E6U);
		const auto expectedCall = isOg ? std::span<const std::uint8_t>{ kFireLaunchOg } :
			(isNg ? std::span<const std::uint8_t>{ kFireLaunchNg } : std::span<const std::uint8_t>{ kFireLaunchAe });
		if (call < text.GetAddress() || call + expectedCall.size() > text.GetAddress() + text.GetSize() ||
			!std::equal(expectedCall.begin(), expectedCall.end(), reinterpret_cast<const std::uint8_t*>(call))) {
			LogLine("fire hook not installed: launch call did not match");
			return;
		}
		s_fireLaunchReturn.store(call + expectedCall.size(), std::memory_order_release);
		F4T::InlineHook::Request fireRequest{};
		fireRequest.target = fire;
		fireRequest.codeSection = { text.GetAddress(), text.GetAddress() + text.GetSize() };
		fireRequest.expectedPrologue = isOg ? std::span<const std::uint8_t>{ kFirePrologueOg } : std::span<const std::uint8_t>{ kFirePrologueNgAe };
		fireRequest.destination = REX::UNRESTRICTED_CAST<void*>(&FireDetour);
		fireRequest.publishOriginal = PublishFireOriginal;
		fireRequest.requiredRelocatedBytes = 5;
		fireRequest.retainAfterEnableFailure = true;
		F4T::InlineHook::Install(fireRequest, LogLine, s_fireHook);
		s_fireReady.store(s_fireHook.verdict == F4T::InlineHookGuard::InstallVerdict::kOk, std::memory_order_release);
	}
}

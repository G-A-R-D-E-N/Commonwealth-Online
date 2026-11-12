#include "pch.h"
// address source:commonlibf4-nomad

#include "F4TSleepBedHook.h"
#include "F4THookThreadTable.h"

#include "F4TDebugLogging.h"
#include "F4TFailClosedNotice.h"
#include "F4TInlineHook.h"
#include "F4TNetworking.h"
#include "F4TReconnect.h"
#include "F4TRuntimeSelect.h"
#include "F4TSleepController.h"

#include "RE/B/BSPointerHandle.h"
#include "RE/M/MenuOpenCloseEvent.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/S/SendHUDMessage.h"
#include "RE/U/UI.h"
#include "RE/U/UI_MESSAGE_TYPE.h"
#include "RE/U/UIMessageQueue.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <string_view>

namespace F4T::SleepBedHook
{
	namespace
	{
		bool s_installAttempted = false;

		void LogLine(std::string_view a_message)
		{
			F4T::DebugLogging::Write("SleepBed", a_message);
		}

		inline constexpr REL::ID kSleepBedMenu{ 846428, 2249519, 2249519 } /* E:/RE-work/co-sleep/sleep-bed-menu-helper.md */;
		inline constexpr REL::ID kCanPassTime{ 763175, 2232915, 2232915 } /* E:/RE-work/co-sleep/sleep-bed-menu-helper.md */;
		inline constexpr REL::ID kNativeSleep{ 781390, 2232887, 2232887 } /* E:/RE-work/co-sleep/sleep-native-functions.md */;
		inline constexpr REL::ID kNativeWait{ 328629, 2232888, 2232888 } /* E:/RE-work/co-sleep/native-wait-ids.md */;
		inline constexpr REL::ID kNativePassTime{ 1005712, 2232891, 2232891 } /* E:/RE-work/co-sleep/native-passtime-ids.md */;
		inline constexpr REL::ID kServePrisonTime{ 1117393, 2233140, 2233140 } /* E:/RE-work/co-sleep/native-serveprison-ids.md */;
		constexpr std::uintptr_t kServePrisonHourOffset = 0xF7;
		constexpr std::string_view kMenuSleepWait = "SleepWaitMenu";
		constexpr std::string_view kSleepBlockedNotice = "Sleep is unavailable right now.";
		constexpr std::string_view kWaitBlockedNotice = "Waiting is disabled in multiplayer. Use a bed to sleep.";
		constexpr std::string_view kBlockIncompleteNotice = "Commonwealth Online could not fully block vanilla sleep and wait: vanilla time may advance in multiplayer.";
		constexpr std::array<std::uint8_t, 26> kExpectedPrologue{
			0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10,
			0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20,
			0x41, 0x56, 0x48, 0x83, 0xEC, 0x20
		};
		constexpr std::array<std::uint8_t, 15> kNativeSleepPrologue{
			0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x20
		};
		constexpr std::array<std::uint8_t, 20> kNativeWaitPrologueOG{
			0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x8B, 0x0D, 0x5C, 0x74, 0xA3, 0x04
		};
		constexpr std::array<std::uint8_t, 15> kNativeWaitPrologueNGAE{
			0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x8B, 0xDA
		};
		constexpr std::array<std::uint8_t, 20> kNativePassTimePrologue{
			0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20
		};
		constexpr std::array<std::uint8_t, 15> kServePrisonHourPrologueNG{
			0x66, 0x0F, 0x6E, 0xCB, 0x0F, 0x5B, 0xC9, 0xF3, 0x0F, 0x59, 0x0D, 0x76, 0x4C, 0x5A, 0x01
		};
		constexpr std::array<std::uint8_t, 15> kServePrisonHourPrologueAE{
			0x66, 0x0F, 0x6E, 0xCB, 0x0F, 0x5B, 0xC9, 0xF3, 0x0F, 0x59, 0x0D, 0x6E, 0x26, 0x71, 0x01
		};

		using SleepBedMenuFn = void (*)(const std::uint32_t*, std::uint8_t);
		using NativeTimeFn = void (*)(RE::PlayerCharacter*, std::int32_t);
		using PassTimeFn = void (*)(RE::PlayerCharacter*, std::int32_t, bool);

		F4T::InlineHook::Result s_sleepHook{};
		F4T::InlineHook::Result s_nativeSleepHook{};
		F4T::InlineHook::Result s_nativeWaitHook{};
		F4T::InlineHook::Result s_passTimeHook{};
		F4T::InlineHook::MidResult s_servePrisonHourHook{};

		bool InMultiplayer()
		{
			return F4T::Networking::IsConnectedToServer() || F4T::Reconnect::IsActive();
		}

		std::atomic<std::uint32_t> g_gameThread{};
		bool g_sinkRegistered = false;
		std::atomic<bool> g_guardsInstalled{ false };
		std::atomic<bool> g_blockIncomplete{ false };
		bool g_incompleteNoticeShown = false;

		bool OnGameThread()
		{
			return g_gameThread.load(std::memory_order_acquire) == REX::W32::GetCurrentThreadId();
		}

		void ShowNotice(std::string_view a_notice)
		{
			if (OnGameThread()) {
				RE::SendHUDMessage::ShowHUDMessage(a_notice.data(), nullptr, true, true);
			} else {
				LogLine("notice skipped: not on the game thread");
			}
		}

		bool VanillaGatesPass()
		{
			auto* const ui = RE::UI::GetSingleton();
			auto* const player = RE::PlayerCharacter::GetSingleton();
			if (!ui || !player || ui->GetMenuOpen(kMenuSleepWait.data()) || ui->GetMenuOpen("WorkshopMenu")) {
				return false;
			}
			REL::Relocation<bool(RE::PlayerCharacter*, void*, bool, bool)> canPassTime{ kCanPassTime };
			return canPassTime(player, nullptr, true, false);
		}

		void SleepBedMenuDetour(const std::uint32_t* a_bedHandle, std::uint8_t a_mode) noexcept
		{
			F4T::HookThread::Note(F4T::HookThread::Id::SleepBed);
			if (!s_sleepHook.hook) {
				return;
			}
			if (!InMultiplayer()) {
				s_sleepHook.hook->original<SleepBedMenuFn>()(a_bedHandle, a_mode);
				return;
			}
			try {
				if (a_mode == 1 && a_bedHandle) {
					RE::ObjectRefHandle handle;
					std::memcpy(&handle, a_bedHandle, sizeof(handle));
					const auto bedRef = handle.get();
					if (bedRef) {
						if (!VanillaGatesPass()) {
							LogLine("sleep not started: vanilla gates refused the bed");
							return;
						}
						if (F4T::SleepController::TryBegin(bedRef.get())) {
							return;
						}
					}
				}
			} catch (...) {
			}
			try {
				LogLine(std::format("vanilla sleep/wait blocked in multiplayer mode={}", a_mode));
				ShowNotice(a_mode == 1 ? kSleepBlockedNotice : kWaitBlockedNotice);
			} catch (...) {
			}
		}

		bool BlockedInMultiplayer(std::string_view a_name, std::string_view a_notice, std::int32_t a_hours)
		{
			if (!InMultiplayer()) {
				return false;
			}
			try {
				LogLine(std::format("vanilla {} blocked in multiplayer hours={}", a_name, a_hours));
				if (!a_notice.empty()) {
					ShowNotice(a_notice);
				}
			} catch (...) {
			}
			return true;
		}

		void NativeSleepDetour(RE::PlayerCharacter* a_player, std::int32_t a_hours) noexcept
		{
			F4T::HookThread::Note(F4T::HookThread::Id::SleepNative);
			if (!s_nativeSleepHook.hook) {
				return;
			}
			if (!BlockedInMultiplayer("native sleep", kSleepBlockedNotice, a_hours)) {
				s_nativeSleepHook.hook->original<NativeTimeFn>()(a_player, a_hours);
			}
		}

		void NativeWaitDetour(RE::PlayerCharacter* a_player, std::int32_t a_hours) noexcept
		{
			F4T::HookThread::Note(F4T::HookThread::Id::WaitNative);
			if (!s_nativeWaitHook.hook) {
				return;
			}
			if (!BlockedInMultiplayer("native wait", kWaitBlockedNotice, a_hours)) {
				s_nativeWaitHook.hook->original<NativeTimeFn>()(a_player, a_hours);
			}
		}

		void NativePassTimeDetour(RE::PlayerCharacter* a_player, std::int32_t a_hours, bool a_flag) noexcept
		{
			F4T::HookThread::Note(F4T::HookThread::Id::PassTimeNative);
			if (!s_passTimeHook.hook) {
				return;
			}
			if (!BlockedInMultiplayer("pass time", "", a_hours)) {
				s_passTimeHook.hook->original<PassTimeFn>()(a_player, a_hours, a_flag);
			}
		}

		void ServePrisonHourDetour(safetyhook::Context& a_context) noexcept
		{
			F4T::HookThread::Note(F4T::HookThread::Id::ServePrisonNative);
			if (InMultiplayer()) {
				a_context.rbx = 0;
			}
		}

		class SleepWaitMenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::MenuOpenCloseEvent& a_event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				const auto* menuName = a_event.menuName.c_str();
				if (!a_event.opening || !menuName || std::string_view{ menuName } != kMenuSleepWait || !InMultiplayer()) {
					return RE::BSEventNotifyControl::kContinue;
				}
				try {
					LogLine("vanilla sleep wait menu opened in multiplayer; hiding it");
					if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
						queue->AddMessage(RE::BSFixedString{ kMenuSleepWait.data() }, RE::UI_MESSAGE_TYPE::kHide);
					}
					ShowNotice(kSleepBlockedNotice);
				} catch (...) {
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		SleepWaitMenuSink g_sleepWaitMenuSink;

		void EnsureSinkRegistered()
		{
			if (g_sinkRegistered || !g_guardsInstalled.load(std::memory_order_acquire)) {
				return;
			}
			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return;
			}
			ui->RegisterSink(&g_sleepWaitMenuSink);
			g_sinkRegistered = true;
		}
	}

	void Tick()
	{
		std::uint32_t expected = 0;
		g_gameThread.compare_exchange_strong(expected, REX::W32::GetCurrentThreadId(), std::memory_order_acq_rel);
		EnsureSinkRegistered();
		if (g_blockIncomplete.load(std::memory_order_acquire) && !g_incompleteNoticeShown && InMultiplayer()) {
			g_incompleteNoticeShown = true;
			F4T::FailClosedNotice::Show(kBlockIncompleteNotice);
		}
	}

	void CallNativeSleep(RE::PlayerCharacter* a_player, std::int32_t a_hours)
	{
		if (s_nativeSleepHook.hook) {
			s_nativeSleepHook.hook->original<NativeTimeFn>()(a_player, a_hours);
			return;
		}
		REL::Relocation<void(RE::PlayerCharacter*, std::int32_t)> func{ kNativeSleep };
		func(a_player, a_hours);
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
				"sleep bed hook not installed: runtime {} is not verified (expected {}); vanilla sleep and wait are not blocked in multiplayer",
				version.string(),
				F4T::RuntimeSelect::VerifiedVersion().string()));
			g_blockIncomplete.store(true, std::memory_order_release);
			return;
		}
		const auto text = module.GetSection(".text");
		F4T::InlineHook::Request request{};
		request.target = kSleepBedMenu.address();
		request.codeSection = { text.GetAddress(), text.GetAddress() + text.GetSize() };
		request.expectedPrologue = std::span<const std::uint8_t>{ kExpectedPrologue };
		request.destination = REX::UNRESTRICTED_CAST<void*>(&SleepBedMenuDetour);
		std::uint32_t missing = 0;
		F4T::InlineHook::Install(request, LogLine, s_sleepHook);
		if (!s_sleepHook.hook) {
			++missing;
			LogLine("sleep bed hook failed: bed sleep is not gated in multiplayer");
		}
		request.target = kNativeSleep.address();
		request.expectedPrologue = std::span<const std::uint8_t>{ kNativeSleepPrologue };
		request.destination = REX::UNRESTRICTED_CAST<void*>(&NativeSleepDetour);
		F4T::InlineHook::Install(request, LogLine, s_nativeSleepHook);
		if (!s_nativeSleepHook.hook) {
			++missing;
			LogLine("native sleep hook failed: vanilla sleep is not blocked in multiplayer");
		}
		request.target = kNativeWait.address();
		request.expectedPrologue = F4T::RuntimeSelect::IsOG() ? std::span<const std::uint8_t>{ kNativeWaitPrologueOG } : std::span<const std::uint8_t>{ kNativeWaitPrologueNGAE };
		request.destination = REX::UNRESTRICTED_CAST<void*>(&NativeWaitDetour);
		F4T::InlineHook::Install(request, LogLine, s_nativeWaitHook);
		if (!s_nativeWaitHook.hook) {
			++missing;
			LogLine("native wait hook failed: vanilla wait is not blocked in multiplayer");
		}
		request.target = kNativePassTime.address();
		request.expectedPrologue = std::span<const std::uint8_t>{ kNativePassTimePrologue };
		request.destination = REX::UNRESTRICTED_CAST<void*>(&NativePassTimeDetour);
		F4T::InlineHook::Install(request, LogLine, s_passTimeHook);
		if (!s_passTimeHook.hook) {
			++missing;
			LogLine("native pass time hook failed: vanilla pass time is not blocked in multiplayer");
		}
		if (!F4T::RuntimeSelect::IsOG()) {
			request.target = kServePrisonTime.address() + kServePrisonHourOffset;
			request.expectedPrologue = REX::FModule::GetRuntimeIndex() == REX::FModule::Runtime::kNG
				? std::span<const std::uint8_t>{ kServePrisonHourPrologueNG }
				: std::span<const std::uint8_t>{ kServePrisonHourPrologueAE };
			F4T::InlineHook::InstallMid(request, &ServePrisonHourDetour, LogLine, s_servePrisonHourHook);
			if (!s_servePrisonHourHook.hook) {
				++missing;
				LogLine("serve prison hour hook failed: jail release advances vanilla time in multiplayer");
			}
		}
		g_guardsInstalled.store(true, std::memory_order_release);
		if (missing != 0) {
			g_blockIncomplete.store(true, std::memory_order_release);
			LogLine(std::format("error: {} vanilla sleep/wait/pass-time hook(s) not installed; vanilla time may advance in multiplayer", missing));
		}
	}
}

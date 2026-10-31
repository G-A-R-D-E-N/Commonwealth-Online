#include "pch.h"

#include "F4TConsoleGuard.h"
#include "F4TConsoleGuardPolicy.h"
#include "F4THookThreadTable.h"

#include "F4TClientCompatibility.h"
#include "F4TInlineHook.h"
#include "F4TNetworking.h"

#include "RE/C/Console.h"
#include "RE/M/MenuOpenCloseEvent.h"
#include "RE/S/Script.h"
#include "RE/S/SendHUDMessage.h"
#include "RE/U/UI.h"
#include "RE/U/UI_MESSAGE_TYPE.h"
#include "RE/U/UIMessageQueue.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <format>
#include <intrin.h>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include <safetyhook.hpp>

namespace F4T::ConsoleGuard
{
	namespace
	{
		using ExecuteCommandFn = void (*)(const char*);
		using CompileAndRunFn = void (*)(void*, void*, std::int32_t, void*);

		constexpr std::array<std::uint8_t, 14> kExecuteCommandPrologueOg{
			0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x48, 0x81, 0xEC, 0x40, 0x08, 0x00, 0x00
		};
		constexpr std::array<std::uint8_t, 24> kExecuteCommandPrologueNgAe{
			0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x78, 0xF8, 0xFF,
			0xFF, 0x48, 0x81, 0xEC, 0x88, 0x08, 0x00, 0x00
		};

		struct VerifiedRuntime
		{
			std::uintptr_t executeCommandRva{};
			std::uintptr_t compileAndRunRva{};
			std::span<const std::uint8_t> executePrologue{};
		};

		std::optional<VerifiedRuntime> ResolveVerifiedRuntime()
		{
			const auto version = REX::FModule::GetExecutingModule().GetFileVersion();
			if (version.build() != 0) {
				return std::nullopt;
			}
			const auto target = F4T::ClientCompatibility::TargetFromRuntime(
				std::format("{}.{}.{}", version.major(), version.minor(), version.patch()));
			if (!target) {
				return std::nullopt;
			}
			using Target = F4T::ClientCompatibility::BuildTarget;
			switch (*target) {
			case Target::OG:
				return VerifiedRuntime{ 0x125B4A0, 0x4E29B0, kExecuteCommandPrologueOg };
			case Target::NG:
				return VerifiedRuntime{ 0xFB0250, 0x55E3E0, kExecuteCommandPrologueNgAe };
			case Target::AE:
				return VerifiedRuntime{ 0x1036250, 0x5B2690, kExecuteCommandPrologueNgAe };
			default:
				return std::nullopt;
			}
		}

		std::atomic<bool> g_consoleAllowed{ false };
		std::atomic<bool> g_welcomeReceived{ false };
		std::atomic<std::uint32_t> g_pendingAllowedCommands{};
		std::atomic<bool> g_blockLogged{ false };
		std::atomic<bool> g_offThreadLogged{ false };
		std::atomic<bool> g_executeTrampolineWarned{ false };
		std::atomic<bool> g_compileTrampolineWarned{ false };
		std::atomic<bool> g_unverifiedRuntimeWarned{ false };
		std::atomic<bool> g_threadMismatchLogged{ false };
		std::atomic<bool> g_textRangeMissingLogged{ false };
		std::atomic<std::uint32_t> g_gameThread{};
		std::atomic<bool> g_sinkRegistered{ false };
		std::atomic<bool> g_executeHookInstalled{ false };
		std::atomic<bool> g_compileHookInstalled{ false };
		std::atomic<std::uint32_t> g_installAttempts{};
		std::atomic<std::uint64_t> g_tickCount{};
		std::atomic<std::uint64_t> g_nextInstallTick{};
		std::atomic<std::uintptr_t> g_ourTextBegin{};
		std::atomic<std::uintptr_t> g_ourTextSize{};
		std::atomic<std::uintptr_t> g_gameTextBegin{};
		std::atomic<std::uintptr_t> g_gameTextSize{};
		std::atomic<ExecuteCommandFn> g_originalExecuteCommand{ nullptr };
		std::atomic<CompileAndRunFn> g_originalCompileAndRun{ nullptr };
		safetyhook::InlineHook g_executeCommandHook{};
		safetyhook::InlineHook g_compileAndRunHook{};
		thread_local std::uint32_t t_internalCallDepth{};

		struct InternalCallScope
		{
			InternalCallScope() noexcept { ++t_internalCallDepth; }
			~InternalCallScope() noexcept { --t_internalCallDepth; }
			InternalCallScope(const InternalCallScope&) = delete;
			InternalCallScope& operator=(const InternalCallScope&) = delete;
		};

		void WarnOnce(std::atomic<bool>& a_flag, std::string_view a_message)
		{
			if (ClaimLogOnce(a_flag)) {
				REX::WARN("ConsoleGuard: {}", a_message);
			}
		}

		bool OnGameThread()
		{
			return g_gameThread.load(std::memory_order_acquire) == REX::W32::GetCurrentThreadId();
		}

		CommandSource ClassifyCaller(std::uintptr_t a_returnAddress)
		{
			return ClassifyCommandSource(
				t_internalCallDepth > 0,
				InsideRange(
					a_returnAddress,
					g_ourTextBegin.load(std::memory_order_acquire),
					g_ourTextSize.load(std::memory_order_acquire)),
				InsideRange(
					a_returnAddress,
					g_gameTextBegin.load(std::memory_order_acquire),
					g_gameTextSize.load(std::memory_order_acquire)));
		}

		bool IsBlocked(CommandSource a_source)
		{
			if (!g_welcomeReceived.load(std::memory_order_acquire)) {
				return false;
			}
			return DecideConsoleUse(
					   F4T::Networking::IsConnectedToServer(),
					   g_consoleAllowed.load(std::memory_order_acquire),
					   a_source) == Decision::kBlock;
		}

		void AnnounceBlocked()
		{
			if (OnGameThread()) {
				RE::SendHUDMessage::ShowHUDMessage(kConsoleBlockedNotice.data(), nullptr, true, true);
			} else {
				WarnOnce(g_offThreadLogged, "console block notice skipped");
			}
			if (!g_blockLogged.exchange(true, std::memory_order_acq_rel)) {
				REX::WARN("ConsoleGuard: console disabled for this player in a multiplayer session");
			}
		}

		void AnnounceLockFailure()
		{
			if (OnGameThread()) {
				RE::SendHUDMessage::ShowHUDMessage(kConsoleLockNotice.data(), nullptr, true, true);
			}
		}

		void HideConsoleMenu()
		{
			auto* queue = RE::UIMessageQueue::GetSingleton();
			if (!queue) {
				return;
			}
			queue->AddMessage(RE::BSFixedString{ RE::Console::MENU_NAME }, RE::UI_MESSAGE_TYPE::kHide);
		}

		void ExecuteCommandDetour(const char* a_command)
		{
			F4T::HookThread::Note(F4T::HookThread::Id::ExecuteCommand);
			const auto original = g_originalExecuteCommand.load(std::memory_order_acquire);
			if (!original) {
				WarnOnce(
					g_executeTrampolineWarned,
					std::format("trampoline not published at {:#x}", RE::ID::Console::ExecuteCommand.address()));
				return;
			}
			const auto returnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			const auto source = ClassifyCaller(returnAddress);
			if (IsBlocked(source)) {
				AnnounceBlocked();
				return;
			}
			if ((source == CommandSource::kInternal || source == CommandSource::kPluginCode) &&
				IsBlocked(CommandSource::kGameCode)) {
				TryAddToken(g_pendingAllowedCommands);
			}
			const InternalCallScope scope;
			original(a_command);
		}

		void CompileAndRunDetour(void* a_script, void* a_compiler, std::int32_t a_compilerIndex, void* a_ownerObject)
		{
			F4T::HookThread::Note(F4T::HookThread::Id::CompileAndRun);
			const auto original = g_originalCompileAndRun.load(std::memory_order_acquire);
			if (!original) {
				WarnOnce(
					g_compileTrampolineWarned,
					std::format("trampoline not published at {:#x}", RE::ID::Script::CompileAndRun.address()));
				return;
			}
			const auto returnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			if (IsBlocked(ClassifyCaller(returnAddress)) && !TryTakeToken(g_pendingAllowedCommands)) {
				AnnounceBlocked();
				return;
			}
			original(a_script, a_compiler, a_compilerIndex, a_ownerObject);
		}

		class MenuOpenCloseSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::MenuOpenCloseEvent& a_event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				const auto* menuName = a_event.menuName.c_str();
				if (!menuName || std::string_view{ menuName } != kConsoleMenuName) {
					return RE::BSEventNotifyControl::kContinue;
				}
				if (a_event.opening && IsBlocked(CommandSource::kGameCode)) {
					HideConsoleMenu();
					AnnounceBlocked();
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		MenuOpenCloseSink g_sink;

		void EnsureSinkRegistered()
		{
			if (g_sinkRegistered.load(std::memory_order_acquire)) {
				return;
			}
			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return;
			}
			ui->RegisterSink(&g_sink);
			g_sinkRegistered.store(true, std::memory_order_release);
		}

		bool ResolveTextRange(REX::FModule a_module, std::atomic<std::uintptr_t>& a_begin, std::atomic<std::uintptr_t>& a_size)
		{
			if (a_size.load(std::memory_order_acquire) != 0) {
				return true;
			}
			const auto text = a_module.GetSection(".text");
			if (text.GetSize() == 0) {
				return false;
			}
			a_begin.store(text.GetAddress(), std::memory_order_release);
			a_size.store(text.GetSize(), std::memory_order_release);
			return true;
		}

		void ResolveTextRanges()
		{
			const bool ours = ResolveTextRange(REX::FModule::GetCurrentModule(), g_ourTextBegin, g_ourTextSize);
			const bool game = ResolveTextRange(REX::FModule::GetExecutingModule(), g_gameTextBegin, g_gameTextSize);
			if (!ours || !game) {
				WarnOnce(g_textRangeMissingLogged, "a .text section is unavailable");
			}
		}

		template <class Fn>
		bool InstallHook(
			std::uintptr_t a_target,
			void* a_detour,
			std::atomic<Fn>& a_original,
			safetyhook::InlineHook& a_hook,
			const char* a_label)
		{
			const auto gameTextBegin = g_gameTextBegin.load(std::memory_order_acquire);
			const F4T::VtableGuard::AddressRange codeSection{
				gameTextBegin,
				gameTextBegin + g_gameTextSize.load(std::memory_order_acquire)
			};
			const auto verdict = F4T::InlineHookGuard::CheckTarget(a_target, F4T::InlineHookGuard::kMinPrologueBytes, codeSection);
			if (verdict != F4T::InlineHookGuard::InstallVerdict::kOk) {
				REX::WARN(
					"ConsoleGuard: {} hook not installed at {:#x}: {}",
					a_label,
					a_target,
					F4T::InlineHookGuard::Describe(verdict));
				return false;
			}
			try {
				auto created = safetyhook::InlineHook::create(
					reinterpret_cast<void*>(a_target),
					a_detour,
					safetyhook::InlineHook::StartDisabled);
				if (!created) {
					REX::WARN("ConsoleGuard: {} hook create failed at {:#x}", a_label, a_target);
					return false;
				}
				a_original.store(created->original<Fn>(), std::memory_order_release);
				a_hook = std::move(*created);
				if (const auto enabled = a_hook.enable(); !enabled) {
					REX::WARN("ConsoleGuard: {} hook enable failed at {:#x}", a_label, a_target);
					a_hook.reset();
					a_original.store(nullptr, std::memory_order_release);
					return false;
				}
				REX::WARN("ConsoleGuard: {} hook installed at {:#x}", a_label, a_target);
				return true;
			} catch (...) {
				REX::WARN("ConsoleGuard: {} hook threw while installing at {:#x}", a_label, a_target);
				return false;
			}
		}

		bool AtVerifiedRva(std::uintptr_t a_address, std::uintptr_t a_rva)
		{
			return a_address - REX::FModule::GetExecutingModule().GetBaseAddress() == a_rva;
		}

		void InstallExecuteCommandHook(const VerifiedRuntime& a_runtime)
		{
			const auto target = RE::ID::Console::ExecuteCommand.address();
			if (!AtVerifiedRva(target, a_runtime.executeCommandRva)) {
				REX::WARN("ConsoleGuard: console command hook not installed: {:#x} is not the verified address", target);
				return;
			}
			const auto text = REX::FModule::GetExecutingModule().GetSection(".text");
			F4T::InlineHook::Request request{};
			request.target = target;
			request.codeSection = { text.GetAddress(), text.GetAddress() + text.GetSize() };
			request.expectedPrologue = a_runtime.executePrologue;
			request.destination = REX::UNRESTRICTED_CAST<void*>(&ExecuteCommandDetour);
			request.publishOriginal = [](void* a_original) {
				g_originalExecuteCommand.store(REX::UNRESTRICTED_CAST<ExecuteCommandFn>(a_original), std::memory_order_release);
			};
			F4T::InlineHook::Result result{};
			F4T::InlineHook::Install(request, nullptr, result);
			if (result.verdict != F4T::InlineHookGuard::InstallVerdict::kOk || !result.hook) {
				REX::WARN(
					"ConsoleGuard: console command hook not installed at {:#x}: {}",
					target,
					F4T::InlineHookGuard::Describe(result.verdict));
				return;
			}
			g_executeCommandHook = std::move(*result.hook);
			g_executeHookInstalled.store(true, std::memory_order_release);
			REX::WARN("ConsoleGuard: console command hook installed at {:#x}", target);
		}

		void InstallCompileAndRunHook(const VerifiedRuntime& a_runtime)
		{
			const auto target = RE::ID::Script::CompileAndRun.address();
			if (!AtVerifiedRva(target, a_runtime.compileAndRunRva)) {
				REX::WARN("ConsoleGuard: script compile hook not installed: {:#x} is not the verified address", target);
				return;
			}
			if (InstallHook(
					target,
					REX::UNRESTRICTED_CAST<void*>(&CompileAndRunDetour),
					g_originalCompileAndRun,
					g_compileAndRunHook,
					"script compile")) {
				g_compileHookInstalled.store(true, std::memory_order_release);
			}
		}

		bool HooksInstalled()
		{
			return g_executeHookInstalled.load(std::memory_order_acquire) &&
			       g_compileHookInstalled.load(std::memory_order_acquire);
		}

		void TryInstallHooks()
		{
			if (HooksInstalled()) {
				return;
			}
			const auto attempts = g_installAttempts.load(std::memory_order_acquire);
			if (attempts >= kMaxHookInstallAttempts) {
				return;
			}
			const auto tick = g_tickCount.load(std::memory_order_acquire);
			if (tick < g_nextInstallTick.load(std::memory_order_acquire)) {
				return;
			}
			g_installAttempts.store(attempts + 1, std::memory_order_release);
			g_nextInstallTick.store(tick + HookRetryDelayTicks(attempts), std::memory_order_release);
			const auto runtime = ResolveVerifiedRuntime();
			if (!runtime) {
				WarnOnce(g_unverifiedRuntimeWarned, "runtime is not a verified build; console hooks not installed");
			} else {
				if (!g_executeHookInstalled.load(std::memory_order_acquire)) {
					InstallExecuteCommandHook(*runtime);
				}
				if (!g_compileHookInstalled.load(std::memory_order_acquire)) {
					InstallCompileAndRunHook(*runtime);
				}
			}
			if (HooksInstalled()) {
				return;
			}
			if (attempts == 0) {
				AnnounceLockFailure();
			}
			if (attempts + 1 == kMaxHookInstallAttempts) {
				REX::WARN("ConsoleGuard: hook install attempts exhausted");
			}
		}
	}

	void NoteWelcome(const nlohmann::json& a_packet)
	{
		g_pendingAllowedCommands.store(0, std::memory_order_release);
		g_consoleAllowed.store(ReadConsoleAllowed(a_packet), std::memory_order_release);
		g_welcomeReceived.store(true, std::memory_order_release);
	}

	void Reset()
	{
		g_consoleAllowed.store(false, std::memory_order_release);
		g_welcomeReceived.store(false, std::memory_order_release);
		g_blockLogged.store(false, std::memory_order_release);
		g_offThreadLogged.store(false, std::memory_order_release);
		g_executeTrampolineWarned.store(false, std::memory_order_release);
		g_compileTrampolineWarned.store(false, std::memory_order_release);
		g_unverifiedRuntimeWarned.store(false, std::memory_order_release);
		g_pendingAllowedCommands.store(0, std::memory_order_release);
	}

	void ExecutePluginCommand(const char* a_command)
	{
		if (a_command == nullptr) {
			return;
		}
		const InternalCallScope scope;
		RE::Console::ExecuteCommand(a_command);
	}

	void Tick()
	{
		g_tickCount.fetch_add(1, std::memory_order_acq_rel);
		const auto threadId = REX::W32::GetCurrentThreadId();
		std::uint32_t expected = 0;
		if (!g_gameThread.compare_exchange_strong(expected, threadId, std::memory_order_acq_rel) && expected != threadId) {
			if (!g_threadMismatchLogged.exchange(true, std::memory_order_acq_rel)) {
				REX::WARN("ConsoleGuard: Tick thread {} is not the game thread {}", threadId, expected);
			}
		}
		if (!F4T::Networking::IsConnectedToServer()) {
			Reset();
			return;
		}
		EnsureSinkRegistered();
		ResolveTextRanges();
		TryInstallHooks();

		const auto* ui = RE::UI::GetSingleton();
		const bool consoleOpen = ui && ui->GetMenuOpen(RE::BSFixedString{ RE::Console::MENU_NAME });
		if (!consoleOpen || !IsBlocked(CommandSource::kGameCode)) {
			return;
		}
		HideConsoleMenu();
		AnnounceBlocked();
	}
}

#include "F4TInlineHook.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <string>
#include <string_view>
#include <thread>
#include <windows.h>

namespace
{
	using F4T::InlineHookGuard::InstallVerdict;
	using F4T::InlineHook::Request;
	using F4T::VtableGuard::AddressRange;

	std::string g_lastLog;
	F4T::InlineHook::Result g_installed{};
	void* g_published{ nullptr };
	int g_publishCount{ 0 };
	bool g_publishedBeforeEnable{ false };
	std::uintptr_t g_publishTarget{ 0 };
	std::array<std::uint8_t, 16> g_publishPrologue{};
	std::atomic<void*> g_leafOriginal{};
	std::atomic<bool> g_leafReady{};
	std::atomic<void*> g_branchOriginal{};
	std::atomic<void*> g_nearBranchOriginal{};
	std::atomic<void*> g_fireOriginal{};
	std::atomic<void*> g_flameOriginal{};
	std::atomic<std::uintptr_t> g_beamObject{};
	std::atomic<std::uintptr_t> g_beamTrampoline{};
	std::atomic<unsigned> g_beamCalls{};
	std::uintptr_t g_beamReturnPc{};
	bool g_captureBeamUnwind{};
	bool g_beamFrameHasMetadata{};
	bool g_beamUnwindMatches{};
	std::atomic<void*> g_unwindOriginal{};
	bool g_unwindMatches{};
	std::uintptr_t g_expectedTask{};
	std::uintptr_t g_expectedTaskReturn{};
	bool g_taskUnwindMatches{};
	int g_taskRegister{};
	std::uintptr_t g_faultAddress{};

	int CaptureIllegalInstruction(EXCEPTION_POINTERS* a_exception)
	{
		if (a_exception->ExceptionRecord->ExceptionCode != EXCEPTION_ILLEGAL_INSTRUCTION) return EXCEPTION_CONTINUE_SEARCH;
		g_faultAddress = reinterpret_cast<std::uintptr_t>(a_exception->ExceptionRecord->ExceptionAddress);
		return EXCEPTION_EXECUTE_HANDLER;
	}

	SAFETYHOOK_NOINLINE bool ExpectIllegalInstruction(int (*a_target)(std::uintptr_t, int))
	{
		__try {
			(void)a_target(17, 23);
		} __except (CaptureIllegalInstruction(GetExceptionInformation())) {
			return true;
		}
		return false;
	}

	void PublishUnwindOriginal(void* a_original)
	{
		g_unwindOriginal.store(a_original, std::memory_order_release);
	}

	SAFETYHOOK_NOINLINE int UnwindDetour(std::uintptr_t a_first, int a_second)
	{
		const auto returnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
		const auto returnStack = reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress()) + sizeof(void*);
		CONTEXT context{};
		RtlCaptureContext(&context);
		DWORD64 imageBase{};
		const auto* function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
		if (function) {
			void* handlerData{};
			DWORD64 establisherFrame{};
			RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, const_cast<PRUNTIME_FUNCTION>(function),
				&context, &handlerData, &establisherFrame, nullptr);
			g_unwindMatches = context.Rip == returnAddress && context.Rsp == returnStack;
			const auto taskRegister = [&]() { return g_taskRegister == 15 ? context.R15 : (g_taskRegister == 13 ? context.R13 : context.R12); };
			if (g_expectedTask != 0 && context.Rip == g_expectedTaskReturn && taskRegister() == g_expectedTask) {
				const auto taskStack = context.Rsp;
				const auto savedRegister = *reinterpret_cast<const std::uintptr_t*>(taskStack + 0x20);
				const auto callerReturn = *reinterpret_cast<const std::uintptr_t*>(taskStack + 0x28);
				const auto* taskFunction = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
				if (taskFunction) {
					RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, const_cast<PRUNTIME_FUNCTION>(taskFunction),
						&context, &handlerData, &establisherFrame, nullptr);
					g_taskUnwindMatches = taskRegister() == savedRegister && context.Rip == callerReturn && context.Rsp == taskStack + 0x30;
				}
			}
		}
		const auto original = reinterpret_cast<int (*)(std::uintptr_t, int)>(g_unwindOriginal.load(std::memory_order_acquire));
		return original(a_first, a_second);
	}

	void PublishLeafOriginal(void* a_original)
	{
		g_leafOriginal.store(a_original, std::memory_order_release);
	}

	int LeafDetour(int a_value)
	{
		const auto original = reinterpret_cast<int (*)(int)>(g_leafOriginal.load(std::memory_order_acquire));
		const auto value = original(a_value);
		return g_leafReady.load(std::memory_order_acquire) ? value + 100 : value;
	}

	void PublishBranchOriginal(void* a_original)
	{
		g_branchOriginal.store(a_original, std::memory_order_release);
	}

	void PublishNearBranchOriginal(void* a_original)
	{
		g_nearBranchOriginal.store(a_original, std::memory_order_release);
	}

	int NearBranchDetour(void* a_first, void* a_second)
	{
		const auto original = reinterpret_cast<int (*)(void*, void*)>(g_nearBranchOriginal.load(std::memory_order_acquire));
		return original(a_first, a_second) + 10;
	}

	using FireProbe = std::uint64_t (*)(std::uint64_t, std::uint64_t, std::uint32_t, std::uint64_t, std::uint64_t);

	void PublishFireOriginal(void* a_original)
	{
		g_fireOriginal.store(a_original, std::memory_order_release);
	}

	std::uint64_t FireProbeDetour(std::uint64_t a_first, std::uint64_t a_second, std::uint32_t a_third,
		std::uint64_t a_fourth, std::uint64_t a_fifth)
	{
		const auto original = reinterpret_cast<FireProbe>(g_fireOriginal.load(std::memory_order_acquire));
		return original(a_first, a_second, a_third, a_fourth, a_fifth) + 100;
	}

	void PublishFlameOriginal(void* a_original)
	{
		g_flameOriginal.store(a_original, std::memory_order_release);
	}

	std::uint64_t FlameProbeDetour(const void* a_object)
	{
		const auto original = reinterpret_cast<std::uint64_t (*)(const void*)>(g_flameOriginal.load(std::memory_order_acquire));
		return original(a_object) + 100;
	}

	void BeamProbe(safetyhook::Context& a_context)
	{
		g_beamObject.store(a_context.rcx, std::memory_order_relaxed);
		g_beamTrampoline.store(a_context.rip, std::memory_order_relaxed);
		g_beamCalls.fetch_add(1, std::memory_order_relaxed);
	}

	SAFETYHOOK_NOINLINE int BeamTargetFirst(const void*)
	{
		if (g_captureBeamUnwind) {
			CONTEXT context{};
			RtlCaptureContext(&context);
			DWORD64 imageBase{};
			auto* entry = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
			if (entry) {
				void* handlerData{};
				DWORD64 establisherFrame{};
				RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, entry,
					&context, &handlerData, &establisherFrame, nullptr);
				g_beamReturnPc = context.Rip;
				const auto stack = context.Rsp;
				const auto callerReturn = *reinterpret_cast<const std::uintptr_t*>(stack + 0x28);
				entry = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
				g_beamFrameHasMetadata = entry != nullptr;
				if (entry) {
					RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, entry,
						&context, &handlerData, &establisherFrame, nullptr);
				} else {
					context.Rip = *reinterpret_cast<const std::uintptr_t*>(context.Rsp);
					context.Rsp += sizeof(std::uintptr_t);
				}
				g_beamUnwindMatches = context.Rip == callerReturn && context.Rsp == stack + 0x30;
			}
		}
		return 17;
	}
	int BeamTargetSecond(const void*) { return 29; }

	int BranchDetour(void* a_value)
	{
		const auto original = reinterpret_cast<int (*)(void*)>(g_branchOriginal.load(std::memory_order_acquire));
		return original(a_value) + 10;
	}

	void CaptureLog(std::string_view a_message)
	{
		g_lastLog.assign(a_message);
	}

	void ThrowingLog(std::string_view)
	{
		throw 1;
	}

	void RecordPublish(void* a_original)
	{
		++g_publishCount;
		g_published = a_original;
		if (a_original) {
			g_publishedBeforeEnable = std::memcmp(
				reinterpret_cast<const void*>(g_publishTarget), g_publishPrologue.data(), g_publishPrologue.size()) == 0;
		}
	}

	SAFETYHOOK_NOINLINE int Target(int a_value)
	{
		volatile int value = a_value;
		value = value * 3 + 7;
		value = value ^ 0x5A;
		value = value - 11;
		return value;
	}

	int Detour(int a_value)
	{
		return g_installed.hook->call<int>(a_value) + 1000;
	}
}

int main()
{
	const auto target = reinterpret_cast<std::uintptr_t>(&Target);
	const AddressRange codeSection{ target, target + 64 };

	std::array<std::uint8_t, 16> prologue{};
	std::memcpy(prologue.data(), reinterpret_cast<const void*>(target), prologue.size());
	const auto originalValue = Target(2);
	const auto destination = reinterpret_cast<void*>(&Detour);

	{
		auto expected = prologue;
		expected[0] ^= 0xFF;
		F4T::InlineHook::Result result{};
		F4T::InlineHook::Install(Request{ target, codeSection, expected, destination }, CaptureLog, result);
		assert(result.verdict == InstallVerdict::kPrologueMismatch);
		assert(!result.hook);
		assert(g_lastLog.find("differ from the expected prologue") != std::string::npos);
		assert(std::memcmp(reinterpret_cast<const void*>(target), prologue.data(), prologue.size()) == 0);
		assert(Target(2) == originalValue);
	}

	{
		alignas(16) std::array<std::uint8_t, 32> fake{};
		fake[0] = 0xE9;
		const auto fakeAddress = reinterpret_cast<std::uintptr_t>(fake.data());
		F4T::InlineHook::Result result{};
		F4T::InlineHook::Install(
			Request{ fakeAddress, AddressRange{ fakeAddress, fakeAddress + fake.size() }, prologue, destination },
			CaptureLog,
			result);
		assert(result.verdict == InstallVerdict::kPriorRel32Jump);
		assert(!result.hook);
		assert(fake[0] == 0xE9);
		assert(g_lastLog.find("rel32 jmp") != std::string::npos);
	}

	{
		F4T::InlineHook::Result result{};
		F4T::InlineHook::Install(Request{ target, codeSection, prologue, nullptr }, nullptr, result);
		assert(result.verdict == InstallVerdict::kDestinationUnresolved);
		assert(!result.hook);
		assert(std::memcmp(reinterpret_cast<const void*>(target), prologue.data(), prologue.size()) == 0);
	}

	{
		F4T::InlineHook::Result result{};
		F4T::InlineHook::Install(Request{ 0, codeSection, prologue, destination }, ThrowingLog, result);
		assert(result.verdict == InstallVerdict::kTargetUnresolved);
	}

	{
		F4T::InlineHook::Result result{};
		F4T::InlineHook::Install(Request{ 0, codeSection, prologue, destination }, nullptr, result);
		assert(result.verdict == InstallVerdict::kTargetUnresolved);
		assert(!result.hook);
	}

	{
		F4T::InlineHook::Result result{};
		F4T::InlineHook::Install(
			Request{ target, AddressRange{ 0x1000, 0x2000 }, prologue, destination }, nullptr, result);
		assert(result.verdict == InstallVerdict::kTargetNotExecutable);
		assert(!result.hook);
	}

	g_publishTarget = target;
	g_publishPrologue = prologue;

	{
		F4T::InlineHook::Result result{};
		g_published = destination;
		F4T::InlineHook::Install(Request{ target, codeSection, prologue, destination, RecordPublish, 1 }, nullptr, result);
		assert(result.verdict == InstallVerdict::kCreateFailed);
		assert(!result.hook);
		assert(g_publishCount == 1);
		assert(g_published == nullptr);
		assert(std::memcmp(reinterpret_cast<const void*>(target), prologue.data(), prologue.size()) == 0);
		g_publishCount = 0;
	}

	{
		auto expected = prologue;
		expected[0] ^= 0xFF;
		F4T::InlineHook::Result result{};
		g_published = destination;
		F4T::InlineHook::Install(Request{ target, codeSection, expected, destination, RecordPublish }, nullptr, result);
		assert(result.verdict == InstallVerdict::kPrologueMismatch);
		assert(g_publishCount == 1);
		assert(g_published == nullptr);
	}

	g_publishCount = 0;
	F4T::InlineHook::Install(Request{ target, codeSection, prologue, destination, RecordPublish }, CaptureLog, g_installed);
	assert(g_installed.verdict == InstallVerdict::kOk);
	assert(g_installed.hook);
	assert(g_publishCount == 1);
	assert(g_publishedBeforeEnable);
	assert(g_published != nullptr);
	assert(g_published == g_installed.hook->original<void*>());
	assert(g_installed.hook->destination_address() == reinterpret_cast<std::uintptr_t>(destination));
	assert(g_lastLog.find("installed and verified") != std::string::npos);
	assert(Target(2) == originalValue + 1000);

	{
		auto expected = prologue;
		expected[0] ^= 0xFF;
		F4T::InlineHook::Install(Request{ target, codeSection, expected, destination }, nullptr, g_installed);
		assert(g_installed.verdict == InstallVerdict::kPrologueMismatch);
		assert(!g_installed.hook);
		assert(std::memcmp(reinterpret_cast<const void*>(target), prologue.data(), prologue.size()) == 0);
		assert(Target(2) == originalValue);
	}

	{
		constexpr std::array<std::uint8_t, 21> code{
			0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x01,
			0xFF, 0x90, 0x18, 0x07, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x28, 0xC3,
			0x90, 0x90, 0x90
		};
		auto* memory = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		assert(memory != nullptr);
		std::memcpy(memory, code.data(), code.size());
		constexpr std::array<std::uint8_t, 8> unwindInfo{ 0x01, 0x04, 0x01, 0x00, 0x04, 0x42, 0x00, 0x00 };
		std::memcpy(static_cast<std::uint8_t*>(memory) + 0x100, unwindInfo.data(), unwindInfo.size());
		assert(FlushInstructionCache(GetCurrentProcess(), memory, code.size()));
		const auto address = reinterpret_cast<std::uintptr_t>(memory);
		RUNTIME_FUNCTION callerRuntime{ 0, 18, 0x100 };
		assert(RtlAddFunctionTable(&callerRuntime, 1, address));
		const auto targetFunction = reinterpret_cast<int (*)(const void*)>(memory);
		std::array<std::uintptr_t, 228> firstTable{};
		std::array<std::uintptr_t, 228> secondTable{};
		firstTable[227] = reinterpret_cast<std::uintptr_t>(&BeamTargetFirst);
		secondTable[227] = reinterpret_cast<std::uintptr_t>(&BeamTargetSecond);
		const auto* firstObject = firstTable.data();
		const auto* secondObject = secondTable.data();
		g_captureBeamUnwind = true;
		assert(targetFunction(&firstObject) == 17 && targetFunction(&secondObject) == 29);
		assert(g_beamReturnPc == address + 13 && g_beamFrameHasMetadata && g_beamUnwindMatches);
		F4T::InlineHook::MidResult result{};
		F4T::InlineHook::InstallMid(
			Request{ address + 7, AddressRange{ address, address + 4096 }, std::span<const std::uint8_t>{ code }.subspan(7) },
			BeamProbe, CaptureLog, result);
		assert(result.verdict == InstallVerdict::kOk && result.hook);
		assert(result.hook->original_bytes().size() == 6);
		g_beamCalls.store(0, std::memory_order_relaxed);
		assert(targetFunction(&firstObject) == 17);
		assert(g_beamReturnPc == g_beamTrampoline.load(std::memory_order_relaxed) + 6);
		assert(g_beamFrameHasMetadata == g_beamUnwindMatches);
		std::fprintf(stderr, "beam trampoline unwind: metadata=%d caller_matches=%d\n",
			g_beamFrameHasMetadata, g_beamUnwindMatches);
		assert(g_beamObject.load(std::memory_order_relaxed) == reinterpret_cast<std::uintptr_t>(&firstObject));
		assert(targetFunction(&secondObject) == 29);
		assert(g_beamObject.load(std::memory_order_relaxed) == reinterpret_cast<std::uintptr_t>(&secondObject));
		assert(g_beamCalls.load(std::memory_order_relaxed) == 2);
		result.hook.reset();
		g_captureBeamUnwind = false;
		assert(targetFunction(&firstObject) == 17 && g_beamCalls.load(std::memory_order_relaxed) == 2);
		assert(RtlDeleteFunctionTable(&callerRuntime));
		assert(VirtualFree(memory, 0, MEM_RELEASE));
	}

	{
		constexpr std::array<std::uint8_t, 16> code{
			0x48, 0x8B, 0x81, 0x60, 0x02, 0x00, 0x00, 0xC3,
			0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
		};
		auto* memory = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		assert(memory != nullptr);
		std::memcpy(memory, code.data(), code.size());
		assert(FlushInstructionCache(GetCurrentProcess(), memory, code.size()));
		const auto address = reinterpret_cast<std::uintptr_t>(memory);
		const auto targetFunction = reinterpret_cast<std::uint64_t (*)(const void*)>(memory);
		std::array<std::uint8_t, 0x268> object{};
		constexpr std::uint64_t value = 0x123456789ABCDEF0ULL;
		std::memcpy(object.data() + 0x260, &value, sizeof(value));
		assert(targetFunction(object.data()) == value);
		F4T::InlineHook::Result result{};
		F4T::InlineHook::Install(
			Request{ address, AddressRange{ address, address + 4096 }, code,
				reinterpret_cast<void*>(&FlameProbeDetour), PublishFlameOriginal, 7 }, CaptureLog, result);
		assert(result.verdict == InstallVerdict::kOk && result.hook);
		assert(std::memcmp(g_flameOriginal.load(std::memory_order_acquire), code.data(), 7) == 0);
		assert(targetFunction(object.data()) == value + 100);
		result.hook.reset();
		assert(targetFunction(object.data()) == value);
		g_flameOriginal.store(nullptr, std::memory_order_release);
		assert(VirtualFree(memory, 0, MEM_RELEASE));
	}

	{
		constexpr std::array<std::array<std::uint8_t, 5>, 2> prefixes{{
			{ 0x44, 0x89, 0x44, 0x24, 0x18 },
			{ 0x4C, 0x89, 0x4C, 0x24, 0x20 }
		}};
		for (const auto& prefix : prefixes) {
			std::array<std::uint8_t, 39> code{
				0, 0, 0, 0, 0, 0x48, 0x89, 0xC8, 0x48, 0xC1, 0xE0, 0x04,
				0x48, 0x09, 0xD0, 0x48, 0xC1, 0xE0, 0x04, 0x4C, 0x09, 0xC0,
				0x48, 0xC1, 0xE0, 0x04, 0x4C, 0x09, 0xC8, 0x48, 0xC1, 0xE0, 0x04,
				0x48, 0x0B, 0x44, 0x24, 0x28, 0xC3
			};
			std::copy(prefix.begin(), prefix.end(), code.begin());
			auto* memory = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			assert(memory != nullptr);
			std::memcpy(memory, code.data(), code.size());
			assert(FlushInstructionCache(GetCurrentProcess(), memory, code.size()));
			const auto address = reinterpret_cast<std::uintptr_t>(memory);
			const auto targetFunction = reinterpret_cast<FireProbe>(memory);
			assert(targetFunction(1, 2, 4, 8, 3) == 0x12483);
			F4T::InlineHook::Result result{};
			F4T::InlineHook::Install(
				Request{ address, AddressRange{ address, address + 4096 }, code,
					reinterpret_cast<void*>(&FireProbeDetour), PublishFireOriginal, 5 }, CaptureLog, result);
			assert(result.verdict == InstallVerdict::kOk && result.hook);
			assert(std::memcmp(g_fireOriginal.load(std::memory_order_acquire), prefix.data(), prefix.size()) == 0);
			assert(targetFunction(1, 2, 4, 8, 3) == 0x12483 + 100);
			result.hook.reset();
			assert(targetFunction(1, 2, 4, 8, 3) == 0x12483);
			g_fireOriginal.store(nullptr, std::memory_order_release);
			assert(VirtualFree(memory, 0, MEM_RELEASE));
		}
	}

	{
		constexpr std::array<std::uint8_t, 16> code{
			0x48, 0x89, 0x5C, 0x24, 0x10, 0x8B, 0xC1, 0xC3,
			0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
		};
		auto* memory = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		assert(memory != nullptr);
		std::memcpy(memory, code.data(), code.size());
		assert(FlushInstructionCache(GetCurrentProcess(), memory, code.size()));
		const auto leafTarget = reinterpret_cast<std::uintptr_t>(memory);
		const auto leaf = reinterpret_cast<int (*)(int)>(memory);
		assert(leaf(7) == 7);
		g_leafReady.store(false, std::memory_order_release);

		std::atomic<bool> stop{};
		std::atomic<bool> incorrect{};
		std::atomic<unsigned> calls{};
		std::barrier started(3);
		auto caller = [&] {
			started.arrive_and_wait();
			while (!stop.load(std::memory_order_relaxed)) {
				const auto value = leaf(7);
				if (value != 7 && value != 107) incorrect.store(true, std::memory_order_relaxed);
				calls.fetch_add(1, std::memory_order_relaxed);
			}
		};
		std::thread first(caller);
		std::thread second(caller);
		started.arrive_and_wait();
		while (calls.load(std::memory_order_relaxed) < 100) std::this_thread::yield();
		F4T::InlineHook::Result result{};
		F4T::InlineHook::Install(
			Request{ leafTarget, AddressRange{ leafTarget, leafTarget + 4096 }, code,
				reinterpret_cast<void*>(&LeafDetour), PublishLeafOriginal, 5 }, CaptureLog, result);
		g_leafReady.store(result.verdict == InstallVerdict::kOk, std::memory_order_release);
		stop.store(true, std::memory_order_relaxed);
		first.join();
		second.join();
		if (result.verdict != InstallVerdict::kOk || !result.hook) std::fprintf(stderr, "leaf hook: %s\n", g_lastLog.c_str());
		assert(result.verdict == InstallVerdict::kOk && result.hook);
		assert(!incorrect.load(std::memory_order_relaxed));
		assert(leaf(7) == 107);
		result.hook.reset();
		assert(leaf(7) == 7);
		g_leafReady.store(false, std::memory_order_release);
		F4T::InlineHook::FailNextVerificationForTest();
		F4T::InlineHook::Install(
			Request{ leafTarget, AddressRange{ leafTarget, leafTarget + 4096 }, code,
				reinterpret_cast<void*>(&LeafDetour), PublishLeafOriginal, 5, true }, nullptr, result);
		g_leafReady.store(result.verdict == InstallVerdict::kOk, std::memory_order_release);
		assert(result.verdict == InstallVerdict::kInstallNotVerified && result.hook);
		assert(g_leafOriginal.load(std::memory_order_acquire) != nullptr);
		assert(leaf(7) == 7);
		result.hook.reset();
		assert(leaf(7) == 7);
		assert(VirtualFree(memory, 0, MEM_RELEASE));
	}

	{
		constexpr std::array<std::uint8_t, 16> code{
			0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10, 0x8B,
			0x40, 0x10, 0x01, 0xC8, 0xC3, 0x90, 0x90, 0x90
		};
		auto* memory = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		assert(memory != nullptr);
		std::memcpy(memory, code.data(), code.size());
		assert(FlushInstructionCache(GetCurrentProcess(), memory, code.size()));
		const auto address = reinterpret_cast<std::uintptr_t>(memory);
		const auto targetFunction = reinterpret_cast<int (*)(std::uintptr_t, int)>(memory);
		assert(targetFunction(17, 23) == 40);
		F4T::InlineHook::Result result{};
		F4T::InlineHook::Install(
			Request{ address, AddressRange{ address, address + 4096 }, code,
				reinterpret_cast<void*>(&UnwindDetour), PublishUnwindOriginal, 7 }, CaptureLog, result);
		assert(result.verdict == InstallVerdict::kOk && result.hook);
		assert(std::memcmp(g_unwindOriginal.load(std::memory_order_acquire), code.data(), 7) == 0);
		assert(targetFunction(17, 23) == 40);
		assert(g_unwindMatches);
		auto* taskCode = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
		assert(taskCode != nullptr);
		std::array<std::uint8_t, 28> taskInstructions{
			0x41, 0x54, 0x48, 0x83, 0xEC, 0x20, 0x49, 0x89, 0xCC, 0x48, 0xB8,
			0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xD0, 0x48, 0x83, 0xC4, 0x20, 0x41, 0x5C, 0xC3
		};
		std::memcpy(taskInstructions.data() + 11, &address, sizeof(address));
		std::memcpy(taskCode, taskInstructions.data(), taskInstructions.size());
		constexpr std::array<std::uint8_t, 8> unwindInfo{ 0x01, 0x06, 0x02, 0x00, 0x06, 0x32, 0x02, 0xC0 };
		std::memcpy(taskCode + 0x100, unwindInfo.data(), unwindInfo.size());
		assert(FlushInstructionCache(GetCurrentProcess(), taskCode, taskInstructions.size()));
		RUNTIME_FUNCTION taskRuntime{ 0, static_cast<DWORD>(taskInstructions.size()), 0x100 };
		assert(RtlAddFunctionTable(&taskRuntime, 1, reinterpret_cast<DWORD64>(taskCode)));
		int task{};
		g_expectedTask = reinterpret_cast<std::uintptr_t>(&task);
		g_expectedTaskReturn = reinterpret_cast<std::uintptr_t>(taskCode + 21);
		const auto taskFunction = reinterpret_cast<int (*)(std::uintptr_t, int)>(taskCode);
		assert(static_cast<std::uint32_t>(taskFunction(g_expectedTask, 23)) == static_cast<std::uint32_t>(g_expectedTask) + 23U);
		assert(g_taskUnwindMatches);
		assert(RtlDeleteFunctionTable(&taskRuntime));
		taskInstructions[1] = 0x57;
		taskInstructions[8] = 0xCF;
		taskInstructions[26] = 0x5F;
		std::memcpy(taskCode, taskInstructions.data(), taskInstructions.size());
		taskCode[0x107] = 0xF0;
		assert(FlushInstructionCache(GetCurrentProcess(), taskCode, taskInstructions.size()));
		assert(RtlAddFunctionTable(&taskRuntime, 1, reinterpret_cast<DWORD64>(taskCode)));
		g_taskRegister = 15;
		g_taskUnwindMatches = false;
		assert(static_cast<std::uint32_t>(taskFunction(g_expectedTask, 23)) == static_cast<std::uint32_t>(g_expectedTask) + 23U);
		assert(g_taskUnwindMatches);
		assert(RtlDeleteFunctionTable(&taskRuntime));
		taskInstructions[1] = 0x55;
		taskInstructions[8] = 0xCD;
		taskInstructions[26] = 0x5D;
		std::memcpy(taskCode, taskInstructions.data(), taskInstructions.size());
		taskCode[0x107] = 0xD0;
		assert(FlushInstructionCache(GetCurrentProcess(), taskCode, taskInstructions.size()));
		assert(RtlAddFunctionTable(&taskRuntime, 1, reinterpret_cast<DWORD64>(taskCode)));
		g_taskRegister = 13;
		g_taskUnwindMatches = false;
		assert(static_cast<std::uint32_t>(taskFunction(g_expectedTask, 23)) == static_cast<std::uint32_t>(g_expectedTask) + 23U);
		assert(g_taskUnwindMatches);
		g_expectedTask = 0;
		g_taskRegister = 0;
		assert(RtlDeleteFunctionTable(&taskRuntime));
		assert(VirtualFree(taskCode, 0, MEM_RELEASE));
		result.hook.reset();
		g_unwindOriginal.store(nullptr, std::memory_order_release);
		assert(VirtualFree(memory, 0, MEM_RELEASE));
	}

	{
		constexpr std::array<std::uint8_t, 16> code{
			0x48, 0x8B, 0xC4, 0x66, 0x66, 0x0F, 0x0B, 0xC3,
			0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
		};
		auto* memory = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		assert(memory != nullptr);
		std::memcpy(memory, code.data(), code.size());
		assert(FlushInstructionCache(GetCurrentProcess(), memory, code.size()));
		const auto address = reinterpret_cast<std::uintptr_t>(memory);
		F4T::InlineHook::Result result{};
		F4T::InlineHook::Install(
			Request{ address, AddressRange{ address, address + 4096 }, code,
				reinterpret_cast<void*>(&UnwindDetour), PublishUnwindOriginal, 7 }, CaptureLog, result);
		assert(result.verdict == InstallVerdict::kOk && result.hook);
		const auto trampoline = reinterpret_cast<std::uintptr_t>(g_unwindOriginal.load(std::memory_order_acquire));
		assert(std::memcmp(reinterpret_cast<const void*>(trampoline), code.data(), 7) == 0);
		g_faultAddress = 0;
		assert(ExpectIllegalInstruction(reinterpret_cast<int (*)(std::uintptr_t, int)>(memory)));
		assert(g_faultAddress == trampoline + 3);
		result.hook.reset();
		g_unwindOriginal.store(nullptr, std::memory_order_release);
		assert(VirtualFree(memory, 0, MEM_RELEASE));
	}

	{
		constexpr std::array<std::uint8_t, 18> code{
			0x48, 0x85, 0xD2, 0x0F, 0x84, 0x06, 0x00, 0x00, 0x00,
			0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3, 0x31, 0xC0, 0xC3
		};
		auto* memory = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		assert(memory != nullptr);
		std::memcpy(memory, code.data(), code.size());
		assert(FlushInstructionCache(GetCurrentProcess(), memory, code.size()));
		const auto address = reinterpret_cast<std::uintptr_t>(memory);
		const auto targetFunction = reinterpret_cast<int (*)(void*, void*)>(memory);
		int value{};
		assert(targetFunction(nullptr, nullptr) == 0 && targetFunction(nullptr, &value) == 1);
		F4T::InlineHook::Result result{};
		F4T::InlineHook::Install(
			Request{ address, AddressRange{ address, address + 4096 }, code,
				reinterpret_cast<void*>(&NearBranchDetour), PublishNearBranchOriginal, 9 }, CaptureLog, result);
		assert(result.verdict == InstallVerdict::kOk && result.hook);
		const auto* trampoline = static_cast<const std::uint8_t*>(g_nearBranchOriginal.load(std::memory_order_acquire));
		assert(trampoline != nullptr && std::memcmp(trampoline, code.data(), 5) == 0);
		std::int32_t branchDisplacement{};
		std::memcpy(&branchDisplacement, trampoline + 5, sizeof(branchDisplacement));
		assert(reinterpret_cast<std::intptr_t>(trampoline + 9) + branchDisplacement == static_cast<std::intptr_t>(address + 15));
		assert(trampoline[9] == 0xE9);
		std::int32_t returnDisplacement{};
		std::memcpy(&returnDisplacement, trampoline + 10, sizeof(returnDisplacement));
		assert(reinterpret_cast<std::intptr_t>(trampoline + 14) + returnDisplacement == static_cast<std::intptr_t>(address + 9));
		assert(targetFunction(nullptr, nullptr) == 10 && targetFunction(nullptr, &value) == 11);
		result.hook.reset();
		g_nearBranchOriginal.store(nullptr, std::memory_order_release);
		assert(VirtualFree(memory, 0, MEM_RELEASE));
	}

	{
		constexpr std::array<std::uint8_t, 20> code{
			0x48, 0x85, 0xC9, 0x74, 0x0B, 0xB8, 0x01, 0x00, 0x00, 0x00,
			0xC3, 0x90, 0x90, 0x90, 0x90, 0x90, 0x31, 0xC0, 0xC3, 0x90
		};
		auto* memory = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		assert(memory != nullptr);
		std::memcpy(memory, code.data(), code.size());
		assert(FlushInstructionCache(GetCurrentProcess(), memory, code.size()));
		const auto targetAddress = reinterpret_cast<std::uintptr_t>(memory);
		const auto branch = reinterpret_cast<int (*)(void*)>(memory);
		int value{};
		assert(branch(nullptr) == 0 && branch(&value) == 1);
		F4T::InlineHook::Result result{};
		F4T::InlineHook::Install(
			Request{ targetAddress, AddressRange{ targetAddress, targetAddress + 4096 }, code,
				reinterpret_cast<void*>(&BranchDetour), PublishBranchOriginal, 5 }, CaptureLog, result);
		if (result.verdict != InstallVerdict::kOk || !result.hook) std::fprintf(stderr, "branch hook: %s\n", g_lastLog.c_str());
		assert(result.verdict == InstallVerdict::kOk && result.hook);
		assert(branch(nullptr) == 10 && branch(&value) == 11);
		result.hook.reset();
		assert(branch(nullptr) == 0 && branch(&value) == 1);
		assert(VirtualFree(memory, 0, MEM_RELEASE));
	}
	return 0;
}

#include "F4TProbeCapture.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>

namespace
{
	std::atomic<std::uintptr_t> g_moduleBase{ 0 };
	F4T::ProbeRing::Ring g_ring{};
	std::atomic<std::uint64_t> g_pushed{ 0 };
}

namespace F4T::ProbeCapture
{
	void SetModuleBase(std::uintptr_t a_base) noexcept
	{
		g_moduleBase.store(a_base, std::memory_order_relaxed);
	}

	std::uintptr_t ModuleBase() noexcept
	{
		return g_moduleBase.load(std::memory_order_relaxed);
	}

	std::uint32_t CurrentThreadId() noexcept
	{
		return static_cast<std::uint32_t>(::GetCurrentThreadId());
	}

	std::uint64_t CurrentTickMicros() noexcept
	{
		const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
		return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
	}

	std::size_t CaptureStackChain(std::uintptr_t a_moduleBase, std::span<std::uint64_t> a_out) noexcept
	{
		if (a_out.empty()) {
			return 0;
		}
		std::array<void*, MaxStackChain> frames{};
		const auto wanted = static_cast<DWORD>(std::min(a_out.size(), frames.size()));
		const auto count = static_cast<std::size_t>(::RtlCaptureStackBackTrace(0, wanted, frames.data(), nullptr));
		for (std::size_t index = 0; index < count; ++index) {
			a_out[index] = reinterpret_cast<std::uintptr_t>(frames[index]) - a_moduleBase;
		}
		return count;
	}

	ProbeRecord::Record Build(
		std::uint16_t a_probeId,
		ProbeRecord::Kind a_kind,
		const Context& a_context,
		std::span<const std::uint64_t> a_values,
		std::uint64_t a_retval) noexcept
	{
		ProbeRecord::Record record;
		record.probeId = a_probeId;
		record.kind = a_kind;
		record.threadId = a_context.threadId;
		record.tickMicros = a_context.tickMicros;
		record.callerRva = a_context.callerAddress >= a_context.moduleBase
			? a_context.callerAddress - a_context.moduleBase
			: 0;
		record.retval = a_retval;
		const auto count = std::min(a_values.size(), ProbeRecord::MaxArgs);
		for (std::size_t index = 0; index < count; ++index) {
			record.args[index] = a_values[index];
		}
		record.argCount = static_cast<std::uint8_t>(count);
		return record;
	}

	ProbeRing::Ring& Ring() noexcept
	{
		return g_ring;
	}

	std::uint64_t PushedCount() noexcept
	{
		return g_pushed.load(std::memory_order_relaxed);
	}

	void Push(
		std::uint16_t a_probeId,
		ProbeRecord::Kind a_kind,
		std::uintptr_t a_returnAddress,
		std::span<const std::uint64_t> a_values,
		std::uint64_t a_retval) noexcept
	{
		std::array<std::uint64_t, MaxStackChain> chain{};
		if (a_values.empty()) {
			const auto count = CaptureStackChain(ModuleBase(), chain);
			a_values = std::span<const std::uint64_t>{ chain.data(), count };
		}
		const Context context{ CurrentThreadId(), CurrentTickMicros(), a_returnAddress, ModuleBase() };
		Ring().Push(Build(a_probeId, a_kind, context, a_values, a_retval));
		g_pushed.fetch_add(1, std::memory_order_relaxed);
	}
}

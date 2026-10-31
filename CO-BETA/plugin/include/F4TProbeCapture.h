#pragma once

#include "F4TProbeRecord.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace F4T::ProbeCapture
{
	inline constexpr std::size_t MaxStackChain = 8;

	struct Context
	{
		std::uint32_t threadId{ 0 };
		std::uint64_t tickMicros{ 0 };
		std::uintptr_t callerAddress{ 0 };
		std::uintptr_t moduleBase{ 0 };
	};

	void SetModuleBase(std::uintptr_t a_base) noexcept;
	[[nodiscard]] std::uintptr_t ModuleBase() noexcept;

	[[nodiscard]] std::uint32_t CurrentThreadId() noexcept;
	[[nodiscard]] std::uint64_t CurrentTickMicros() noexcept;
	[[nodiscard]] std::size_t CaptureStackChain(std::uintptr_t a_moduleBase, std::span<std::uint64_t> a_out) noexcept;

	[[nodiscard]] ProbeRecord::Record Build(
		std::uint16_t a_probeId,
		ProbeRecord::Kind a_kind,
		const Context& a_context,
		std::span<const std::uint64_t> a_values,
		std::uint64_t a_retval) noexcept;

	[[nodiscard]] ProbeRing::Ring& Ring() noexcept;
	[[nodiscard]] std::uint64_t PushedCount() noexcept;

	void Push(
		std::uint16_t a_probeId,
		ProbeRecord::Kind a_kind,
		std::uintptr_t a_returnAddress,
		std::span<const std::uint64_t> a_values,
		std::uint64_t a_retval) noexcept;
}

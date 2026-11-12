#pragma once

#include "F4TInlineHook.h"
#include "F4TProbeManifest.h"
#include "F4TProbeVersionGate.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace F4T::ProbeRegistry
{
	inline constexpr std::size_t MaxProbes = 64;
	inline constexpr std::size_t MaxSpecs = 32;
	inline constexpr std::uint16_t SelfTestProbeId = 1;

	using LogFn = void (*)(std::string_view);

	struct HookPlan
	{
		std::uintptr_t target{ 0 };
		std::span<const std::uint8_t> expectedPrologue{};
		void* destination{ nullptr };
		safetyhook::MidHookFn midDestination{ nullptr };
	};

	struct RuntimeSettings
	{
		std::uint16_t probeId{ 0 };
		std::string_view name{};
		std::uint32_t sampling{ 1 };
		std::uint32_t perProbeCap{ 0 };
		ProbeVersionGate::RuntimeVersion version{};
	};

	enum class InstallOutcome
	{
		Disabled,
		Enabled
	};

	using Installer = InstallOutcome (*)(const RuntimeSettings&, HookPlan&);

	struct Spec
	{
		std::uint16_t probeId{ 0 };
		std::string_view name{};
		Installer install{ nullptr };
	};

	[[nodiscard]] bool Register(const Spec& a_spec) noexcept;
	void Install(
		const ProbeManifest::Document& a_manifest,
		ProbeVersionGate::RuntimeVersion a_version,
		LogFn a_log);
	[[nodiscard]] bool ShouldRecord(std::uint16_t a_probeId) noexcept;
}

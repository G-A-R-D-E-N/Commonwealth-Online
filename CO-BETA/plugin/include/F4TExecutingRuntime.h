#pragma once

#include "F4TClientCompatibility.h"

#include <cstdint>
#include <format>
#include <optional>

namespace F4T::ExecutingRuntime
{
	inline std::optional<F4T::ClientCompatibility::BuildTarget> Target()
	{
		const auto version = REX::FModule::GetExecutingModule().GetFileVersion();
		if (version.build() != 0) {
			return std::nullopt;
		}
		return F4T::ClientCompatibility::TargetFromRuntime(
			std::format("{}.{}.{}", version.major(), version.minor(), version.patch()));
	}

	inline std::uintptr_t RvaFor(
		F4T::ClientCompatibility::BuildTarget a_target,
		std::uintptr_t a_og,
		std::uintptr_t a_ng,
		std::uintptr_t a_ae)
	{
		using Target = F4T::ClientCompatibility::BuildTarget;
		return a_target == Target::OG ? a_og : a_target == Target::NG ? a_ng : a_ae;
	}
}

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace F4T::ModpackManifest
{
	inline constexpr std::size_t kMaximumProbeEntries = 512;
	inline constexpr std::uint64_t kMaximumProbeHashedBytes = 256ull * 1024 * 1024;
	inline constexpr std::uint64_t kMaximumProbeMilliseconds = 2000;

	struct ProbeBudget
	{
		std::size_t entries{};
		std::uint64_t hashedBytes{};
		std::uint64_t elapsedMilliseconds{};
	};

	enum class ProbeBudgetKind
	{
		entries,
		hashedBytes,
		elapsedMilliseconds
	};

	[[nodiscard]] bool ProbeBudgetExhausted(const ProbeBudget& a_budget) noexcept;
	[[nodiscard]] bool ProbeHashBytesFit(const ProbeBudget& a_budget, std::uint64_t a_fileBytes) noexcept;
	[[nodiscard]] std::optional<ProbeBudgetKind> ProbeBudgetLimit(const ProbeBudget& a_budget) noexcept;
}

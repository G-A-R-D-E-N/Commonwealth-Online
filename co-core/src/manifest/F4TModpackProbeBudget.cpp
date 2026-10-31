#include "F4TModpackProbeBudget.h"

#include <cstddef>
#include <cstdint>

namespace F4T::ModpackManifest
{
	bool ProbeBudgetExhausted(const ProbeBudget& a_budget) noexcept
	{
		return a_budget.entries >= kMaximumProbeEntries ||
			a_budget.elapsedMilliseconds >= kMaximumProbeMilliseconds;
	}

	bool ProbeHashBytesFit(const ProbeBudget& a_budget, std::uint64_t a_fileBytes) noexcept
	{
		return a_fileBytes <= kMaximumProbeHashedBytes &&
			a_budget.hashedBytes <= kMaximumProbeHashedBytes - a_fileBytes;
	}

	std::optional<ProbeBudgetKind> ProbeBudgetLimit(const ProbeBudget& a_budget) noexcept
	{
		if (a_budget.entries >= kMaximumProbeEntries) {
			return ProbeBudgetKind::entries;
		}
		if (a_budget.elapsedMilliseconds >= kMaximumProbeMilliseconds) {
			return ProbeBudgetKind::elapsedMilliseconds;
		}
		if (a_budget.hashedBytes >= kMaximumProbeHashedBytes) {
			return ProbeBudgetKind::hashedBytes;
		}
		return std::nullopt;
	}
}

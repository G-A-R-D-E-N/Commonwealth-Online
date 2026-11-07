#include "F4TModpackProbeBudget.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace
{
	using namespace F4T::ModpackManifest;

	void TestProbeStopsAtEntryCap()
	{
		ProbeBudget budget;
		budget.entries = kMaximumProbeEntries - 1;
		assert(!ProbeBudgetExhausted(budget));
		budget.entries = kMaximumProbeEntries;
		assert(ProbeBudgetExhausted(budget));
		budget.entries = kMaximumProbeEntries + 1;
		assert(ProbeBudgetExhausted(budget));
	}

	void TestProbeStopsAtDeadline()
	{
		ProbeBudget budget;
		budget.elapsedMilliseconds = kMaximumProbeMilliseconds - 1;
		assert(!ProbeBudgetExhausted(budget));
		budget.elapsedMilliseconds = kMaximumProbeMilliseconds;
		assert(ProbeBudgetExhausted(budget));
		budget.elapsedMilliseconds = kMaximumProbeMilliseconds + 1;
		assert(ProbeBudgetExhausted(budget));
	}

	void TestProbeStopsAtHashedByteCap()
	{
		ProbeBudget budget;
		budget.hashedBytes = kMaximumProbeHashedBytes - 8;
		assert(ProbeHashBytesFit(budget, 8));
		assert(!ProbeHashBytesFit(budget, 9));
		assert(ProbeHashBytesFit(ProbeBudget{}, kMaximumProbeHashedBytes));
		assert(!ProbeHashBytesFit(ProbeBudget{}, kMaximumProbeHashedBytes + 1));
	}

	void TestBudgetLimitNamesTheFirstLimit()
	{
		assert(!ProbeBudgetLimit(ProbeBudget{}).has_value());
		ProbeBudget entries;
		entries.entries = kMaximumProbeEntries;
		assert(ProbeBudgetLimit(entries) == ProbeBudgetKind::entries);
		ProbeBudget elapsed;
		elapsed.elapsedMilliseconds = kMaximumProbeMilliseconds;
		assert(ProbeBudgetLimit(elapsed) == ProbeBudgetKind::elapsedMilliseconds);
		ProbeBudget bytes;
		bytes.hashedBytes = kMaximumProbeHashedBytes;
		assert(ProbeBudgetLimit(bytes) == ProbeBudgetKind::hashedBytes);
		ProbeBudget all;
		all.entries = kMaximumProbeEntries;
		all.elapsedMilliseconds = kMaximumProbeMilliseconds;
		all.hashedBytes = kMaximumProbeHashedBytes;
		assert(ProbeBudgetLimit(all) == ProbeBudgetKind::entries);
	}

	void TestProbeOfFreshRunAllowsWork()
	{
		const ProbeBudget budget;
		assert(!ProbeBudgetExhausted(budget));
		assert(ProbeHashBytesFit(budget, 1));
	}
}

int main()
{
	TestProbeStopsAtEntryCap();
	TestProbeStopsAtDeadline();
	TestProbeStopsAtHashedByteCap();
	TestProbeOfFreshRunAllowsWork();
	TestBudgetLimitNamesTheFirstLimit();
	return 0;
}

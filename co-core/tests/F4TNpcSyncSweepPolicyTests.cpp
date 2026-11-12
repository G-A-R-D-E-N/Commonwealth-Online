#include "F4TNpcSyncSweepPolicy.h"

#include <cassert>
#include <cstdint>

int main()
{
	using namespace F4T::NpcSyncSweep;

	const AuthoritySnapshot base{ true, 7, 3 };
	assert(!AuthorityChanged(base, base));
	assert(AuthorityChanged(base, AuthoritySnapshot{ false, 7, 3 }));
	assert(AuthorityChanged(base, AuthoritySnapshot{ true, 8, 3 }));
	assert(AuthorityChanged(base, AuthoritySnapshot{ true, 7, 4 }));
	assert(AuthorityChanged(AuthoritySnapshot{}, AuthoritySnapshot{ false, 0, 1 }));

	assert(DecidePuppetDelete(false, false) == PuppetDeleteOutcome::kUnresolved);
	assert(DecidePuppetDelete(false, true) == PuppetDeleteOutcome::kUnresolved);
	assert(DecidePuppetDelete(true, true) == PuppetDeleteOutcome::kAlreadyDeleted);
	assert(DecidePuppetDelete(true, false) == PuppetDeleteOutcome::kDeleted);

	SweepTally tally{};
	assert(tally.Empty());
	tally.Count(PuppetDeleteOutcome::kDeleted);
	tally.Count(PuppetDeleteOutcome::kDeleted);
	tally.Count(PuppetDeleteOutcome::kAlreadyDeleted);
	tally.Count(PuppetDeleteOutcome::kUnresolved);
	assert(tally.deleted == 2 && tally.alreadyDeleted == 1 && tally.unresolved == 1);
	assert(!tally.Empty());

	SweepTally sum{};
	sum.Add(tally);
	sum.Add(tally);
	assert(sum.deleted == 4 && sum.alreadyDeleted == 2 && sum.unresolved == 2);

	ThrottledTally throttle;
	SweepTally none{};
	assert(!throttle.Take(none, 0, 2000).has_value());

	SweepTally one{};
	one.Count(PuppetDeleteOutcome::kDeleted);
	const auto first = throttle.Take(one, 100, 2000);
	assert(first.has_value() && first->deleted == 1);

	SweepTally more{};
	more.Count(PuppetDeleteOutcome::kDeleted);
	more.Count(PuppetDeleteOutcome::kUnresolved);
	assert(!throttle.Take(more, 1500, 2000).has_value());
	assert(!throttle.Take(none, 2099, 2000).has_value());

	const auto released = throttle.Take(none, 2100, 2000);
	assert(released.has_value() && released->deleted == 1 && released->unresolved == 1);
	assert(!throttle.Take(none, 2200, 2000).has_value());

	using Action = AuthorityFlipGate::Action;
	AuthorityFlipGate gate;
	const AuthoritySnapshot hostA{ true, 7, 3 };
	const AuthoritySnapshot hostB{ false, 9, 4 };
	const AuthoritySnapshot nobody{};
	assert(gate.Observe(nobody, false, 0, 1000) == Action::kNone);
	assert(gate.Observe(hostA, true, 10, 1000) == Action::kApply);
	assert(gate.Observe(hostA, true, 20, 1000) == Action::kNone);
	assert(gate.Observe(nobody, false, 100, 1000) == Action::kHold);
	assert(gate.Observe(nobody, false, 600, 1000) == Action::kHold);
	assert(gate.Observe(nobody, false, 1099, 1000) == Action::kHold);
	assert(gate.Observe(nobody, false, 1100, 1000) == Action::kApply);
	assert(gate.Observe(nobody, false, 1200, 1000) == Action::kNone);

	AuthorityFlipGate regained;
	assert(regained.Observe(hostA, true, 0, 1000) == Action::kApply);
	assert(regained.Observe(nobody, false, 50, 1000) == Action::kHold);
	assert(regained.Observe(hostA, true, 500, 1000) == Action::kNone);
	assert(regained.Observe(nobody, false, 600, 1000) == Action::kHold);
	assert(regained.Observe(nobody, false, 1599, 1000) == Action::kHold);
	assert(regained.Observe(nobody, false, 1600, 1000) == Action::kApply);

	AuthorityFlipGate switched;
	assert(switched.Observe(hostA, true, 0, 1000) == Action::kApply);
	assert(switched.Observe(hostB, true, 5, 1000) == Action::kApply);
	assert(switched.Observe(hostB, true, 6, 1000) == Action::kNone);

	switched.Reset();
	assert(switched.Observe(nobody, false, 0, 1000) == Action::kNone);
	assert(switched.Observe(hostA, true, 1, 1000) == Action::kApply);
	return 0;
}

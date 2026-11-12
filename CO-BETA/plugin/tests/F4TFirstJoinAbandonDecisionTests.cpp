#include "F4TFirstJoinAbandonDecision.h"

#include <cassert>
#include <cstdint>
#include <optional>

namespace FirstJoinAbandon = F4T::FirstJoinAbandon;

namespace
{
	using Action = FirstJoinAbandon::TickAction;

	struct Pending
	{
		bool active{ true };
		bool replyStored{};
		int completions{};
		bool replyApplied{};
	};

	void Complete(Pending& a_pending, Action a_action)
	{
		a_pending.active = false;
		++a_pending.completions;
		a_pending.replyApplied = a_action == Action::Finish;
		a_pending.replyStored = false;
	}

	void Tick(Pending& a_pending, bool a_connected, bool a_sameGeneration, bool a_deadlinePassed)
	{
		if (!a_pending.active) return;
		const auto action = FirstJoinAbandon::DecideTick(a_pending.replyStored, a_connected, a_sameGeneration, a_deadlinePassed);
		if (action != Action::Wait) Complete(a_pending, action);
	}

	void Cancel(Pending& a_pending)
	{
		if (!a_pending.active) return;
		Complete(a_pending, FirstJoinAbandon::DecideCancel(a_pending.replyStored));
	}

	enum class BeginAction
	{
		SendScopedAbandon,
		SendUnscopedAbandon
	};

	struct PendingBegin
	{
		std::optional<std::uint32_t> holdSlot;
		std::optional<std::uint32_t> selectedSlot;
		BeginAction action{ BeginAction::SendUnscopedAbandon };
		std::optional<std::uint32_t> sentSlot;
	};

	void BeginForSaveLessHold(PendingBegin& a_begin)
	{
		if (!FirstJoinAbandon::HoldSlotKnown(a_begin.holdSlot)) {
			a_begin.action = BeginAction::SendUnscopedAbandon;
			a_begin.sentSlot.reset();
			return;
		}
		a_begin.action = BeginAction::SendScopedAbandon;
		a_begin.sentSlot = a_begin.holdSlot;
	}
}

int main()
{
	assert(FirstJoinAbandon::DecideTick(true, false, false, true) == Action::Finish);
	assert(FirstJoinAbandon::DecideTick(true, true, true, false) == Action::Finish);
	assert(FirstJoinAbandon::DecideTick(false, false, true, false) == Action::ConnectionEnded);
	assert(FirstJoinAbandon::DecideTick(false, true, false, false) == Action::ConnectionEnded);
	assert(FirstJoinAbandon::DecideTick(false, true, true, true) == Action::TimedOut);
	assert(FirstJoinAbandon::DecideTick(false, true, true, false) == Action::Wait);
	assert(FirstJoinAbandon::DecideCancel(true) == Action::Finish);
	assert(FirstJoinAbandon::DecideCancel(false) == Action::ConnectionEnded);

	Pending replyTaskFirst;
	replyTaskFirst.replyStored = true;
	Cancel(replyTaskFirst);
	Tick(replyTaskFirst, false, false, true);
	assert(replyTaskFirst.completions == 1 && replyTaskFirst.replyApplied);

	Pending replyTickFirst;
	replyTickFirst.replyStored = true;
	Tick(replyTickFirst, false, false, true);
	Cancel(replyTickFirst);
	assert(replyTickFirst.completions == 1 && replyTickFirst.replyApplied);

	Pending noReplyTaskFirst;
	Cancel(noReplyTaskFirst);
	Tick(noReplyTaskFirst, false, false, true);
	assert(noReplyTaskFirst.completions == 1 && !noReplyTaskFirst.replyApplied);

	Pending noReplyTickFirst;
	Tick(noReplyTickFirst, false, false, false);
	Cancel(noReplyTickFirst);
	assert(noReplyTickFirst.completions == 1 && !noReplyTickFirst.replyApplied);

	Pending timedOut;
	Tick(timedOut, true, true, true);
	Cancel(timedOut);
	assert(timedOut.completions == 1 && !timedOut.replyApplied);

	// Constraint: a hold that recorded no slot asks the server to find its leftovers, never the selected slot.
	PendingBegin legacy;
	legacy.selectedSlot = 2;
	BeginForSaveLessHold(legacy);
	assert(legacy.action == BeginAction::SendUnscopedAbandon && !legacy.sentSlot.has_value());
	assert(legacy.sentSlot != legacy.selectedSlot);

	// Constraint: a recorded slot is the only abandon target, whatever is selected now.
	PendingBegin recorded;
	recorded.holdSlot = 1;
	recorded.selectedSlot = 2;
	BeginForSaveLessHold(recorded);
	assert(recorded.action == BeginAction::SendScopedAbandon && recorded.sentSlot == std::optional<std::uint32_t>{ 1 });
	assert(recorded.sentSlot != recorded.selectedSlot);
	return 0;
}

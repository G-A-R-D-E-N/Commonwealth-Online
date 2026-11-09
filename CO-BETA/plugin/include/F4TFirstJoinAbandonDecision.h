#pragma once

#include <cstdint>
#include <optional>

namespace F4T::FirstJoinAbandon
{
	enum class TickAction
	{
		Wait,
		Finish,
		ConnectionEnded,
		TimedOut
	};

	inline bool HoldSlotKnown(std::optional<std::uint32_t> a_holdSlot)
	{
		return a_holdSlot.has_value();
	}

	inline TickAction DecideCancel(bool a_replyStored)
	{
		return a_replyStored ? TickAction::Finish : TickAction::ConnectionEnded;
	}

	inline TickAction DecideTick(bool a_replyStored, bool a_connected, bool a_sameGeneration, bool a_deadlinePassed)
	{
		if (a_replyStored) return TickAction::Finish;
		if (!a_connected || !a_sameGeneration) return TickAction::ConnectionEnded;
		return a_deadlinePassed ? TickAction::TimedOut : TickAction::Wait;
	}
}

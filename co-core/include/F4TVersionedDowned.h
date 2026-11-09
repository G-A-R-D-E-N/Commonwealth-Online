#pragma once

#include <cstdint>

namespace F4T::VersionedDowned
{
	struct State
	{
		std::uint64_t incarnation{};
		std::uint64_t revision{};
		bool downed{};
		bool dead{};
	};

	inline bool Accept(State& a_current, const State& a_incoming)
	{
		if (a_incoming.incarnation == 0 || a_incoming.revision == 0 || (a_incoming.dead && !a_incoming.downed) ||
			a_incoming.incarnation < a_current.incarnation ||
			(a_incoming.incarnation == a_current.incarnation && a_incoming.revision < a_current.revision)) {
			return false;
		}
		if (a_incoming.incarnation == a_current.incarnation && a_incoming.revision == a_current.revision &&
			(a_incoming.downed != a_current.downed || a_incoming.dead != a_current.dead)) {
			return false;
		}
		a_current = a_incoming;
		return true;
	}
}

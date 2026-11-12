#include "F4TInterestScope.h"

#include <cmath>

namespace F4T::InterestScope
{
	bool StatesShareInterest(
		const std::optional<StateScope>& a_left,
		const std::optional<StateScope>& a_right) noexcept
	{
		if (!a_left || !a_right) {
			return false;
		}
		if (a_left->cellId == a_right->cellId) {
			return true;
		}
		if (a_left->worldspaceId.empty() || a_left->worldspaceId != a_right->worldspaceId) {
			return false;
		}
		const auto dx = a_left->x - a_right->x;
		const auto dy = a_left->y - a_right->y;
		return std::hypot(dx, dy) <= ExteriorInterestRadius;
	}
}

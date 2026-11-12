#pragma once

#include <optional>
#include <string>

namespace F4T::InterestScope
{
	inline constexpr double ExteriorInterestRadius = 8192.0;

	struct StateScope
	{
		std::string cellId;
		std::string worldspaceId;
		double x{};
		double y{};
	};

	[[nodiscard]] bool StatesShareInterest(
		const std::optional<StateScope>& a_left,
		const std::optional<StateScope>& a_right) noexcept;
}

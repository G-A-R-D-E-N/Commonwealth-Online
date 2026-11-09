#pragma once

#include "F4TWorkshopTransactions.h"

#include <cstdint>

namespace F4T::WorkshopUpkeep
{
	inline constexpr double kDecayFractionPerUnpaidHour{ 0.02 };

	struct UpkeepState
	{
		std::uint64_t paidUntilUnix{};
		bool decaying{};
	};

	[[nodiscard]] UpkeepState Evaluate(std::uint64_t a_nowUnix, std::uint64_t a_paidUntilUnix) noexcept;
	[[nodiscard]] std::uint64_t HourlyCostFromCounts(
		std::uint64_t a_woodPieces,
		std::uint64_t a_scrapPieces,
		std::uint64_t a_concretePieces,
		std::uint64_t a_reinforcedPieces) noexcept;
	void TickDecay(Workshop::ObjectState& a_object, bool a_unpaid, double a_dtHours) noexcept;
}

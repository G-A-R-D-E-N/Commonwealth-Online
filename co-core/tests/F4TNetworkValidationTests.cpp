#include "F4TNetworkValidation.h"

#include <cassert>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

int main()
{
	using namespace F4T::NetworkValidation;

	assert(IsValidCoordinate(0.0));
	assert(IsValidCoordinate(10'000'000.0));
	assert(!IsValidCoordinate(10'000'001.0));
	assert(!IsValidCoordinate(std::numeric_limits<double>::infinity()));

	assert(IsValidMovementSpeed(0.0));
	assert(IsValidMovementSpeed(100'000.0));
	assert(!IsValidMovementSpeed(-1.0));
	assert(!IsValidMovementSpeed(100'001.0));

	assert(IsValidHexFormId("0001A4D7", false, false));
	assert(IsValidHexFormId("", true, true));
	assert(!IsValidHexFormId("NOTNPC", false, false));
	assert(!IsValidHexFormId("00000000", false, false));

	const auto noon = ParseServerTimeHHmm("1200");
	assert(noon.has_value());
	assert(std::abs(*noon - 12.0F) < 0.001F);
	const auto afternoon = ParseServerTimeHHmm("1430");
	assert(afternoon.has_value());
	assert(std::abs(*afternoon - 14.5F) < 0.001F);
	assert(!ParseServerTimeHHmm("2360").has_value());
	assert(!ParseServerTimeHHmm("12;quit").has_value());

	assert(IsAllowedRemoteAction(1, "meleeattackStart"));
	assert(IsAllowedRemoteAction(2, "meleeattackStart"));
	assert(IsAllowedRemoteAction(3, "fireSingle"));
	assert(!IsAllowedRemoteAction(3, "arbitraryGraphEvent"));
	assert(!IsAllowedRemoteAction(99, "fireSingle"));

	assert(IsMovementTransitionType("teleport"));
	assert(IsMovementTransitionType("cell_change"));
	assert(IsMovementTransitionType("worldspace_change"));
	assert(IsMovementTransitionType("load"));
	assert(IsMovementTransitionType("spawn"));
	assert(IsMovementTransitionType("fast_travel"));
	assert(!IsMovementTransitionType("normal"));

	const MovementState origin{ 0.0, 0.0, 0.0, "0000003C", "0000003D", "normal" };
	const MovementState nearby{ 1000.0, 0.0, 0.0, "0000003C", "0000003D", "normal" };
	const MovementState tooFar{ 4000.0, 0.0, 0.0, "0000003C", "0000003D", "normal" };
	const MovementState otherCell{ 0.0, 0.0, 0.0, "0000003E", "0000003D", "normal" };
	const MovementState teleport{ 9'000'000.0, 0.0, 0.0, "0000003E", "0000003D", "teleport" };
	const MovementState respawn{ 9'000'000.0, 0.0, 0.0, "0000003E", "0000003D", "spawn" };
	const MovementTransitionBudget freshBudget{};

	const auto first = ValidateMovement(std::nullopt, std::nullopt, origin, 10.0, freshBudget);
	assert(first.Accepted());
	assert(first.verdict == MovementVerdict::AcceptedFirstTransform);

	const auto normal = ValidateMovement(origin, 10.0, nearby, 10.2, freshBudget);
	assert(normal.Accepted());
	assert(normal.verdict == MovementVerdict::AcceptedNormalMovement);
	assert(std::abs(normal.elapsedSeconds - 0.2) < 0.0001);
	assert(std::abs(normal.allowedDistance - 1012.0) < 0.0001);

	const auto excessive = ValidateMovement(origin, 10.0, tooFar, 10.2, freshBudget);
	assert(!excessive.Accepted());
	assert(excessive.verdict == MovementVerdict::RejectedDistance);

	const auto scopeChange = ValidateMovement(origin, 10.0, otherCell, 10.2, freshBudget);
	assert(!scopeChange.Accepted());
	assert(scopeChange.verdict == MovementVerdict::RejectedScopeChange);

	const auto explicitTransition = ValidateMovement(origin, 10.0, teleport, 10.2, freshBudget);
	assert(explicitTransition.Accepted());
	assert(explicitTransition.verdict == MovementVerdict::AcceptedExplicitTransition);

	const auto midSessionSpawn = ValidateMovement(origin, 10.0, respawn, 10.2, freshBudget);
	assert(!midSessionSpawn.Accepted());
	assert(midSessionSpawn.verdict == MovementVerdict::RejectedTransitionContext);

	MovementTransitionBudget spentBudget{};
	spentBudget.Record(10.0);
	const auto rapidTransition = ValidateMovement(origin, 10.0, teleport, 10.2, spentBudget);
	assert(!rapidTransition.Accepted());
	assert(rapidTransition.verdict == MovementVerdict::RejectedTransitionRate);

	const auto spacedTransition = ValidateMovement(origin, 10.0, teleport, 12.0, spentBudget);
	assert(spacedTransition.verdict == MovementVerdict::AcceptedExplicitTransition);

	MovementTransitionBudget burstBudget{};
	double stamp = 0.0;
	for (std::uint32_t index = 0; index < MaximumMovementTransitionsPerWindow; ++index) {
		stamp += MinimumMovementTransitionIntervalSeconds;
		assert(ValidateMovement(origin, stamp, teleport, stamp, burstBudget).verdict == MovementVerdict::AcceptedExplicitTransition);
		burstBudget.Record(stamp);
	}
	stamp += MinimumMovementTransitionIntervalSeconds;
	assert(ValidateMovement(origin, stamp, teleport, stamp, burstBudget).verdict == MovementVerdict::RejectedTransitionRate);

	const auto afterWindow = stamp + MovementTransitionWindowSeconds + 1.0;
	assert(ValidateMovement(origin, afterWindow, teleport, afterWindow, burstBudget).verdict == MovementVerdict::AcceptedExplicitTransition);

	const auto notANumber = std::numeric_limits<double>::quiet_NaN();
	assert(!ValidateMovement(std::nullopt, std::nullopt, origin, notANumber, freshBudget).Accepted());
	assert(!ValidateMovement(origin, 10.0, teleport, notANumber, freshBudget).Accepted());
	assert(!ValidateMovement(origin, 10.0, origin, std::numeric_limits<double>::infinity(), freshBudget).Accepted());
	assert(!ValidateMovement(origin, 10.0, origin, 9.0, freshBudget).Accepted());

	MovementTransitionBudget slidingBudget{};
	slidingBudget.Record(0.0);
	for (double second = 50.0; second <= 60.0; second += 1.0) {
		slidingBudget.Record(second);
	}
	assert(ValidateMovement(origin, 61.0, teleport, 61.0, slidingBudget).verdict == MovementVerdict::AcceptedExplicitTransition);
	slidingBudget.Record(61.0);
	assert(ValidateMovement(origin, 62.0, teleport, 62.0, slidingBudget).verdict == MovementVerdict::RejectedTransitionRate);

	const auto negativeElapsed = ValidateMovement(origin, 10.0, nearby, 9.0, freshBudget);
	assert(!negativeElapsed.Accepted());
	assert(negativeElapsed.verdict == MovementVerdict::RejectedTime);

	const MovementState longWindowMove{ 12'000.0, 0.0, 0.0, "0000003C", "0000003D", "normal" };
	const auto cappedElapsed = ValidateMovement(origin, 1.0, longWindowMove, 100.0, freshBudget);
	assert(cappedElapsed.Accepted());
	assert(cappedElapsed.elapsedSeconds == MaximumMovementValidationElapsedSeconds);
	assert(cappedElapsed.allowedDistance == MovementGraceDistance + MaximumNormalMovementSpeed * MaximumMovementValidationElapsedSeconds);

	std::vector<std::string> newCells;
	for (std::uint32_t index = 1; index <= 40; ++index) {
		newCells.push_back("0000" + std::to_string(1000U + index));
	}
	const auto stepCrossing = [](std::optional<MovementState>& a_previous, std::optional<double>& a_previousTime, MovementTransitionBudget& a_budget, const MovementState& a_entered, double a_now) {
		const auto result = ValidateMovement(a_previous, a_previousTime, a_entered, a_now, a_budget);
		if (result.Accepted()) {
			a_budget.RecordBoundaryCrossing(a_now, a_previous->cellId, a_entered.cellId);
			a_previous = a_entered;
			a_previousTime = a_now;
		}
		return result.verdict;
	};

	MovementTransitionBudget boundaryBudget{};
	boundaryBudget.Record(10.0);
	MovementTransitionBudget jitterBudget{};
	std::optional<MovementState> jitterPrevious = origin;
	std::optional<double> jitterPreviousTime = 10.0;
	for (std::uint32_t index = 1; index <= 200; ++index) {
		const auto now = 10.0 + 0.05 * index;
		const MovementState jitter{ 20.0 * index, 0.0, 0.0, index % 2U != 0U ? "0000003E" : "0000003C", "0000003D", "cell_change" };
		const auto verdict = stepCrossing(jitterPrevious, jitterPreviousTime, jitterBudget, jitter, now);
		assert(verdict == MovementVerdict::AcceptedBoundaryCrossing);
	}

	MovementTransitionBudget newCellBudget{};
	std::optional<MovementState> burstPrevious = origin;
	std::optional<double> burstPreviousTime = 10.0;
	std::uint32_t burstAccepted = 0;
	for (std::size_t index = 0; index < newCells.size(); ++index) {
		const auto now = 10.0 + 0.1 * static_cast<double>(index + 1);
		const MovementState entered{ 0.0, 0.0, 0.0, newCells[index], "0000003D", "cell_change" };
		const auto verdict = stepCrossing(burstPrevious, burstPreviousTime, newCellBudget, entered, now);
		if (verdict == MovementVerdict::AcceptedBoundaryCrossing) {
			++burstAccepted;
		} else {
			assert(verdict == MovementVerdict::RejectedTransitionRate);
		}
	}
	assert(burstAccepted >= 13 && burstAccepted <= 17);

	MovementTransitionBudget capBudget{};
	std::optional<MovementState> capPrevious = origin;
	std::optional<double> capPreviousTime = 10.0;
	for (std::size_t index = 0; index < 31; ++index) {
		const auto now = 10.0 + 0.5 * static_cast<double>(index + 1);
		const MovementState entered{ 0.0, 0.0, 0.0, newCells[index], "0000003D", "cell_change" };
		const auto verdict = stepCrossing(capPrevious, capPreviousTime, capBudget, entered, now);
		assert(verdict == (index < MaximumBoundaryCrossingsPerWindow ?
		       MovementVerdict::AcceptedBoundaryCrossing : MovementVerdict::RejectedTransitionRate));
	}

	MovementTransitionBudget walkBudget{};
	std::optional<MovementState> walkPrevious = origin;
	std::optional<double> walkPreviousTime = 10.0;
	for (std::size_t index = 0; index < 40; ++index) {
		const auto now = 10.0 + 3.0 * static_cast<double>(index + 1);
		const MovementState entered{ 0.0, 0.0, 0.0, newCells[index], "0000003D", "cell_change" };
		const auto verdict = stepCrossing(walkPrevious, walkPreviousTime, walkBudget, entered, now);
		assert(verdict == MovementVerdict::AcceptedBoundaryCrossing);
	}

	const MovementState boundaryJump{ 5000.0, 0.0, 0.0, "0000003E", "0000003D", "cell_change" };
	assert(ValidateMovement(origin, 10.0, boundaryJump, 10.05, freshBudget).verdict == MovementVerdict::AcceptedExplicitTransition);
	assert(ValidateMovement(origin, 10.0, boundaryJump, 10.05, boundaryBudget).verdict == MovementVerdict::RejectedTransitionRate);

	const MovementState otherWorldspaceCell{ 20.0, 0.0, 0.0, "0000003E", "0000003F", "cell_change" };
	assert(ValidateMovement(origin, 10.0, otherWorldspaceCell, 10.05, boundaryBudget).verdict == MovementVerdict::RejectedTransitionRate);
	const MovementState interiorOrigin{ 0.0, 0.0, 0.0, "0000004A", "", "normal" };
	const MovementState interiorDoor{ 20.0, 0.0, 0.0, "0000004B", "", "cell_change" };
	assert(ValidateMovement(interiorOrigin, 10.0, interiorDoor, 10.05, boundaryBudget).verdict == MovementVerdict::RejectedTransitionRate);

	const MovementState worldspaceChange{ 0.0, 0.0, 0.0, "0000003E", "0000003F", "worldspace_change" };
	MovementTransitionBudget worldspaceBudget{};
	assert(ValidateMovement(origin, 10.0, worldspaceChange, 10.2, worldspaceBudget).verdict == MovementVerdict::AcceptedExplicitTransition);
	worldspaceBudget.Record(10.2);
	assert(ValidateMovement(worldspaceChange, 10.2, worldspaceChange, 10.7, worldspaceBudget).verdict == MovementVerdict::RejectedTransitionRate);

	const MovementState untaggedJump{ 5000.0, 0.0, 0.0, "0000003C", "0000003D", "normal" };
	assert(ValidateMovement(origin, 10.0, untaggedJump, 10.05, freshBudget).verdict == MovementVerdict::RejectedDistance);

	const MovementState sprint{ 2500.0, 0.0, 0.0, "0000003C", "0000003D", "normal" };
	assert(ValidateMovement(origin, 10.0, sprint, 11.0, freshBudget).verdict == MovementVerdict::AcceptedNormalMovement);

	const MovementState recovered{ 0.0, 0.0, 0.0, newCells[31], "0000003D", "cell_change" };
	const auto recoveredVerdict = stepCrossing(capPrevious, capPreviousTime, capBudget, recovered, 71.0);
	assert(recoveredVerdict == MovementVerdict::AcceptedBoundaryCrossing);

	const MovementState cellB{ 0.0, 0.0, 0.0, "0000003E", "0000003D", "cell_change" };
	const MovementState cellA{ 0.0, 0.0, 0.0, "0000003C", "0000003D", "cell_change" };
	const MovementState cellC{ 0.0, 0.0, 0.0, "0000003F", "0000003D", "cell_change" };
	MovementTransitionBudget flipBudget{};
	std::optional<MovementState> flipPrevious = origin;
	std::optional<double> flipPreviousTime = 10.0;
	const auto flipToB = stepCrossing(flipPrevious, flipPreviousTime, flipBudget, cellB, 11.0);
	assert(flipToB == MovementVerdict::AcceptedBoundaryCrossing);
	assert(flipBudget.recentBoundaryCount == 1);
	const auto flipBackToA = stepCrossing(flipPrevious, flipPreviousTime, flipBudget, cellA, 11.1);
	assert(flipBackToA == MovementVerdict::AcceptedBoundaryCrossing);
	assert(flipBudget.recentBoundaryCount == 1);
	const auto flipToC = stepCrossing(flipPrevious, flipPreviousTime, flipBudget, cellC, 13.0);
	assert(flipToC == MovementVerdict::AcceptedBoundaryCrossing);
	assert(flipBudget.recentBoundaryCount == 2);

	MovementTransitionBudget resetBudget{};
	std::optional<MovementState> resetPrevious = origin;
	std::optional<double> resetPreviousTime = 10.0;
	const auto resetToB = stepCrossing(resetPrevious, resetPreviousTime, resetBudget, cellB, 11.0);
	assert(resetToB == MovementVerdict::AcceptedBoundaryCrossing);
	resetBudget.Record(12.0);
	const auto resetToA = stepCrossing(resetPrevious, resetPreviousTime, resetBudget, cellA, 13.0);
	assert(resetToA == MovementVerdict::AcceptedBoundaryCrossing);
	assert(resetBudget.recentBoundaryCount == 2);

	return 0;
}

#include "F4TNetworkValidation.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string>

namespace F4T::NetworkValidation
{
	bool IsValidCoordinate(double a_value)
	{
		return std::isfinite(a_value) &&
		       a_value >= -MaximumAbsoluteCoordinate &&
		       a_value <= MaximumAbsoluteCoordinate;
	}

	bool IsValidMovementSpeed(double a_value)
	{
		return std::isfinite(a_value) && a_value >= 0.0 && a_value <= MaximumMovementSpeed;
	}

	bool IsValidHexFormId(std::string_view a_value, bool a_allowEmpty, bool a_allowZero)
	{
		if (a_value.empty()) {
			return a_allowEmpty;
		}
		if (a_value.size() > 8) {
			return false;
		}

		std::uint32_t value = 0;
		const auto* begin = a_value.data();
		const auto* end = begin + a_value.size();
		const auto result = std::from_chars(begin, end, value, 16);
		if (result.ec != std::errc{} || result.ptr != end) {
			return false;
		}
		return a_allowZero || value != 0;
	}

	std::optional<float> ParseServerTimeHHmm(std::string_view a_value)
	{
		if (a_value.size() != 4) {
			return std::nullopt;
		}
		for (const auto ch : a_value) {
			if (ch < '0' || ch > '9') {
				return std::nullopt;
			}
		}

		int hours = 0;
		int minutes = 0;
		const auto* begin = a_value.data();
		const auto* split = begin + 2;
		const auto* end = begin + 4;
		const auto hourResult = std::from_chars(begin, split, hours);
		const auto minuteResult = std::from_chars(split, end, minutes);
		if (hourResult.ec != std::errc{} || hourResult.ptr != split ||
			minuteResult.ec != std::errc{} || minuteResult.ptr != end ||
			hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
			return std::nullopt;
		}

		return static_cast<float>(hours) + static_cast<float>(minutes) / 60.0F;
	}

	bool IsAllowedRemoteAction(std::uint32_t a_type, std::string_view a_eventName)
	{
		return (a_type == 1U || a_type == 2U) ?
		       a_eventName == "meleeattackStart" :
		       a_type == 3U && a_eventName == "fireSingle";
	}

	bool IsMovementTransitionType(std::string_view a_movementType) noexcept
	{
		return a_movementType == "teleport" ||
		       a_movementType == "cell_change" ||
		       a_movementType == "worldspace_change" ||
		       a_movementType == "load" ||
		       a_movementType == "spawn" ||
		       a_movementType == "fast_travel";
	}

	bool IsSessionStartMovementType(std::string_view a_movementType) noexcept
	{
		return a_movementType == "load" || a_movementType == "spawn";
	}

	namespace
	{
		template <std::size_t N>
		void PushRecent(std::array<double, N>& a_recent, std::uint32_t& a_count, double a_nowMonotonicSeconds) noexcept
		{
			if (a_count == N) {
				std::rotate(a_recent.begin(), a_recent.begin() + 1, a_recent.end());
				a_count -= 1;
			}
			a_recent[a_count++] = a_nowMonotonicSeconds;
		}

		template <std::size_t N>
		bool RateAllows(
			const std::array<double, N>& a_recent,
			std::uint32_t a_count,
			double a_nowMonotonicSeconds,
			double a_minimumIntervalSeconds) noexcept
		{
			if (a_count != 0) {
				const auto sinceLast = a_nowMonotonicSeconds - a_recent[a_count - 1];
				if (!(sinceLast >= a_minimumIntervalSeconds)) {
					return false;
				}
			}
			if (a_count < N) {
				return true;
			}
			return a_nowMonotonicSeconds - a_recent.front() > MovementTransitionWindowSeconds;
		}

		bool IsSameWorldspaceCellChange(const MovementState& a_previous, const MovementState& a_current) noexcept
		{
			return a_current.movementType == "cell_change" &&
			       !a_current.worldspaceId.empty() &&
			       a_previous.worldspaceId == a_current.worldspaceId;
		}
	}

	void MovementTransitionBudget::Record(double a_nowMonotonicSeconds) noexcept
	{
		PushRecent(recentTransitionSeconds, recentTransitionCount, a_nowMonotonicSeconds);
		leftBoundaryCellId.clear();
	}

	void MovementTransitionBudget::RecordBoundaryCrossing(double a_nowMonotonicSeconds, std::string_view a_leftCellId, std::string_view a_enteredCellId)
	{
		if (a_enteredCellId != leftBoundaryCellId) {
			PushRecent(recentBoundarySeconds, recentBoundaryCount, a_nowMonotonicSeconds);
		}
		leftBoundaryCellId.assign(a_leftCellId);
	}

	MovementValidationResult ValidateMovement(
		const std::optional<MovementState>& a_previous,
		const std::optional<double>& a_previousMonotonicSeconds,
		const MovementState& a_current,
		double a_nowMonotonicSeconds,
		const MovementTransitionBudget& a_transitions) noexcept
	{
		if (!std::isfinite(a_nowMonotonicSeconds)) {
			return { MovementVerdict::RejectedTime };
		}
		if (!a_previous || !a_previousMonotonicSeconds) {
			return { MovementVerdict::AcceptedFirstTransform };
		}
		if (a_nowMonotonicSeconds < *a_previousMonotonicSeconds) {
			return { MovementVerdict::RejectedTime };
		}

		auto elapsed = a_nowMonotonicSeconds - *a_previousMonotonicSeconds;
		if (!std::isfinite(elapsed) || elapsed < 0.0) {
			elapsed = 0.0;
		}
		elapsed = std::min(elapsed, MaximumMovementValidationElapsedSeconds);

		const auto dx = a_current.x - a_previous->x;
		const auto dy = a_current.y - a_previous->y;
		const auto dz = a_current.z - a_previous->z;
		const auto distance = std::sqrt(dx * dx + dy * dy + dz * dz);
		const auto allowed = MovementGraceDistance + MaximumNormalMovementSpeed * elapsed;
		const auto withinEnvelope = std::isfinite(distance) && distance <= allowed;
		const auto boundaryCrossing = withinEnvelope && IsSameWorldspaceCellChange(*a_previous, a_current);

		if (IsMovementTransitionType(a_current.movementType) && !boundaryCrossing) {
			if (IsSessionStartMovementType(a_current.movementType)) {
				return { MovementVerdict::RejectedTransitionContext };
			}
			if (!RateAllows(a_transitions.recentTransitionSeconds, a_transitions.recentTransitionCount, a_nowMonotonicSeconds, MinimumMovementTransitionIntervalSeconds)) {
				return { MovementVerdict::RejectedTransitionRate };
			}
			return { MovementVerdict::AcceptedExplicitTransition };
		}

		if (boundaryCrossing && a_previous->cellId != a_current.cellId) {
			const auto flipBack = a_current.cellId == a_transitions.leftBoundaryCellId;
			if (!flipBack && !RateAllows(a_transitions.recentBoundarySeconds, a_transitions.recentBoundaryCount, a_nowMonotonicSeconds, MinimumBoundaryCrossingIntervalSeconds)) {
				return { MovementVerdict::RejectedTransitionRate };
			}
			return { MovementVerdict::AcceptedBoundaryCrossing, distance, allowed, elapsed };
		}

		if (!boundaryCrossing &&
			(a_previous->cellId != a_current.cellId || a_previous->worldspaceId != a_current.worldspaceId)) {
			return { MovementVerdict::RejectedScopeChange };
		}

		const auto verdict = withinEnvelope ?
		                     MovementVerdict::AcceptedNormalMovement :
		                     MovementVerdict::RejectedDistance;
		return { verdict, distance, allowed, elapsed };
	}
}

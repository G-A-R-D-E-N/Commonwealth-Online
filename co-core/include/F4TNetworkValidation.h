#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::NetworkValidation
{
	inline constexpr double MaximumAbsoluteCoordinate = 10'000'000.0;
	inline constexpr double MaximumMovementSpeed = 100'000.0;
	inline constexpr double MaximumNormalMovementSpeed = 2'500.0;
	inline constexpr double MaximumActorHealth = 1'000'000.0;
	inline constexpr double MovementGraceDistance = 512.0;
	inline constexpr double MaximumMovementValidationElapsedSeconds = 5.0;
	inline constexpr double MinimumMovementTransitionIntervalSeconds = 1.0;
	inline constexpr double MovementTransitionWindowSeconds = 60.0;
	inline constexpr std::uint32_t MaximumMovementTransitionsPerWindow = 12;
	inline constexpr double MinimumBoundaryCrossingIntervalSeconds = 0.25;
	inline constexpr std::uint32_t MaximumBoundaryCrossingsPerWindow = 30;
	inline constexpr double MaximumWorldExtent = 393'216.0;
	inline constexpr std::uint32_t RuntimeFormIdBase = 0xFF000000U;

	struct MovementState
	{
		double x{};
		double y{};
		double z{};
		std::string_view cellId;
		std::string_view worldspaceId;
		std::string_view movementType{ "normal" };
	};

	struct MovementTransitionBudget
	{
		std::array<double, MaximumMovementTransitionsPerWindow> recentTransitionSeconds{};
		std::uint32_t recentTransitionCount{};
		std::array<double, MaximumBoundaryCrossingsPerWindow> recentBoundarySeconds{};
		std::uint32_t recentBoundaryCount{};
		std::string leftBoundaryCellId;

		void Record(double a_nowMonotonicSeconds) noexcept;
		void RecordBoundaryCrossing(double a_nowMonotonicSeconds, std::string_view a_leftCellId, std::string_view a_enteredCellId);
	};

	enum class MovementVerdict : std::uint8_t
	{
		AcceptedFirstTransform,
		AcceptedExplicitTransition,
		AcceptedBoundaryCrossing,
		AcceptedNormalMovement,
		RejectedScopeChange,
		RejectedDistance,
		RejectedTransitionContext,
		RejectedTransitionRate,
		RejectedTime,
		RejectedDestination
	};

	struct MovementValidationResult
	{
		MovementVerdict verdict{ MovementVerdict::AcceptedFirstTransform };
		double distance{};
		double allowedDistance{};
		double elapsedSeconds{};

		[[nodiscard]] bool Accepted() const noexcept
		{
			return verdict == MovementVerdict::AcceptedFirstTransform ||
			       verdict == MovementVerdict::AcceptedExplicitTransition ||
			       verdict == MovementVerdict::AcceptedBoundaryCrossing ||
			       verdict == MovementVerdict::AcceptedNormalMovement;
		}
	};

	bool IsValidCoordinate(double a_value);
	bool IsValidMovementSpeed(double a_value);
	bool IsValidHexFormId(std::string_view a_value, bool a_allowEmpty, bool a_allowZero);
	bool IsValidNpcSourceFormId(std::string_view a_value, bool a_runtimeNpc);
	inline bool IsValidNpcSource(const nlohmann::json& a_object, const std::string& a_source)
	{
		const auto found = a_object.find("runtimeNpc");
		if (found == a_object.end()) {
			return IsValidNpcSourceFormId(a_source, false);
		}
		return found->is_boolean() && IsValidNpcSourceFormId(a_source, found->get<bool>());
	}
	std::optional<float> ParseServerTimeHHmm(std::string_view a_value);
	bool IsAllowedRemoteAction(std::uint32_t a_type, std::string_view a_eventName);
	bool IsMovementTransitionType(std::string_view a_movementType) noexcept;
	bool IsSessionStartMovementType(std::string_view a_movementType) noexcept;
	MovementValidationResult ValidateMovement(
		const std::optional<MovementState>& a_previous,
		const std::optional<double>& a_previousMonotonicSeconds,
		const MovementState& a_current,
		double a_nowMonotonicSeconds,
		const MovementTransitionBudget& a_transitions) noexcept;
}

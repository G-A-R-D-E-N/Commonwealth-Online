#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace F4T::SettlementCapturePolicy
{
	inline constexpr float kDefaultCaptureRadius = 800.0F;

	struct Point
	{
		float x{};
		float y{};
		float z{};
	};

	struct Anchor
	{
		std::uint64_t buildZoneId{};
		bool hasWorkbench{};
		Point workbench{};
		float captureRadius{ kDefaultCaptureRadius };
	};

	[[nodiscard]] inline double SquaredDistance(const Point& a_left, const Point& a_right) noexcept
	{
		const auto dx = static_cast<double>(a_left.x) - static_cast<double>(a_right.x);
		const auto dy = static_cast<double>(a_left.y) - static_cast<double>(a_right.y);
		const auto dz = static_cast<double>(a_left.z) - static_cast<double>(a_right.z);
		return dx * dx + dy * dy + dz * dz;
	}

	[[nodiscard]] inline bool IsWithinCaptureRadius(const Point& a_player, const Anchor& a_anchor) noexcept
	{
		if (!a_anchor.hasWorkbench) {
			return false;
		}
		const auto radius = static_cast<double>(a_anchor.captureRadius);
		return SquaredDistance(a_player, a_anchor.workbench) <= radius * radius;
	}

	[[nodiscard]] inline std::optional<std::uint64_t> FindNearest(const Point& a_player, std::span<const Anchor> a_anchors) noexcept
	{
		std::optional<std::uint64_t> nearest;
		double nearestDistance{};
		for (const auto& anchor : a_anchors) {
			if (!IsWithinCaptureRadius(a_player, anchor)) {
				continue;
			}
			const auto distance = SquaredDistance(a_player, anchor.workbench);
			if (!nearest || distance < nearestDistance) {
				nearest = anchor.buildZoneId;
				nearestDistance = distance;
			}
		}
		return nearest;
	}

	[[nodiscard]] inline bool CanStartCapture(bool a_busy, std::string_view a_ownerProfileId, std::string_view a_selfProfileId) noexcept
	{
		return !a_busy && !a_selfProfileId.empty() && a_ownerProfileId != a_selfProfileId;
	}
}

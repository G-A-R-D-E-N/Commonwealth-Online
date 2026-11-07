#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::SettlementCapturePolicy
{
	inline constexpr float kDefaultCaptureRadius = 800.0F;
	inline constexpr float kWorkbenchPromptRadius = 500.0F;

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

	[[nodiscard]] inline bool IsAtWorkbench(const Point& a_player, const Anchor& a_anchor) noexcept
	{
		const auto radius = static_cast<double>(kWorkbenchPromptRadius);
		return a_anchor.hasWorkbench && SquaredDistance(a_player, a_anchor.workbench) <= radius * radius;
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

	[[nodiscard]] inline bool IsActiveStatus(std::string_view a_status) noexcept
	{
		return a_status == "started" || a_status == "holding" || a_status == "contested";
	}

	[[nodiscard]] inline bool IsStale(std::chrono::steady_clock::time_point a_since, std::chrono::steady_clock::time_point a_now, std::chrono::steady_clock::duration a_maxAge) noexcept
	{
		return a_now - a_since > a_maxAge;
	}

	template <typename Record>
	void CancelOnReject(std::map<std::uint64_t, Record>& a_records, std::uint64_t a_buildZoneId, std::string_view a_rejectStatus)
	{
		const auto found = a_records.find(a_buildZoneId);
		if (found != a_records.end() && IsActiveStatus(found->second.status) && a_rejectStatus != "vetoed") {
			found->second.status = "cancelled";
		}
	}

	template <typename Record>
	[[nodiscard]] std::vector<std::uint64_t> CancelStale(std::map<std::uint64_t, Record>& a_records, std::chrono::steady_clock::time_point a_now, std::chrono::steady_clock::duration a_maxAge)
	{
		std::vector<std::uint64_t> expired;
		for (auto& [buildZoneId, record] : a_records) {
			if (IsActiveStatus(record.status) && IsStale(record.receivedAt, a_now, a_maxAge)) {
				record.status = "cancelled";
				expired.push_back(buildZoneId);
			}
		}
		return expired;
	}
}

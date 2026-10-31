#pragma once

#include "F4TPlayerStateValidation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace F4T::PlayerVitals
{
	inline constexpr double kMinimumPublishIntervalSeconds = 0.5;
	inline constexpr double kRefreshIntervalSeconds = 2.0;
	// Wire level range must match co-core/src/validation/F4TPlayerStateValidation.cpp.
	inline constexpr std::int32_t kMinimumPlayerLevel = 1;
	inline constexpr std::int32_t kMaximumPlayerLevel = 65535;

	struct Vitals
	{
		std::int32_t healthPercent{};
		bool isDowned{ false };
		bool isDead{ false };
		std::optional<std::int32_t> level;

		bool operator==(const Vitals&) const = default;
	};

	struct PublishState
	{
		std::optional<Vitals> lastPublished;
		std::optional<double> lastPublishedSeconds;
	};

	[[nodiscard]] inline std::optional<std::int32_t> ComputeHealthPercent(float a_current, float a_maximum)
	{
		if (!std::isfinite(a_current) || !std::isfinite(a_maximum) || a_maximum <= 0.0F) {
			return std::nullopt;
		}
		const auto fraction = std::clamp(static_cast<double>(a_current) / static_cast<double>(a_maximum), 0.0, 1.0);
		return static_cast<std::int32_t>(std::lround(fraction * 100.0));
	}

	[[nodiscard]] inline bool ShouldPublish(PublishState& a_state, const Vitals& a_vitals, double a_nowSeconds)
	{
		if (!std::isfinite(a_nowSeconds)) {
			return false;
		}
		const bool changed = !a_state.lastPublished || !(a_vitals == *a_state.lastPublished);
		const auto elapsed = a_state.lastPublishedSeconds ?
			a_nowSeconds - *a_state.lastPublishedSeconds :
			kRefreshIntervalSeconds;
		if (elapsed < 0.0 || elapsed < kMinimumPublishIntervalSeconds) {
			return false;
		}
		if (!changed && elapsed < kRefreshIntervalSeconds) {
			return false;
		}
		a_state.lastPublished = a_vitals;
		a_state.lastPublishedSeconds = a_nowSeconds;
		return true;
	}

	[[nodiscard]] inline std::optional<std::int32_t> NormalizeLevel(std::int32_t a_level)
	{
		if (a_level < kMinimumPlayerLevel || a_level > kMaximumPlayerLevel) {
			return std::nullopt;
		}
		return a_level;
	}

	[[nodiscard]] inline bool IsLevelInRange(std::int32_t a_level)
	{
		return NormalizeLevel(a_level).has_value();
	}

	[[nodiscard]] inline std::optional<nlohmann::json> Encode(const Vitals& a_vitals)
	{
		nlohmann::json node{
			{ "healthPercent", a_vitals.healthPercent },
			{ "isDowned", a_vitals.isDowned },
			{ "isDead", a_vitals.isDead }
		};
		if (a_vitals.level && IsLevelInRange(*a_vitals.level)) {
			node["level"] = *a_vitals.level;
		}
		const auto normalized = PlayerStateValidation::NormalizePlayerState({ { "vitals", std::move(node) } });
		return normalized ? std::optional{ std::move((*normalized)["vitals"]) } : std::nullopt;
	}

	[[nodiscard]] inline std::optional<Vitals> Decode(const nlohmann::json& a_vitals)
	{
		const auto normalized = PlayerStateValidation::NormalizePlayerState({ { "vitals", a_vitals } });
		if (!normalized) {
			return std::nullopt;
		}
		const auto& clean = (*normalized)["vitals"];
		Vitals vitals{};
		vitals.healthPercent = static_cast<std::int32_t>(clean.at("healthPercent").get<std::int64_t>());
		vitals.isDowned = clean.value("isDowned", false);
		vitals.isDead = clean.value("isDead", false);
		if (const auto level = clean.find("level"); level != clean.end()) {
			vitals.level = static_cast<std::int32_t>(level->get<std::int64_t>());
		}
		return vitals;
	}

	bool SendVitalsPacket(const Vitals& a_vitals);
	void PublishTick();
	bool ApplyRemoteVitalsFromPacket(std::uint32_t a_playerId, const nlohmann::json& a_packet);
}

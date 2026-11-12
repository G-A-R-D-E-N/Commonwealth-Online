#pragma once

#include "F4TWorldPolicy.h"

#include <cstdio>
#include <string>
#include <vector>

namespace F4T::VatsPolicy
{
	inline constexpr std::string_view kMaxEngageDistanceSetting{ "fVATSMaxEngageDistance" };
	inline constexpr float kVanillaMaxEngageDistance{ 5000.0F };
	inline constexpr std::string_view kEnterMultSetting{ "fVATSTimeMultEnter" };
	inline constexpr std::string_view kExitMultSetting{ "fVATSTimeMultExit" };
	inline constexpr std::string_view kTargetSelectMultSetting{ "fVATSTimeMultTargetSelect" };
	inline constexpr std::string_view kMagicSlowdownMultSetting{ "fVATSPlayerMagicTimeSlowdownMult" };
	inline constexpr std::string_view kMovementEventsSetting{ "bVATSMovementEvents" };

	struct Snapshot
	{
		float maxEngageDistance{ kVanillaMaxEngageDistance };
		float enterMult{ 1.0F };
		float exitMult{ 1.0F };
		float targetSelectMult{ 1.0F };
		float magicSlowdownMult{ 1.0F };
		bool movementEvents{ true };
		bool valid{};

		[[nodiscard]] bool operator==(const Snapshot&) const noexcept = default;
	};

	[[nodiscard]] inline std::string FormatGsFloat(float a_value)
	{
		char text[32]{};
		std::snprintf(text, sizeof(text), "%.9g", static_cast<double>(a_value));
		return std::string{ text };
	}

	[[nodiscard]] inline float RestoreEngageDistance(const Snapshot& a_snapshot)
	{
		return a_snapshot.valid ? a_snapshot.maxEngageDistance : kVanillaMaxEngageDistance;
	}

	[[nodiscard]] inline std::vector<std::string> ApplyCommands(F4T::WorldPolicy::Vats a_vats, const Snapshot& a_snapshot)
	{
		using F4T::WorldPolicy::Vats;
		switch (a_vats) {
		case Vats::RealTime:
			return {
				"setgs " + std::string{ kMaxEngageDistanceSetting } + " " + FormatGsFloat(RestoreEngageDistance(a_snapshot)),
				"setgs " + std::string{ kEnterMultSetting } + " 1",
				"setgs " + std::string{ kExitMultSetting } + " 1",
				"setgs " + std::string{ kTargetSelectMultSetting } + " 1",
				"setgs " + std::string{ kMagicSlowdownMultSetting } + " 1",
				"setgs " + std::string{ kMovementEventsSetting } + " 1",
			};
		case Vats::Vanilla:
			return { "setgs " + std::string{ kMaxEngageDistanceSetting } + " " + FormatGsFloat(RestoreEngageDistance(a_snapshot)) };
		case Vats::Disabled:
		default:
			return { "setgs " + std::string{ kMaxEngageDistanceSetting } + " 0" };
		}
	}

	[[nodiscard]] inline std::vector<std::string> RestoreCommands(const Snapshot& a_snapshot)
	{
		if (!a_snapshot.valid) {
			return { "setgs " + std::string{ kMaxEngageDistanceSetting } + " " + FormatGsFloat(kVanillaMaxEngageDistance) };
		}
		return {
			"setgs " + std::string{ kMaxEngageDistanceSetting } + " " + FormatGsFloat(a_snapshot.maxEngageDistance),
			"setgs " + std::string{ kEnterMultSetting } + " " + FormatGsFloat(a_snapshot.enterMult),
			"setgs " + std::string{ kExitMultSetting } + " " + FormatGsFloat(a_snapshot.exitMult),
			"setgs " + std::string{ kTargetSelectMultSetting } + " " + FormatGsFloat(a_snapshot.targetSelectMult),
			"setgs " + std::string{ kMagicSlowdownMultSetting } + " " + FormatGsFloat(a_snapshot.magicSlowdownMult),
			"setgs " + std::string{ kMovementEventsSetting } + (a_snapshot.movementEvents ? " 1" : " 0"),
		};
	}
}

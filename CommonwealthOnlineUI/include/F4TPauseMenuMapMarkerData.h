#pragma once

#include <cstdint>

namespace F4T::PauseMenuMap
{
	struct ResolvedMarkerFields
	{
		std::uint8_t  flags{};
		std::uint8_t  originalFlags{};
		std::uint16_t type{};
		const char*   layout{ "members" };
	};

	ResolvedMarkerFields ResolveMarkerFields(const RE::MapMarkerData* a_data);
	bool IsMarkerInWorldSpace(RE::TESObjectREFR* a_marker, RE::TESWorldSpace* a_world);
	std::uint16_t OverrideTypeFromName(std::uint16_t a_type, const char* a_name);
	const char* SafeGetLocationName(RE::MapMarkerData* a_data);
}

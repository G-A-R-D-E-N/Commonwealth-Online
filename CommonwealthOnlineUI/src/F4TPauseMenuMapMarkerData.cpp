#include "pch.h"

#include "F4TPauseMenuMapMarkerData.h"

#include <Windows.h>

#include <cctype>
#include <string>

namespace F4T::PauseMenuMap
{
	namespace
	{
		bool IsPlausibleMarkerType(std::uint16_t a_type)
		{
			return a_type < static_cast<std::uint16_t>(RE::MARKER_TYPE::kCountTotal);
		}

		const char* CallGetLocationName(RE::MapMarkerData* a_data)
		{
			using func_t = const char* (*)(RE::MapMarkerData*);
			static REL::Relocation<func_t> func{ RE::ID::MapMarkerData::GetLocationName };
			return func(a_data);
		}
	}

	ResolvedMarkerFields ResolveMarkerFields(const RE::MapMarkerData* a_data)
	{
		ResolvedMarkerFields out{};
		if (!a_data) {
			return out;
		}

		const auto* raw = reinterpret_cast<const std::uint8_t*>(a_data);

		const auto flagsVtbl = raw[0x10];
		const auto originalVtbl = raw[0x11];
		const auto typeVtbl8 = static_cast<std::uint16_t>(raw[0x12]);
		const auto typeVtbl16 = *reinterpret_cast<const std::uint16_t*>(raw + 0x12);

		out.flags = flagsVtbl;
		out.originalFlags = originalVtbl;
		out.type = typeVtbl8;
		out.layout = "vtbl@12u8";

		if (typeVtbl8 == 0 && IsPlausibleMarkerType(typeVtbl16) && typeVtbl16 != 0) {
			out.type = typeVtbl16;
			out.layout = "vtbl@12u16";
		}

		return out;
	}

	bool IsMarkerInWorldSpace(RE::TESObjectREFR* a_marker, RE::TESWorldSpace* a_world)
	{
		if (!a_marker || !a_world) {
			return true;
		}

		const auto* cell = a_marker->GetParentCell();
		if (!cell) {
			return true;
		}
		if (cell->IsInterior()) {
			return false;
		}
		return cell->worldSpace == a_world;
	}

	std::uint16_t OverrideTypeFromName(std::uint16_t a_type, const char* a_name)
	{
		if (!a_name || !a_name[0]) {
			return a_type;
		}

		std::string lower{ a_name };
		for (char& ch : lower) {
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		}
		if (lower.find("vault") != std::string::npos) {
			return static_cast<std::uint16_t>(RE::MARKER_TYPE::kVault);
		}
		return a_type;
	}

	const char* SafeGetLocationName(RE::MapMarkerData* a_data)
	{
		if (!a_data) {
			return nullptr;
		}
		__try {
			return CallGetLocationName(a_data);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return nullptr;
		}
	}
}

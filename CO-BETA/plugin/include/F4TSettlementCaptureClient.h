#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace RE
{
	class TESObjectREFR;
}

namespace F4T::SettlementCaptureClient
{
	bool TryHandlePacket(std::string_view a_type, const nlohmann::json& a_packet);
	void Reset();
	void NoteLocalPosition(float a_x, float a_y, float a_z, const RE::TESObjectREFR& a_player);
	bool CannotBuildAtWorkbench(const RE::TESObjectREFR& a_workbench);
	[[nodiscard]] std::optional<std::array<float, 3>> WorkbenchPointOf(std::uint64_t a_buildZoneId);
	[[nodiscard]] std::optional<std::uint64_t> BuildZoneForWorkbench(const RE::TESObjectREFR& a_workbench);
	void Tick();
	void RefreshHud();
	std::string ZoneName(std::uint64_t a_buildZoneId);
	bool HandleAction(std::string_view a_actionJson);
	std::string WithCaptureState(std::string a_stateJson);
	[[nodiscard]] nlohmann::json MapCaptures();
}

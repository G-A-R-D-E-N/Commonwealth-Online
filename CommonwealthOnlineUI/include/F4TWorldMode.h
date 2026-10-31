#pragma once

#include <string>
#include <string_view>

namespace COUI::WorldMode
{
	inline constexpr std::string_view kCoopStory{ "coop-story" };
	inline constexpr std::string_view kFactionWar{ "faction-war" };
	inline constexpr std::string_view kInstancedStory{ "instanced-story" };
	inline constexpr std::string_view kFreeRoam{ "free-roam" };

	[[nodiscard]] std::string Normalize(std::string_view a_mode);
	[[nodiscard]] std::string_view Label(std::string_view a_mode) noexcept;
	void Set(std::string_view a_mode);
	void Push();
	[[nodiscard]] std::string ToJson();
}

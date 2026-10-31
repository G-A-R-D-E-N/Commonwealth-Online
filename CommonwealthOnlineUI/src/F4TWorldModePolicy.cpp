#include "F4TWorldMode.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace COUI::WorldMode
{
	namespace
	{
		struct Spelling
		{
			std::string_view text;
			std::string_view mode;
		};

		constexpr Spelling kSpellings[]{
			{ "coop-story", kCoopStory },
			{ "co-op story", kCoopStory },
			{ "co-op", kCoopStory },
			{ "faction-war", kFactionWar },
			{ "faction war", kFactionWar },
			{ "instanced-story", kInstancedStory },
			{ "instanced story", kInstancedStory },
			{ "free-roam", kFreeRoam },
			{ "free roam", kFreeRoam }
		};
	}

	std::string Normalize(std::string_view a_mode)
	{
		constexpr std::string_view whitespace{ " \t\n\r\v\f" };
		const auto first = a_mode.find_first_not_of(whitespace);
		if (first == std::string_view::npos) {
			return {};
		}
		const auto last = a_mode.find_last_not_of(whitespace);
		std::string candidate{ a_mode.substr(first, last - first + 1) };
		std::ranges::transform(candidate, candidate.begin(), [](unsigned char a_character) {
			return static_cast<char>(std::tolower(a_character));
		});
		for (const auto& spelling : kSpellings) {
			if (candidate == spelling.text) {
				return std::string{ spelling.mode };
			}
		}
		return {};
	}

	std::string_view Label(std::string_view a_mode) noexcept
	{
		if (a_mode == kFreeRoam) {
			return "Free roam";
		}
		if (a_mode == kFactionWar) {
			return "Faction war";
		}
		if (a_mode == kInstancedStory) {
			return "Instanced story";
		}
		if (a_mode == kCoopStory) {
			return "Co-op story";
		}
		return {};
	}
}

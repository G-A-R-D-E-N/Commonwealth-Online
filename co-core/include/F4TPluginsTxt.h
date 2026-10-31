#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::PluginsTxt
{
	inline constexpr std::size_t kMaximumFileBytes = 1024 * 1024;

	struct Line
	{
		std::string name;
		bool active{};
	};

	struct Activation
	{
		std::string text;
		std::vector<std::string> activated;
		std::vector<std::string> deactivated;
		bool reordered{};
	};

	[[nodiscard]] std::optional<std::vector<Line>> Parse(std::string_view a_text);

	[[nodiscard]] bool IsImplicitlyLoaded(std::string_view a_name, const std::vector<std::string>& a_creationClubNames);

	[[nodiscard]] std::optional<Activation> Activate(
		std::string_view a_current,
		const std::vector<std::string>& a_requiredOrder,
		const std::vector<std::string>& a_creationClubNames,
		const std::vector<std::string>& a_dataFileNames);
}

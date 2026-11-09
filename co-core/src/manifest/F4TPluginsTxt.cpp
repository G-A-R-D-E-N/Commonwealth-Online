#include "F4TPluginsTxt.h"

#include "F4TModpackContentPolicy.h"

#include <array>
#include <unordered_map>
#include <unordered_set>

namespace F4T::PluginsTxt
{
	namespace
	{
		using ModpackManifest::EqualsIgnoringAsciiCase;

		constexpr std::array<std::string_view, 8> kOfficialMasters{
			"Fallout4.esm",
			"DLCRobot.esm",
			"DLCworkshop01.esm",
			"DLCCoast.esm",
			"DLCworkshop02.esm",
			"DLCworkshop03.esm",
			"DLCNukaWorld.esm",
			"DLCUltraHighResolution.esm",
		};

		[[nodiscard]] std::string Lower(std::string_view a_text)
		{
			std::string lowered{ a_text };
			for (auto& character : lowered) {
				if (character >= 'A' && character <= 'Z') {
					character = static_cast<char>(character - 'A' + 'a');
				}
			}
			return lowered;
		}

		[[nodiscard]] std::string_view TrimRight(std::string_view a_text)
		{
			while (!a_text.empty() && (a_text.back() == '\r' || a_text.back() == ' ' || a_text.back() == '\t')) {
				a_text.remove_suffix(1);
			}
			return a_text;
		}

		[[nodiscard]] bool IsValidName(std::string_view a_name)
		{
			if (a_name.empty() || a_name.front() == '*' || a_name.front() == '#' || a_name.front() == ' ') {
				return false;
			}
			for (const auto character : a_name) {
				const auto byte = static_cast<unsigned char>(character);
				if (byte < 0x20 || byte == 0x7F) {
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] std::vector<std::string_view> SplitLines(std::string_view a_text)
		{
			std::vector<std::string_view> lines;
			std::size_t start = 0;
			while (start <= a_text.size()) {
				const auto end = a_text.find('\n', start);
				const auto line = a_text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
				lines.push_back(TrimRight(line));
				if (end == std::string_view::npos) {
					break;
				}
				start = end + 1;
			}
			return lines;
		}
	}

	std::optional<std::vector<Line>> Parse(std::string_view a_text)
	{
		if (a_text.size() > kMaximumFileBytes) {
			return std::nullopt;
		}
		if (a_text.starts_with("\xEF\xBB\xBF")) {
			a_text.remove_prefix(3);
		}
		std::vector<Line> lines;
		std::unordered_set<std::string> seen;
		for (const auto raw : SplitLines(a_text)) {
			if (raw.empty() || raw.front() == '#') {
				continue;
			}
			Line line;
			auto name = raw;
			if (name.front() == '*') {
				line.active = true;
				name.remove_prefix(1);
			}
			if (!IsValidName(name)) {
				return std::nullopt;
			}
			if (!seen.insert(Lower(name)).second) {
				continue;
			}
			line.name = std::string{ name };
			lines.push_back(std::move(line));
		}
		return lines;
	}

	bool IsImplicitlyLoaded(std::string_view a_name, const std::vector<std::string>& a_creationClubNames)
	{
		for (const auto official : kOfficialMasters) {
			if (EqualsIgnoringAsciiCase(a_name, official)) {
				return true;
			}
		}
		for (const auto& creationClub : a_creationClubNames) {
			if (EqualsIgnoringAsciiCase(a_name, creationClub)) {
				return true;
			}
		}
		return false;
	}

	std::optional<Activation> Activate(
		std::string_view a_current,
		const std::vector<std::string>& a_requiredOrder,
		const std::vector<std::string>& a_creationClubNames,
		const std::vector<std::string>& a_dataFileNames)
	{
		try {
			const auto current = Parse(a_current);
			if (!current) {
				return std::nullopt;
			}
			std::unordered_map<std::string, std::string> displayNames;
			for (const auto& name : a_dataFileNames) {
				displayNames.emplace(Lower(name), name);
			}
			std::unordered_map<std::string, const Line*> existing;
			for (const auto& line : *current) {
				existing.emplace(Lower(line.name), &line);
				displayNames.emplace(Lower(line.name), line.name);
			}

			Activation activation;
			std::string text;
			auto body = a_current;
			if (body.starts_with("\xEF\xBB\xBF")) {
				body.remove_prefix(3);
			}
			for (const auto raw : SplitLines(body)) {
				if (raw.empty() || raw.front() != '#') {
					break;
				}
				text += raw;
				text += "\r\n";
			}

			std::unordered_set<std::string> required;
			std::vector<std::string> requiredActive;
			for (const auto& name : a_requiredOrder) {
				if (!IsValidName(name)) {
					return std::nullopt;
				}
				const auto key = Lower(name);
				if (!required.insert(key).second || IsImplicitlyLoaded(name, a_creationClubNames)) {
					continue;
				}
				const auto display = displayNames.find(key);
				const auto& written = display != displayNames.end() ? display->second : name;
				text += '*';
				text += written;
				text += "\r\n";
				requiredActive.push_back(key);
				const auto before = existing.find(key);
				if (before == existing.end() || !before->second->active) {
					activation.activated.push_back(written);
				}
			}

			std::vector<std::string> previouslyActive;
			for (const auto& line : *current) {
				const auto key = Lower(line.name);
				if (required.contains(key)) {
					if (line.active) {
						previouslyActive.push_back(key);
					}
					continue;
				}
				if (IsImplicitlyLoaded(line.name, a_creationClubNames)) {
					continue;
				}
				text += line.name;
				text += "\r\n";
				if (line.active) {
					activation.deactivated.push_back(line.name);
				}
			}

			std::vector<std::string> requiredPreviouslyActive;
			std::unordered_set<std::string> previous(previouslyActive.begin(), previouslyActive.end());
			for (const auto& key : requiredActive) {
				if (previous.contains(key)) {
					requiredPreviouslyActive.push_back(key);
				}
			}
			activation.reordered = requiredPreviouslyActive != previouslyActive;
			activation.text = std::move(text);
			return activation;
		} catch (...) {
			return std::nullopt;
		}
	}
}

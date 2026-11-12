#include "F4TNexusSources.h"
#include "F4TContentManifest.h"
#include "F4TModpackContentPolicy.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>
#include <set>

namespace F4T::NexusSources
{
	std::optional<std::vector<Source>> Parse(std::string_view a_json, const ModpackManifest::Manifest& a_manifest)
	{
		if (a_json.size() > ModpackManifest::kMaximumManifestBytes) return std::nullopt;
		try {
			bool valid = true;
			std::vector<std::set<std::string>> keys;
			const auto scan = [&](int depth, nlohmann::json::parse_event_t event, nlohmann::json& value) {
				if (depth > 4) valid = false;
				if (event == nlohmann::json::parse_event_t::object_start) keys.emplace_back();
				if (event == nlohmann::json::parse_event_t::key &&
					(keys.empty() || !keys.back().insert(value.get<std::string>()).second)) valid = false;
				if (event == nlohmann::json::parse_event_t::object_end && !keys.empty()) keys.pop_back();
				return valid;
			};
			const auto root = nlohmann::json::parse(a_json, scan, false);
			if (!valid) return std::nullopt;
			if (!root.is_object() || root.size() != 2 || !root.contains("version") || !root["version"].is_number_unsigned() || root["version"] != 1 ||
				!root.contains("files") || !root["files"].is_array() || root["files"].size() > a_manifest.entries.size()) return std::nullopt;
			std::vector<Source> sources;
			std::set<std::string> seen;
			for (const auto& file : root["files"]) {
				if (!file.is_object() || file.size() != 3 || !file.contains("name") || !file["name"].is_string() ||
					!file.contains("modId") || !file["modId"].is_number_unsigned() ||
					!file.contains("fileId") || !file["fileId"].is_number_unsigned()) return std::nullopt;
				const auto& name = file["name"].get_ref<const std::string&>();
				const auto mod = file["modId"].get<std::uint64_t>();
				const auto id = file["fileId"].get<std::uint64_t>();
				const auto found = std::find_if(a_manifest.entries.begin(), a_manifest.entries.end(),
					[&](const auto& entry) { return ModpackManifest::EqualsIgnoringAsciiCase(entry.name, name); });
				if (found == a_manifest.entries.end() || !ContentManifest::IsValidFingerprint(found->sha256Hex) || !seen.insert(found->name).second || mod == 0 || id == 0 ||
					mod > std::numeric_limits<std::uint32_t>::max() || id > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
				sources.push_back({ found->name, static_cast<std::uint32_t>(mod), static_cast<std::uint32_t>(id) });
			}
			return sources;
		} catch (...) {
			return std::nullopt;
		}
	}

	std::string FilePage(const Source& a_source)
	{
		if (a_source.modId == 0 || a_source.fileId == 0) return {};
		return "https://www.nexusmods.com/fallout4/mods/" + std::to_string(a_source.modId) +
			"?tab=files#file-expander-header-" + std::to_string(a_source.fileId);
	}

	std::optional<Collection> ParseCollection(std::string_view a_slug, std::uint64_t a_revision)
	{
		if (a_slug.size() != 6 || a_revision == 0 || a_revision > std::numeric_limits<std::uint32_t>::max() ||
			!std::all_of(a_slug.begin(), a_slug.end(), [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); })) return std::nullopt;
		return Collection{ std::string{ a_slug }, static_cast<std::uint32_t>(a_revision) };
	}

	std::string CollectionPage(const Collection& a_collection)
	{
		if (!ParseCollection(a_collection.slug, a_collection.revision)) return {};
		return "https://next.nexusmods.com/fallout4/collections/" + a_collection.slug + "/revisions/" + std::to_string(a_collection.revision);
	}
}

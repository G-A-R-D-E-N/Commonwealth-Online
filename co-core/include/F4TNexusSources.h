#pragma once

#include "F4TModpackManifest.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::NexusSources
{
	struct Source
	{
		std::string name;
		std::uint32_t modId{};
		std::uint32_t fileId{};
	};
	struct Collection
	{
		std::string slug;
		std::uint32_t revision{};
	};

	[[nodiscard]] std::optional<std::vector<Source>> Parse(std::string_view a_json, const ModpackManifest::Manifest& a_manifest);
	[[nodiscard]] std::string FilePage(const Source& a_source);
	[[nodiscard]] std::optional<Collection> ParseCollection(std::string_view a_slug, std::uint64_t a_revision);
	[[nodiscard]] std::string CollectionPage(const Collection& a_collection);
}

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::ModpackManifest
{
	inline constexpr std::size_t kMaximumManifestBytes = 2 * 1024 * 1024;
	inline constexpr std::size_t kMaximumModpackNameBytes = 80;

	enum class EntryKind
	{
		plugin,
		native,
		config,
		archive
	};

	enum class PluginType
	{
		none,
		esm,
		esp,
		esl
	};

	struct Entry
	{
		EntryKind kind{ EntryKind::plugin };
		PluginType pluginType{ PluginType::none };
		std::string name;
		std::uint32_t order{};
		std::string sha256Hex;
		std::optional<std::uint64_t> sizeBytes;
	};

	struct Manifest
	{
		std::vector<Entry> entries;
	};

	struct ManifestDiff
	{
		std::vector<Entry> missing;
		std::vector<Entry> unexpected;
		std::vector<Entry> wrongContent;
		std::vector<Entry> wrongOrder;
		std::vector<Entry> wrongNative;
		std::vector<Entry> wrongConfig;
		std::vector<Entry> wrongArchive;
	};

	[[nodiscard]] std::optional<Manifest> NormalizeManifest(Manifest a_manifest);
	[[nodiscard]] std::optional<Manifest> ParseManifest(std::string_view a_json);
	[[nodiscard]] std::optional<std::string> SerializeManifest(const Manifest& a_manifest);
	[[nodiscard]] std::optional<std::string> ComputeFingerprint(const Manifest& a_manifest);
	[[nodiscard]] std::optional<std::string> MatchingFingerprint(const Manifest& a_required, const Manifest& a_client);
	[[nodiscard]] std::optional<ManifestDiff> DiffManifests(const Manifest& a_required, const Manifest& a_client);
	[[nodiscard]] std::string FormatMismatch(const ManifestDiff& a_diff);
	[[nodiscard]] bool IsValidModpackName(std::string_view a_name);
}

#include "F4TModpackProbeCache.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace F4T::ModpackProbeCache
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr std::uint64_t kSchemaVersion = 1;

		char LowerAscii(char a_character) noexcept
		{
			return a_character >= 'A' && a_character <= 'Z' ? static_cast<char>(a_character - 'A' + 'a') : a_character;
		}

		bool EqualsAsciiIgnoreCase(std::string_view a_left, std::string_view a_right) noexcept
		{
			if (a_left.size() != a_right.size()) {
				return false;
			}
			for (std::size_t index = 0; index < a_left.size(); ++index) {
				if (LowerAscii(a_left[index]) != LowerAscii(a_right[index])) {
					return false;
				}
			}
			return true;
		}

		bool IsLowerHex(std::string_view a_value, std::size_t a_length) noexcept
		{
			if (a_value.size() != a_length) {
				return false;
			}
			for (const char character : a_value) {
				if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) {
					return false;
				}
			}
			return true;
		}

		bool HasControlByte(std::string_view a_value) noexcept
		{
			for (const unsigned char character : a_value) {
				if (character < 0x20 || character == 0x7f) {
					return true;
				}
			}
			return false;
		}

		bool IsValidPath(std::string_view a_path) noexcept
		{
			if (a_path.empty() || a_path.size() > kMaximumPathBytes || HasControlByte(a_path) || a_path.front() == '/' || a_path.front() == '\\') {
				return false;
			}
			std::size_t cursor = 0;
			while (true) {
				const auto next = a_path.find('/', cursor);
				const auto segment = a_path.substr(cursor, next == std::string_view::npos ? std::string_view::npos : next - cursor);
				if (segment.empty() || segment == "." || segment == "..") {
					return false;
				}
				if (next == std::string_view::npos) {
					return true;
				}
				cursor = next + 1;
			}
		}

		bool IsValidPluginName(std::string_view a_name) noexcept
		{
			return !a_name.empty() && a_name.size() <= kMaximumPluginNameBytes && !HasControlByte(a_name) &&
				a_name.find('/') == std::string_view::npos && a_name.find('\\') == std::string_view::npos;
		}
	}

	std::optional<std::string> SerializeEntry(const Entry& a_entry)
	{
		try {
			if (!IsLowerHex(a_entry.fingerprint, 64) || a_entry.files.empty() || a_entry.files.size() > kMaximumFiles ||
				a_entry.loadedPlugins.empty() || a_entry.loadedPlugins.size() > kMaximumPlugins) {
				return std::nullopt;
			}
			Json files = Json::array();
			std::unordered_set<std::string> seenPaths;
			for (const auto& file : a_entry.files) {
				if (!IsValidPath(file.path) || file.sizeBytes == kMissingFileSize || !seenPaths.insert(file.path).second) {
					return std::nullopt;
				}
				files.push_back(Json{
					{ "path", file.path },
					{ "size", file.sizeBytes },
					{ "mtime", file.modifiedTicks }
				});
			}
			Json plugins = Json::array();
			std::unordered_set<std::string> seenPlugins;
			for (const auto& plugin : a_entry.loadedPlugins) {
				if (!IsValidPluginName(plugin) || !seenPlugins.insert(plugin).second) {
					return std::nullopt;
				}
				plugins.push_back(plugin);
			}
			const auto text = Json{
				{ "version", kSchemaVersion },
				{ "fingerprint", a_entry.fingerprint },
				{ "files", std::move(files) },
				{ "plugins", std::move(plugins) }
			}.dump();
			if (text.size() > kMaximumBlobBytes) {
				return std::nullopt;
			}
			return text;
		} catch (...) {
			return std::nullopt;
		}
	}

	std::optional<Entry> ParseEntry(std::string_view a_text)
	{
		try {
			if (a_text.empty() || a_text.size() > kMaximumBlobBytes) {
				return std::nullopt;
			}
			const auto root = Json::parse(a_text, nullptr, false);
			if (root.is_discarded() || !root.is_object()) {
				return std::nullopt;
			}
			const auto version = root.find("version");
			const auto fingerprint = root.find("fingerprint");
			const auto files = root.find("files");
			const auto plugins = root.find("plugins");
			if (version == root.end() || !version->is_number_unsigned() || version->get<std::uint64_t>() != kSchemaVersion ||
				fingerprint == root.end() || !fingerprint->is_string() ||
				files == root.end() || !files->is_array() || files->empty() || files->size() > kMaximumFiles ||
				plugins == root.end() || !plugins->is_array() || plugins->empty() || plugins->size() > kMaximumPlugins) {
				return std::nullopt;
			}
			Entry entry;
			entry.fingerprint = fingerprint->get<std::string>();
			if (!IsLowerHex(entry.fingerprint, 64)) {
				return std::nullopt;
			}
			std::unordered_set<std::string> seenPaths;
			for (const auto& item : *files) {
				if (!item.is_object()) {
					return std::nullopt;
				}
				const auto path = item.find("path");
				const auto size = item.find("size");
				const auto mtime = item.find("mtime");
				if (path == item.end() || !path->is_string() || size == item.end() || !size->is_number_unsigned() ||
					mtime == item.end() || !mtime->is_number_integer()) {
					return std::nullopt;
				}
				FileFact fact;
				fact.path = path->get<std::string>();
				fact.sizeBytes = size->get<std::uint64_t>();
				fact.modifiedTicks = mtime->get<std::int64_t>();
				if (!IsValidPath(fact.path) || fact.sizeBytes == kMissingFileSize || !seenPaths.insert(fact.path).second) {
					return std::nullopt;
				}
				entry.files.push_back(std::move(fact));
			}
			std::unordered_set<std::string> seenPlugins;
			for (const auto& item : *plugins) {
				if (!item.is_string()) {
					return std::nullopt;
				}
				auto plugin = item.get<std::string>();
				if (!IsValidPluginName(plugin) || !seenPlugins.insert(plugin).second) {
					return std::nullopt;
				}
				entry.loadedPlugins.push_back(std::move(plugin));
			}
			return entry;
		} catch (...) {
			return std::nullopt;
		}
	}

	Match Validate(
		const Entry& a_entry,
		const std::vector<FileFact>& a_currentFiles,
		const std::vector<std::string>& a_currentPlugins)
	{
		if (a_entry.fingerprint.empty() || a_entry.files.empty() || a_entry.loadedPlugins.empty()) {
			return Match::empty;
		}
		if (a_currentPlugins.size() != a_entry.loadedPlugins.size()) {
			return Match::changedPlugins;
		}
		for (std::size_t index = 0; index < a_currentPlugins.size(); ++index) {
			if (!EqualsAsciiIgnoreCase(a_currentPlugins[index], a_entry.loadedPlugins[index])) {
				return Match::changedPlugins;
			}
		}
		if (a_currentFiles.size() != a_entry.files.size()) {
			return Match::missingFile;
		}
		for (std::size_t index = 0; index < a_currentFiles.size(); ++index) {
			const auto& cached = a_entry.files[index];
			const auto& current = a_currentFiles[index];
			if (current.path != cached.path || current.sizeBytes == kMissingFileSize) {
				return Match::missingFile;
			}
			if (current.sizeBytes != cached.sizeBytes || current.modifiedTicks != cached.modifiedTicks) {
				return Match::changedFile;
			}
		}
		return Match::hit;
	}
}

#include "F4TModOrganizer.h"

#include "F4TModOrganizerHashCache.h"
#include "F4TModpackProbe.h"
#include "F4TModpackProbeBudget.h"
#include "F4TUtf8Path.h"
#include "F4TWindowsCaseFold.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>

namespace
{
	std::string NameKey(std::string_view a_text)
	{
		auto key = F4T::WindowsCaseFold::Key(a_text);
		if (!key) throw std::invalid_argument("An MO2 name has invalid UTF-8 or unsupported Windows case mapping");
		return std::move(*key);
	}

	std::string LowerAscii(std::string a_text)
	{
		for (auto& ch : a_text) {
			if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
		}
		return a_text;
	}

	std::string Trim(std::string a_text)
	{
		const auto first = a_text.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) return {};
		return a_text.substr(first, a_text.find_last_not_of(" \t\r\n") - first + 1);
	}

	std::optional<std::vector<std::string>> ReadLines(const std::filesystem::path& a_path)
	{
		std::error_code error;
		const auto size = std::filesystem::file_size(a_path, error);
		if (error || size > 16 * 1024 * 1024) return std::nullopt;
		std::ifstream input(a_path, std::ios::binary);
		if (!input) return std::nullopt;
		std::string text(static_cast<std::size_t>(size), '\0');
		if (size != 0 && !input.read(text.data(), static_cast<std::streamsize>(size))) return std::nullopt;
		if (input.peek() != std::char_traits<char>::eof() || input.bad()) return std::nullopt;
		std::istringstream stream(std::move(text));
		std::vector<std::string> lines;
		std::string line;
		while (std::getline(stream, line)) {
			if (lines.empty() && line.starts_with("\xef\xbb\xbf")) line.erase(0, 3);
			lines.push_back(Trim(std::move(line)));
		}
		if (stream.bad()) return std::nullopt;
		return lines;
	}

	void AppendUtf8(std::string& a_text, std::uint32_t a_code)
	{
		if (a_code <= 0x7f) a_text += static_cast<char>(a_code);
		else if (a_code <= 0x7ff) {
			a_text += static_cast<char>(0xc0 | (a_code >> 6));
			a_text += static_cast<char>(0x80 | (a_code & 0x3f));
		} else if (a_code <= 0xffff) {
			a_text += static_cast<char>(0xe0 | (a_code >> 12));
			a_text += static_cast<char>(0x80 | ((a_code >> 6) & 0x3f));
			a_text += static_cast<char>(0x80 | (a_code & 0x3f));
		} else {
			a_text += static_cast<char>(0xf0 | (a_code >> 18));
			a_text += static_cast<char>(0x80 | ((a_code >> 12) & 0x3f));
			a_text += static_cast<char>(0x80 | ((a_code >> 6) & 0x3f));
			a_text += static_cast<char>(0x80 | (a_code & 0x3f));
		}
	}

	bool IsUtf8(std::string_view a_text)
	{
		for (std::size_t i = 0; i < a_text.size();) {
			const auto lead = static_cast<unsigned char>(a_text[i]);
			if (lead < 0x80) { ++i; continue; }
			std::size_t length = 0;
			unsigned char low = 0x80;
			unsigned char high = 0xbf;
			if (lead >= 0xc2 && lead <= 0xdf) length = 2;
			else if (lead >= 0xe0 && lead <= 0xef) {
				length = 3;
				if (lead == 0xe0) low = 0xa0;
				if (lead == 0xed) high = 0x9f;
			} else if (lead >= 0xf0 && lead <= 0xf4) {
				length = 4;
				if (lead == 0xf0) low = 0x90;
				if (lead == 0xf4) high = 0x8f;
			} else return false;
			if (i + length > a_text.size()) return false;
			for (std::size_t k = 1; k < length; ++k) {
				const auto next = static_cast<unsigned char>(a_text[i + k]);
				if (next < (k == 1 ? low : 0x80) || next > (k == 1 ? high : 0xbf)) return false;
			}
			i += length;
		}
		return true;
	}

	std::string Windows1252ToUtf8(std::string_view a_text)
	{
		static constexpr std::array<std::uint32_t, 32> high{
			0x20ac, 0x81, 0x201a, 0x192, 0x201e, 0x2026, 0x2020, 0x2021, 0x2c6, 0x2030, 0x160, 0x2039, 0x152, 0x8d, 0x17d, 0x8f,
			0x90, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x2dc, 0x2122, 0x161, 0x203a, 0x153, 0x9d, 0x17e, 0x178 };
		std::string result;
		for (const char ch : a_text) {
			const auto byte = static_cast<unsigned char>(ch);
			AppendUtf8(result, byte >= 0x80 && byte <= 0x9f ? high[static_cast<std::size_t>(byte - 0x80)] : byte);
		}
		return result;
	}

	std::optional<std::vector<std::string>> ReadPluginLines(const std::filesystem::path& a_path)
	{
		auto lines = ReadLines(a_path);
		if (!lines) return std::nullopt;
		for (auto& line : *lines) if (!IsUtf8(line)) line = Windows1252ToUtf8(line);
		return lines;
	}

	bool SingleName(const std::string& a_name)
	{
		return !a_name.empty() && a_name != "." && a_name != ".." &&
			a_name.find_first_of("/\\:") == std::string::npos &&
			std::none_of(a_name.begin(), a_name.end(), [](unsigned char ch) { return ch < 0x20 || ch == 0x7f; });
	}

	std::optional<bool> IsLightPlugin(const std::filesystem::path& a_path, const std::string& a_name)
	{
		std::ifstream input(a_path, std::ios::binary);
		std::array<unsigned char, 12> header{};
		if (!input.read(reinterpret_cast<char*>(header.data()), header.size()) ||
			header[0] != 'T' || header[1] != 'E' || header[2] != 'S' || header[3] != '4') return std::nullopt;
		return LowerAscii(a_name).ends_with(".esl") || (header[9] & 0x02u) != 0;
	}

	std::optional<std::string> DecodeQtValue(std::string a_value)
	{
		if (a_value.size() >= 2 && a_value.front() == '"' && a_value.back() == '"') a_value = a_value.substr(1, a_value.size() - 2);
		const bool byteArray = a_value.starts_with("@ByteArray(") && a_value.ends_with(')');
		if (byteArray) a_value = a_value.substr(11, a_value.size() - 12);
		std::string result;
		const auto hex = [](char ch) -> int {
			if (ch >= '0' && ch <= '9') return ch - '0';
			if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
			if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
			return -1;
		};
		const auto unit = [&](std::size_t& pos) -> std::optional<std::uint32_t> {
			if (pos + 1 >= a_value.size() || a_value[pos] != '\\') return std::nullopt;
			const bool hexadecimal = a_value[pos + 1] == 'x';
			pos += hexadecimal ? 2 : 1;
			const auto start = pos;
			const std::uint32_t base = hexadecimal ? 16 : 8;
			std::uint32_t value = 0;
			while (pos < a_value.size()) {
				const auto digit = hex(a_value[pos]);
				if (digit < 0 || static_cast<std::uint32_t>(digit) >= base) break;
				if (value > 0xffff / base || value * base + static_cast<std::uint32_t>(digit) > 0xffff) return std::nullopt;
				value = value * base + static_cast<std::uint32_t>(digit);
				++pos;
			}
			if (pos == start) return std::nullopt;
			return value;
		};
		for (std::size_t i = 0; i < a_value.size();) {
			if (a_value[i] != '\\' || i + 1 == a_value.size()) { result += a_value[i++]; continue; }
			if (a_value[i + 1] == '\\') { result += '\\'; i += 2; continue; }
			if (a_value[i + 1] == '"' || a_value[i + 1] == '\'' || a_value[i + 1] == '?') { result += a_value[i + 1]; i += 2; continue; }
			if (a_value[i + 1] != 'x' && (a_value[i + 1] < '0' || a_value[i + 1] > '7')) { result += a_value[i++]; continue; }
			auto code = unit(i);
			if (!code) return std::nullopt;
			if (byteArray) {
				if (*code == 0 || *code > 255) return std::nullopt;
				result += static_cast<char>(*code);
				continue;
			}
			if (*code >= 0xd800 && *code <= 0xdbff) {
				const auto low = unit(i);
				if (!low || *low < 0xdc00 || *low > 0xdfff) return std::nullopt;
				*code = 0x10000 + ((*code - 0xd800) << 10) + (*low - 0xdc00);
			} else if (*code >= 0xdc00 && *code <= 0xdfff) return std::nullopt;
			if (*code == 0) return std::nullopt;
			AppendUtf8(result, *code);
		}
		return result;
	}

	std::optional<std::map<std::string, std::string>> ReadMo2Settings(const std::filesystem::path& a_instance)
	{
		const auto lines = ReadLines(a_instance / "ModOrganizer.ini");
		if (!lines) return std::nullopt;
		std::map<std::string, std::string> values;
		std::string section;
		for (const auto& line : *lines) {
			if (line.empty() || line[0] == '#' || line[0] == ';') continue;
			if (line.front() == '[' && line.back() == ']') { section = LowerAscii(line.substr(1, line.size() - 2)); continue; }
			const auto equal = line.find('=');
			if (equal == std::string::npos) continue;
			const auto key = section + "/" + LowerAscii(Trim(line.substr(0, equal)));
			if (key != "general/gamepath" && key != "general/gamename" && key != "settings/base_directory" &&
				key != "settings/mod_directory" && key != "settings/profiles_directory" && key != "settings/overwrite_directory" &&
				key != "settings/skip_file_suffixes" && key != "settings/skip_directories") continue;
			const auto raw = Trim(line.substr(equal + 1));
			const auto value = key == "settings/skip_file_suffixes" || key == "settings/skip_directories" ? std::optional{ raw } : DecodeQtValue(raw);
			if (!value || !values.emplace(key, *value).second) return std::nullopt;
		}
		return values;
	}

	std::optional<std::vector<std::string>> ReadFilters(const std::map<std::string, std::string>& a_settings,
		const std::string& a_key, std::vector<std::string> a_default)
	{
		const auto found = a_settings.find(a_key);
		if (found == a_settings.end()) return a_default;
		if (found->second.empty() || found->second == "@Invalid()") return std::vector<std::string>{};
		std::vector<std::string> result;
		std::string item;
		bool quoted = false;
		const auto append = [&]() {
			auto value = DecodeQtValue(Trim(std::move(item)));
			item.clear();
			if (!value) return false;
			if (value->starts_with("@@")) value->erase(0, 1);
			*value = Trim(std::move(*value));
			if (value->empty()) return true;
			if (!SingleName(*value)) return false;
			const auto key = F4T::WindowsCaseFold::Key(*value);
			if (!key) return false;
			result.push_back(*key);
			return true;
		};
		for (std::size_t i = 0; i < found->second.size(); ++i) {
			const char ch = found->second[i];
			if (ch == '\\' && i + 1 < found->second.size()) {
				item += ch;
				item += found->second[++i];
			} else if (ch == '"') {
				quoted = !quoted;
			} else if (ch == ',' && !quoted) {
				if (!append()) return std::nullopt;
			} else item += ch;
		}
		if (quoted || !append()) return std::nullopt;
		return result;
	}

	std::optional<std::filesystem::path> Mo2Directory(
		const std::map<std::string, std::string>& a_settings, const std::string& a_key,
		const std::filesystem::path& a_base, const std::string& a_default, [[maybe_unused]] std::string& a_error)
	{
		const auto found = a_settings.find(a_key);
		auto value = found == a_settings.end() ? a_default : found->second;
		if (value.empty() || value.find('\0') != std::string::npos) return std::nullopt;
		const auto marker = LowerAscii(value).find("%base_dir%");
		if (marker != std::string::npos) {
			if (marker != 0 || (value.size() > 10 && value[10] != '/' && value[10] != '\\')) return std::nullopt;
			value.erase(0, value.size() > 10 ? 11 : 10);
			return value.empty() ? a_base : a_base / F4T::Utf8Path::Decode(value);
		}
		if (value.find('%') != std::string::npos) return std::nullopt;
		auto path = F4T::Utf8Path::Decode(value);
#ifndef _WIN32
		const bool windowsText = (value.size() >= 2 && value[1] == ':' && std::isalpha(static_cast<unsigned char>(value[0]))) || value.find('\\') != std::string::npos;
		if (!path.is_absolute() && windowsText) {
			a_error = "MO2 setting " + a_key + " is a Windows path that must be mapped to a path on this server";
			return std::nullopt;
		}
#endif
		return path.is_absolute() ? path : a_base / path;
	}

	std::optional<std::filesystem::path> FindDirectory(
		const std::filesystem::path& a_parent, const std::string& a_name, std::string& a_error)
	{
		const auto key = NameKey(a_name);
		std::optional<std::filesystem::path> match;
		std::error_code error;
		std::size_t visited = 0;
		for (std::filesystem::directory_iterator it(a_parent, error), end; !error && it != end; it.increment(error)) {
			if (++visited > 1000000) { a_error = "The MO2 profile directory exceeds the scan limit"; return std::nullopt; }
			if (NameKey(F4T::Utf8Path::Encode(it->path().filename())) != key) continue;
			if (match) { a_error = "Ambiguous MO2 profile directory"; return std::nullopt; }
			match = it->path();
		}
		if (error || !match || !std::filesystem::is_directory(*match, error) || error) {
			a_error = "Cannot read the MO2 profile directory";
			return std::nullopt;
		}
		return match;
	}

	struct HashProgress
	{
		std::chrono::steady_clock::time_point lastReport = std::chrono::steady_clock::now();
		std::size_t files = 0;
		bool reported = false;
	};

	void ReportHashProgress(HashProgress& a_progress, std::uint64_t a_hashedBytes)
	{
		++a_progress.files;
		const auto now = std::chrono::steady_clock::now();
		if (now - a_progress.lastReport < std::chrono::seconds{ 5 }) return;
		a_progress.lastReport = now;
		a_progress.reported = true;
		char gib[32];
		std::snprintf(gib, sizeof(gib), "%.1f", static_cast<double>(a_hashedBytes) / (1024.0 * 1024.0 * 1024.0));
		std::cout << "MO2 modlist: hashed " << a_progress.files << " files, " << gib << " GiB so far" << std::endl;
	}

	F4T::ModpackProbe::FileHashResult HashWithCache(
		const std::filesystem::path& a_path,
		std::uint64_t a_maxBytes,
		std::chrono::steady_clock::time_point a_deadline,
		F4T::ModOrganizerHashCache::Cache& a_cache)
	{
		std::error_code error;
		const auto size = std::filesystem::file_size(a_path, error);
		if (error) return {};
		const auto modified = std::filesystem::last_write_time(a_path, error);
		if (error) return {};
		const auto ticks = static_cast<std::int64_t>(modified.time_since_epoch().count());
		std::error_code absoluteError;
		const auto absolute = std::filesystem::absolute(a_path, absoluteError);
		const auto generic = (absoluteError ? a_path : absolute).generic_u8string();
		const std::string key{ generic.begin(), generic.end() };
		if (const auto cached = a_cache.Find(key, size, ticks)) {
			if (size > a_maxBytes) return F4T::ModpackProbe::FileHashResult{ .status = F4T::ModpackProbe::FileHashStatus::limit };
			return F4T::ModpackProbe::FileHashResult{ F4T::ModpackProbe::FileHashStatus::complete, *cached, size, ticks };
		}
		auto hash = F4T::ModpackProbe::HashFile(a_path, a_maxBytes, a_deadline);
		if (hash.status == F4T::ModpackProbe::FileHashStatus::complete) a_cache.Put(key, hash.size, hash.modifiedTicks, hash.sha256);
		return hash;
	}
}

namespace F4T::ModOrganizer
{
	std::optional<std::filesystem::path> ResolveFile(
		const std::vector<std::filesystem::path>& a_layers,
		const std::filesystem::path& a_relativePath,
		std::string& a_error,
		std::chrono::steady_clock::time_point a_deadline,
		std::size_t* a_visited)
	{
		a_error.clear();
		if (a_relativePath.empty() || a_relativePath.is_absolute() || a_relativePath.has_root_name()) {
			a_error = "Invalid mod file path";
			return std::nullopt;
		}
		for (const auto& part : a_relativePath) {
			if (part == "." || part == ".." || part.empty()) {
				a_error = "Invalid mod file path";
				return std::nullopt;
			}
		}
		for (const auto& layer : a_layers) {
			std::error_code rootError;
			const auto canonicalRoot = std::filesystem::canonical(layer, rootError);
			if (rootError) {
				a_error = "Cannot read mod folder";
				return std::nullopt;
			}
			auto current = layer;
			bool missing = false;
			for (const auto& part : a_relativePath) {
				std::error_code error;
				std::optional<std::filesystem::path> match;
				const auto partKey = F4T::WindowsCaseFold::Key(F4T::Utf8Path::Encode(part));
				if (!partKey) { a_error = "Invalid or unsupported mod file name"; return std::nullopt; }
				for (std::filesystem::directory_iterator it(current, error), end; !error && it != end; it.increment(error)) {
					if (std::chrono::steady_clock::now() >= a_deadline || (a_visited && ++*a_visited > 50000000)) {
						a_error = "The MO2 profile exceeds the scan limit";
						return std::nullopt;
					}
					const auto candidateKey = F4T::WindowsCaseFold::Key(F4T::Utf8Path::Encode(it->path().filename()));
					if (!candidateKey) { a_error = "Invalid or unsupported mod file name"; return std::nullopt; }
					if (*candidateKey == *partKey) {
						if (match) {
							a_error = "Ambiguous mod file name";
							return std::nullopt;
						}
						match = it->path();
					}
				}
				if (error) {
					a_error = "Cannot read mod folder";
					return std::nullopt;
				}
				if (!match) {
					missing = true;
					break;
				}
				const auto status = std::filesystem::symlink_status(*match, error);
				if (error || std::filesystem::is_symlink(status) || std::filesystem::is_other(status)) {
					a_error = "Cannot use linked or unreadable mod file";
					return std::nullopt;
				}
				const auto canonicalMatch = std::filesystem::canonical(*match, error);
				if (error || std::mismatch(canonicalRoot.begin(), canonicalRoot.end(), canonicalMatch.begin(), canonicalMatch.end()).first != canonicalRoot.end()) {
					a_error = "Mod file escapes its source folder";
					return std::nullopt;
				}
				current = std::move(*match);
			}
			if (missing) {
				continue;
			}
			std::error_code error;
			if (!std::filesystem::is_regular_file(current, error) || error) {
				a_error = "Mod file is not a regular file";
				return std::nullopt;
			}
			return current;
		}
		a_error = "Mod file not found";
		return std::nullopt;
	}

	std::optional<ModpackManifest::Manifest> BuildPluginManifest(
		const std::filesystem::path& a_profile,
		const std::filesystem::path& a_mods,
		const std::filesystem::path& a_overwrite,
		const std::filesystem::path& a_data,
		const std::filesystem::path& a_creationClubList,
		std::string& a_error,
		const Filters& a_filters,
		const std::filesystem::path& a_hashCacheFile)
	{
		const auto fail = [&a_error](std::string a_message) -> std::optional<ModpackManifest::Manifest> {
			a_error = std::move(a_message);
			return std::nullopt;
		};
		try {
			auto filters = a_filters;
			for (auto& suffix : filters.suffixes) {
				if (!SingleName(suffix)) return fail("Invalid MO2 suffix filter");
				suffix = NameKey(suffix);
			}
			for (auto& directory : filters.directories) {
				if (!SingleName(directory)) return fail("Invalid MO2 directory filter");
				directory = NameKey(directory);
			}
			const auto modLines = ReadLines(a_profile / "modlist.txt");
			const auto enabledLines = ReadPluginLines(a_profile / "plugins.txt");
			const auto orderLines = ReadPluginLines(a_profile / "loadorder.txt");
			if (!modLines || !enabledLines || !orderLines) return fail("Cannot read the MO2 profile lists");
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{ ModpackManifest::kMaximumProbeMilliseconds };
			std::size_t visited = 0;
			std::vector<std::filesystem::path> layers;
			std::error_code ec;
			if (std::filesystem::is_directory(a_overwrite, ec) && !ec) layers.push_back(a_overwrite);
			else if (ec) return fail("Cannot read the MO2 overwrite folder");
			std::set<std::string> modNames;
			std::map<std::string, std::filesystem::path> modFolders;
			std::set<std::string> ambiguousFolders;
			bool indexedMods = false;
			for (const auto& line : *modLines) {
				if (line.empty() || line[0] != '+') continue;
				const auto name = line.substr(1);
				if (LowerAscii(name).ends_with("_separator")) continue;
				const auto key = NameKey(name);
				if (!SingleName(name) || !modNames.insert(key).second) return fail("Invalid or duplicate MO2 mod name");
				if (!indexedMods) {
					for (std::filesystem::directory_iterator it(a_mods, ec), end; !ec && it != end; it.increment(ec)) {
						if (++visited > 1000000 || std::chrono::steady_clock::now() >= deadline) return fail("The MO2 profile exceeds the scan limit");
						const auto folderKey = NameKey(F4T::Utf8Path::Encode(it->path().filename()));
						if (!modFolders.emplace(folderKey, it->path()).second) ambiguousFolders.insert(folderKey);
					}
					if (ec) return fail("Cannot read the MO2 mods directory");
					indexedMods = true;
				}
				if (ambiguousFolders.contains(key)) return fail("Ambiguous MO2 mod folder names");
				const auto folder = modFolders.find(key);
				if (folder == modFolders.end() || !std::filesystem::is_directory(folder->second, ec) || ec) return fail("An enabled MO2 mod folder cannot be read");
				layers.push_back(folder->second);
			}
			if (!std::filesystem::is_directory(a_data, ec) || ec) return fail("Cannot read the Fallout 4 Data folder");
			layers.push_back(a_data);
			std::set<std::string> enabled;
			for (const auto& line : *enabledLines) {
				if (line.empty() || line[0] != '*') continue;
				const auto name = Trim(line.substr(1));
				if (!SingleName(name) || !enabled.insert(NameKey(name)).second) return fail("Invalid or duplicate enabled plugin");
			}
			std::set<std::string> official;
			if (std::filesystem::exists(a_creationClubList, ec)) {
				const auto lines = ReadLines(a_creationClubList);
				if (!lines) return fail("Cannot read Fallout4.ccc");
				for (const auto& line : *lines) if (!line.empty() && line[0] != '#') official.insert(NameKey(line));
			} else if (ec) return fail("Cannot read Fallout4.ccc");
			ModpackManifest::Manifest manifest;
			std::vector<ModpackManifest::Entry> lightEntries;
			std::set<std::string> seen;
			std::uint64_t hashedBytes = 0;
			HashProgress progress;
			auto hashCache = a_hashCacheFile.empty() ? F4T::ModOrganizerHashCache::Cache{} : F4T::ModOrganizerHashCache::Cache::Load(a_hashCacheFile);
			std::vector<std::tuple<std::filesystem::path, std::uint64_t, std::filesystem::file_time_type>> fileFacts;
			std::size_t resolvedVisited = 0;
			for (auto name : *orderLines) {
				if (name.empty() || name[0] == '#') continue;
				if (name[0] == '*') name = Trim(name.substr(1));
				const auto lower = NameKey(name);
				if (!SingleName(name) || !seen.insert(lower).second) return fail("Invalid or duplicate plugin in MO2 load order");
				const bool base = lower == "fallout4.esm" || lower == "dlcrobot.esm" || lower == "dlccoast.esm" ||
					lower == "dlcnukaworld.esm" || lower == "dlcworkshop01.esm" || lower == "dlcworkshop02.esm" ||
					lower == "dlcworkshop03.esm" || lower == "dlcultrahighresolution.esm";
				const bool implicit = base || official.contains(lower);
				if (!implicit && !enabled.contains(lower)) continue;
				if (!lower.ends_with(".esp") && !lower.ends_with(".esm") && !lower.ends_with(".esl")) return fail("Invalid plugin extension in MO2 load order");
				if (std::any_of(filters.suffixes.begin(), filters.suffixes.end(), [&](const auto& suffix) { return lower.ends_with(suffix); })) return fail("An enabled plugin is hidden by the MO2 filters");
				const auto path = ResolveFile(layers, F4T::Utf8Path::Decode(name), a_error, deadline, &resolvedVisited);
				if (!path) return std::nullopt;
				const auto size = std::filesystem::file_size(*path, ec);
				if (ec) return fail("Cannot read an enabled plugin");
				const auto modified = std::filesystem::last_write_time(*path, ec);
				if (ec) return fail("Cannot read an enabled plugin");
				const auto light = IsLightPlugin(*path, name);
				if (!light) return fail("An enabled plugin has an invalid file header");
				ModpackManifest::Entry entry;
				entry.kind = ModpackManifest::EntryKind::plugin;
				entry.pluginType = ModpackProbe::PluginTypeFromName(name);
				entry.name = name;
				const bool gameFile = std::filesystem::equivalent(path->parent_path(), a_data, ec);
				if (ec) return fail("Cannot identify an enabled plugin source");
				if (implicit && gameFile) entry.sizeBytes = size;
				else {
					if (size > ModpackManifest::kMaximumProbeHashedBytes - hashedBytes) return fail("The MO2 plugins exceed the client hash limit");
					const auto hash = HashWithCache(*path, ModpackManifest::kMaximumProbeHashedBytes - hashedBytes, deadline, hashCache);
					if (hash.status != ModpackProbe::FileHashStatus::complete) return fail("Cannot hash an enabled plugin or the client hash limit was exceeded");
					hashedBytes += hash.size;
					ReportHashProgress(progress, hashedBytes);
					entry.sha256Hex = hash.sha256;
				}
				if (std::filesystem::file_size(*path, ec) != size || ec || std::filesystem::last_write_time(*path, ec) != modified || ec) return fail("An enabled plugin changed while being read");
				fileFacts.emplace_back(*path, size, modified);
				if (*light) lightEntries.push_back(std::move(entry));
				else manifest.entries.push_back(std::move(entry));
				if (manifest.entries.size() + lightEntries.size() > ModpackManifest::kMaximumProbeEntries) return fail("The MO2 profile exceeds the client plugin limit");
			}
			for (const auto& name : enabled) if (!seen.contains(name)) return fail("An enabled plugin is missing from the MO2 load order");
			for (auto& entry : lightEntries) manifest.entries.push_back(std::move(entry));
			for (std::size_t index = 0; index < manifest.entries.size(); ++index) manifest.entries[index].order = static_cast<std::uint32_t>(index);
			std::map<std::string, bool> winners;
			for (std::size_t layerIndex = 0; layerIndex + 1 < layers.size(); ++layerIndex) {
				std::set<std::string> layerNames;
				const auto canonicalRoot = std::filesystem::canonical(layers[layerIndex], ec);
				if (ec) return fail("Cannot read an MO2 mod folder");
				std::filesystem::recursive_directory_iterator it(layers[layerIndex], ec), end;
				if (ec) return fail("Cannot enumerate an MO2 mod folder");
				for (; it != end; it.increment(ec)) {
					if (ec) return fail("Cannot enumerate an MO2 mod folder");
					if (++visited > 1000000 || std::chrono::steady_clock::now() >= deadline) return fail("The MO2 profile exceeds the scan limit");
					const auto relative = it->path().lexically_relative(layers[layerIndex]);
					const auto generic = relative.generic_u8string();
					const std::string name(generic.begin(), generic.end());
					const auto lower = NameKey(name);
					const auto leaf = NameKey(F4T::Utf8Path::Encode(relative.filename()));
					const auto status = it->symlink_status(ec);
					if (ec || std::filesystem::is_symlink(status) || std::filesystem::is_other(status)) return fail("Cannot use linked or unreadable MO2 content");
					const auto canonicalPath = std::filesystem::canonical(it->path(), ec);
					if (ec || std::mismatch(canonicalRoot.begin(), canonicalRoot.end(), canonicalPath.begin(), canonicalPath.end()).first != canonicalRoot.end()) return fail("MO2 content escapes its source folder");
					const bool directory = std::filesystem::is_directory(status);
					const bool root = relative.parent_path().empty();
					if ((directory && (std::find(filters.directories.begin(), filters.directories.end(), leaf) != filters.directories.end() || (root && (leaf == "fomod" || leaf == "root" || leaf == "server")))) ||
						(!directory && ((root && (leaf == "meta.ini" || leaf == "readme.txt")) || std::any_of(filters.suffixes.begin(), filters.suffixes.end(), [&](const auto& suffix) { return leaf.ends_with(suffix); })))) {
						if (directory) it.disable_recursion_pending();
						continue;
					}
					if (!layerNames.insert(lower).second) return fail("Ambiguous MO2 content names");
					const auto existing = winners.find(lower);
					if (existing != winners.end()) {
						if (directory && !existing->second) it.disable_recursion_pending();
						continue;
					}
					winners.emplace(lower, directory);
					if (directory) continue;
					if (!std::filesystem::is_regular_file(status)) return fail("MO2 content is not a regular file");
					if (root && (lower.ends_with(".esp") || lower.ends_with(".esm") || lower.ends_with(".esl"))) continue;
					if (manifest.entries.size() >= ModpackManifest::kMaximumProbeEntries) return fail("The MO2 profile exceeds the client file limit");
					const auto hash = HashWithCache(it->path(), ModpackManifest::kMaximumProbeHashedBytes - hashedBytes, deadline, hashCache);
					if (hash.status != ModpackProbe::FileHashStatus::complete) return fail("Cannot hash MO2 content or the client hash limit was exceeded");
					hashedBytes += hash.size;
					ReportHashProgress(progress, hashedBytes);
					const auto modified = std::filesystem::last_write_time(it->path(), ec);
					if (ec || static_cast<std::int64_t>(modified.time_since_epoch().count()) != hash.modifiedTicks) return fail("MO2 content changed while being read");
					fileFacts.emplace_back(it->path(), hash.size, modified);
					ModpackManifest::Entry entry;
					entry.name = name;
					entry.sha256Hex = hash.sha256;
					if (lower.ends_with(".ba2")) { entry.kind = ModpackManifest::EntryKind::archive; entry.sizeBytes = hash.size; }
					else if (lower.starts_with("f4se/plugins/") && lower.ends_with(".dll")) entry.kind = ModpackManifest::EntryKind::native;
					else if (lower.ends_with(".ini")) entry.kind = ModpackManifest::EntryKind::config;
					else entry.kind = ModpackManifest::EntryKind::asset;
					manifest.entries.push_back(std::move(entry));
				}
				if (ec) return fail("Cannot enumerate an MO2 mod folder");
			}
			if (ReadLines(a_profile / "modlist.txt") != modLines || ReadPluginLines(a_profile / "plugins.txt") != enabledLines ||
				ReadPluginLines(a_profile / "loadorder.txt") != orderLines) return fail("The MO2 profile changed while being read");
			for (const auto& [path, size, modified] : fileFacts) {
				if (std::filesystem::file_size(path, ec) != size || ec || std::filesystem::last_write_time(path, ec) != modified || ec) return fail("MO2 content changed while being read");
			}
			const auto normalized = ModpackManifest::NormalizeManifest(std::move(manifest));
			if (!normalized) return fail("The MO2 profile did not produce a valid plugin manifest");
			if (progress.reported) std::cout << "MO2 modlist: done, " << progress.files << " files" << std::endl;
			if (!a_hashCacheFile.empty() && !hashCache.Save(a_hashCacheFile)) std::cerr << "MO2 modlist: could not save hash cache" << std::endl;
			a_error.clear();
			return normalized;
		} catch (const std::exception& exception) {
			return fail(exception.what());
		}
	}

	std::optional<ModpackManifest::Manifest> BuildProfileManifest(
		const std::filesystem::path& a_instance,
		const std::string& a_profileName,
		std::string& a_error,
		const std::filesystem::path& a_hashCacheFile)
	{
		try {
			if (!SingleName(a_profileName)) { a_error = "Invalid MO2 profile name"; return std::nullopt; }
			const auto settings = ReadMo2Settings(a_instance);
			if (!settings) { a_error = "Cannot read ModOrganizer.ini"; return std::nullopt; }
			const auto gameName = settings->find("general/gamename");
			if (gameName == settings->end() || LowerAscii(gameName->second) != "fallout 4") { a_error = "The MO2 instance is not configured for Fallout 4"; return std::nullopt; }
			std::string directoryError;
			const auto base = Mo2Directory(*settings, "settings/base_directory", a_instance, ".", directoryError);
			if (!base) { a_error = directoryError.empty() ? "Invalid MO2 base directory" : directoryError; return std::nullopt; }
			const auto mods = Mo2Directory(*settings, "settings/mod_directory", *base, "mods", directoryError);
			const auto profiles = Mo2Directory(*settings, "settings/profiles_directory", *base, "profiles", directoryError);
			const auto overwrite = Mo2Directory(*settings, "settings/overwrite_directory", *base, "overwrite", directoryError);
			const auto game = Mo2Directory(*settings, "general/gamepath", a_instance, "", directoryError);
			if (!mods || !profiles || !overwrite || !game) { a_error = directoryError.empty() ? "Invalid MO2 folder settings" : directoryError; return std::nullopt; }
			const auto suffixes = ReadFilters(*settings, "settings/skip_file_suffixes", { ".mohidden" });
			const auto directories = ReadFilters(*settings, "settings/skip_directories", { ".git" });
			if (!suffixes || !directories) { a_error = "Unsupported MO2 virtual filesystem filters"; return std::nullopt; }
			const auto profile = FindDirectory(*profiles, a_profileName, a_error);
			if (!profile) return std::nullopt;
			auto manifest = BuildPluginManifest(*profile, *mods, *overwrite, *game / "Data", *game / "Fallout4.ccc", a_error, Filters{ *suffixes, *directories }, a_hashCacheFile);
			if (manifest && ReadMo2Settings(a_instance) != settings) { a_error = "The MO2 settings changed while being read"; return std::nullopt; }
			return manifest;
		} catch (const std::exception&) {
			a_error = "Cannot read the MO2 instance";
			return std::nullopt;
		}
	}
}

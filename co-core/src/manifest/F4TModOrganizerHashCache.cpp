#include "F4TModOrganizerHashCache.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
	constexpr std::string_view kVersion = "co-mo2-hash-cache-v1";
	constexpr std::uintmax_t kMaximumFileBytes = 16ull * 1024 * 1024;
	constexpr std::size_t kMaximumEntries = 40000;

	bool Cacheable(std::string_view a_path)
	{
		return !a_path.empty() && a_path.find_first_of("\t\r\n") == std::string_view::npos;
	}

	bool ValidHash(std::string_view a_hash)
	{
		return a_hash.size() == 64 && std::all_of(a_hash.begin(), a_hash.end(), [](char a_ch) { return (a_ch >= '0' && a_ch <= '9') || (a_ch >= 'a' && a_ch <= 'f'); });
	}
}

namespace F4T::ModOrganizerHashCache
{
	Cache Cache::Load(const std::filesystem::path& a_file)
	{
		std::error_code error;
		const auto size = std::filesystem::file_size(a_file, error);
		if (error || size > kMaximumFileBytes) return {};
		std::ifstream input(a_file, std::ios::binary);
		if (!input) return {};
		std::istringstream stream(std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} });
		std::string line;
		if (!std::getline(stream, line) || line != kVersion) return {};
		Cache cache;
		std::size_t lines = 0;
		while (std::getline(stream, line)) {
			if (++lines > kMaximumEntries) return {};
			const auto first = line.find('\t');
			if (first == std::string::npos) continue;
			const auto second = line.find('\t', first + 1);
			if (second == std::string::npos) continue;
			const auto third = line.find('\t', second + 1);
			if (third == std::string::npos) continue;
			const std::string_view text{ line };
			const auto hash = text.substr(0, first);
			const auto sizeText = text.substr(first + 1, second - first - 1);
			const auto ticksText = text.substr(second + 1, third - second - 1);
			auto path = line.substr(third + 1);
			if (!ValidHash(hash) || !Cacheable(path)) continue;
			std::uint64_t parsedSize{};
			std::int64_t parsedTicks{};
			const auto sizeEnd = sizeText.data() + sizeText.size();
			const auto ticksEnd = ticksText.data() + ticksText.size();
			const auto sized = std::from_chars(sizeText.data(), sizeEnd, parsedSize);
			const auto timed = std::from_chars(ticksText.data(), ticksEnd, parsedTicks);
			if (sized.ec != std::errc{} || sized.ptr != sizeEnd || timed.ec != std::errc{} || timed.ptr != ticksEnd) continue;
			cache.entries_.insert_or_assign(path, Entry{ { path, parsedSize, parsedTicks }, std::string{ hash } });
		}
		return cache;
	}

	std::optional<std::string> Cache::Find(const std::string& a_path, std::uint64_t a_size, std::int64_t a_ticks) const
	{
		if (!Cacheable(a_path)) return std::nullopt;
		const auto found = entries_.find(a_path);
		if (found == entries_.end() || found->second.key.size != a_size || found->second.key.modifiedTicks != a_ticks) return std::nullopt;
		found->second.used = true;
		return found->second.sha256;
	}

	void Cache::Put(const std::string& a_path, std::uint64_t a_size, std::int64_t a_ticks, std::string a_sha256Hex)
	{
		if (!Cacheable(a_path)) return;
		entries_.insert_or_assign(a_path, Entry{ { a_path, a_size, a_ticks }, std::move(a_sha256Hex), true });
	}

	bool Cache::Save(const std::filesystem::path& a_file) const
	{
		std::size_t count = 0;
		for (const auto& item : entries_) if (item.second.used) ++count;
		if (count > kMaximumEntries) return false;
		std::ostringstream content;
		content << kVersion << '\n';
		for (const auto& [path, entry] : entries_) {
			if (entry.used) content << entry.sha256 << '\t' << entry.key.size << '\t' << entry.key.modifiedTicks << '\t' << path << '\n';
		}
		const auto text = content.str();
		if (text.size() > kMaximumFileBytes) return false;
		auto temporary = a_file;
		temporary += ".tmp";
		std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
		if (!output) return false;
		output << text;
		output.close();
		std::error_code error;
		if (output.fail()) {
			std::filesystem::remove(temporary, error);
			return false;
		}
		std::filesystem::rename(temporary, a_file, error);
		if (error) {
			std::filesystem::remove(temporary, error);
			return false;
		}
		return true;
	}
}

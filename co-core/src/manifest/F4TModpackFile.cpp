#include "F4TModpackProbe.h"

#include "F4TContentManifest.h"

#include <array>
#include <fstream>

namespace
{
	bool EndsWithIgnoreCase(std::string_view a_name, std::string_view a_suffix) noexcept
	{
		if (a_name.size() < a_suffix.size()) {
			return false;
		}
		const auto start = a_name.size() - a_suffix.size();
		for (std::size_t index = 0; index < a_suffix.size(); ++index) {
			auto character = a_name[start + index];
			if (character >= 'A' && character <= 'Z') {
				character = static_cast<char>(character - 'A' + 'a');
			}
			if (character != a_suffix[index]) {
				return false;
			}
		}
		return true;
	}
}

namespace F4T::ModpackProbe
{
	FileHashResult HashFile(
		const std::filesystem::path& a_path,
		std::uint64_t a_maxBytes,
		std::chrono::steady_clock::time_point a_deadline,
		const std::function<bool()>& a_cancelled,
		const std::function<void(std::uint64_t)>& a_progress)
	{
		const auto stopped = [&]() -> std::optional<FileHashStatus> {
			if (a_cancelled && a_cancelled()) return FileHashStatus::cancelled;
			if (std::chrono::steady_clock::now() >= a_deadline) return FileHashStatus::limit;
			return std::nullopt;
		};
		if (const auto status = stopped()) return FileHashResult{ .status = *status };
		std::error_code error;
		const auto size = std::filesystem::file_size(a_path, error);
		if (error) return {};
		if (size > a_maxBytes) return FileHashResult{ .status = FileHashStatus::limit };
		const auto modified = std::filesystem::last_write_time(a_path, error);
		if (error) return {};
		std::ifstream input(a_path, std::ios::binary);
		if (!input) return {};
		ContentManifest::Sha256Stream hasher;
		std::array<char, 1 << 16> buffer{};
		std::uint64_t bytes = 0;
		while (input) {
			if (const auto status = stopped()) return FileHashResult{ .status = *status };
			input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
			const auto count = input.gcount();
			if (count > 0) {
				bytes += static_cast<std::uint64_t>(count);
				if (bytes > size || bytes > a_maxBytes) return FileHashResult{ .status = FileHashStatus::changed };
				hasher.Update(std::string_view{ buffer.data(), static_cast<std::size_t>(count) });
				if (a_progress) a_progress(bytes);
			}
		}
		if (!input.eof()) return {};
		if (const auto status = stopped()) return FileHashResult{ .status = *status };
		if (bytes != size || std::filesystem::file_size(a_path, error) != size || error ||
			std::filesystem::last_write_time(a_path, error) != modified || error) return FileHashResult{ .status = FileHashStatus::changed };
		return FileHashResult{ FileHashStatus::complete, hasher.FinishHex(), bytes, static_cast<std::int64_t>(modified.time_since_epoch().count()) };
	}


	ModpackManifest::PluginType PluginTypeFromName(std::string_view a_name) noexcept
	{
		if (EndsWithIgnoreCase(a_name, ".esl")) {
			return ModpackManifest::PluginType::esl;
		}
		if (EndsWithIgnoreCase(a_name, ".esm")) {
			return ModpackManifest::PluginType::esm;
		}
		return ModpackManifest::PluginType::esp;
	}
}

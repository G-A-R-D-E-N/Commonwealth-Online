#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace CommonwealthOnline::Server
{
	class AdminTokenStore
	{
	public:
		using Generator = std::function<std::string()>;

		[[nodiscard]] static std::string LoadOrCreate(const std::filesystem::path& a_path, Generator a_generator);
		[[nodiscard]] static std::string LoadOrCreateHexKey(const std::filesystem::path& a_path, Generator a_generator);
		[[nodiscard]] static std::string Load(const std::filesystem::path& a_path);
		[[nodiscard]] static std::optional<std::string> TryLoad(const std::filesystem::path& a_path);
		static void Replace(const std::filesystem::path& a_path, const std::string& a_value);
#ifdef _WIN32
		[[nodiscard]] static bool VolumeEnforcesAcls(std::uint32_t a_fileSystemFlags) noexcept;
#endif
	};
}

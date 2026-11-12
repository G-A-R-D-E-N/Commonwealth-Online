#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::SteamHostLink
{
	inline constexpr std::string_view kBindCommand = "steam_bind";
	inline constexpr std::string_view kUnbindCommand = "steam_unbind";
	inline constexpr std::string_view kClearCommand = "steam_clear";
	inline constexpr std::string_view kHostCommand = "steam_host";
	inline constexpr std::string_view kPolicyCommand = "steam_policy";
	inline constexpr std::string_view kApplySettingsCommand = "apply_settings";
	inline constexpr std::string_view kServerConfigCommand = "server_config";
	inline constexpr std::size_t kMaximumBindings = 64;
	inline constexpr std::size_t kHostProofHexLength = 64;

	[[nodiscard]] inline std::optional<std::filesystem::path> Directory()
	{
		std::string base;
#ifdef _WIN32
		char* raw = nullptr;
		std::size_t length = 0;
		if (_dupenv_s(&raw, &length, "LOCALAPPDATA") == 0 && raw != nullptr) {
			base = raw;
		}
		std::free(raw);
#else
		if (const char* const raw = std::getenv("LOCALAPPDATA")) {
			base = raw;
		}
#endif
		if (base.empty()) {
			return std::nullopt;
		}
		return std::filesystem::path{ base } / "CommonwealthOnline" / "SteamHostLink";
	}

	[[nodiscard]] inline bool IsHostProofText(std::string_view a_text)
	{
		return a_text.size() == kHostProofHexLength && std::all_of(a_text.begin(), a_text.end(), [](char a_c) {
			return (a_c >= '0' && a_c <= '9') || (a_c >= 'a' && a_c <= 'f');
		});
	}

	[[nodiscard]] inline std::filesystem::path TokenPath(const std::filesystem::path& a_directory, std::uint16_t a_gamePort)
	{
		return a_directory / (std::to_string(a_gamePort) + ".token");
	}

	[[nodiscard]] inline std::filesystem::path PortPath(const std::filesystem::path& a_directory, std::uint16_t a_gamePort)
	{
		return a_directory / (std::to_string(a_gamePort) + ".port");
	}

	inline void RemoveLinkFiles(const std::filesystem::path& a_directory, std::uint16_t a_gamePort) noexcept
	{
		std::error_code ignored;
		std::filesystem::remove(PortPath(a_directory, a_gamePort), ignored);
		std::filesystem::remove(TokenPath(a_directory, a_gamePort), ignored);
	}
}

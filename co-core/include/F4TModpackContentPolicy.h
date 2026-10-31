#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::ModpackManifest
{
	inline constexpr std::size_t kMaximumAutoInstallNameBytes = 200;
	inline constexpr std::size_t kMinimumVoiceLanguageBytes = 2;
	inline constexpr std::size_t kMaximumVoiceLanguageBytes = 8;
	inline constexpr std::uint64_t kMaximumAutoInstallFileBytes = 4ull * 1024 * 1024 * 1024;
	inline constexpr std::uint64_t kMaximumAutoInstallTotalBytes = 16ull * 1024 * 1024 * 1024;
	inline constexpr std::size_t kMaximumAutoInstallFiles = 128;

	enum class AutoInstallRefusal
	{
		none,
		emptyName,
		nameTooLong,
		pathSeparator,
		forbiddenCharacter,
		leadingOrTrailingDotOrSpace,
		reservedDeviceName,
		unsupportedType,
		officialContent,
		creationClub,
		commonwealthOnlineContent
	};

	enum class AutoInstallFileKind
	{
		plugin,
		mainArchive,
		texturesArchive,
		voicesArchive,
		previsGeometry,
		precombinedGeometry,
		textureIndex
	};

	struct AutoInstallFile
	{
		AutoInstallFileKind kind{ AutoInstallFileKind::plugin };
		std::string pluginStem;
		std::string voiceLanguage;
	};

	[[nodiscard]] std::optional<AutoInstallFile> ClassifyAutoInstallName(std::string_view a_name);
	[[nodiscard]] AutoInstallRefusal CheckAutoInstallName(std::string_view a_name);
	[[nodiscard]] std::string_view DescribeAutoInstallRefusal(AutoInstallRefusal a_refusal) noexcept;
	[[nodiscard]] std::string_view DescribeAutoInstallFileKind(AutoInstallFileKind a_kind) noexcept;
	[[nodiscard]] bool EqualsIgnoringAsciiCase(std::string_view a_left, std::string_view a_right) noexcept;
}

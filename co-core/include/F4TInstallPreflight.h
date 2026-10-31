#pragma once

#include "F4TClientCompatibility.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::InstallPreflight
{
	enum class CheckId : std::uint8_t
	{
		GameDirectory = 1,
		Fallout4Executable = 2,
		RuntimeSupported = 3,
		F4seLoader = 4,
		PluginsDirectory = 5,
		AddressLibrary = 6,
		RequiredFile = 7,
		RequiredFileHashes = 8
	};

	enum class FixTarget : std::uint8_t
	{
		F4se = 1,
		AddressLibrary = 2,
		CoInstall = 3
	};

	inline constexpr std::string_view kF4seFixUrl = "https://f4se.silverlock.org/";
	inline constexpr std::string_view kAddressLibraryFixUrl = "https://www.nexusmods.com/fallout4/mods/47327";
	inline constexpr std::string_view kCoInstallFixUrl =
		"https://git.nomadicinteractive.dev/GARDEN/CommonwealthOnline-Matrix/src/branch/nomad-dev/CO-BETA/docs/install-and-migration.md";

	[[nodiscard]] std::string_view FixUrl(FixTarget a_target) noexcept;

	// One observed file in the game's Data tree. Paths are forward-slashed and
	// relative to Data, matching the content manifest's own path spelling.
	struct FileFact
	{
		std::string relativePath;
		bool present{};
		std::string sha256Hex;
	};

	// One hash the shipped package recorded for a required file.
	struct ExpectedFile
	{
		std::string relativePath;
		std::string sha256Hex;
	};

	// Observed install facts only: the checker never touches the filesystem, so a
	// caller (CLI tool, installer, test) collects these and hands them over.
	struct InstallProbe
	{
		std::string gameDirectory;
		bool gameDirectoryExists{};
		bool fallout4ExecutableExists{};
		std::string fallout4ExecutableVersion;
		bool f4seLoaderExists{};
		bool pluginsDirectoryExists{};
		std::string addressLibraryFileName;
		std::vector<std::string> otherAddressLibraryBins;
		std::vector<FileFact> files;
		std::vector<ExpectedFile> expectedHashes;
		// True when expectedHashes came from a shipped manifest; a missing entry fails.
		bool hashesSupplied{};
	};

	struct Check
	{
		CheckId id{ CheckId::GameDirectory };
		std::string subject;
		bool ok{};
		std::string message;
		std::string fixUrl;
	};

	struct Report
	{
		bool ok{};
		std::vector<Check> checks;
	};

	inline constexpr std::size_t kWin32MaxPathLength = 260;

	// Win32 file APIs need the \\?\ (or \\?\UNC\) prefix at kWin32MaxPathLength characters.
	[[nodiscard]] std::wstring LongPathForWin32(std::wstring a_path);

	// Fallout4.exe carries a four-part file version ("1.11.240.0"); a trailing
	// ".0" revision is dropped so the three supported runtime versions match.
	[[nodiscard]] std::string NormalizeRuntimeVersion(std::string_view a_version);

	[[nodiscard]] std::string_view AddressLibraryBinFor(F4T::ClientCompatibility::BuildTarget a_target) noexcept;

	// Required Commonwealth Online files for one runtime, relative to Data.
	[[nodiscard]] std::vector<std::string> RequiredPluginPaths(F4T::ClientCompatibility::BuildTarget a_target);

	[[nodiscard]] std::vector<std::string_view> SupportedRuntimeVersions();

	[[nodiscard]] Report Evaluate(const InstallProbe& a_probe);
}

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <winver.h>

#include "F4TContentManifest.h"
#include "F4TInstallPreflight.h"
#include "F4TModpackManifest.h"

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
	using F4T::ClientCompatibility::BuildTarget;
	using F4T::InstallPreflight::ExpectedFile;
	using F4T::InstallPreflight::FileFact;
	using F4T::InstallPreflight::InstallProbe;

	constexpr std::string_view kExecutableName = "Fallout4.exe";
	constexpr std::string_view kLoaderName = "f4se_loader.exe";

	struct Options
	{
		std::filesystem::path gameDirectory;
		std::filesystem::path expectedManifest;
	};

	// Reported bin plus the other known bins beside it, so a mismatch can list what is installed.
	struct AddressLibraryDetection
	{
		std::string fileName;
		std::vector<std::string> otherBins;
	};

	void PrintUsage(std::ostream& a_out)
	{
		a_out <<
			"Usage: F4TInstallPreflightTool --game-dir <path> [--expected-manifest <path>]\n"
			"\n"
			"Read-only pre-flight check of a Fallout 4 install against the supported runtimes\n"
			"1.10.163 (OG), 1.10.984 (NG) and 1.11.240 (AE). Probes the game directory, reads\n"
			"Fallout4.exe's file version, looks for f4se_loader.exe, Data\\F4SE\\Plugins, the\n"
			"Address Library bin for the detected runtime and the required Commonwealth Online\n"
			"files (hashed with SHA-256), then prints one line per check.\n"
			"Address Library bin names are from the documentation (docs/proof/native-character-setup-migration.md).\n"
			"\n"
			"--expected-manifest <path> names a modpack-manifest JSON (schema version 1) whose\n"
			"\"native\" entries supply the expected SHA-256 for a required file path, e.g.\n"
			"  {\"version\":1,\"entries\":[{\"kind\":\"native\",\"name\":\"F4SE/Plugins/\n"
			"  CommonwealthOnline.dll\",\"sha256\":\"<64 hex>\"}]}\n"
			"When a manifest is supplied every required file must carry an entry: a missing\n"
			"entry fails the check. Without one the files are only checked for presence and the\n"
			"report says the hashes were not verified.\n"
			"\n"
			"Nothing is written or launched; the probe only reads the install directory.\n"
			"\n"
			"Exit status: 0 when every check passes, 1 on a failed check, 2 on bad arguments.\n";
	}

	bool ParseOptions(int a_argc, char** a_argv, Options& a_options)
	{
		for (int index = 1; index < a_argc; ++index) {
			const std::string_view arg{ a_argv[index] };
			if (arg == "--help" || arg == "-h") {
				PrintUsage(std::cout);
				std::exit(0);
			}
			if (arg == "--game-dir") {
				if (index + 1 >= a_argc) {
					return false;
				}
				a_options.gameDirectory = a_argv[++index];
				continue;
			}
			if (arg == "--expected-manifest") {
				if (index + 1 >= a_argc) {
					return false;
				}
				a_options.expectedManifest = a_argv[++index];
				continue;
			}
			return false;
		}
		return !a_options.gameDirectory.empty();
	}

	std::string ToUtf8(const std::filesystem::path& a_path)
	{
		const auto utf8 = a_path.u8string();
		std::string text;
		text.reserve(utf8.size());
		for (const auto character : utf8) {
			text.push_back(static_cast<char>(character));
		}
		return text;
	}

	std::wstring VersionQueryPath(const std::filesystem::path& a_path)
	{
		std::error_code error;
		auto normalized = std::filesystem::absolute(a_path, error);
		if (error) {
			return a_path.wstring();
		}
		normalized = normalized.lexically_normal();
		normalized.make_preferred();
		return F4T::InstallPreflight::LongPathForWin32(normalized.wstring());
	}

	std::string ReadFileVersion(const std::filesystem::path& a_path)
	{
		const std::wstring wide = VersionQueryPath(a_path);
		DWORD handle = 0;
		const DWORD size = GetFileVersionInfoSizeW(wide.c_str(), &handle);
		if (size == 0) {
			return {};
		}
		std::vector<unsigned char> block(size);
		if (GetFileVersionInfoW(wide.c_str(), 0, size, block.data()) == FALSE) {
			return {};
		}
		VS_FIXEDFILEINFO* info = nullptr;
		UINT length = 0;
		if (VerQueryValueW(block.data(), L"\\", reinterpret_cast<LPVOID*>(&info), &length) == FALSE ||
		    info == nullptr || length < sizeof(VS_FIXEDFILEINFO)) {
			return {};
		}
		std::ostringstream text;
		text << HIWORD(info->dwFileVersionMS) << '.' << LOWORD(info->dwFileVersionMS) << '.'
		     << HIWORD(info->dwFileVersionLS) << '.' << LOWORD(info->dwFileVersionLS);
		return text.str();
	}

	std::optional<BuildTarget> TargetOf(const InstallProbe& a_probe)
	{
		return F4T::ClientCompatibility::TargetFromRuntime(
			F4T::InstallPreflight::NormalizeRuntimeVersion(a_probe.fallout4ExecutableVersion));
	}

	std::vector<std::string> KnownAddressLibraryBins()
	{
		std::vector<std::string> bins;
		for (const auto runtime : F4T::InstallPreflight::SupportedRuntimeVersions()) {
			const auto target = F4T::ClientCompatibility::TargetFromRuntime(runtime);
			if (target) {
				bins.push_back(std::string{ F4T::InstallPreflight::AddressLibraryBinFor(*target) });
			}
		}
		return bins;
	}

	bool EqualsIgnoreCase(std::string_view a_left, std::string_view a_right)
	{
		if (a_left.size() != a_right.size()) {
			return false;
		}
		for (std::size_t index = 0; index < a_left.size(); ++index) {
			const auto left = static_cast<unsigned char>(a_left[index]);
			const auto right = static_cast<unsigned char>(a_right[index]);
			if (std::tolower(left) != std::tolower(right)) {
				return false;
			}
		}
		return true;
	}

	AddressLibraryDetection DetectAddressLibraryBin(const std::filesystem::path& a_pluginsDirectory, bool a_pluginsDirectoryExists, std::optional<BuildTarget> a_target)
	{
		AddressLibraryDetection detection;
		if (!a_pluginsDirectoryExists) {
			return detection;
		}
		std::error_code error;
		std::vector<std::string> present;
		for (const auto& bin : KnownAddressLibraryBins()) {
			if (std::filesystem::is_regular_file(a_pluginsDirectory / bin, error) && !error) {
				present.push_back(bin);
			}
			error.clear();
		}
		if (present.empty()) {
			return detection;
		}
		if (a_target) {
			const auto expected = std::string{ F4T::InstallPreflight::AddressLibraryBinFor(*a_target) };
			for (const auto& bin : present) {
				if (bin == expected) {
					detection.fileName = bin;
					break;
				}
			}
		}
		if (detection.fileName.empty()) {
			detection.fileName = present.front();
		}
		for (const auto& bin : present) {
			if (bin != detection.fileName) {
				detection.otherBins.push_back(bin);
			}
		}
		return detection;
	}

	std::optional<std::vector<ExpectedFile>> ReadExpectedHashes(const std::filesystem::path& a_manifestPath, const std::vector<std::string>& a_requiredPaths)
	{
		std::ifstream input(a_manifestPath, std::ios::binary);
		if (!input) {
			std::cerr << "error: cannot read --expected-manifest " << ToUtf8(a_manifestPath) << "\n";
			return std::nullopt;
		}
		std::ostringstream text;
		text << input.rdbuf();
		const auto manifest = F4T::ModpackManifest::ParseManifest(text.str());
		if (!manifest) {
			std::cerr << "error: --expected-manifest is not a valid modpack manifest (schema version 1)\n";
			return std::nullopt;
		}

		std::vector<ExpectedFile> expected;
		for (const auto& entry : manifest->entries) {
			if (entry.kind != F4T::ModpackManifest::EntryKind::native || entry.sha256Hex.empty()) {
				continue;
			}
			for (const auto& path : a_requiredPaths) {
				if (EqualsIgnoreCase(path, entry.name)) {
					expected.push_back(ExpectedFile{ .relativePath = path, .sha256Hex = entry.sha256Hex });
				}
			}
		}
		return expected;
	}

	InstallProbe Probe(const std::filesystem::path& a_gameDirectory)
	{
		InstallProbe probe;
		probe.gameDirectory = ToUtf8(a_gameDirectory);

		std::error_code error;
		probe.gameDirectoryExists = std::filesystem::is_directory(a_gameDirectory, error) && !error;
		error.clear();

		const auto executable = a_gameDirectory / std::string{ kExecutableName };
		probe.fallout4ExecutableExists = std::filesystem::is_regular_file(executable, error) && !error;
		error.clear();
		if (probe.fallout4ExecutableExists) {
			probe.fallout4ExecutableVersion = ReadFileVersion(executable);
		}

		const auto loader = a_gameDirectory / std::string{ kLoaderName };
		probe.f4seLoaderExists = std::filesystem::is_regular_file(loader, error) && !error;
		error.clear();

		const auto plugins = a_gameDirectory / "Data" / "F4SE" / "Plugins";
		probe.pluginsDirectoryExists = std::filesystem::is_directory(plugins, error) && !error;
		error.clear();

		const auto target = TargetOf(probe);
		const auto detection = DetectAddressLibraryBin(plugins, probe.pluginsDirectoryExists, target);
		probe.addressLibraryFileName = detection.fileName;
		probe.otherAddressLibraryBins = detection.otherBins;
		if (target && probe.pluginsDirectoryExists) {
			for (const auto& path : F4T::InstallPreflight::RequiredPluginPaths(*target)) {
				const auto name = path.substr(path.rfind('/') + 1);
				const auto file = plugins / name;
				const bool present = std::filesystem::is_regular_file(file, error) && !error;
				error.clear();
				FileFact fact{ .relativePath = path, .present = present, .sha256Hex = {} };
				if (present) {
					fact.sha256Hex = F4T::ContentManifest::Sha256HexFile(file).value_or(std::string{});
				}
				probe.files.push_back(std::move(fact));
			}
		}
		return probe;
	}

	void PrintReport(const F4T::InstallPreflight::Report& a_report)
	{
		for (const auto& check : a_report.checks) {
			std::cout << (check.ok ? "[ OK ] " : "[FAIL] ") << check.message;
			if (!check.ok && !check.fixUrl.empty()) {
				std::cout << "  (fix: " << check.fixUrl << ')';
			}
			std::cout << '\n';
		}
		std::cout << (a_report.ok ? "preflight: ok\n" : "preflight: failed\n");
	}
}

int main(int argc, char** argv)
{
	Options options;
	if (!ParseOptions(argc, argv, options)) {
		std::cerr << "error: expected --game-dir <path> [--expected-manifest <path>] (see --help)\n";
		return 2;
	}

	auto probe = Probe(options.gameDirectory);

	if (!options.expectedManifest.empty()) {
		std::vector<std::string> requiredPaths;
		if (const auto target = TargetOf(probe)) {
			requiredPaths = F4T::InstallPreflight::RequiredPluginPaths(*target);
		}
		const auto expected = ReadExpectedHashes(options.expectedManifest, requiredPaths);
		if (!expected) {
			return 2;
		}
		probe.expectedHashes = *expected;
		probe.hashesSupplied = true;
	}

	const auto report = F4T::InstallPreflight::Evaluate(probe);
	PrintReport(report);
	return report.ok ? 0 : 1;
}

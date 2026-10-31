#include "F4TInstallPreflight.h"
#include "F4TContentManifest.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace F4T::InstallPreflight
{
	namespace
	{
		using F4T::ClientCompatibility::BuildTarget;

		// Address Library bin names and expected paths are from the documentation (docs/proof/native-character-setup-migration.md).
		constexpr std::string_view kOgBinName = "version-1-10-163-0.bin";
		constexpr std::string_view kNgBinName = "version-1-10-984-0.bin";
		constexpr std::string_view kAeBinName = "version-1-11-240-0.bin";

		constexpr std::string_view kPluginsPrefix = "F4SE/Plugins/";
		constexpr std::string_view kOgClientDll = "CommonwealthOnline-OG.dll";
		constexpr std::string_view kNgClientDll = "CommonwealthOnline-NG.dll";
		constexpr std::string_view kAeClientDll = "CommonwealthOnline.dll";
		constexpr std::string_view kGnsBridgeDll = "commonwealth_online_gns_bridge.dll";
		constexpr std::string_view kUiDll = "CommonwealthOnlineUI.dll";
		constexpr std::string_view kPrismaUiDll = "PrismaUI_F4.dll";
		constexpr std::string_view kClientManifestJson = "CommonwealthOnlineClient.json";

		std::string_view TargetLabel(BuildTarget a_target)
		{
			switch (a_target) {
			case BuildTarget::OG:
				return "OG 1.10.163";
			case BuildTarget::NG:
				return "NG 1.10.984";
			case BuildTarget::AE:
				return "AE 1.11.240";
			}
			return "an unsupported target";
		}

		std::string SupportedSummary()
		{
			return "supported versions are " + std::string{ F4T::ClientCompatibility::kOgRuntimeVersion } +
			       " (OG), " + std::string{ F4T::ClientCompatibility::kNgRuntimeVersion } +
			       " (NG) and " + std::string{ F4T::ClientCompatibility::kAeRuntimeVersion } + " (AE)";
		}

		std::string DescribeDirectory(const InstallProbe& a_probe)
		{
			return a_probe.gameDirectory.empty() ? std::string{ "<no directory supplied>" } : a_probe.gameDirectory;
		}

		std::string DescribeValue(std::string_view a_value)
		{
			return a_value.empty() ? std::string{ "<unknown>" } : std::string{ a_value };
		}

		std::string JoinNames(const std::vector<std::string>& a_names)
		{
			std::string text;
			for (std::size_t index = 0; index < a_names.size(); ++index) {
				if (index != 0) {
					text += ", ";
				}
				text += a_names[index];
			}
			return text;
		}

		std::string PluginsPath(std::string_view a_name)
		{
			return std::string{ kPluginsPrefix } + std::string{ a_name };
		}

		template <typename T>
		const T* Find(const std::vector<T>& a_items, std::string_view a_relativePath)
		{
			for (const auto& item : a_items) {
				if (item.relativePath == a_relativePath) {
					return &item;
				}
			}
			return nullptr;
		}
	}

	std::string_view FixUrl(FixTarget a_target) noexcept
	{
		switch (a_target) {
		case FixTarget::F4se:
			return kF4seFixUrl;
		case FixTarget::AddressLibrary:
			return kAddressLibraryFixUrl;
		case FixTarget::CoInstall:
			return kCoInstallFixUrl;
		}
		return {};
	}

	std::wstring LongPathForWin32(std::wstring a_path)
	{
		constexpr std::wstring_view kExtendedPrefix = L"\\\\?\\";
		constexpr std::wstring_view kUncPrefix = L"\\\\?\\UNC\\";
		constexpr std::wstring_view kUncLead = L"\\\\";

		if (a_path.size() < kWin32MaxPathLength || a_path.starts_with(kExtendedPrefix)) {
			return a_path;
		}
		if (a_path.starts_with(kUncLead)) {
			return std::wstring{ kUncPrefix } + a_path.substr(kUncLead.size());
		}
		return std::wstring{ kExtendedPrefix } + a_path;
	}

	std::string NormalizeRuntimeVersion(std::string_view a_version)
	{
		std::string text{ a_version };
		const auto first = text.find('.');
		if (first == std::string::npos) {
			return text;
		}
		const auto second = text.find('.', first + 1);
		if (second == std::string::npos) {
			return text;
		}
		const auto third = text.find('.', second + 1);
		if (third == std::string::npos) {
			return text;
		}
		// Only a zero build revision is dropped, so any other fourth part stays unsupported.
		if (text.compare(third + 1, std::string::npos, "0") == 0) {
			text.erase(third);
		}
		return text;
	}

	std::string_view AddressLibraryBinFor(BuildTarget a_target) noexcept
	{
		switch (a_target) {
		case BuildTarget::OG:
			return kOgBinName;
		case BuildTarget::NG:
			return kNgBinName;
		case BuildTarget::AE:
			return kAeBinName;
		}
		return {};
	}

	std::vector<std::string> RequiredPluginPaths(BuildTarget a_target)
	{
		std::string_view client{};
		switch (a_target) {
		case BuildTarget::OG:
			client = kOgClientDll;
			break;
		case BuildTarget::NG:
			client = kNgClientDll;
			break;
		case BuildTarget::AE:
			client = kAeClientDll;
			break;
		default:
			return {};
		}
		return {
			PluginsPath(client),
			PluginsPath(kGnsBridgeDll),
			PluginsPath(kUiDll),
			PluginsPath(kPrismaUiDll),
			PluginsPath(kClientManifestJson)
		};
	}

	std::vector<std::string_view> SupportedRuntimeVersions()
	{
		return {
			F4T::ClientCompatibility::kOgRuntimeVersion,
			F4T::ClientCompatibility::kNgRuntimeVersion,
			F4T::ClientCompatibility::kAeRuntimeVersion
		};
	}

	Report Evaluate(const InstallProbe& a_probe)
	{
		Report report;
		const auto directory = DescribeDirectory(a_probe);

		report.checks.push_back(Check{
			.id = CheckId::GameDirectory,
			.subject = "game-directory",
			.ok = a_probe.gameDirectoryExists,
			.message = a_probe.gameDirectoryExists
				? "Fallout 4 install directory found: " + directory
				: "Fallout 4 install directory not found: " + directory,
			.fixUrl = std::string{ FixUrl(FixTarget::CoInstall) }
		});

		report.checks.push_back(Check{
			.id = CheckId::Fallout4Executable,
			.subject = "Fallout4.exe",
			.ok = a_probe.fallout4ExecutableExists,
			.message = a_probe.fallout4ExecutableExists
				? "Fallout4.exe found in " + directory
				: "Fallout4.exe is missing from " + directory,
			.fixUrl = std::string{ FixUrl(FixTarget::CoInstall) }
		});

		std::optional<BuildTarget> target;
		if (a_probe.fallout4ExecutableExists) {
			const auto rawVersion = a_probe.fallout4ExecutableVersion;
			target = F4T::ClientCompatibility::TargetFromRuntime(NormalizeRuntimeVersion(rawVersion));
			std::string message;
			if (target) {
				message = "Fallout4.exe version " + DescribeValue(rawVersion) + " is supported (" +
				          std::string{ TargetLabel(*target) } + ")";
			} else if (rawVersion.empty()) {
				message = "could not read Fallout4.exe version from " + directory + "; " + SupportedSummary();
			} else {
				message = "unsupported Fallout4.exe version \"" + DescribeValue(rawVersion) + "\"; " +
				          SupportedSummary();
			}
			report.checks.push_back(Check{
				.id = CheckId::RuntimeSupported,
				.subject = "Fallout4.exe-version",
				.ok = target.has_value(),
				.message = std::move(message),
				.fixUrl = std::string{ FixUrl(FixTarget::CoInstall) }
			});
		}

		report.checks.push_back(Check{
			.id = CheckId::F4seLoader,
			.subject = "f4se_loader.exe",
			.ok = a_probe.f4seLoaderExists,
			.message = a_probe.f4seLoaderExists
				? "f4se_loader.exe found in " + directory
				: "f4se_loader.exe is missing from " + directory,
			.fixUrl = std::string{ FixUrl(FixTarget::F4se) }
		});

		report.checks.push_back(Check{
			.id = CheckId::PluginsDirectory,
			.subject = std::string{ kPluginsPrefix },
			.ok = a_probe.pluginsDirectoryExists,
			.message = a_probe.pluginsDirectoryExists
				? "Data/F4SE/Plugins directory found in " + directory
				: "Data/F4SE/Plugins directory is missing from " + directory,
			.fixUrl = std::string{ FixUrl(FixTarget::F4se) }
		});

		if (target && a_probe.pluginsDirectoryExists) {
			const auto label = std::string{ TargetLabel(*target) };
			const auto binName = std::string{ AddressLibraryBinFor(*target) };
			const auto binPath = PluginsPath(binName);
			const bool binPresent = a_probe.addressLibraryFileName == binName;

			std::string binMessage;
			if (binPresent) {
				binMessage = "Address Library " + binName + " found for " + label;
			} else if (a_probe.addressLibraryFileName.empty()) {
				binMessage = "Address Library bin for " + label + " is missing: " + binPath;
			} else {
				binMessage = "Address Library bin for " + label + " is missing: " + binPath +
				             "; found " + a_probe.addressLibraryFileName + " instead";
			}
			if (!binPresent && !a_probe.otherAddressLibraryBins.empty()) {
				binMessage += "; other Address Library bins found: " + JoinNames(a_probe.otherAddressLibraryBins);
			}
			report.checks.push_back(Check{
				.id = CheckId::AddressLibrary,
				.subject = binPath,
				.ok = binPresent,
				.message = std::move(binMessage),
				.fixUrl = std::string{ FixUrl(FixTarget::AddressLibrary) }
			});

			if (!a_probe.hashesSupplied) {
				report.checks.push_back(Check{
					.id = CheckId::RequiredFileHashes,
					.subject = "sha256",
					.ok = true,
					.message = "required-file sha256 hashes were not verified (no expected manifest supplied)",
					.fixUrl = std::string{ FixUrl(FixTarget::CoInstall) }
				});
			}

			for (const auto& path : RequiredPluginPaths(*target)) {
				const auto* fact = Find(a_probe.files, path);
				const auto* expected = Find(a_probe.expectedHashes, path);
				bool ok = fact != nullptr && fact->present;
				std::string message;
				if (!ok) {
					message = path + " is missing (required for " + label + ")";
				} else if (expected == nullptr && a_probe.hashesSupplied) {
					ok = false;
					message = path + " has no sha256 in the expected manifest (required for " + label + ")";
				} else if (expected == nullptr) {
					message = path + " found (sha256 not verified)";
				} else if (!F4T::ContentManifest::IsValidFingerprint(expected->sha256Hex)) {
					ok = false;
					message = path + " shipped hash is not a valid SHA-256: " + DescribeValue(expected->sha256Hex);
				} else if (fact->sha256Hex != expected->sha256Hex) {
					ok = false;
					message = path + " sha256 " + DescribeValue(fact->sha256Hex) +
					          " does not match the shipped hash " + expected->sha256Hex;
				} else {
					message = path + " found (sha256 verified)";
				}
				report.checks.push_back(Check{
					.id = CheckId::RequiredFile,
					.subject = path,
					.ok = ok,
					.message = std::move(message),
					.fixUrl = std::string{ FixUrl(FixTarget::CoInstall) }
				});
			}
		}

		report.ok = !report.checks.empty();
		for (const auto& check : report.checks) {
			if (!check.ok) {
				report.ok = false;
			}
		}
		return report;
	}
}

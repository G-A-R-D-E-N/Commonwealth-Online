#include "F4TInstallPreflight.h"
#include "F4TContentManifest.h"

#include <cassert>
#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using F4T::ClientCompatibility::BuildTarget;
	using F4T::InstallPreflight::AddressLibraryBinFor;
	using F4T::InstallPreflight::Check;
	using F4T::InstallPreflight::CheckId;
	using F4T::InstallPreflight::Evaluate;
	using F4T::InstallPreflight::ExpectedFile;
	using F4T::InstallPreflight::FileFact;
	using F4T::InstallPreflight::FixTarget;
	using F4T::InstallPreflight::FixUrl;
	using F4T::InstallPreflight::InstallProbe;
	using F4T::InstallPreflight::Report;
	using F4T::InstallPreflight::RequiredPluginPaths;

	std::string HashOf(std::string_view a_text)
	{
		return F4T::ContentManifest::Sha256Hex(a_text);
	}

	InstallProbe MakeProbe(BuildTarget a_target, std::string_view a_runtimeVersion)
	{
		InstallProbe probe;
		probe.gameDirectory = "C:\\Games\\Fallout 4";
		probe.gameDirectoryExists = true;
		probe.fallout4ExecutableExists = true;
		probe.fallout4ExecutableVersion = std::string{ a_runtimeVersion };
		probe.f4seLoaderExists = true;
		probe.pluginsDirectoryExists = true;
		probe.addressLibraryFileName = std::string{ AddressLibraryBinFor(a_target) };
		for (const auto& path : RequiredPluginPaths(a_target)) {
			const auto hash = HashOf(path);
			probe.files.push_back(FileFact{ .relativePath = path, .present = true, .sha256Hex = hash });
			probe.expectedHashes.push_back(ExpectedFile{ .relativePath = path, .sha256Hex = hash });
		}
		probe.hashesSupplied = true;
		return probe;
	}

	std::size_t FailingCount(const Report& a_report)
	{
		std::size_t count = 0;
		for (const auto& check : a_report.checks) {
			if (!check.ok) {
				++count;
			}
		}
		return count;
	}

	const Check* FindCheck(const Report& a_report, CheckId a_id, std::string_view a_subject)
	{
		for (const auto& check : a_report.checks) {
			if (check.id == a_id && check.subject == a_subject) {
				return &check;
			}
		}
		return nullptr;
	}

	std::size_t CountChecks(const Report& a_report, CheckId a_id)
	{
		std::size_t count = 0;
		for (const auto& check : a_report.checks) {
			if (check.id == a_id) {
				++count;
			}
		}
		return count;
	}

	bool Contains(const std::string& a_text, std::string_view a_needle)
	{
		return a_text.find(a_needle) != std::string::npos;
	}

	void TestAllOkForEachRuntime()
	{
		struct Case
		{
			BuildTarget target;
			std::string_view runtime;
			std::string_view clientDll;
		};
		const Case cases[] = {
			{ BuildTarget::AE, "1.11.240.0", "CommonwealthOnline.dll" },
			{ BuildTarget::NG, "1.10.984.0", "CommonwealthOnline-NG.dll" },
			{ BuildTarget::OG, "1.10.163.0", "CommonwealthOnline-OG.dll" }
		};

		for (const auto& item : cases) {
			const auto report = Evaluate(MakeProbe(item.target, item.runtime));
			assert(report.ok);
			assert(report.checks.size() == 11);
			assert(FailingCount(report) == 0);
			for (const auto& check : report.checks) {
				assert(check.ok);
			}

			const auto* client = FindCheck(report, CheckId::RequiredFile, "F4SE/Plugins/" + std::string{ item.clientDll });
			assert(client != nullptr);
			assert(client->ok);
			assert(Contains(client->message, "sha256 verified"));

			assert(FindCheck(report, CheckId::AddressLibrary, "F4SE/Plugins/" + std::string{ AddressLibraryBinFor(item.target) }) != nullptr);
			assert(FindCheck(report, CheckId::RequiredFile, "F4SE/Plugins/commonwealth_online_gns_bridge.dll") != nullptr);
			assert(FindCheck(report, CheckId::RequiredFile, "F4SE/Plugins/CommonwealthOnlineUI.dll") != nullptr);
			assert(FindCheck(report, CheckId::RequiredFile, "F4SE/Plugins/PrismaUI_F4.dll") != nullptr);
			assert(FindCheck(report, CheckId::RequiredFile, "F4SE/Plugins/CommonwealthOnlineClient.json") != nullptr);
		}
	}

	void TestRequiredPathsFollowTheStagedPackages()
	{
		const auto ae = RequiredPluginPaths(BuildTarget::AE);
		const auto ng = RequiredPluginPaths(BuildTarget::NG);
		const auto og = RequiredPluginPaths(BuildTarget::OG);

		assert(ae.size() == 5);
		assert(ae.front() == "F4SE/Plugins/CommonwealthOnline.dll");
		assert(ng.front() == "F4SE/Plugins/CommonwealthOnline-NG.dll");
		assert(og.front() == "F4SE/Plugins/CommonwealthOnline-OG.dll");

		for (const auto& paths : { ae, ng, og }) {
			assert(Contains(paths.at(1), "commonwealth_online_gns_bridge.dll"));
			assert(Contains(paths.at(2), "CommonwealthOnlineUI.dll"));
			assert(Contains(paths.at(3), "PrismaUI_F4.dll"));
			assert(Contains(paths.at(4), "CommonwealthOnlineClient.json"));
		}

		assert(!Contains(ae.front(), "-NG"));
		assert(!Contains(ae.front(), "-OG"));
		assert(!Contains(ng.front(), "-OG"));
	}

	void TestAddressLibraryNamesAndFixUrls()
	{
		assert(AddressLibraryBinFor(BuildTarget::OG) == "version-1-10-163-0.bin");
		assert(AddressLibraryBinFor(BuildTarget::NG) == "version-1-10-984-0.bin");
		assert(AddressLibraryBinFor(BuildTarget::AE) == "version-1-11-240-0.bin");

		const auto f4se = FixUrl(FixTarget::F4se);
		const auto library = FixUrl(FixTarget::AddressLibrary);
		const auto install = FixUrl(FixTarget::CoInstall);
		assert(f4se.starts_with("https://"));
		assert(library.starts_with("https://"));
		assert(install.starts_with("https://"));
		assert(f4se != library);
		assert(library != install);
		assert(f4se != install);
		assert(F4T::InstallPreflight::kF4seFixUrl == f4se);
		assert(F4T::InstallPreflight::kAddressLibraryFixUrl == library);
		assert(F4T::InstallPreflight::kCoInstallFixUrl == install);
	}

	void TestRuntimeVersionNormalization()
	{
		using F4T::InstallPreflight::NormalizeRuntimeVersion;

		const auto versions = F4T::InstallPreflight::SupportedRuntimeVersions();
		assert(versions.size() == 3);
		assert(versions.at(0) == F4T::ClientCompatibility::kOgRuntimeVersion);
		assert(versions.at(1) == F4T::ClientCompatibility::kNgRuntimeVersion);
		assert(versions.at(2) == F4T::ClientCompatibility::kAeRuntimeVersion);

		assert(NormalizeRuntimeVersion("1.11.240.0") == "1.11.240");
		assert(NormalizeRuntimeVersion("1.10.984.0") == "1.10.984");
		assert(NormalizeRuntimeVersion("1.10.163.0") == "1.10.163");
		assert(NormalizeRuntimeVersion("1.11.240") == "1.11.240");
		assert(NormalizeRuntimeVersion("1.11.221.0") == "1.11.221");
		assert(NormalizeRuntimeVersion("1.11.240.1") == "1.11.240.1");
		assert(NormalizeRuntimeVersion("1") == "1");
		assert(NormalizeRuntimeVersion("") == "");

		assert(F4T::ClientCompatibility::TargetFromRuntime(NormalizeRuntimeVersion("1.11.240.0")) == BuildTarget::AE);
		assert(F4T::ClientCompatibility::TargetFromRuntime(NormalizeRuntimeVersion("1.10.984.0")) == BuildTarget::NG);
		assert(F4T::ClientCompatibility::TargetFromRuntime(NormalizeRuntimeVersion("1.10.163.0")) == BuildTarget::OG);
	}

	void TestUnsupportedRuntimeVersionsAreRejected()
	{
		const std::string_view rejected[] = { "1.11.221", "1.11.221.0", "1.10.980", "1.10.980.0", "1.11.240.1", "" };
		for (const auto version : rejected) {
			const auto report = Evaluate(MakeProbe(BuildTarget::AE, version));
			assert(!report.ok);
			assert(FailingCount(report) == 1);

			const auto* runtime = FindCheck(report, CheckId::RuntimeSupported, "Fallout4.exe-version");
			assert(runtime != nullptr);
			assert(!runtime->ok);
			assert(Contains(runtime->message, "1.10.163"));
			assert(Contains(runtime->message, "1.10.984"));
			assert(Contains(runtime->message, "1.11.240"));

			assert(CountChecks(report, CheckId::AddressLibrary) == 0);
			assert(CountChecks(report, CheckId::RequiredFile) == 0);
		}
	}

	void TestUnreadableExecutableVersionIsNamedAsSuch()
	{
		const auto report = Evaluate(MakeProbe(BuildTarget::AE, ""));
		assert(!report.ok);
		assert(FailingCount(report) == 1);
		assert(report.checks.size() == 5);
		assert(CountChecks(report, CheckId::AddressLibrary) == 0);
		assert(CountChecks(report, CheckId::RequiredFile) == 0);

		const auto* runtime = FindCheck(report, CheckId::RuntimeSupported, "Fallout4.exe-version");
		assert(runtime != nullptr);
		assert(!runtime->ok);
		assert(Contains(runtime->message, "could not read Fallout4.exe version"));
		assert(Contains(runtime->message, "1.10.163"));
		assert(Contains(runtime->message, "1.10.984"));
		assert(Contains(runtime->message, "1.11.240"));
		assert(!Contains(runtime->message, "<unknown>"));
		assert(runtime->fixUrl.starts_with("https://"));
	}

	void TestWrongOrMissingAddressLibraryBinFails()
	{
		auto wrong = MakeProbe(BuildTarget::NG, "1.10.984.0");
		wrong.addressLibraryFileName = std::string{ AddressLibraryBinFor(BuildTarget::AE) };
		const auto wrongReport = Evaluate(wrong);
		assert(!wrongReport.ok);
		assert(FailingCount(wrongReport) == 1);
		const auto* wrongCheck = FindCheck(wrongReport, CheckId::AddressLibrary, "F4SE/Plugins/version-1-10-984-0.bin");
		assert(wrongCheck != nullptr);
		assert(!wrongCheck->ok);
		assert(Contains(wrongCheck->message, "version-1-11-240-0.bin"));
		assert(Contains(wrongCheck->message, "NG 1.10.984"));
		assert(wrongCheck->fixUrl.starts_with("https://"));

		auto missing = MakeProbe(BuildTarget::AE, "1.11.240.0");
		missing.addressLibraryFileName.clear();
		const auto missingReport = Evaluate(missing);
		assert(!missingReport.ok);
		assert(FailingCount(missingReport) == 1);
		const auto* missingCheck = FindCheck(missingReport, CheckId::AddressLibrary, "F4SE/Plugins/version-1-11-240-0.bin");
		assert(missingCheck != nullptr);
		assert(!missingCheck->ok);
		assert(Contains(missingCheck->message, "is missing"));
	}

	// Address Library bin names are from the documentation (docs/proof/native-character-setup-migration.md).
	void TestAddressLibraryMismatchListsOtherBinsFound()
	{
		auto probe = MakeProbe(BuildTarget::NG, "1.10.984.0");
		probe.addressLibraryFileName = std::string{ AddressLibraryBinFor(BuildTarget::OG) };
		probe.otherAddressLibraryBins = { std::string{ AddressLibraryBinFor(BuildTarget::AE) } };

		const auto report = Evaluate(probe);
		assert(!report.ok);
		assert(FailingCount(report) == 1);
		const auto* check = FindCheck(report, CheckId::AddressLibrary, "F4SE/Plugins/version-1-10-984-0.bin");
		assert(check != nullptr);
		assert(!check->ok);
		assert(Contains(check->message, "version-1-10-984-0.bin"));
		assert(Contains(check->message, "version-1-10-163-0.bin"));
		assert(Contains(check->message, "other Address Library bins found"));
		assert(Contains(check->message, "version-1-11-240-0.bin"));
	}

	void TestHashMismatchFails()
	{
		auto probe = MakeProbe(BuildTarget::AE, "1.11.240.0");
		const std::string path = probe.files.front().relativePath;
		probe.expectedHashes.front().sha256Hex = HashOf("tampered payload");

		const auto report = Evaluate(probe);
		assert(!report.ok);
		assert(FailingCount(report) == 1);
		const auto* check = FindCheck(report, CheckId::RequiredFile, path);
		assert(check != nullptr);
		assert(!check->ok);
		assert(Contains(check->message, path));
		assert(Contains(check->message, "does not match the shipped hash"));
		assert(check->fixUrl.starts_with("https://"));

		auto unhashed = MakeProbe(BuildTarget::AE, "1.11.240.0");
		unhashed.files.front().sha256Hex.clear();
		const auto unhashedReport = Evaluate(unhashed);
		assert(!unhashedReport.ok);
		assert(FailingCount(unhashedReport) == 1);
		assert(Contains(FindCheck(unhashedReport, CheckId::RequiredFile, path)->message, "<unknown>"));

		auto invalidExpected = MakeProbe(BuildTarget::AE, "1.11.240.0");
		invalidExpected.expectedHashes.front().sha256Hex = "NOTHEX";
		const auto invalidReport = Evaluate(invalidExpected);
		assert(!invalidReport.ok);
		assert(FailingCount(invalidReport) == 1);
		assert(Contains(FindCheck(invalidReport, CheckId::RequiredFile, path)->message, "not a valid SHA-256"));
	}

	void TestRequiredFileMissingFromManifestFails()
	{
		auto probe = MakeProbe(BuildTarget::AE, "1.11.240.0");
		const std::string path = probe.files.at(1).relativePath;
		for (auto it = probe.expectedHashes.begin(); it != probe.expectedHashes.end(); ++it) {
			if (it->relativePath == path) {
				probe.expectedHashes.erase(it);
				break;
			}
		}

		const auto report = Evaluate(probe);
		assert(!report.ok);
		assert(FailingCount(report) == 1);
		const auto* check = FindCheck(report, CheckId::RequiredFile, path);
		assert(check != nullptr);
		assert(!check->ok);
		assert(Contains(check->message, path));
		assert(Contains(check->message, "has no sha256 in the expected manifest"));
		assert(Contains(check->message, "AE 1.11.240"));
		assert(check->fixUrl.starts_with("https://"));
		assert(FindCheck(report, CheckId::RequiredFileHashes, "sha256") == nullptr);

		auto presenceOnly = probe;
		presenceOnly.hashesSupplied = false;
		presenceOnly.expectedHashes.clear();
		const auto presenceReport = Evaluate(presenceOnly);
		assert(presenceReport.ok);
		assert(FailingCount(presenceReport) == 0);
		assert(FindCheck(presenceReport, CheckId::RequiredFile, path)->ok);
	}

	void TestPresenceOnlyReportWhenNoManifestSupplied()
	{
		auto probe = MakeProbe(BuildTarget::AE, "1.11.240.0");
		probe.expectedHashes.clear();
		probe.hashesSupplied = false;

		const auto report = Evaluate(probe);
		assert(report.ok);
		assert(FailingCount(report) == 0);
		assert(report.checks.size() == 12);

		const auto* info = FindCheck(report, CheckId::RequiredFileHashes, "sha256");
		assert(info != nullptr);
		assert(info->ok);
		assert(Contains(info->message, "hashes were not verified"));
		assert(info->fixUrl.starts_with("https://"));

		const auto* client = FindCheck(report, CheckId::RequiredFile, "F4SE/Plugins/CommonwealthOnline.dll");
		assert(client != nullptr);
		assert(client->ok);
		assert(Contains(client->message, "found (sha256 not verified)"));
	}

	void TestEachCheckFailsAlone()
	{
		{
			auto probe = MakeProbe(BuildTarget::AE, "1.11.240.0");
			probe.gameDirectoryExists = false;
			const auto report = Evaluate(probe);
			assert(FailingCount(report) == 1);
			assert(!FindCheck(report, CheckId::GameDirectory, "game-directory")->ok);
		}
		{
			auto probe = MakeProbe(BuildTarget::AE, "1.11.240.0");
			probe.fallout4ExecutableExists = false;
			const auto report = Evaluate(probe);
			assert(FailingCount(report) == 1);
			assert(report.checks.size() == 4);
			assert(CountChecks(report, CheckId::RuntimeSupported) == 0);
			assert(!FindCheck(report, CheckId::Fallout4Executable, "Fallout4.exe")->ok);
		}
		{
			auto probe = MakeProbe(BuildTarget::AE, "1.11.240.0");
			probe.f4seLoaderExists = false;
			const auto report = Evaluate(probe);
			assert(FailingCount(report) == 1);
			assert(!FindCheck(report, CheckId::F4seLoader, "f4se_loader.exe")->ok);
			assert(Contains(FindCheck(report, CheckId::F4seLoader, "f4se_loader.exe")->message, "f4se_loader.exe"));
		}
		{
			auto probe = MakeProbe(BuildTarget::AE, "1.11.240.0");
			probe.pluginsDirectoryExists = false;
			const auto report = Evaluate(probe);
			assert(report.checks.size() == 5);
			assert(FailingCount(report) == 1);
			assert(!FindCheck(report, CheckId::PluginsDirectory, "F4SE/Plugins/")->ok);
		}
		{
			auto probe = MakeProbe(BuildTarget::AE, "1.11.240.0");
			const std::string path = probe.files.at(2).relativePath;
			probe.files.at(2).present = false;
			const auto report = Evaluate(probe);
			assert(FailingCount(report) == 1);
			const auto* check = FindCheck(report, CheckId::RequiredFile, path);
			assert(check != nullptr);
			assert(!check->ok);
			assert(Contains(check->message, "is missing"));
			assert(Contains(check->message, "AE 1.11.240"));
		}
	}

	void TestEveryFailureCarriesAFixUrl()
	{
		InstallProbe empty;
		empty.gameDirectory = "D:\\Fallout 4";
		empty.fallout4ExecutableExists = true;
		empty.fallout4ExecutableVersion = "1.11.221.0";
		const auto emptyReport = Evaluate(empty);
		assert(!emptyReport.ok);
		assert(emptyReport.checks.size() == 5);
		assert(FailingCount(emptyReport) == 4);
		for (const auto& check : emptyReport.checks) {
			assert(check.fixUrl.starts_with("https://"));
			if (!check.ok) {
				assert(!check.message.empty());
			}
		}

		auto tampered = MakeProbe(BuildTarget::OG, "1.10.163.0");
		tampered.f4seLoaderExists = false;
		tampered.addressLibraryFileName.clear();
		tampered.files.at(4).present = false;
		for (auto& expected : tampered.expectedHashes) {
			expected.sha256Hex = HashOf(expected.relativePath + " tampered");
		}
		const auto tamperedReport = Evaluate(tampered);
		assert(!tamperedReport.ok);
		assert(FailingCount(tamperedReport) == 7);
		for (const auto& check : tamperedReport.checks) {
			assert(!check.fixUrl.empty());
			assert(check.fixUrl.starts_with("https://"));
		}
		assert(!FindCheck(tamperedReport, CheckId::F4seLoader, "f4se_loader.exe")->ok);
		assert(!FindCheck(tamperedReport, CheckId::AddressLibrary, "F4SE/Plugins/version-1-10-163-0.bin")->ok);
	}

	void TestLongPathForWin32Prefixing()
	{
		using F4T::InstallPreflight::kWin32MaxPathLength;
		using F4T::InstallPreflight::LongPathForWin32;

		const std::wstring shortPath = L"C:\\Games\\Fallout 4\\Fallout4.exe";
		assert(LongPathForWin32(shortPath) == shortPath);

		const std::wstring belowLimit(kWin32MaxPathLength - 1, L'x');
		assert(LongPathForWin32(belowLimit) == belowLimit);

		const std::wstring longDrivePath = L"C:\\Games\\" + std::wstring(kWin32MaxPathLength, L'x');
		assert(LongPathForWin32(longDrivePath) == L"\\\\?\\" + longDrivePath);

		const std::wstring longUncPath = L"\\\\server\\share\\" + std::wstring(kWin32MaxPathLength, L'y');
		assert(LongPathForWin32(longUncPath) == L"\\\\?\\UNC\\" + longUncPath.substr(2));

		const std::wstring prefixedDrive = L"\\\\?\\" + longDrivePath;
		assert(LongPathForWin32(prefixedDrive) == prefixedDrive);

		const std::wstring prefixedUnc = L"\\\\?\\UNC\\" + longUncPath.substr(2);
		assert(LongPathForWin32(prefixedUnc) == prefixedUnc);
	}

	void Run(void (*a_test)(), const char* a_name)
	{
		std::cout << "[ RUN      ] " << a_name << '\n' << std::flush;
		a_test();
		std::cout << "[       OK ] " << a_name << '\n' << std::flush;
	}
}

int main()
{
	Run(TestAllOkForEachRuntime, "TestAllOkForEachRuntime");
	Run(TestRequiredPathsFollowTheStagedPackages, "TestRequiredPathsFollowTheStagedPackages");
	Run(TestAddressLibraryNamesAndFixUrls, "TestAddressLibraryNamesAndFixUrls");
	Run(TestRuntimeVersionNormalization, "TestRuntimeVersionNormalization");
	Run(TestUnsupportedRuntimeVersionsAreRejected, "TestUnsupportedRuntimeVersionsAreRejected");
	Run(TestUnreadableExecutableVersionIsNamedAsSuch, "TestUnreadableExecutableVersionIsNamedAsSuch");
	Run(TestWrongOrMissingAddressLibraryBinFails, "TestWrongOrMissingAddressLibraryBinFails");
	Run(TestAddressLibraryMismatchListsOtherBinsFound, "TestAddressLibraryMismatchListsOtherBinsFound");
	Run(TestHashMismatchFails, "TestHashMismatchFails");
	Run(TestRequiredFileMissingFromManifestFails, "TestRequiredFileMissingFromManifestFails");
	Run(TestPresenceOnlyReportWhenNoManifestSupplied, "TestPresenceOnlyReportWhenNoManifestSupplied");
	Run(TestEachCheckFailsAlone, "TestEachCheckFailsAlone");
	Run(TestEveryFailureCarriesAFixUrl, "TestEveryFailureCarriesAFixUrl");
	Run(TestLongPathForWin32Prefixing, "TestLongPathForWin32Prefixing");
	std::cout << "install preflight tests passed\n";
	return 0;
}

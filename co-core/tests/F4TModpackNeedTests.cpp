#include "F4TModpackNeed.h"

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

namespace
{
	using namespace F4T::ModpackManifest;

	const std::string kShaA(64, 'a');
	const std::string kShaB(64, 'b');
	const std::string kShaC(64, 'c');
	const std::string kShaD(64, 'd');
	const std::string kShaE(64, 'e');

	Entry Plugin(std::string a_name, PluginType a_type, std::uint32_t a_order, std::string a_sha)
	{
		return Entry{ EntryKind::plugin, a_type, std::move(a_name), a_order, std::move(a_sha), std::nullopt };
	}

	Entry SizeOnlyPlugin(std::string a_name, PluginType a_type, std::uint32_t a_order, std::uint64_t a_size)
	{
		return Entry{ EntryKind::plugin, a_type, std::move(a_name), a_order, {}, a_size };
	}

	Entry Archive(std::string a_name, std::string a_sha, std::uint64_t a_size)
	{
		return Entry{ EntryKind::archive, PluginType::none, std::move(a_name), 0, std::move(a_sha), a_size };
	}

	Entry Native(std::string a_name, std::string a_sha)
	{
		return Entry{ EntryKind::native, PluginType::none, std::move(a_name), 0, std::move(a_sha), std::nullopt };
	}

	const NeedFile* FindDownload(const NeedVerdict& a_verdict, const std::string& a_name)
	{
		for (const auto& file : a_verdict.download) {
			if (file.name == a_name) {
				return &file;
			}
		}
		return nullptr;
	}

	const ManualFile* FindManual(const NeedVerdict& a_verdict, const std::string& a_name)
	{
		for (const auto& file : a_verdict.manual) {
			if (file.name == a_name) {
				return &file;
			}
		}
		return nullptr;
	}

	void TestMissingAndDifferentPluginsAndTheirArchivesAreNeeded()
	{
		Manifest required{ {
			SizeOnlyPlugin("Fallout4.esm", PluginType::esm, 0, 100),
			Plugin("MyMod.esp", PluginType::esp, 1, kShaA),
			Archive("MyMod - Main.ba2", kShaB, 1000),
			Archive("MyMod - Textures.ba2", kShaC, 2000),
			Archive("Orphan - Main.ba2", kShaD, 10),
			Native("F4SE/Plugins/x.dll", kShaE),
		} };
		Manifest client{ {
			SizeOnlyPlugin("Fallout4.esm", PluginType::esm, 0, 100),
			Plugin("MyMod.esp", PluginType::esp, 1, kShaE),
			Archive("MyMod - Textures.ba2", kShaC, 2000),
		} };
		const auto verdict = BuildNeedVerdict(required, client);
		assert(verdict);
		assert(verdict->download.size() == 2);
		const auto* plugin = FindDownload(*verdict, "mymod.esp");
		assert(plugin && plugin->reason == NeedReason::differentContent && plugin->kind == AutoInstallFileKind::plugin);
		assert(plugin->sha256Hex == kShaA && !plugin->sizeBytes);
		assert(plugin->replacedSha256Hex == kShaE && !plugin->replacedSizeBytes);
		assert(verdict->replacements == 1);
		const auto* main = FindDownload(*verdict, "mymod - main.ba2");
		assert(main && main->reason == NeedReason::missing && main->kind == AutoInstallFileKind::mainArchive && main->pluginStem == "mymod");
		assert(main->sizeBytes && *main->sizeBytes == 1000);
		assert(main->replacedSha256Hex.empty() && !main->replacedSizeBytes);
		assert(!FindDownload(*verdict, "mymod - textures.ba2"));
		assert(verdict->manual.size() == 2);
		assert(FindManual(*verdict, "orphan - main.ba2")->reason == ManualReason::notACompanion);
		assert(FindManual(*verdict, "f4se/plugins/x.dll")->reason == ManualReason::notAutoInstallable);
		assert(verdict->knownBytes == 1000);
		assert(verdict->unknownSizeFiles == 1);
		assert(verdict->capIssue == NeedCapIssue::none);
		assert(NeedVerdictCanDownload(*verdict));
	}

	void TestOfficialAndUnhashedEntriesAreManual()
	{
		Manifest required{ {
			SizeOnlyPlugin("Fallout4.esm", PluginType::esm, 0, 100),
			SizeOnlyPlugin("DLCCoast.esm", PluginType::esm, 1, 200),
			SizeOnlyPlugin("SomeMaster.esm", PluginType::esm, 2, 300),
			SizeOnlyPlugin("ccBGSFO4001-PipBoy(Black).esl", PluginType::esl, 3, 400),
		} };
		Manifest client{ { SizeOnlyPlugin("Fallout4.esm", PluginType::esm, 0, 100) } };
		const auto verdict = BuildNeedVerdict(required, client);
		assert(verdict);
		assert(verdict->download.empty());
		assert(!NeedVerdictCanDownload(*verdict));
		const auto* official = FindManual(*verdict, "dlccoast.esm");
		assert(official && official->reason == ManualReason::refusedName && official->refusal == AutoInstallRefusal::officialContent);
		const auto* cc = FindManual(*verdict, "ccbgsfo4001-pipboy(black).esl");
		assert(cc && cc->refusal == AutoInstallRefusal::creationClub);
		assert(FindManual(*verdict, "somemaster.esm")->reason == ManualReason::noHash);
	}

	void TestPerFileLimitMovesTheFileToManual()
	{
		Manifest required{ {
			Plugin("Big.esp", PluginType::esp, 0, kShaA),
			Archive("Big - Main.ba2", kShaB, kMaximumAutoInstallFileBytes),
			Archive("Big - Textures.ba2", kShaC, kMaximumAutoInstallFileBytes + 1),
		} };
		Manifest client{ { Plugin("Big.esp", PluginType::esp, 0, kShaA) } };
		const auto verdict = BuildNeedVerdict(required, client);
		assert(verdict);
		assert(FindDownload(*verdict, "big - main.ba2"));
		assert(FindManual(*verdict, "big - textures.ba2")->reason == ManualReason::tooLarge);
		assert(verdict->capIssue == NeedCapIssue::none);
	}

	void TestTotalLimitBlocksTheDownload()
	{
		Manifest required{ {
			Plugin("Big.esp", PluginType::esp, 0, kShaA),
			Archive("Big - Main.ba2", kShaB, kMaximumAutoInstallFileBytes),
			Archive("Big - Textures.ba2", kShaB, kMaximumAutoInstallFileBytes),
			Archive("Big - Geometry.psg", kShaB, kMaximumAutoInstallFileBytes),
			Archive("Big - Geometry.csg", kShaB, kMaximumAutoInstallFileBytes),
			Archive("Big.cdx", kShaB, 1),
		} };
		Manifest client{ { Plugin("Big.esp", PluginType::esp, 0, kShaA) } };
		const auto verdict = BuildNeedVerdict(required, client);
		assert(verdict);
		assert(verdict->download.size() == 5);
		assert(verdict->knownBytes > kMaximumAutoInstallTotalBytes);
		assert(verdict->capIssue == NeedCapIssue::totalTooLarge);
		assert(!NeedVerdictCanDownload(*verdict));
	}

	void TestTotalExactlyAtTheLimitIsAllowed()
	{
		Manifest required{ {
			Plugin("Big.esp", PluginType::esp, 0, kShaA),
			Archive("Big - Main.ba2", kShaB, kMaximumAutoInstallFileBytes),
			Archive("Big - Textures.ba2", kShaB, kMaximumAutoInstallFileBytes),
			Archive("Big - Geometry.psg", kShaB, kMaximumAutoInstallFileBytes),
			Archive("Big - Geometry.csg", kShaB, kMaximumAutoInstallFileBytes),
		} };
		Manifest client{ { Plugin("Big.esp", PluginType::esp, 0, kShaA) } };
		const auto verdict = BuildNeedVerdict(required, client);
		assert(verdict);
		assert(verdict->knownBytes == kMaximumAutoInstallTotalBytes);
		assert(verdict->capIssue == NeedCapIssue::none);
		assert(NeedVerdictCanDownload(*verdict));
	}

	void TestFileCountLimitBlocksTheDownload()
	{
		Manifest required;
		for (std::uint32_t index = 0; index <= kMaximumAutoInstallFiles; ++index) {
			required.entries.push_back(Plugin("Mod" + std::to_string(index) + ".esp", PluginType::esp, index, kShaA));
		}
		const Manifest client{ { SizeOnlyPlugin("Fallout4.esm", PluginType::esm, 0, 100) } };
		const auto verdict = BuildNeedVerdict(required, client);
		assert(verdict);
		assert(verdict->download.size() == kMaximumAutoInstallFiles + 1);
		assert(verdict->otherDifferences == 1);
		assert(verdict->capIssue == NeedCapIssue::tooManyFiles);
		assert(!NeedVerdictCanDownload(*verdict));
	}

	void TestInvalidManifestGivesNoVerdict()
	{
		Manifest broken{ { Entry{ EntryKind::plugin, PluginType::esp, "Bad.esp", 0, kShaA, 10 } } };
		const Manifest valid{ { SizeOnlyPlugin("Fallout4.esm", PluginType::esm, 0, 100) } };
		assert(!BuildNeedVerdict(broken, valid));
		assert(!BuildNeedVerdict(valid, broken));
		assert(!BuildNeedVerdict(valid, Manifest{}));
	}

	void TestDescriptions()
	{
		assert(DescribeManualReason(ManualReason::notACompanion) == "is not an archive of a plugin this server requires");
		assert(DescribeNeedCapIssue(NeedCapIssue::totalTooLarge) == "larger in total than the download limit");
	}
}

int main()
{
	TestMissingAndDifferentPluginsAndTheirArchivesAreNeeded();
	TestOfficialAndUnhashedEntriesAreManual();
	TestPerFileLimitMovesTheFileToManual();
	TestTotalLimitBlocksTheDownload();
	TestTotalExactlyAtTheLimitIsAllowed();
	TestFileCountLimitBlocksTheDownload();
	TestInvalidManifestGivesNoVerdict();
	TestDescriptions();
	return 0;
}

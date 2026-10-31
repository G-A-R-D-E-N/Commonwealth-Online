#include "F4TModpackServe.h"

#include "F4TContentManifest.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>

namespace
{
	using namespace F4T::ModpackServe;
	using F4T::ModpackManifest::Entry;
	using F4T::ModpackManifest::EntryKind;
	using F4T::ModpackManifest::Manifest;
	using F4T::ModpackManifest::PluginType;

	const std::string kFingerprint(64, 'f');

	void Write(const std::filesystem::path& a_path, const std::string& a_bytes)
	{
		std::ofstream stream(a_path, std::ios::binary | std::ios::trunc);
		stream << a_bytes;
	}

	struct Folder
	{
		std::filesystem::path path{ std::filesystem::temp_directory_path() / ("co-modpack-serve-test-" + std::to_string(std::random_device{}())) };

		Folder()
		{
			std::filesystem::remove_all(path);
			std::filesystem::create_directories(path);
		}

		~Folder()
		{
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	};

	Manifest Required()
	{
		return Manifest{ {
			Entry{ EntryKind::plugin, PluginType::esm, "Fallout4.esm", 0, {}, 100 },
			Entry{ EntryKind::plugin, PluginType::esp, "MyMod.esp", 1, F4T::ContentManifest::Sha256Hex("plugin"), std::nullopt },
			Entry{ EntryKind::archive, PluginType::none, "MyMod - Main.ba2", 0, F4T::ContentManifest::Sha256Hex("archive"), 7 },
			Entry{ EntryKind::plugin, PluginType::esp, "Secret.esp", 2, F4T::ContentManifest::Sha256Hex("secret"), std::nullopt },
			Entry{ EntryKind::plugin, PluginType::esp, "Stale.esp", 3, F4T::ContentManifest::Sha256Hex("expected"), std::nullopt },
			Entry{ EntryKind::plugin, PluginType::esp, "Absent.esp", 4, F4T::ContentManifest::Sha256Hex("absent"), std::nullopt },
		} };
	}

	void TestCatalogServesOnlyAllowlistedMatchingFiles()
	{
		Folder folder;
		Write(folder.path / "MYMOD.ESP", "plugin");
		Write(folder.path / "MyMod - Main.ba2", "archive");
		Write(folder.path / "Secret.esp", "secret");
		Write(folder.path / "Stale.esp", "changed");
		Write(folder.path / "Fallout4.esm", "official");
		const auto catalog = BuildCatalog(Required(), kFingerprint, folder.path, { "mymod.esp", "MyMod - Main.ba2", "Stale.esp", "Absent.esp", "Fallout4.esm" });
		assert(catalog.entries.size() == 2);
		assert(FindEntry(catalog, "mymod.esp") && FindEntry(catalog, "MYMOD - MAIN.BA2"));
		assert(!FindEntry(catalog, "secret.esp"));
		assert(!FindEntry(catalog, "fallout4.esm"));
		assert(catalog.problems.size() == 2);

		const auto everything = BuildCatalog(Required(), kFingerprint, folder.path, { "*" });
		assert(everything.entries.size() == 3);
	}

	void TestOfferAndChangeDetection()
	{
		Folder folder;
		Write(folder.path / "MyMod.esp", "plugin");
		Write(folder.path / "MyMod - Main.ba2", "archive");
		const auto catalog = BuildCatalog(Required(), kFingerprint, folder.path, { "*" });
		const auto offer = BuildOffer(catalog, { kFingerprint, { "mymod.esp", "mymod - main.ba2", "secret.esp" } });
		assert(offer.files.size() == 2 && offer.files[0].sizeBytes == 6 && offer.files[1].sizeBytes == 7);
		assert(BuildOffer(catalog, { std::string(64, '0'), { "mymod.esp" } }).files.empty());
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		Write(folder.path / "MyMod.esp", "plugin2");
		assert(!StillMatches(*FindEntry(catalog, "mymod.esp")));
		assert(BuildOffer(catalog, { kFingerprint, { "mymod.esp" } }).files.empty());
	}

	void TestMissingFolderIsAProblem()
	{
		const auto catalog = BuildCatalog(Required(), kFingerprint, std::filesystem::temp_directory_path() / "co-no-such-folder-494", { "*" });
		assert(catalog.entries.empty() && catalog.problems.size() == 1);
	}

	void TestLimiter()
	{
		Limiter limiter(2, 100);
		assert(limiter.TryStart("a", 0) && limiter.TryStart("a", 0));
		assert(!limiter.TryStart("b", 0));
		limiter.Finish("a");
		assert(limiter.Active() == 1);
		assert(limiter.Charge("a", 60, 1));
		assert(!limiter.Charge("a", 60, 2));
		limiter.Finish("a");
		assert(!limiter.TryStart("a", 3));
		assert(limiter.TryStart("b", 3));
		assert(limiter.TryStart("a", kQuotaWindowSeconds + 1));
		limiter.Finish("zzz");
		assert(limiter.Active() == 2);
	}
}

int main()
{
	TestCatalogServesOnlyAllowlistedMatchingFiles();
	TestOfferAndChangeDetection();
	TestMissingFolderIsAProblem();
	TestLimiter();
	return 0;
}

#include "F4TModOrganizer.h"
#include "F4TContentManifest.h"
#include "F4TModpackProbeBudget.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

int main()
{
	const auto root = std::filesystem::temp_directory_path() /
		("co-mo2-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	const auto high = root / "high";
	const auto low = root / "low";
	std::filesystem::create_directories(high / "F4SE" / "Plugins");
	std::filesystem::create_directories(low / "f4se" / "plugins");
	std::ofstream(high / "F4SE" / "Plugins" / "Test.dll") << "high";
	std::ofstream(low / "f4se" / "plugins" / "test.dll") << "low";
	std::ofstream(low / "only.esp") << "low";
	std::string error;
	using F4T::ModOrganizer::ResolveFile;
	const auto found = ResolveFile({ high, low }, "f4se/plugins/TEST.dll", error);
	assert(found && *found == high / "F4SE" / "Plugins" / "Test.dll" && error.empty());
	assert(ResolveFile({ high, low }, "ONLY.ESP", error) == low / "only.esp");
	assert(!ResolveFile({ high, low }, "../only.esp", error) && !error.empty());
	assert(!ResolveFile({ high, low }, "missing.esp", error) && !error.empty());
	assert(!ResolveFile({ high }, "only.esp", error, std::chrono::steady_clock::time_point::min()) && !error.empty());
	std::filesystem::create_directories(high / "folder.esp");
	std::ofstream(low / "folder.esp") << "low";
	assert(!ResolveFile({ high, low }, "folder.esp", error) && !error.empty());
	std::error_code ec;
	std::filesystem::create_symlink(low / "only.esp", high / "link.esp", ec);
	if (!ec) {
		assert(!ResolveFile({ high, low }, "link.esp", error) && !error.empty());
	}
	const auto profile = root / "profile";
	const auto mods = root / "mods";
	const auto data = root / "Data";
	const auto overwrite = root / "overwrite";
	std::filesystem::create_directories(profile);
	std::filesystem::create_directories(mods / "Top");
	std::filesystem::create_directories(mods / "Bottom");
	std::filesystem::create_directories(data);
	std::filesystem::create_directories(overwrite);
	std::ofstream(profile / "modlist.txt") << "+Top\n+Bottom\n-Disabled\n";
	std::ofstream(profile / "plugins.txt") << "*test.esp\n*light.esp\n";
	std::ofstream(profile / "loadorder.txt") << "Fallout4.esm\nlight.esp\ntest.esp\nDLCExample.esm\n";
	const auto plugin = [](const std::filesystem::path& path, bool light, char payload = '\0') {
		std::ofstream file(path, std::ios::binary);
		const char header[12] = { 'T', 'E', 'S', '4', 0, 0, 0, 0, 0, static_cast<char>(light ? 2 : 0), 0, 0 };
		file.write(header, sizeof(header));
		if (payload != '\0') file.put(payload);
	};
	plugin(data / "Fallout4.esm", false);
	plugin(mods / "Top" / "test.esp", false, 'T');
	plugin(mods / "Bottom" / "test.esp", false, 'B');
	plugin(mods / "Top" / "light.esp", true);
	plugin(mods / "Top" / "DLCExample.esm", false, 'X');
	std::filesystem::create_directories(mods / "Top" / "textures");
	std::filesystem::create_directories(mods / "Bottom" / "textures");
	std::filesystem::create_directories(mods / "Top" / "F4SE" / "Plugins");
	std::filesystem::create_directories(mods / "Top" / ".git");
	std::filesystem::create_directories(mods / "Top" / "fomod");
	std::filesystem::create_directories(mods / "Top" / "Root");
	std::filesystem::create_directories(overwrite / "server");
	std::filesystem::create_directories(overwrite / "Root");
	std::ofstream(mods / "Top" / "textures" / "test.dds") << "top";
	std::ofstream(mods / "Bottom" / "textures" / "test.dds") << "bottom";
	std::ofstream(mods / "Top" / "textures" / std::filesystem::path{ u8"\u00c4.dds" }) << "upper";
	std::ofstream(mods / "Bottom" / "textures" / std::filesystem::path{ u8"\u00e4.dds" }) << "lower";
	std::ofstream(mods / "Top" / "textures" / "hidden.dds.mohidden") << "hidden";
	std::ofstream(mods / "Top" / ".git" / "config") << "hidden";
	std::ofstream(mods / "Top" / "fomod" / "info.xml") << "hidden";
	std::ofstream(mods / "Top" / "Root" / "f4se_loader.exe") << "game root";
	std::ofstream(overwrite / "server" / ".admin-token") << "server-only";
	std::ofstream(overwrite / "Root" / "localstorage") << "local state";
	std::ofstream(mods / "Top" / "meta.ini") << "metadata";
	std::ofstream(mods / "Top" / "readme.txt") << "metadata";
	std::ofstream(mods / "Top" / "test.ba2") << "archive";
	std::ofstream(mods / "Top" / "F4SE" / "Plugins" / "test.dll") << "native";
	std::ofstream(mods / "Top" / "F4SE" / "Plugins" / "test.ini") << "config";
	const auto manifest = F4T::ModOrganizer::BuildPluginManifest(profile, mods, overwrite, data, root / "Fallout4.ccc", error);
	assert(manifest && manifest->entries.size() == 8);
	assert(!F4T::ModOrganizer::BuildPluginManifest(profile, mods, overwrite, data, root / "Fallout4.ccc", error, F4T::ModOrganizer::Filters{ { "" }, {} }) && error == "Invalid MO2 suffix filter");
	assert(manifest->entries[0].name == "fallout4.esm" && manifest->entries[0].sizeBytes == 12);
	assert(manifest->entries[1].name == "test.esp" && manifest->entries[1].sha256Hex == F4T::ContentManifest::Sha256HexFile(mods / "Top" / "test.esp"));
	assert(manifest->entries[2].name == "light.esp" && manifest->entries[2].order == 2);
	const auto asset = std::find_if(manifest->entries.begin(), manifest->entries.end(), [](const auto& entry) { return entry.name == "textures/test.dds"; });
	assert(asset != manifest->entries.end() && asset->kind == F4T::ModpackManifest::EntryKind::asset &&
		asset->sha256Hex == F4T::ContentManifest::Sha256HexFile(mods / "Top" / "textures" / "test.dds"));
	const auto unicodeAsset = std::find_if(manifest->entries.begin(), manifest->entries.end(), [](const auto& entry) { return entry.name == "textures/\xc3\x84.dds"; });
	assert(unicodeAsset != manifest->entries.end() && unicodeAsset->sha256Hex == F4T::ContentManifest::Sha256HexFile(mods / "Top" / "textures" / std::filesystem::path{ u8"\u00c4.dds" }));
	assert(std::any_of(manifest->entries.begin(), manifest->entries.end(), [](const auto& entry) { return entry.kind == F4T::ModpackManifest::EntryKind::archive && entry.name == "test.ba2"; }));
	assert(std::any_of(manifest->entries.begin(), manifest->entries.end(), [](const auto& entry) { return entry.kind == F4T::ModpackManifest::EntryKind::native && entry.name == "f4se/plugins/test.dll"; }));
	assert(std::any_of(manifest->entries.begin(), manifest->entries.end(), [](const auto& entry) { return entry.kind == F4T::ModpackManifest::EntryKind::config && entry.name == "f4se/plugins/test.ini"; }));
	const auto unicodeMod = mods / std::filesystem::path{ u8"\u00c4Mod" };
	std::filesystem::rename(mods / "Top", unicodeMod);
	std::ofstream(profile / "modlist.txt", std::ios::trunc) << "+\xc3\xa4" "mod\n+Bottom\n-Disabled\n";
	const auto renamedMod = F4T::ModOrganizer::BuildPluginManifest(profile, mods, overwrite, data, root / "Fallout4.ccc", error);
	assert(renamedMod && F4T::ModpackManifest::ComputeFingerprint(*renamedMod) == F4T::ModpackManifest::ComputeFingerprint(*manifest));
#ifndef _WIN32
	std::filesystem::create_directories(mods / std::filesystem::path{ u8"\u00e4mod" });
	assert(!F4T::ModOrganizer::BuildPluginManifest(profile, mods, overwrite, data, root / "Fallout4.ccc", error));
	std::filesystem::remove_all(mods / std::filesystem::path{ u8"\u00e4mod" });
#endif
	std::filesystem::rename(unicodeMod, mods / "Top");
	std::ofstream(profile / "modlist.txt", std::ios::trunc) << "+Top\n+Bottom\n-Disabled\n";
	const auto unicodePluginPath = mods / "Top" / std::filesystem::path{ u8"\u00c4.esp" };
	plugin(unicodePluginPath, false, 'U');
	std::ofstream(profile / "plugins.txt", std::ios::app) << "*\xc3\xa4.esp\n";
	std::ofstream(profile / "loadorder.txt", std::ios::app) << "\xc3\x84.esp\n";
	const auto unicodePlugin = F4T::ModOrganizer::BuildPluginManifest(profile, mods, overwrite, data, root / "Fallout4.ccc", error);
	assert(unicodePlugin && std::any_of(unicodePlugin->entries.begin(), unicodePlugin->entries.end(), [](const auto& entry) { return entry.name == "\xc3\x84.esp"; }));
	std::filesystem::remove(unicodePluginPath);
	std::ofstream(profile / "plugins.txt", std::ios::trunc) << "*test.esp\n*light.esp\n";
	std::ofstream(profile / "loadorder.txt", std::ios::trunc) << "Fallout4.esm\nlight.esp\ntest.esp\nDLCExample.esm\n";
	plugin(mods / "Top" / "Fallout4.esm", false, 'M');
	const auto overriddenBase = F4T::ModOrganizer::BuildPluginManifest(profile, mods, overwrite, data, root / "Fallout4.ccc", error);
	assert(overriddenBase && !overriddenBase->entries[0].sha256Hex.empty() && !overriddenBase->entries[0].sizeBytes);
	std::filesystem::remove(mods / "Top" / "Fallout4.esm");
	std::ofstream(profile / "plugins.txt", std::ios::trunc) << "*test.esp\n*light.esp\n*DLCExample.esm\n";
	const auto customDlc = F4T::ModOrganizer::BuildPluginManifest(profile, mods, overwrite, data, root / "Fallout4.ccc", error);
	assert(customDlc && std::any_of(customDlc->entries.begin(), customDlc->entries.end(), [](const auto& entry) { return entry.name == "dlcexample.esm" && !entry.sha256Hex.empty() && !entry.sizeBytes; }));
	std::ofstream(profile / "plugins.txt", std::ios::trunc) << "*test.esp\n*light.esp\n";
	{
		std::ofstream ini(root / "ModOrganizer.ini");
		ini << "[General]\ngameName=Fallout 4\ngamePath=@ByteArray(" << root.generic_string()
			<< ")\n[Settings]\nbase_directory=@ByteArray(" << root.generic_string()
			<< ")\nmod_directory=%BASE_DIR%/m\\x6f\\x64s\nprofiles_directory=%BASE_DIR%\noverwrite_directory=%BASE_DIR%/overwrite\n";
	}
	const auto fromInstance = F4T::ModOrganizer::BuildProfileManifest(root, "profile", error);
	assert(fromInstance && F4T::ModpackManifest::ComputeFingerprint(*fromInstance) == F4T::ModpackManifest::ComputeFingerprint(*manifest));
	const auto unicodeProfile = root / std::filesystem::path{ u8"\u00c4Profile" };
	std::filesystem::rename(profile, unicodeProfile);
	const auto renamedProfile = F4T::ModOrganizer::BuildProfileManifest(root, "\xc3\xa4profile", error);
	assert(renamedProfile && F4T::ModpackManifest::ComputeFingerprint(*renamedProfile) == F4T::ModpackManifest::ComputeFingerprint(*manifest));
	std::filesystem::rename(unicodeProfile, profile);
	std::filesystem::create_directories(mods / "Top" / "Cache, Profiles");
	std::filesystem::create_directories(mods / "Top" / "@cache");
	std::filesystem::create_directories(mods / "Top" / std::filesystem::path{ u8"\u00c4Cache" });
	std::ofstream(mods / "Top" / "Cache, Profiles" / "ignored.txt") << "hidden";
	std::ofstream(mods / "Top" / "@cache" / "ignored.txt") << "hidden";
	std::ofstream(mods / "Top" / std::filesystem::path{ u8"\u00c4Cache" } / "ignored.txt") << "hidden";
	std::ofstream(mods / "Top" / "ignored.tmp,foo") << "hidden";
	{
		std::ofstream ini(root / "ModOrganizer.ini", std::ios::app);
		ini << "skip_file_suffixes=.mohidden, .dds, \".tmp,foo\"\nskip_directories=.git, \"Cache, Profiles\", @@c\\141che, \xc3\xa4" "Cache\n";
	}
	const auto filtered = F4T::ModOrganizer::BuildProfileManifest(root, "profile", error);
	assert(filtered && filtered->entries.size() == 6);
	assert(std::none_of(filtered->entries.begin(), filtered->entries.end(), [](const auto& entry) { return entry.name == "textures/test.dds"; }));
	std::filesystem::remove_all(mods / "Top" / "Cache, Profiles");
	std::filesystem::remove_all(mods / "Top" / "@cache");
	std::filesystem::remove_all(mods / "Top" / std::filesystem::path{ u8"\u00c4Cache" });
	std::filesystem::remove(mods / "Top" / "ignored.tmp,foo");
	{
		std::ofstream ini(root / "ModOrganizer.ini", std::ios::trunc);
		ini << "[General]\ngameName=Fallout 4\ngamePath=@ByteArray(" << root.generic_string()
			<< ")\n[Settings]\nbase_directory=@ByteArray(" << root.generic_string()
			<< ")\nmod_directory=%BASE_DIR%/mods\nprofiles_directory=%BASE_DIR%\noverwrite_directory=%BASE_DIR%/overwrite\nskip_file_suffixes=@Invalid()\n";
	}
	const auto noSuffixFilters = F4T::ModOrganizer::BuildProfileManifest(root, "profile", error);
	assert(noSuffixFilters && std::any_of(noSuffixFilters->entries.begin(), noSuffixFilters->entries.end(), [](const auto& entry) { return entry.name == "textures/hidden.dds.mohidden"; }));
	const auto unicodeMods = root / std::filesystem::path{ u8"m\u00e9ds" };
	std::filesystem::rename(mods, unicodeMods);
	{
		std::ofstream ini(root / "ModOrganizer.ini", std::ios::trunc);
		ini << "[General]\ngameName=Fallout 4\ngamePath=@ByteArray(" << root.generic_string()
			<< ")\n[Settings]\nbase_directory=@ByteArray(" << root.generic_string()
			<< ")\nmod_directory=@ByteArray(%BASE_DIR%/m\\xc3\\xa9\\x64s)\nprofiles_directory=%BASE_DIR%\noverwrite_directory=%BASE_DIR%/overwrite\n";
	}
	const auto unicodeInstance = F4T::ModOrganizer::BuildProfileManifest(root, "profile", error);
	assert(unicodeInstance && F4T::ModpackManifest::ComputeFingerprint(*unicodeInstance) == F4T::ModpackManifest::ComputeFingerprint(*manifest));
	std::filesystem::rename(unicodeMods, mods);
	assert(!F4T::ModOrganizer::BuildProfileManifest(root, "../profile", error));
	const auto latin1Plugin = mods / "Top" / std::filesystem::path{ u8"\u00e9.esp" };
	plugin(latin1Plugin, false, 'L');
	std::ofstream(profile / "plugins.txt", std::ios::trunc) << "*\xe9.esp\n";
	std::ofstream(profile / "loadorder.txt", std::ios::trunc) << "\xe9.esp\n";
	const auto latin1 = F4T::ModOrganizer::BuildPluginManifest(profile, mods, overwrite, data, root / "Fallout4.ccc", error);
	assert(latin1 && std::any_of(latin1->entries.begin(), latin1->entries.end(), [](const auto& entry) { return entry.name == "\xc3\xa9.esp"; }));
	std::filesystem::remove(latin1Plugin);
	std::ofstream(profile / "plugins.txt", std::ios::trunc) << "*test.esp\n*light.esp\n";
	std::ofstream(profile / "loadorder.txt", std::ios::trunc) << "Fallout4.esm\nlight.esp\ntest.esp\nDLCExample.esm\n";
	std::filesystem::create_directories(mods / "Gamma");
	std::error_code caseError;
	if (std::filesystem::create_directory(mods / "gamma", caseError) && !caseError) {
		std::ofstream(profile / "modlist.txt", std::ios::trunc) << "+Top\n+Bottom\n-Gamma\n";
		assert(F4T::ModOrganizer::BuildPluginManifest(profile, mods, overwrite, data, root / "Fallout4.ccc", error));
		std::ofstream(profile / "modlist.txt", std::ios::trunc) << "+Top\n+Bottom\n+gamma\n";
		assert(!F4T::ModOrganizer::BuildPluginManifest(profile, mods, overwrite, data, root / "Fallout4.ccc", error) && error == "Ambiguous MO2 mod folder names");
		std::filesystem::remove(mods / "gamma");
	}
	std::filesystem::remove_all(mods / "Gamma");
	std::ofstream(profile / "modlist.txt", std::ios::trunc) << "+Top\n+Bottom\n-Disabled\n";
	{
		std::ofstream ini(root / "ModOrganizer.ini", std::ios::trunc);
		ini << "[General]\ngameName=Fallout 4\ngamePath=C:\\Games\\Fallout 4\n[Settings]\nbase_directory=@ByteArray(" << root.generic_string()
			<< ")\nmod_directory=%BASE_DIR%/mods\nprofiles_directory=%BASE_DIR%\noverwrite_directory=%BASE_DIR%/overwrite\n";
	}
#ifndef _WIN32
	assert(!F4T::ModOrganizer::BuildProfileManifest(root, "profile", error) && error == "MO2 setting general/gamepath is a Windows path that must be mapped to a path on this server");
#endif
	std::ofstream(profile / "plugins.txt", std::ios::trunc) << "*missing.esp\n";
	assert(!F4T::ModOrganizer::BuildPluginManifest(profile, mods, overwrite, data, root / "Fallout4.ccc", error));
	{
		std::ofstream list(profile / "modlist.txt", std::ios::binary | std::ios::trunc);
		const std::string invalid{ "+Top\0bad\n", 9 };
		list.write(invalid.data(), static_cast<std::streamsize>(invalid.size()));
	}
	assert(!F4T::ModOrganizer::BuildPluginManifest(profile, mods, overwrite, data, root / "Fallout4.ccc", error));
	std::ofstream(profile / "modlist.txt", std::ios::trunc) << "+Top\n+Bottom\n-Disabled\n";
	std::ofstream(profile / "plugins.txt", std::ios::trunc) << "*test.esp\n*light.esp\n";
	const auto hashCache = root / "hash-cache.txt";
	const auto hashed = mods / "Top" / "test.esp";
	const auto writeCache = [&](const std::string& a_header, const std::filesystem::path& a_file) {
		const auto generic = std::filesystem::absolute(a_file).generic_u8string();
		std::ofstream(hashCache, std::ios::binary | std::ios::trunc) << a_header << "\n" << std::string(64, 'f') << '\t' << std::filesystem::file_size(a_file) << '\t' << std::filesystem::last_write_time(a_file).time_since_epoch().count() << '\t' << std::string{ generic.begin(), generic.end() } << '\n';
	};
	const auto uncached = F4T::ModOrganizer::BuildPluginManifest(profile, mods, overwrite, data, root / "Fallout4.ccc", error, {});
	const auto cachedFirst = F4T::ModOrganizer::BuildPluginManifest(profile, mods, overwrite, data, root / "Fallout4.ccc", error, {}, hashCache);
	assert(std::filesystem::exists(hashCache));
	const auto cachedSecond = F4T::ModOrganizer::BuildPluginManifest(profile, mods, overwrite, data, root / "Fallout4.ccc", error, {}, hashCache);
	assert(uncached && cachedFirst && cachedSecond && F4T::ModpackManifest::ComputeFingerprint(*uncached) == F4T::ModpackManifest::ComputeFingerprint(*cachedFirst) &&
		F4T::ModpackManifest::ComputeFingerprint(*cachedFirst) == F4T::ModpackManifest::ComputeFingerprint(*cachedSecond));
	writeCache("co-mo2-hash-cache-v1", hashed);
	const auto served = F4T::ModOrganizer::BuildPluginManifest(profile, mods, overwrite, data, root / "Fallout4.ccc", error, {}, hashCache);
	assert(served && std::any_of(served->entries.begin(), served->entries.end(), [](const auto& entry) { return entry.name == "test.esp" && entry.sha256Hex == std::string(64, 'f'); }));
	{
		std::ofstream file(hashed, std::ios::binary | std::ios::app);
		file << "changed";
	}
	const auto changed = F4T::ModOrganizer::BuildPluginManifest(profile, mods, overwrite, data, root / "Fallout4.ccc", error, {}, hashCache);
	assert(changed && std::any_of(changed->entries.begin(), changed->entries.end(), [&](const auto& entry) { return entry.name == "test.esp" && entry.sha256Hex == F4T::ContentManifest::Sha256HexFile(hashed); }));
	std::ofstream(hashCache, std::ios::binary | std::ios::trunc) << std::string(256, '\xff');
	const auto garbage = F4T::ModOrganizer::BuildPluginManifest(profile, mods, overwrite, data, root / "Fallout4.ccc", error, {}, hashCache);
	writeCache("co-mo2-hash-cache-v0", hashed);
	const auto wrongVersion = F4T::ModOrganizer::BuildPluginManifest(profile, mods, overwrite, data, root / "Fallout4.ccc", error, {}, hashCache);
	assert(garbage && wrongVersion && F4T::ModpackManifest::ComputeFingerprint(*garbage) == F4T::ModpackManifest::ComputeFingerprint(*changed) &&
		F4T::ModpackManifest::ComputeFingerprint(*wrongVersion) == F4T::ModpackManifest::ComputeFingerprint(*changed));
	const auto texture = mods / "Top" / "textures" / "test.dds";
	writeCache("co-mo2-hash-cache-v1", hashed);
	const auto originalTime = std::filesystem::last_write_time(hashed);
	{
		std::fstream file(hashed, std::ios::binary | std::ios::in | std::ios::out);
		file.seekp(-7, std::ios::end);
		file << "CHANGED";
	}
	std::filesystem::last_write_time(hashed, originalTime + std::chrono::seconds{ 1 });
	const auto rewritten = F4T::ModOrganizer::BuildPluginManifest(profile, mods, overwrite, data, root / "Fallout4.ccc", error, {}, hashCache);
	assert(rewritten && std::any_of(rewritten->entries.begin(), rewritten->entries.end(), [&](const auto& entry) { return entry.name == "test.esp" && entry.sha256Hex != std::string(64, 'f') && entry.sha256Hex == F4T::ContentManifest::Sha256HexFile(hashed); }));
	{
		std::fstream file(hashed, std::ios::binary | std::ios::in | std::ios::out);
		file.seekp(-7, std::ios::end);
		file << "changed";
	}
	std::filesystem::last_write_time(hashed, originalTime);
	writeCache("co-mo2-hash-cache-v1", texture);
	const auto servedTexture = F4T::ModOrganizer::BuildPluginManifest(profile, mods, overwrite, data, root / "Fallout4.ccc", error, {}, hashCache);
	assert(servedTexture && std::any_of(servedTexture->entries.begin(), servedTexture->entries.end(), [](const auto& entry) { return entry.name == "textures/test.dds" && entry.sha256Hex == std::string(64, 'f'); }));
	const auto nested = root / "nested";
	const auto nestedMods = nested / "mods";
	const auto nestedData = nested / "Data";
	const auto nestedOverwrite = nested / "overwrite";
	const auto nestedProfile = nested / "profile";
	std::filesystem::create_directories(nestedMods / "Mod" / "Data" / "meshes" / ".hidden");
	std::filesystem::create_directories(nestedMods / "Mod" / "normal" / "dir" / ".git");
	std::filesystem::create_directories(nestedMods / "Mod" / ".hidden");
	std::filesystem::create_directories(nestedData);
	std::filesystem::create_directories(nestedOverwrite);
	std::filesystem::create_directories(nestedProfile);
	std::ofstream(nestedProfile / "modlist.txt") << "+Mod\n";
	std::ofstream(nestedProfile / "plugins.txt");
	std::ofstream(nestedProfile / "loadorder.txt");
	std::ofstream(nestedMods / "Mod" / "Data" / "meshes" / ".hidden" / "x.nif") << "nested";
	std::ofstream(nestedMods / "Mod" / "normal" / "dir" / ".git" / "y.txt") << "git";
	std::ofstream(nestedMods / "Mod" / "normal" / "dir" / "z.txt") << "kept";
	std::ofstream(nestedMods / "Mod" / ".hidden" / "top.nif") << "top";
	const auto nestedDefaults = F4T::ModOrganizer::BuildPluginManifest(nestedProfile, nestedMods, nestedOverwrite, nestedData, nested / "Fallout4.ccc", error);
	assert(nestedDefaults && nestedDefaults->entries.size() == 3 && error.empty());
	assert(std::any_of(nestedDefaults->entries.begin(), nestedDefaults->entries.end(), [](const auto& entry) { return entry.name == "data/meshes/.hidden/x.nif" && entry.kind == F4T::ModpackManifest::EntryKind::asset; }));
	assert(std::any_of(nestedDefaults->entries.begin(), nestedDefaults->entries.end(), [](const auto& entry) { return entry.name == "normal/dir/z.txt" && entry.kind == F4T::ModpackManifest::EntryKind::asset; }));
	assert(std::any_of(nestedDefaults->entries.begin(), nestedDefaults->entries.end(), [](const auto& entry) { return entry.name == ".hidden/top.nif" && entry.kind == F4T::ModpackManifest::EntryKind::asset; }));
	assert(std::none_of(nestedDefaults->entries.begin(), nestedDefaults->entries.end(), [](const auto& entry) { return entry.name.find(".git") != std::string::npos; }));
	const auto nestedFiltered = F4T::ModOrganizer::BuildPluginManifest(nestedProfile, nestedMods, nestedOverwrite, nestedData, nested / "Fallout4.ccc", error,
		F4T::ModOrganizer::Filters{ { ".mohidden" }, { ".git", ".hidden" } });
	assert(nestedFiltered && nestedFiltered->entries.size() == 1 && nestedFiltered->entries[0].name == "normal/dir/z.txt");
	std::filesystem::remove_all(nested);
	const auto links = root / "links";
	const auto linkMods = links / "mods";
	const auto linkProfile = links / "profile";
	const auto linkOverwrite = links / "overwrite";
	const auto linkData = links / "Data";
	std::filesystem::create_directories(linkMods / "Mod");
	std::filesystem::create_directories(links / "outside");
	std::filesystem::create_directories(linkProfile);
	std::filesystem::create_directories(linkOverwrite);
	std::filesystem::create_directories(linkData);
	std::ofstream(links / "outside" / "secret.nif") << "outside";
	std::ofstream(linkMods / "Mod" / "kept.nif") << "kept";
	std::ofstream(linkProfile / "modlist.txt") << "+Mod\n";
	std::ofstream(linkProfile / "plugins.txt");
	std::ofstream(linkProfile / "loadorder.txt");
	std::error_code linkError;
	std::filesystem::create_directory_symlink(links / "outside", linkMods / "Mod" / "linked", linkError);
	if (!linkError) {
		assert(!F4T::ModOrganizer::BuildPluginManifest(linkProfile, linkMods, linkOverwrite, linkData, links / "Fallout4.ccc", error) &&
			error == "Cannot use linked or unreadable MO2 content");
		std::filesystem::remove(linkMods / "Mod" / "linked", linkError);
	}
	std::filesystem::remove_all(links);
	const auto bulk = root / "bulk";
	const auto bulkMods = bulk / "mods";
	const auto bulkTextures = bulkMods / "Bulk" / "textures";
	const auto bulkProfile = bulk / "profile";
	const auto bulkOverwrite = bulk / "overwrite";
	const auto bulkData = bulk / "Data";
	const auto bulkFile = [&](std::size_t a_index) { return bulkTextures / ("asset" + std::to_string(a_index) + ".dds"); };
	std::filesystem::create_directories(bulkTextures);
	std::filesystem::create_directories(bulkProfile);
	std::filesystem::create_directories(bulkData);
	std::filesystem::create_directories(bulkOverwrite);
	std::ofstream(bulkProfile / "modlist.txt") << "+Bulk\n";
	std::ofstream(bulkProfile / "plugins.txt");
	std::ofstream(bulkProfile / "loadorder.txt");
	for (std::size_t index = 0; index <= F4T::ModpackManifest::kMaximumProbeEntries; ++index) std::ofstream(bulkFile(index));
	const auto overflow = F4T::ModOrganizer::BuildPluginManifest(bulkProfile, bulkMods, bulkOverwrite, bulkData, bulk / "Fallout4.ccc", error);
	assert(!overflow && error == "The MO2 profile exceeds the client file limit");
	std::filesystem::remove(bulkFile(F4T::ModpackManifest::kMaximumProbeEntries));
	const auto atLimit = F4T::ModOrganizer::BuildPluginManifest(bulkProfile, bulkMods, bulkOverwrite, bulkData, bulk / "Fallout4.ccc", error);
	assert(atLimit && atLimit->entries.size() == F4T::ModpackManifest::kMaximumProbeEntries && error.empty());
	std::filesystem::remove_all(bulk);
	std::filesystem::remove_all(root);
}

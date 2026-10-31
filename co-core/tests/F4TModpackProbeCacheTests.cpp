#include "F4TModpackProbeCache.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
	using namespace F4T::ModpackProbeCache;

	constexpr std::string_view kFingerprint = "ed7002b439e9ac845f22357d822bac1444730fbdb6016d3ec9432297b9ec9f73";

	FileFact File(std::string a_path, std::uint64_t a_size, std::int64_t a_mtime)
	{
		return FileFact{ std::move(a_path), a_size, a_mtime };
	}

	Entry Cached()
	{
		Entry entry;
		entry.fingerprint = std::string{ kFingerprint };
		entry.files = { File("fallout4.esm", 100, 1000), File("a.esp", 200, 2000), File("f4se/plugins/co.dll", 300, 3000) };
		entry.loadedPlugins = { "Fallout4.esm", "A.esp" };
		return entry;
	}

	std::vector<FileFact> CurrentFiles()
	{
		return { File("fallout4.esm", 100, 1000), File("a.esp", 200, 2000), File("f4se/plugins/co.dll", 300, 3000) };
	}

	void TestRoundTrip()
	{
		const auto serialized = SerializeEntry(Cached());
		assert(serialized.has_value());
		const auto parsed = ParseEntry(*serialized);
		assert(parsed.has_value());
		assert(parsed->fingerprint == std::string{ kFingerprint });
		assert(parsed->files.size() == 3);
		assert(parsed->loadedPlugins.size() == 2);
	}

	void TestHitAndCaseInsensitivePlugins()
	{
		assert(Validate(Cached(), CurrentFiles(), { "fallout4.ESM", "a.ESP" }) == Match::hit);
	}

	void TestSizeChange()
	{
		auto files = CurrentFiles();
		files[1].sizeBytes = 201;
		assert(Validate(Cached(), files, Cached().loadedPlugins) == Match::changedFile);
	}

	void TestMtimeChange()
	{
		auto files = CurrentFiles();
		files[2].modifiedTicks = 3001;
		assert(Validate(Cached(), files, Cached().loadedPlugins) == Match::changedFile);
	}

	void TestMissingFile()
	{
		auto files = CurrentFiles();
		files[1].sizeBytes = kMissingFileSize;
		assert(Validate(Cached(), files, Cached().loadedPlugins) == Match::missingFile);
		files.pop_back();
		assert(Validate(Cached(), files, Cached().loadedPlugins) == Match::missingFile);
	}

	void TestPluginAdded()
	{
		assert(Validate(Cached(), CurrentFiles(), { "Fallout4.esm", "A.esp", "Extra.esp" }) == Match::changedPlugins);
	}

	void TestPluginRemoved()
	{
		assert(Validate(Cached(), CurrentFiles(), { "Fallout4.esm" }) == Match::changedPlugins);
	}

	void TestPluginOrderChanged()
	{
		assert(Validate(Cached(), CurrentFiles(), { "A.esp", "Fallout4.esm" }) == Match::changedPlugins);
	}

	void TestEmptyEntryIsNotHit()
	{
		Entry empty;
		assert(Validate(empty, CurrentFiles(), {}) == Match::empty);
		assert(!SerializeEntry(empty).has_value());
	}

	void TestMalformedEntryIsRejected()
	{
		const auto good = SerializeEntry(Cached());
		assert(good.has_value());
		assert(!ParseEntry("").has_value());
		assert(!ParseEntry("{").has_value());
		assert(!ParseEntry("[]").has_value());
		assert(!ParseEntry(*good + std::string(kMaximumBlobBytes, ' ')).has_value());
		std::string badHash = *good;
		const auto position = badHash.find(std::string{ kFingerprint });
		assert(position != std::string::npos);
		badHash.replace(position, 64, std::string(64, 'z'));
		assert(!ParseEntry(badHash).has_value());
		std::string badPath = *good;
		const auto pathPosition = badPath.find("fallout4.esm");
		assert(pathPosition != std::string::npos);
		badPath.replace(pathPosition, 12, "../escape.esm");
		assert(!ParseEntry(badPath).has_value());
		std::string duplicate = *good;
		const std::string duplicateNeedle = "\"path\":\"a.esp\"";
		const auto duplicatePosition = duplicate.find(duplicateNeedle);
		assert(duplicatePosition != std::string::npos);
		duplicate.replace(duplicatePosition, duplicateNeedle.size(), "\"path\":\"fallout4.esm\"");
		assert(!ParseEntry(duplicate).has_value());
	}

	void TestOversizedSerializationIsRejected()
	{
		Entry entry = Cached();
		entry.files.clear();
		for (std::size_t index = 0; index < kMaximumFiles; ++index) {
			entry.files.push_back(File("file" + std::to_string(index) + std::string(250, 'x'), index, static_cast<std::int64_t>(index)));
		}
		assert(!SerializeEntry(entry).has_value());
	}
}

int main()
{
	TestRoundTrip();
	TestHitAndCaseInsensitivePlugins();
	TestSizeChange();
	TestMtimeChange();
	TestMissingFile();
	TestPluginAdded();
	TestPluginRemoved();
	TestPluginOrderChanged();
	TestEmptyEntryIsNotHit();
	TestMalformedEntryIsRejected();
	TestOversizedSerializationIsRejected();
	return 0;
}

#include "F4TClientCompatibility.h"
#include "F4TContentManifest.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using F4T::ClientCompatibility::BuildTarget;
	using F4T::ClientCompatibility::Metadata;
	using F4T::ContentManifest::Entry;

	constexpr std::string_view kValidFingerprint =
		"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

	constexpr std::string_view kExpectedFixtureFingerprint =
		"fe2a5a579905dc21b00d894953f25cbf874ad025e2145e8afa429ce9d51b0c8b";

	void TestCanonicalConstruction()
	{
		const auto& ae = F4T::ClientCompatibility::AE();
		const auto& ng = F4T::ClientCompatibility::NG();
		const auto& og = F4T::ClientCompatibility::OG();
		const auto& current = F4T::ClientCompatibility::Current();
		const auto& oldGen = F4T::ClientCompatibility::OldGen();

		assert(ae.buildTarget == BuildTarget::AE);
		assert(ng.buildTarget == BuildTarget::NG);
		assert(og.buildTarget == BuildTarget::OG);
		assert(current.buildTarget == BuildTarget::Current);
		assert(oldGen.buildTarget == BuildTarget::OldGen);
		assert(current.clientVersion == F4T::ClientCompatibility::kClientVersion);
		assert(oldGen.clientVersion == F4T::ClientCompatibility::kClientVersion);
		assert(current.falloutRuntimeVersion == F4T::ClientCompatibility::kCurrentRuntimeVersion);
		assert(oldGen.falloutRuntimeVersion == F4T::ClientCompatibility::kOldGenRuntimeVersion);
		assert(ae.falloutRuntimeVersion == "1.11.240");
		assert(ng.falloutRuntimeVersion == "1.10.984");
		assert(og.falloutRuntimeVersion == "1.10.163");
		assert(std::addressof(current) == std::addressof(ae));
		assert(std::addressof(oldGen) == std::addressof(og));

		assert(std::addressof(F4T::ClientCompatibility::Current()) == std::addressof(current));
		assert(std::addressof(F4T::ClientCompatibility::OldGen()) == std::addressof(oldGen));

		assert(current.clientVersion == oldGen.clientVersion);
		assert(current.capabilities == oldGen.capabilities);
		assert(current.capabilities.empty());
		assert(current.contentManifestFingerprint == oldGen.contentManifestFingerprint);
		assert(current.falloutRuntimeVersion != oldGen.falloutRuntimeVersion);

		assert(!F4T::ClientCompatibility::IsComplete(current));
		assert(!F4T::ClientCompatibility::IsComplete(oldGen));

		Metadata complete = current;
		complete.contentManifestFingerprint = std::string{ kValidFingerprint };
		assert(F4T::ClientCompatibility::IsComplete(complete));
	}

	void TestTargetSelection()
	{
		assert(!F4T::ClientCompatibility::TargetFromSelection(false, false).has_value());
		assert(!F4T::ClientCompatibility::TargetFromSelection(true, true).has_value());
		assert(F4T::ClientCompatibility::TargetFromSelection(true, false) == BuildTarget::Current);
		assert(F4T::ClientCompatibility::TargetFromSelection(false, true) == BuildTarget::OldGen);
		assert(!F4T::ClientCompatibility::TargetFromSelection(false, false, false).has_value());
		assert(!F4T::ClientCompatibility::TargetFromSelection(true, true, false).has_value());
		assert(F4T::ClientCompatibility::TargetFromSelection(true, false, false) == BuildTarget::AE);
		assert(F4T::ClientCompatibility::TargetFromSelection(false, true, false) == BuildTarget::NG);
		assert(F4T::ClientCompatibility::TargetFromSelection(false, false, true) == BuildTarget::OG);
		assert(F4T::ClientCompatibility::TargetFromRuntime("1.11.240") == BuildTarget::AE);
		assert(F4T::ClientCompatibility::TargetFromRuntime("1.10.984") == BuildTarget::NG);
		assert(F4T::ClientCompatibility::TargetFromRuntime("1.10.163") == BuildTarget::OG);
		assert(!F4T::ClientCompatibility::TargetFromRuntime("1.11.221").has_value());
		assert(F4T::ClientCompatibility::IsRuntimeCompatible(BuildTarget::AE, "1.11.240"));
		assert(F4T::ClientCompatibility::IsRuntimeCompatible(BuildTarget::NG, "1.10.984"));
		assert(F4T::ClientCompatibility::IsRuntimeCompatible(BuildTarget::OG, "1.10.163"));
		assert(!F4T::ClientCompatibility::IsRuntimeCompatible(BuildTarget::AE, "1.11.221"));

		assert(!F4T::ClientCompatibility::ResolveCompileTimeTarget().has_value());
		assert(!F4T::ClientCompatibility::kBuildTargetSelected);

		assert(F4T::ClientCompatibility::Find(BuildTarget::Current) ==
		       std::addressof(F4T::ClientCompatibility::Current()));
		assert(F4T::ClientCompatibility::Find(BuildTarget::OldGen) ==
		       std::addressof(F4T::ClientCompatibility::OldGen()));

		assert(F4T::ClientCompatibility::Find(static_cast<BuildTarget>(0)) == nullptr);
		assert(F4T::ClientCompatibility::Find(static_cast<BuildTarget>(99)) == nullptr);
	}

	void TestHelloProjectionIsDeterministic()
	{
		Metadata metadata = F4T::ClientCompatibility::Current();
		metadata.contentManifestFingerprint = std::string{ kValidFingerprint };
		metadata.capabilities = { "player-state-v1", "interest-v1", "player-state-v1", "b", "a" };

		const auto first = F4T::ClientCompatibility::HelloFields(metadata);
		const auto second = F4T::ClientCompatibility::HelloFields(metadata);
		assert(first.has_value());
		assert(second.has_value());
		assert(first->dump() == second->dump());

	assert(first->dump() == R"({"capabilities":["a","b","interest-v1","player-state-v1","save-attestation-v1"],"clientVersion":"0.1.0","contentManifestFingerprint":"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","falloutRuntimeVersion":"1.11.240"})");
		assert(first->size() == 4);
		assert(first->at("clientVersion") == "0.1.0");
	assert(first->at("falloutRuntimeVersion") == "1.11.240");
		assert(first->at("contentManifestFingerprint").get<std::string>() == std::string{ kValidFingerprint });
		assert(first->at("capabilities").size() == 5);

		Metadata oldGen = F4T::ClientCompatibility::OldGen();
		oldGen.contentManifestFingerprint = std::string{ kValidFingerprint };
		const auto oldGenHello = F4T::ClientCompatibility::HelloFields(oldGen);
		assert(oldGenHello.has_value());
		assert(oldGenHello->at("falloutRuntimeVersion") == "1.10.163");
		assert(oldGenHello->at("clientVersion") == first->at("clientVersion"));
		assert(oldGenHello->at("contentManifestFingerprint") == first->at("contentManifestFingerprint"));
	}

	void TestIncompleteMetadataFailsClosed()
	{
		Metadata metadata = F4T::ClientCompatibility::Current();

		assert(!F4T::ClientCompatibility::IsComplete(metadata));
		assert(!F4T::ClientCompatibility::HelloFields(metadata).has_value());

		metadata.contentManifestFingerprint = std::string(63, 'a');
		assert(!F4T::ClientCompatibility::IsComplete(metadata));
		assert(!F4T::ClientCompatibility::HelloFields(metadata).has_value());

		metadata.contentManifestFingerprint = std::string(64, 'A');
		assert(!F4T::ClientCompatibility::IsComplete(metadata));
		assert(!F4T::ClientCompatibility::HelloFields(metadata).has_value());

		metadata.contentManifestFingerprint = std::string{ kValidFingerprint };
		metadata.clientVersion.clear();
		assert(!F4T::ClientCompatibility::IsComplete(metadata));
		assert(!F4T::ClientCompatibility::HelloFields(metadata).has_value());
	}

	void TestSha256FipsVectors()
	{
		using F4T::ContentManifest::Sha256Hex;
		assert(Sha256Hex("") ==
		       "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
		assert(Sha256Hex("abc") ==
		       "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
		assert(Sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
		       "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
		assert(Sha256Hex(std::string(1000000, 'a')) ==
		       "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
	}

	void TestSha256HexFile()
	{
		using F4T::ContentManifest::Sha256Hex;
		using F4T::ContentManifest::Sha256HexFile;

		const auto scratch = std::filesystem::temp_directory_path() /
			("co-content-manifest-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::error_code error;
		std::filesystem::remove_all(scratch, error);
		std::filesystem::create_directories(scratch, error);
		assert(!error);

		{
			const auto path = scratch / "sample.txt";
			std::ofstream stream(path, std::ios::binary);
			stream << "abc";
			stream.close();
			assert(Sha256HexFile(path) == Sha256Hex("abc"));

			const auto missing = scratch / "missing.bin";
			assert(!Sha256HexFile(missing).has_value());
		}

		std::filesystem::remove_all(scratch, error);
	}

	void TestContentFingerprint()
	{
		using F4T::ContentManifest::ComputeFingerprint;
		using F4T::ContentManifest::Sha256Hex;

		const Entry esp{ "CommonwealthOnline.esp", "ed7002b439e9ac845f22357d822bac1444730fbdb6016d3ec9432297b9ec9f73" };
		const Entry script{ "Scripts/COCharacterSetupQuestScript.pex", "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb" };

		const auto forward = ComputeFingerprint(std::array<Entry, 2>{ esp, script });
		const auto reversed = ComputeFingerprint(std::array<Entry, 2>{ script, esp });
		assert(forward.has_value());
		assert(reversed == forward);

		assert(*forward == kExpectedFixtureFingerprint);

		const Entry windowsPath{ R"(.\CommonwealthOnline.esp)", esp.sha256Hex };
		assert(ComputeFingerprint(std::array<Entry, 1>{ windowsPath }) ==
		       ComputeFingerprint(std::array<Entry, 1>{ esp }));

		assert(ComputeFingerprint(std::array<Entry, 2>{ esp, script }) == forward);

		Entry changedHash = esp;
		changedHash.sha256Hex = Sha256Hex("other");
		assert(ComputeFingerprint(std::array<Entry, 2>{ esp, script }) !=
		       ComputeFingerprint(std::array<Entry, 2>{ changedHash, script }));
	}

	void TestContentFingerprintFailsClosed()
	{
		using F4T::ContentManifest::ComputeFingerprint;

		const Entry ok{ "CommonwealthOnline.esp", "ed7002b439e9ac845f22357d822bac1444730fbdb6016d3ec9432297b9ec9f73" };

		assert(!ComputeFingerprint({}).has_value());

		Entry badHex = ok;
		badHex.sha256Hex = "NOTHEX";
		assert(!ComputeFingerprint(std::array<Entry, 1>{ badHex }).has_value());

		Entry upperHex = ok;
		upperHex.sha256Hex = std::string(64, 'E');
		assert(!ComputeFingerprint(std::array<Entry, 1>{ upperHex }).has_value());

		Entry emptyPath = ok;
		emptyPath.relativePath.clear();
		assert(!ComputeFingerprint(std::array<Entry, 1>{ emptyPath }).has_value());

		Entry parentPath = ok;
		parentPath.relativePath = "../Data/CommonwealthOnline.esp";
		assert(!ComputeFingerprint(std::array<Entry, 1>{ parentPath }).has_value());

		Entry duplicate = ok;
		assert(!ComputeFingerprint(std::array<Entry, 2>{ ok, duplicate }).has_value());
	}

	void TestFingerprintValidation()
	{
		using F4T::ContentManifest::IsValidFingerprint;
		assert(IsValidFingerprint(kValidFingerprint));
		assert(!IsValidFingerprint(std::string(63, 'a')));
		assert(!IsValidFingerprint(std::string(65, 'a')));
		assert(!IsValidFingerprint(std::string(64, 'E')));
		assert(!IsValidFingerprint(std::string(64, 'g')));
		assert(!IsValidFingerprint(""));
	}

	void TestPackagedClientManifest()
	{
		const auto manifest = F4T::ClientCompatibility::PackagedClientManifest(kValidFingerprint);
		assert(manifest.has_value());
		assert(manifest->at("clientVersion") == std::string{ F4T::ClientCompatibility::kClientVersion });
		assert(manifest->at("contentManifestFingerprint").get<std::string>() == std::string{ kValidFingerprint });
		// Deterministic: the same fingerprint always yields the same two fields.
		assert(manifest->size() == 2);
		assert(*F4T::ClientCompatibility::PackagedClientManifest(kValidFingerprint) == *manifest);
	}

	void TestPackagedClientManifestFailsClosed()
	{
		// Any fingerprint the release pipeline could not have produced must not
		// be packaged: empty, wrong length, non-hex, or uppercase output.
		assert(!F4T::ClientCompatibility::PackagedClientManifest(""));
		assert(!F4T::ClientCompatibility::PackagedClientManifest(std::string(63, 'a')));
		assert(!F4T::ClientCompatibility::PackagedClientManifest(std::string(64, 'E')));
		assert(!F4T::ClientCompatibility::PackagedClientManifest(std::string(64, 'g')));
	}

	void TestEntryValidation()
	{
		using F4T::ContentManifest::IsValidEntry;
		assert(IsValidEntry(Entry{ "CommonwealthOnline.esp", "ed7002b439e9ac845f22357d822bac1444730fbdb6016d3ec9432297b9ec9f73" }));
		assert(IsValidEntry(Entry{ R"(Scripts\CommonwealthOnlineDebug.pex)", "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb" }));
		assert(!IsValidEntry(Entry{ "bad", "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48b" }));
		assert(!IsValidEntry(Entry{ "/CommonwealthOnline.esp", "ed7002b439e9ac845f22357d822bac1444730fbdb6016d3ec9432297b9ec9f73" }));
		assert(!IsValidEntry(Entry{ R"(\Scripts\CommonwealthOnlineDebug.pex)", "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb" }));
	}
}

int main()
{
	TestCanonicalConstruction();
	TestTargetSelection();
	TestHelloProjectionIsDeterministic();
	TestIncompleteMetadataFailsClosed();
	TestSha256FipsVectors();
	TestSha256HexFile();
	TestContentFingerprint();
	TestContentFingerprintFailsClosed();
	TestFingerprintValidation();
	TestEntryValidation();
	TestPackagedClientManifest();
	TestPackagedClientManifestFailsClosed();
	return 0;
}

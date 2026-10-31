#include "F4TModpackInstall.h"

#include "F4TContentManifest.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace F4T::ModpackInstall;

	struct Crash : std::runtime_error
	{
		Crash() :
			std::runtime_error("crash")
		{}
	};

	std::string Key(const std::filesystem::path& a_path)
	{
		auto text = a_path.lexically_normal().generic_string();
		for (auto& character : text) {
			if (character >= 'A' && character <= 'Z') {
				character = static_cast<char>(character - 'A' + 'a');
			}
		}
		return text;
	}

	class FakeFileSystem final : public FileSystem
	{
	public:
		std::map<std::string, std::string> files;
		std::map<std::string, std::string> names;
		std::set<std::string> directories;
		std::set<std::string> links;
		int crashAt{ -1 };
		int mutations{};
		bool failMoves{};

		void Mutate()
		{
			++mutations;
			if (crashAt >= 0 && mutations >= crashAt) {
				throw Crash{};
			}
		}

		void Put(const std::filesystem::path& a_path, std::string a_bytes)
		{
			files[Key(a_path)] = std::move(a_bytes);
			names[Key(a_path)] = a_path.filename().string();
		}

		bool Exists(const std::filesystem::path& a_path) override
		{
			return files.contains(Key(a_path)) || directories.contains(Key(a_path));
		}

		bool IsPlainFile(const std::filesystem::path& a_path) override
		{
			return files.contains(Key(a_path)) && !links.contains(Key(a_path));
		}

		bool IsPlainDirectory(const std::filesystem::path& a_path) override
		{
			return directories.contains(Key(a_path)) && !links.contains(Key(a_path));
		}

		bool CreateDirectories(const std::filesystem::path& a_path) override
		{
			directories.insert(Key(a_path));
			return !links.contains(Key(a_path));
		}

		bool CreateNewDirectory(const std::filesystem::path& a_path) override
		{
			Mutate();
			return directories.insert(Key(a_path)).second;
		}

		bool MoveNoReplace(const std::filesystem::path& a_from, const std::filesystem::path& a_to) override
		{
			Mutate();
			if (failMoves || !files.contains(Key(a_from)) || Exists(a_to)) {
				return false;
			}
			files[Key(a_to)] = files[Key(a_from)];
			names[Key(a_to)] = a_to.filename().string();
			files.erase(Key(a_from));
			names.erase(Key(a_from));
			return true;
		}

		bool WriteDurably(const std::filesystem::path& a_path, std::string_view a_bytes) override
		{
			Mutate();
			Put(a_path, std::string{ a_bytes });
			return true;
		}

		std::optional<std::string> Read(const std::filesystem::path& a_path, std::size_t a_maximumBytes) override
		{
			const auto found = files.find(Key(a_path));
			if (found == files.end() || found->second.size() > a_maximumBytes) {
				return std::nullopt;
			}
			return found->second;
		}

		bool Remove(const std::filesystem::path& a_path) override
		{
			Mutate();
			files.erase(Key(a_path));
			names.erase(Key(a_path));
			directories.erase(Key(a_path));
			return true;
		}

		bool Matches(const std::filesystem::path& a_path, std::string_view a_sha256Hex, std::uint64_t a_sizeBytes) override
		{
			const auto found = files.find(Key(a_path));
			return found != files.end() && found->second.size() == a_sizeBytes && F4T::ContentManifest::Sha256Hex(found->second) == a_sha256Hex;
		}

		std::optional<std::string> ActualName(const std::filesystem::path& a_path) override
		{
			const auto found = names.find(Key(a_path));
			return found == names.end() ? std::nullopt : std::optional<std::string>{ found->second };
		}

		bool SameVolume(const std::filesystem::path&, const std::filesystem::path&) override
		{
			return true;
		}

		std::optional<std::uint64_t> FreeBytes(const std::filesystem::path&) override
		{
			return 1ull << 40;
		}

		std::vector<std::string> List(const std::filesystem::path& a_directory) override
		{
			std::vector<std::string> listed;
			const auto prefix = Key(a_directory) + "/";
			for (const auto& [key, name] : names) {
				if (key.starts_with(prefix) && key.find('/', prefix.size()) == std::string::npos) {
					listed.push_back(name);
				}
			}
			return listed;
		}
	};

	const std::filesystem::path kGame{ "C:/Games/Fallout 4" };
	const std::filesystem::path kPluginsTxt{ "C:/Users/Player/AppData/Local/Fallout4/plugins.txt" };
	const std::string kFingerprint(64, 'f');
	const std::string kOriginalPluginsTxt = "# This file is used by Fallout4 to keep track of your downloaded content.\r\n*Unofficial Patch.esp\r\n*MyMod.esp\r\n*Extra.esp\r\n";
	const std::string kNewPlugin = "new plugin bytes";
	const std::string kNewArchive = "new archive bytes";

	struct Scenario
	{
		FakeFileSystem fileSystem;
		Paths paths{ PathsForGame(kGame, kPluginsTxt) };
		InstallPlan plan;
	};

	Scenario MakeScenario()
	{
		Scenario scenario;
		auto& fs = scenario.fileSystem;
		const auto& paths = scenario.paths;
		for (const auto& directory : { paths.dataDirectory, paths.stagingRoot, paths.StagingFor(kFingerprint), paths.installDirectory, kPluginsTxt.parent_path() }) {
			fs.directories.insert(Key(directory));
		}
		fs.Put(paths.dataDirectory / "Fallout4.esm", "official");
		fs.Put(paths.dataDirectory / "Unofficial Patch.esp", "patch");
		fs.Put(paths.dataDirectory / "MyMod.esp", "old plugin bytes");
		fs.Put(paths.dataDirectory / "Extra.esp", "extra");
		fs.Put(kPluginsTxt, kOriginalPluginsTxt);
		fs.Put(paths.StagingFor(kFingerprint) / "mymod.esp", kNewPlugin);
		fs.Put(paths.StagingFor(kFingerprint) / "mymod - main.ba2", kNewArchive);
		scenario.plan = InstallPlan{
			kFingerprint,
			"server-key",
			"example.org:7777",
			ServerEndpoint{ "example.org", 7777, "" },
			{
				PlannedFile{ "mymod.esp", F4T::ContentManifest::Sha256Hex(kNewPlugin), kNewPlugin.size(), true },
				PlannedFile{ "mymod - main.ba2", F4T::ContentManifest::Sha256Hex(kNewArchive), kNewArchive.size(), false },
			},
			{ "fallout4.esm", "unofficial patch.esp", "mymod.esp" },
			true
		};
		return scenario;
	}

	void ExpectOriginalState(Scenario& a_scenario)
	{
		auto& fs = a_scenario.fileSystem;
		const auto& paths = a_scenario.paths;
		assert(fs.Read(paths.dataDirectory / "MyMod.esp", 1 << 20) == std::optional<std::string>{ "old plugin bytes" });
		assert(!fs.Exists(paths.dataDirectory / "mymod - main.ba2"));
		assert(fs.Read(kPluginsTxt, 1 << 20) == std::optional<std::string>{ kOriginalPluginsTxt });
		assert(fs.Read(paths.StagingFor(kFingerprint) / "mymod.esp", 1 << 20) == std::optional<std::string>{ kNewPlugin });
		assert(fs.Read(paths.StagingFor(kFingerprint) / "mymod - main.ba2", 1 << 20) == std::optional<std::string>{ kNewArchive });
		assert(!fs.Exists(paths.JournalFile()));
	}

	void TestPlanRoundTripAndStrictParsing()
	{
		const auto scenario = MakeScenario();
		const auto text = SerializePlan(scenario.plan);
		const auto parsed = ParsePlan(text);
		assert(parsed && parsed->files.size() == 2 && parsed->files[0].replaces && parsed->loadOrder.size() == 3);
		assert(parsed->serverLabel == "example.org:7777" && parsed->endpoint.host == "example.org" && parsed->endpoint.port == 7777);
		auto bad = scenario.plan;
		bad.files[0].name = "..\\evil.esp";
		assert(!ParsePlan(SerializePlan(bad)));
		bad = scenario.plan;
		bad.files[0].name = "Fallout4.esm";
		assert(!ParsePlan(SerializePlan(bad)));
		bad = scenario.plan;
		bad.fingerprint = "../../Windows";
		assert(!ParsePlan(SerializePlan(bad)));
		bad = scenario.plan;
		bad.files.push_back(bad.files[0]);
		bad.files.back().name = "MYMOD.ESP";
		assert(!ParsePlan(SerializePlan(bad)));
		bad = scenario.plan;
		bad.files[0].sizeBytes = 0;
		assert(!ParsePlan(SerializePlan(bad)));
		assert(!ParsePlan("{}"));
		assert(!ParsePlan(std::string(kMaximumPlanBytes + 1, ' ')));
	}

	void TestApplyInstallsBacksUpAndActivates()
	{
		auto scenario = MakeScenario();
		Installer installer(scenario.fileSystem, scenario.paths);
		assert(installer.WritePendingPlan(scenario.plan));
		const auto result = installer.ApplyPending(1);
		assert(result.outcome == Outcome::applied);
		assert(result.files == 2);
		assert(result.serverKey == "server-key" && result.endpoint.port == 7777);
		assert(result.deactivated == std::vector<std::string>{ "Extra.esp" });
		auto& fs = scenario.fileSystem;
		const auto& paths = scenario.paths;
		assert(fs.Read(paths.dataDirectory / "mymod.esp", 1 << 20) == std::optional<std::string>{ kNewPlugin });
		assert(fs.Read(paths.dataDirectory / "mymod - main.ba2", 1 << 20) == std::optional<std::string>{ kNewArchive });
		assert(fs.Read(paths.backupRoot / "ffffffffffff-1" / "mymod.esp", 1 << 20) == std::optional<std::string>{ "old plugin bytes" });
		assert(fs.Read(paths.backupRoot / "ffffffffffff-1" / "plugins.txt", 1 << 20) == std::optional<std::string>{ kOriginalPluginsTxt });
		const auto pluginsTxt = fs.Read(kPluginsTxt, 1 << 20);
		assert(pluginsTxt && pluginsTxt->find("*Unofficial Patch.esp\r\n*MyMod.esp\r\nExtra.esp\r\n") != std::string::npos);
		assert(!installer.HasPendingPlan());
		assert(installer.IsVerifying());

		const auto confirmed = installer.ConfirmVerified();
		assert(confirmed.outcome == Outcome::applied);
		assert(!installer.IsVerifying());
		assert(fs.Exists(paths.LastInstall()));

		const auto restored = installer.RestoreLastInstall();
		assert(restored.outcome == Outcome::restored);
		ExpectOriginalState(scenario);
		assert(!fs.Exists(paths.LastInstall()));
	}

	void TestGameThatNeverFinishesLoadingIsRolledBack()
	{
		auto scenario = MakeScenario();
		Installer installer(scenario.fileSystem, scenario.paths);
		assert(installer.WritePendingPlan(scenario.plan));
		assert(installer.ApplyPending(1).outcome == Outcome::applied);
		Installer nextStart(scenario.fileSystem, scenario.paths);
		const auto recovered = nextStart.Recover();
		assert(recovered.outcome == Outcome::rolledBack);
		assert(recovered.detail.find("did not finish loading") != std::string::npos);
		ExpectOriginalState(scenario);
		assert(nextStart.ApplyPending(2).outcome == Outcome::nothingToDo);
	}

	void TestCrashAtEveryStepIsUndoneAndNeverRetried()
	{
		auto probe = MakeScenario();
		{
			Installer installer(probe.fileSystem, probe.paths);
			assert(installer.WritePendingPlan(probe.plan));
			probe.fileSystem.mutations = 0;
			assert(installer.ApplyPending(1).outcome == Outcome::applied);
		}
		const int steps = probe.fileSystem.mutations;
		assert(steps >= 8);
		for (int crashAt = 1; crashAt <= steps; ++crashAt) {
			auto scenario = MakeScenario();
			Installer installer(scenario.fileSystem, scenario.paths);
			assert(installer.WritePendingPlan(scenario.plan));
			scenario.fileSystem.mutations = 0;
			scenario.fileSystem.crashAt = crashAt;
			bool crashed = false;
			try {
				(void)installer.ApplyPending(1);
			} catch (const Crash&) {
				crashed = true;
			}
			assert(crashed);
			scenario.fileSystem.crashAt = -1;
			Installer nextStart(scenario.fileSystem, scenario.paths);
			const auto recovered = nextStart.Recover();
			assert(recovered.outcome == Outcome::rolledBack || recovered.outcome == Outcome::nothingToDo);
			ExpectOriginalState(scenario);
			const auto retried = nextStart.ApplyPending(2).outcome;
			assert(crashAt == 1 ? retried == Outcome::applied : retried == Outcome::nothingToDo);
		}
	}

	void TestCrashDuringRollbackIsFinishedOnTheNextStart()
	{
		auto scenario = MakeScenario();
		Installer installer(scenario.fileSystem, scenario.paths);
		assert(installer.WritePendingPlan(scenario.plan));
		assert(installer.ApplyPending(1).outcome == Outcome::applied);
		scenario.fileSystem.mutations = 0;
		scenario.fileSystem.crashAt = 2;
		try {
			(void)Installer(scenario.fileSystem, scenario.paths).Recover();
			assert(false);
		} catch (const Crash&) {
		}
		scenario.fileSystem.crashAt = -1;
		assert(Installer(scenario.fileSystem, scenario.paths).Recover().outcome == Outcome::rolledBack);
		ExpectOriginalState(scenario);
	}

	void TestFailedMoveIsUndoneImmediately()
	{
		auto scenario = MakeScenario();
		Installer installer(scenario.fileSystem, scenario.paths);
		assert(installer.WritePendingPlan(scenario.plan));
		scenario.fileSystem.failMoves = true;
		const auto result = installer.ApplyPending(1);
		assert(result.outcome == Outcome::failed);
		scenario.fileSystem.failMoves = false;
		ExpectOriginalState(scenario);
	}

	void TestUnsafeTargetsAndChangedStagingAreRefused()
	{
		{
			auto scenario = MakeScenario();
			scenario.fileSystem.names[Key(scenario.paths.dataDirectory / "MyMod.esp")] = "Other.esp";
			Installer installer(scenario.fileSystem, scenario.paths);
			assert(installer.WritePendingPlan(scenario.plan));
			const auto result = installer.ApplyPending(1);
			assert(result.outcome == Outcome::refused && result.detail.find("resolves to a different file") != std::string::npos);
			ExpectOriginalState(scenario);
		}
		{
			auto scenario = MakeScenario();
			scenario.fileSystem.links.insert(Key(scenario.paths.dataDirectory));
			Installer installer(scenario.fileSystem, scenario.paths);
			assert(installer.WritePendingPlan(scenario.plan));
			assert(installer.ApplyPending(1).outcome == Outcome::refused);
			ExpectOriginalState(scenario);
		}
		{
			auto scenario = MakeScenario();
			scenario.fileSystem.Put(scenario.paths.StagingFor(kFingerprint) / "mymod.esp", "tampered");
			Installer installer(scenario.fileSystem, scenario.paths);
			assert(installer.WritePendingPlan(scenario.plan));
			const auto result = installer.ApplyPending(1);
			assert(result.outcome == Outcome::failed && result.detail.find("no longer matches") != std::string::npos);
			assert(!installer.HasPendingPlan());
		}
		{
			auto scenario = MakeScenario();
			scenario.fileSystem.Put(scenario.paths.PendingPlan(), "{ not json");
			Installer installer(scenario.fileSystem, scenario.paths);
			assert(installer.ApplyPending(1).outcome == Outcome::failed);
			assert(!installer.HasPendingPlan());
			ExpectOriginalState(scenario);
		}
	}

	void TestRestoreRequestFileUndoesTheLastInstall()
	{
		auto scenario = MakeScenario();
		Installer installer(scenario.fileSystem, scenario.paths);
		assert(installer.WritePendingPlan(scenario.plan));
		assert(installer.ApplyPending(1).outcome == Outcome::applied);
		assert(installer.ConfirmVerified().outcome == Outcome::applied);
		scenario.fileSystem.Put(scenario.paths.RestoreRequest(), "");
		const auto result = Installer(scenario.fileSystem, scenario.paths).Recover();
		assert(result.outcome == Outcome::restored);
		ExpectOriginalState(scenario);
		assert(!scenario.fileSystem.Exists(scenario.paths.RestoreRequest()));
		assert(Installer(scenario.fileSystem, scenario.paths).RestoreLastInstall().outcome == Outcome::nothingToDo);
	}

	void TestJournalParsingIsStrict()
	{
		Journal journal{ kFingerprint, "key", "label", ServerEndpoint{ "h", 1, "" }, "ffffffffffff-1", JournalState::verifying, { JournalOp{ "mymod.esp", OpStep::placed, true } }, true, true };
		const auto parsed = ParseJournal(SerializeJournal(journal));
		assert(parsed && parsed->state == JournalState::verifying && parsed->ops.size() == 1 && parsed->ops[0].step == OpStep::placed);
		journal.backupFolder = "..\\..\\Windows";
		assert(!ParseJournal(SerializeJournal(journal)));
		journal.backupFolder = "ffffffffffff-1";
		journal.ops[0].name = "C:\\x.esp";
		assert(!ParseJournal(SerializeJournal(journal)));
	}

	void TestModManagerDetection()
	{
		assert(DetectModManager(true, {}) == ModManager::modOrganizer);
		assert(DetectModManager(false, { "Fallout4.esm", "vortex.deployment.json" }) == ModManager::vortex);
		assert(DetectModManager(false, { "Vortex.Deployment.Fallout4.JSON" }) == ModManager::vortex);
		assert(DetectModManager(false, { "Fallout4.esm" }) == ModManager::none);
	}

	void TestNativeFileSystemInstallAndRollback()
	{
		const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) / ("co-modpack-install-test-" + std::to_string(std::random_device{}()));
		std::filesystem::remove_all(root);
		const auto game = root / "Fallout 4";
		const auto pluginsTxt = root / "Local" / "plugins.txt";
		const auto paths = PathsForGame(game, pluginsTxt);
		std::filesystem::create_directories(paths.dataDirectory);
		std::filesystem::create_directories(paths.StagingFor(kFingerprint));
		std::filesystem::create_directories(pluginsTxt.parent_path());
		const auto write = [](const std::filesystem::path& a_path, const std::string& a_bytes) {
			std::ofstream stream(a_path, std::ios::binary);
			stream << a_bytes;
		};
		write(paths.dataDirectory / "MyMod.esp", "old plugin bytes");
		write(pluginsTxt, kOriginalPluginsTxt);
		write(paths.StagingFor(kFingerprint) / "mymod.esp", kNewPlugin);
		write(paths.StagingFor(kFingerprint) / "mymod - main.ba2", kNewArchive);

		auto fileSystem = MakeNativeFileSystem();
		auto plan = MakeScenario().plan;
		Installer installer(*fileSystem, paths);
		assert(installer.WritePendingPlan(plan));
		const auto applied = installer.ApplyPending(7);
		assert(applied.outcome == Outcome::applied);
		assert(fileSystem->Matches(paths.dataDirectory / "mymod - main.ba2", plan.files[1].sha256Hex, plan.files[1].sizeBytes));
#ifdef _WIN32
		assert(fileSystem->ActualName(paths.dataDirectory / "MYMOD - MAIN.BA2") == std::optional<std::string>{ "mymod - main.ba2" });
		assert(fileSystem->Read(paths.backupRoot / (kFingerprint.substr(0, 12) + "-7") / "mymod.esp", 1 << 20) == std::optional<std::string>{ "old plugin bytes" });
#endif
		const auto recovered = Installer(*fileSystem, paths).Recover();
		assert(recovered.outcome == Outcome::rolledBack);
		assert(fileSystem->Read(paths.dataDirectory / "MyMod.esp", 1 << 20) == std::optional<std::string>{ "old plugin bytes" });
		assert(!fileSystem->Exists(paths.dataDirectory / "mymod - main.ba2"));
		assert(fileSystem->Read(pluginsTxt, 1 << 20) == std::optional<std::string>{ kOriginalPluginsTxt });
		assert(fileSystem->SameVolume(paths.dataDirectory, paths.backupRoot));
		assert(fileSystem->FreeBytes(paths.dataDirectory).has_value());
		std::filesystem::remove_all(root);
	}
}

int main()
{
	TestPlanRoundTripAndStrictParsing();
	TestApplyInstallsBacksUpAndActivates();
	TestGameThatNeverFinishesLoadingIsRolledBack();
	TestCrashAtEveryStepIsUndoneAndNeverRetried();
	TestCrashDuringRollbackIsFinishedOnTheNextStart();
	TestFailedMoveIsUndoneImmediately();
	TestUnsafeTargetsAndChangedStagingAreRefused();
	TestRestoreRequestFileUndoesTheLastInstall();
	TestJournalParsingIsStrict();
	TestModManagerDetection();
	TestNativeFileSystemInstallAndRollback();
	return 0;
}

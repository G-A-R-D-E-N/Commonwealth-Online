#pragma once

#include "F4TModpackContentPolicy.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::ModpackInstall
{
	inline constexpr std::size_t kMaximumPlanBytes = 1024 * 1024;
	inline constexpr std::size_t kMaximumServerKeyBytes = 512;
	inline constexpr std::size_t kMaximumLoadOrderEntries = 8192;

	struct PlannedFile
	{
		std::string name;
		std::string sha256Hex;
		std::uint64_t sizeBytes{};
		bool replaces{};
	};

	struct ServerEndpoint
	{
		std::string host;
		std::uint16_t port{};
		std::string joinId;
	};

	struct InstallPlan
	{
		std::string fingerprint;
		std::string serverKey;
		std::string serverLabel;
		ServerEndpoint endpoint;
		std::vector<PlannedFile> files;
		std::vector<std::string> loadOrder;
		bool activatePlugins{ true };
	};

	[[nodiscard]] std::string SerializePlan(const InstallPlan& a_plan);
	[[nodiscard]] std::optional<InstallPlan> ParsePlan(std::string_view a_text);

	enum class OpStep
	{
		planned,
		backedUp,
		placed
	};

	struct JournalOp
	{
		std::string name;
		OpStep step{ OpStep::planned };
		bool hadOriginal{};
	};

	enum class JournalState
	{
		applying,
		verifying,
		applied,
		rolledBack
	};

	struct Journal
	{
		std::string fingerprint;
		std::string serverKey;
		std::string serverLabel;
		ServerEndpoint endpoint;
		std::string backupFolder;
		JournalState state{ JournalState::applying };
		std::vector<JournalOp> ops;
		bool pluginsTxtReplaced{};
		bool pluginsTxtExisted{};
	};

	[[nodiscard]] std::string SerializeJournal(const Journal& a_journal);
	[[nodiscard]] std::optional<Journal> ParseJournal(std::string_view a_text);

	class FileSystem
	{
	public:
		virtual ~FileSystem() = default;

		[[nodiscard]] virtual bool Exists(const std::filesystem::path& a_path) = 0;
		[[nodiscard]] virtual bool IsPlainFile(const std::filesystem::path& a_path) = 0;
		[[nodiscard]] virtual bool IsPlainDirectory(const std::filesystem::path& a_path) = 0;
		[[nodiscard]] virtual bool CreateDirectories(const std::filesystem::path& a_path) = 0;
		[[nodiscard]] virtual bool CreateNewDirectory(const std::filesystem::path& a_path) = 0;
		[[nodiscard]] virtual bool MoveNoReplace(const std::filesystem::path& a_from, const std::filesystem::path& a_to) = 0;
		[[nodiscard]] virtual bool WriteDurably(const std::filesystem::path& a_path, std::string_view a_bytes) = 0;
		[[nodiscard]] virtual std::optional<std::string> Read(const std::filesystem::path& a_path, std::size_t a_maximumBytes) = 0;
		[[nodiscard]] virtual bool Remove(const std::filesystem::path& a_path) = 0;
		[[nodiscard]] virtual bool Matches(const std::filesystem::path& a_path, std::string_view a_sha256Hex, std::uint64_t a_sizeBytes) = 0;
		[[nodiscard]] virtual std::optional<std::string> ActualName(const std::filesystem::path& a_path) = 0;
		[[nodiscard]] virtual bool SameVolume(const std::filesystem::path& a_left, const std::filesystem::path& a_right) = 0;
		[[nodiscard]] virtual std::optional<std::uint64_t> FreeBytes(const std::filesystem::path& a_path) = 0;
		[[nodiscard]] virtual std::vector<std::string> List(const std::filesystem::path& a_directory) = 0;
	};

	[[nodiscard]] std::unique_ptr<FileSystem> MakeNativeFileSystem();

	struct Paths
	{
		std::filesystem::path dataDirectory;
		std::filesystem::path stagingRoot;
		std::filesystem::path backupRoot;
		std::filesystem::path installDirectory;
		std::filesystem::path pluginsTxt;
		std::filesystem::path creationClubList;

		[[nodiscard]] std::filesystem::path PendingPlan() const;
		[[nodiscard]] std::filesystem::path JournalFile() const;
		[[nodiscard]] std::filesystem::path LastInstall() const;
		[[nodiscard]] std::filesystem::path RestoreRequest() const;
		[[nodiscard]] std::filesystem::path StagingFor(std::string_view a_fingerprint) const;
	};

	[[nodiscard]] Paths PathsForGame(const std::filesystem::path& a_gameDirectory, const std::filesystem::path& a_pluginsTxt);

	enum class ModManager
	{
		none,
		modOrganizer,
		vortex
	};

	[[nodiscard]] ModManager DetectModManager(bool a_virtualFileSystemLoaded, const std::vector<std::string>& a_dataFileNames);
	[[nodiscard]] std::string_view DescribeModManager(ModManager a_manager) noexcept;

	enum class Outcome
	{
		nothingToDo,
		applied,
		rolledBack,
		restored,
		refused,
		failed
	};

	struct Result
	{
		Outcome outcome{ Outcome::nothingToDo };
		std::string detail;
		std::size_t files{};
		std::vector<std::string> activated;
		std::vector<std::string> deactivated;
		std::string serverKey;
		std::string serverLabel;
		ServerEndpoint endpoint;
	};

	class Installer
	{
	public:
		Installer(FileSystem& a_fileSystem, Paths a_paths);

		[[nodiscard]] Result Recover();
		[[nodiscard]] Result ApplyPending(std::uint64_t a_uniqueSuffix);
		[[nodiscard]] Result ConfirmVerified();
		[[nodiscard]] Result RestoreLastInstall();
		[[nodiscard]] bool WritePendingPlan(const InstallPlan& a_plan);
		[[nodiscard]] bool HasPendingPlan();
		[[nodiscard]] bool HasRestoreRequest();
		[[nodiscard]] bool IsVerifying();

	private:
		[[nodiscard]] Result Apply(const InstallPlan& a_plan, std::uint64_t a_uniqueSuffix);
		[[nodiscard]] Result Rollback(Journal a_journal, Outcome a_outcome);
		[[nodiscard]] bool SaveJournal(const Journal& a_journal);
		[[nodiscard]] std::optional<std::string> CheckTarget(const std::string& a_name);
		[[nodiscard]] std::vector<std::string> CreationClubNames();

		FileSystem& fileSystem;
		Paths paths;
	};
}

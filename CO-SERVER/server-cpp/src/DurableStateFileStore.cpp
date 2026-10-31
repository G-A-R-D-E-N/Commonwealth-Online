#include "DurableStateFileStore.h"
#include "F4TCharacterSlots.h"
#include "F4TSnapshotDeltaWire.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace CommonwealthOnline::Server
{
#ifdef CO_DURABLE_COMMIT_CRASH_PROBE
	void DurableCommitCrashPause(std::string_view a_stage);
#endif
	namespace
	{
		std::runtime_error IoFailure(std::string_view a_operation, const std::filesystem::path& a_path, std::uint64_t a_code)
		{
			return std::runtime_error(
				std::string{ a_operation } + " failed for '" + a_path.string() + "' (code " + std::to_string(a_code) + ")");
		}
	}

	DurableStateFileStore::DurableStateFileStore(
		std::filesystem::path a_path,
		Clock a_clock,
		InstanceIdGenerator a_instanceIdGenerator,
		PostCommitHook a_postCommitHook) :
		_path(std::filesystem::absolute(std::move(a_path))),
		_backupPath(_path.string() + ".bak"),
		_clock(std::move(a_clock)),
		_instanceIdGenerator(std::move(a_instanceIdGenerator)),
		_postCommitHook(std::move(a_postCommitHook)),
		_writer([this](const std::string& a_contents) { CommitFromWriter(a_contents); })
	{
		if (!_clock || !_instanceIdGenerator) {
			throw std::invalid_argument("Durable state store requires clock and server-instance-id generator callbacks.");
		}
		std::filesystem::create_directories(_path.parent_path());
		LoadOrCreate();
	}

	nlohmann::json DurableStateFileStore::InitializeInventory(std::string_view a_profileId, const nlohmann::json& a_catalog)
	{
		const std::scoped_lock lock(_gate);
		(void)MutateAndPersistLocked([&](nlohmann::json& a_root) {
			return F4T::DurableState::InitializeInventory(a_root, a_profileId, a_catalog, _clock());
		}, true);
		return *F4T::DurableState::GetInventory(_root, a_profileId);
	}

	std::optional<nlohmann::json> DurableStateFileStore::GetInventory(std::string_view a_profileId) const
	{
		const std::scoped_lock lock(_gate);
		return F4T::DurableState::GetInventory(_root, a_profileId);
	}

	std::optional<F4T::Replication::SnapshotState> DurableStateFileStore::GetDropSnapshot() const
	{
		const std::scoped_lock lock(_gate);
		if (!_dropSnapshotCached) {
			_dropSnapshot = BuildDropSnapshotLocked();
			_dropSnapshotCached = true;
		}
		return _dropSnapshot;
	}

	bool DurableStateFileStore::InventoryAuthorityBlocked() const
	{
		const std::scoped_lock lock(_gate);
		return _root.at("inventoryAuthorityBlocked").get<bool>();
	}

	std::optional<F4T::Replication::SnapshotState> DurableStateFileStore::BuildDropSnapshotLocked() const
	{
		if (_root.at("inventoryAuthorityBlocked").get<bool>()) return std::nullopt;
		auto snapshot = F4T::Drops::BuildStableDropSnapshot(_root.at("drops").at("live"));
		const auto baseline = snapshot ? F4T::Replication::EncodeBaseline(*snapshot, 1, F4T::Replication::Channel::Reliable) : std::nullopt;
		if (!baseline || baseline->size() > F4T::Drops::kMaximumDropBaselineBytes) return std::nullopt;
		return snapshot;
	}

	F4T::Drops::TransactionResult DurableStateFileStore::DropInventory(const std::string& a_profileId,
		const nlohmann::json& a_request, const F4T::Drops::InventoryPosition& a_position)
	{
		const std::scoped_lock lock(_gate);
		if (auto screened = F4T::Drops::DropLedger::Precheck(_root, a_profileId, a_request, true)) return *screened;
		F4T::Drops::TransactionResult result;
		const auto committed = MutateAndPersistLocked([&](nlohmann::json& a_root) {
			result = F4T::Drops::DropLedger::Drop(a_root, a_profileId, a_request, a_position);
			return result.outcome == F4T::Drops::Outcome::Committed;
		}, true);
		if (committed) _dropSnapshotCached = false;
		return result;
	}

	F4T::Drops::TransactionResult DurableStateFileStore::PickupInventory(const std::string& a_profileId,
		const nlohmann::json& a_request, const F4T::Drops::InventoryPosition& a_position)
	{
		const std::scoped_lock lock(_gate);
		if (auto screened = F4T::Drops::DropLedger::Precheck(_root, a_profileId, a_request, false)) return *screened;
		F4T::Drops::TransactionResult result;
		const auto committed = MutateAndPersistLocked([&](nlohmann::json& a_root) {
			result = F4T::Drops::DropLedger::Pickup(a_root, a_profileId, a_request, a_position);
			return result.outcome == F4T::Drops::Outcome::Committed;
		}, true);
		if (committed) _dropSnapshotCached = false;
		return result;
	}

	std::string DurableStateFileStore::ServerInstanceId() const
	{
		const std::scoped_lock lock(_gate);
		return _root.at("serverInstanceId").get<std::string>();
	}

	std::optional<std::string> DurableStateFileStore::RecoveryNotice() const
	{
		const std::scoped_lock lock(_gate);
		return _recoveryNotice;
	}

	std::optional<std::string> DurableStateFileStore::TakePersistenceWarning()
	{
		const std::scoped_lock lock(_gate);
		if (auto failure = _writer.TakeFailure()) {
			_persistenceWarning = std::move(failure);
		}
		auto warning = std::move(_persistenceWarning);
		_persistenceWarning.reset();
		return warning;
	}

	std::optional<F4T::Identity::PersistedResumeCredential> DurableStateFileStore::GetResumeCredential(
		std::string_view a_profileId)
	{
		const std::scoped_lock lock(_gate);
		return F4T::DurableState::GetResumeCredential(_root, a_profileId);
	}

	void DurableStateFileStore::SaveResumeCredential(
		std::string_view a_profileId,
		const F4T::Identity::TokenHash& a_tokenHash,
		const std::optional<F4T::Identity::TokenHash>& a_previousTokenHash,
		bool a_confirmed,
		double a_expiresAt)
	{
		const std::scoped_lock lock(_gate);
		(void)MutateAndPersistLocked([&](nlohmann::json& a_root) {
			F4T::DurableState::SaveResumeCredential(
				a_root, a_profileId, a_tokenHash, a_previousTokenHash, a_confirmed, a_expiresAt, _clock());
			return true;
		}, true);
	}

	bool DurableStateFileStore::DeleteResumeCredential(std::string_view a_profileId)
	{
		const std::scoped_lock lock(_gate);
		return MutateAndPersistLocked([&](nlohmann::json& a_root) {
			return F4T::DurableState::DeleteResumeCredential(a_root, a_profileId, _clock());
		}, true);
	}

	std::optional<nlohmann::json> DurableStateFileStore::GetPlayerState(std::string_view a_profileId) const
	{
		const std::scoped_lock lock(_gate);
		return F4T::DurableState::GetPlayerState(_root, a_profileId);
	}

	std::optional<std::string> DurableStateFileStore::GetProfileSecret(std::string_view a_profileId) const
	{
		const std::scoped_lock lock(_gate);
		return F4T::DurableState::GetProfileSecret(_root, a_profileId);
	}

	bool DurableStateFileStore::EnsureProfileSecret(std::string_view a_profileId, std::string_view a_secret)
	{
		const std::scoped_lock lock(_gate);
		return MutateAndPersistLocked([&](nlohmann::json& a_root) {
			return F4T::DurableState::EnsureProfileSecret(a_root, a_profileId, a_secret, _clock());
		}, true);
	}

	std::vector<F4T::SaveProfileLogic::AttestedSave> DurableStateFileStore::GetSaveAttestations(
		std::string_view a_profileId) const
	{
		const std::scoped_lock lock(_gate);
		return F4T::DurableState::GetSaveAttestations(_root, a_profileId);
	}

	bool DurableStateFileStore::RecordSaveAttestation(
		std::string_view a_profileId,
		const F4T::SaveProfileLogic::AttestedSave& a_save)
	{
		const std::scoped_lock lock(_gate);
		return MutateAndPersistLocked([&](nlohmann::json& a_root) {
			F4T::DurableState::RecordSaveAttestation(a_root, a_profileId, a_save, _clock());
			return true;
		}, true);
	}

	bool DurableStateFileStore::UpdatePlayerState(std::string_view a_profileId, const nlohmann::json& a_playerState)
	{
		if (!F4T::DurableState::CarriesDurablePlayerState(a_playerState)) {
			return false;
		}

		const std::scoped_lock lock(_gate);
		if (F4T::DurableState::InspectPlayerStateUpdate(_root, a_profileId, a_playerState) !=
			F4T::DurableState::PlayerStateMutationOutcome::Changed) {
			return false;
		}
		return MutateAndPersistLocked([&](nlohmann::json& a_root) {
			return F4T::DurableState::UpdatePlayerState(a_root, a_profileId, a_playerState, _clock()) ==
				F4T::DurableState::PlayerStateMutationOutcome::Changed;
		}, true);
	}

	bool DurableStateFileStore::UpdatePlayerStates(const std::unordered_map<std::string, nlohmann::json>& a_playerStates)
	{
		const std::scoped_lock lock(_gate);
		bool queued = !a_playerStates.empty() && MutateAndPersistLocked([&](nlohmann::json& a_root) {
			bool changed = false;
			for (const auto& [profileId, playerState] : a_playerStates) {
				if (F4T::DurableState::InspectPlayerStateUpdate(a_root, profileId, playerState) !=
					F4T::DurableState::PlayerStateMutationOutcome::Changed) {
					continue;
				}
				changed = F4T::DurableState::UpdatePlayerState(a_root, profileId, playerState, _clock()) ==
					F4T::DurableState::PlayerStateMutationOutcome::Changed || changed;
			}
			return changed;
		}, false);
		if (!queued && _writer.LastWriteFailed()) {
			PostRootLocked(false);
			queued = true;
		}
		return queued;
	}

	void DurableStateFileStore::WaitForCommits()
	{
		_writer.Drain();
	}

	nlohmann::json DurableStateFileStore::GetServerWorldState() const
	{
		const std::scoped_lock lock(_gate);
		return F4T::DurableState::GetWorldState(_root);
	}

	void DurableStateFileStore::UpdateServerWorldState(const nlohmann::json& a_worldState)
	{
		const std::scoped_lock lock(_gate);
		(void)MutateAndPersistLocked([&](nlohmann::json& a_root) {
			F4T::DurableState::UpdateWorldState(a_root, a_worldState);
			return true;
		}, true);
	}

	nlohmann::json DurableStateFileStore::GetFactions() const
	{
		const std::scoped_lock lock(_gate);
		return _root.value("factions", nlohmann::json::object());
	}

	void DurableStateFileStore::SaveFactions(const nlohmann::json& a_factions)
	{
		const std::scoped_lock lock(_gate);
		(void)MutateAndPersistLocked([&](nlohmann::json& a_root) {
			a_root["factions"] = a_factions;
			return true;
		}, true);
	}

	nlohmann::json DurableStateFileStore::GetSettlementAccess() const
	{
		const std::scoped_lock lock(_gate);
		return _root.value("settlementAccess", nlohmann::json::array());
	}

	void DurableStateFileStore::SaveSettlementAccess(const nlohmann::json& a_settlementAccess)
	{
		const std::scoped_lock lock(_gate);
		(void)MutateAndPersistLocked([&](nlohmann::json& a_root) {
			a_root["settlementAccess"] = a_settlementAccess;
			return true;
		}, true);
	}

	nlohmann::json DurableStateFileStore::GetQuests() const
	{
		const std::scoped_lock lock(_gate);
		return _root.value("quests", nlohmann::json::object());
	}

	void DurableStateFileStore::SaveQuests(const nlohmann::json& a_quests, bool a_awaitCommit)
	{
		const std::scoped_lock lock(_gate);
		(void)MutateAndPersistLocked([&](nlohmann::json& a_root) {
			a_root["quests"] = a_quests;
			return true;
		}, a_awaitCommit);
	}

	void DurableStateFileStore::SaveQuestsBackup(const nlohmann::json& a_quests)
	{
		const std::scoped_lock lock(_gate);
		(void)MutateAndPersistLocked([&](nlohmann::json& a_root) {
			a_root["quests_backup"] = a_quests;
			return true;
		}, true);
	}

	nlohmann::json DurableStateFileStore::GetWorkshop() const
	{
		const std::scoped_lock lock(_gate);
		return _root.value("workshop", nlohmann::json::object());
	}

	void DurableStateFileStore::SaveWorkshop(const nlohmann::json& a_workshop, bool a_awaitCommit)
	{
		const std::scoped_lock lock(_gate);
		(void)MutateAndPersistLocked([&](nlohmann::json& a_root) {
			a_root["workshop"] = a_workshop;
			return true;
		}, a_awaitCommit);
	}

	nlohmann::json DurableStateFileStore::GetSteamIdentities() const
	{
		const std::scoped_lock lock(_gate);
		return _root.value("steamIdentities", nlohmann::json(nullptr));
	}

	void DurableStateFileStore::SaveSteamIdentities(const nlohmann::json& a_steamIdentities)
	{
		const std::scoped_lock lock(_gate);
		(void)MutateAndPersistLocked([&](nlohmann::json& a_root) {
			if (a_root.value("steamIdentities", nlohmann::json(nullptr)) == a_steamIdentities) return false;
			a_root["steamIdentities"] = a_steamIdentities;
			return true;
		}, false);
	}

	bool DurableStateFileStore::MutateAndPersistLocked(const std::function<bool(nlohmann::json&)>& a_mutate, bool a_awaitCommit)
	{
		auto snapshot = _root;
		bool changed = false;
		try {
			changed = a_mutate(_root);
		} catch (...) {
			_root = std::move(snapshot);
			throw;
		}
		if (!changed) {
			_root = std::move(snapshot);
			return false;
		}

		try {
			PostRootLocked(a_awaitCommit);
		} catch (...) {
			_root = std::move(snapshot);
			throw;
		}
		return true;
	}

	void DurableStateFileStore::PostRootLocked(bool a_awaitCommit)
	{
		auto contents = BuildContentsLocked();
		if (a_awaitCommit) {
			_writer.PostAndWait(std::move(contents));
		} else {
			_writer.Post(std::move(contents));
		}
	}

	void DurableStateFileStore::CommitFromWriter(const std::string& a_contents)
	{
		bool committed = false;
		try {
			CommitContents(a_contents, committed);
		} catch (const std::exception& exception) {
			if (!committed) {
				throw;
			}
			_writer.ReportFailure(exception.what());
		} catch (...) {
			if (!committed) {
				throw;
			}
			_writer.ReportFailure("Durable state committed with an unspecified post-commit warning.");
		}
	}

	nlohmann::json DurableStateFileStore::LoadFile(const std::filesystem::path& a_path, bool& a_migrated) const
	{
		std::ifstream input(a_path, std::ios::binary);
		if (!input) {
			throw std::runtime_error("Could not open durable state '" + a_path.string() + "'.");
		}
		std::ostringstream buffer;
		buffer << input.rdbuf();
		if (!input.good() && !input.eof()) {
			throw std::runtime_error("Could not read durable state '" + a_path.string() + "'.");
		}
		auto parsed = nlohmann::json::parse(buffer.str());
		auto loaded = F4T::DurableState::LoadAndMigrate(std::move(parsed), _clock());
		a_migrated = loaded.migrated;
		return std::move(loaded.root);
	}

	void DurableStateFileStore::LoadOrCreate()
	{
		const std::scoped_lock lock(_gate);
		if (std::filesystem::exists(_path)) {
			try {
				bool migrated = false;
				_root = LoadFile(_path, migrated);
				if (migrated) {
					PersistLocked();
					_recoveryNotice = "Migrated durable state to schema 4.";
				}
				return;
			} catch (const std::exception& primaryError) {
				if (!std::filesystem::exists(_backupPath)) {
					throw std::runtime_error(
						"Durable state '" + _path.string() + "' is corrupt and no valid backup is available: " + primaryError.what());
				}
				try {
					bool ignoredMigration = false;
					_root = LoadFile(_backupPath, ignoredMigration);
					_root["inventoryAuthorityBlocked"] = true;
					RestoreRecoveredPrimaryLocked();
					_recoveryNotice = "Recovered durable state from '" + _backupPath.string() +
						"' after primary validation failed: " + primaryError.what();
					return;
				} catch (const std::exception& backupError) {
					throw std::runtime_error(
						"Durable state and backup are both invalid. Primary: " + std::string{ primaryError.what() } +
						" Backup: " + backupError.what());
				}
			}
		}

		if (std::filesystem::exists(_backupPath)) {
			try {
				bool ignoredMigration = false;
				_root = LoadFile(_backupPath, ignoredMigration);
				_root["inventoryAuthorityBlocked"] = true;
				PersistLocked();
				_recoveryNotice = "Recovered durable state from '" + _backupPath.string() + "' because the primary file was missing.";
				return;
			} catch (const std::exception& backupError) {
				throw std::runtime_error(
					"Primary durable state is missing and backup '" + _backupPath.string() + "' is invalid: " + backupError.what());
			}
		}

		_root = F4T::DurableState::NewRoot(_instanceIdGenerator(), _clock());
		PersistLocked();
	}

	void DurableStateFileStore::RestoreRecoveredPrimaryLocked()
	{
		if (std::filesystem::exists(_path)) {
			std::filesystem::remove(_path);
			SyncDirectory(_path.parent_path());
		}
		PersistLocked();
	}

	void DurableStateFileStore::PersistLocked()
	{
		bool committed = false;
		CommitContents(BuildContentsLocked(), committed);
	}

	std::string DurableStateFileStore::BuildContentsLocked()
	{
		_root["schemaVersion"] = F4T::DurableState::CurrentSchemaVersion;
		_root["savedAt"] = _clock();
		F4T::DurableState::ValidateRoot(_root);
		return _root.dump(2) + "\n";
	}

	void DurableStateFileStore::CommitContents(std::string_view a_contents, bool& a_committed)
	{
		const auto suffix = UniqueSuffix();
		const auto tempPath = std::filesystem::path{ _path.string() + ".tmp-" + suffix };
		const auto backupTempPath = std::filesystem::path{ _backupPath.string() + ".tmp-" + suffix };
		try {
			WriteFileSync(tempPath, a_contents);
#ifdef CO_DURABLE_COMMIT_CRASH_PROBE
			DurableCommitCrashPause("temp-synced");
#endif

			if (std::filesystem::exists(_path)) {
				std::filesystem::copy_file(_path, backupTempPath, std::filesystem::copy_options::none);
				SyncExistingFile(backupTempPath);
				AtomicReplace(backupTempPath, _backupPath);
#ifdef CO_DURABLE_COMMIT_CRASH_PROBE
				DurableCommitCrashPause("backup-replaced");
#endif
			}
			AtomicReplace(tempPath, _path, std::addressof(a_committed));
			if (a_committed && _postCommitHook) {
				_postCommitHook();
			}
		} catch (...) {
			std::error_code ignored;
			std::filesystem::remove(tempPath, ignored);
			std::filesystem::remove(backupTempPath, ignored);
			throw;
		}
	}

	void DurableStateFileStore::WriteFileSync(const std::filesystem::path& a_path, std::string_view a_contents)
	{
#ifdef _WIN32
		const HANDLE handle = CreateFileW(
			a_path.c_str(),
			GENERIC_WRITE,
			0,
			nullptr,
			CREATE_NEW,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
			nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			throw IoFailure("CreateFileW", a_path, GetLastError());
		}
		std::size_t offset = 0;
		try {
			while (offset < a_contents.size()) {
				const auto remaining = a_contents.size() - offset;
				const auto chunk = static_cast<DWORD>((std::min)(remaining, static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
				DWORD written = 0;
				if (!WriteFile(handle, a_contents.data() + offset, chunk, &written, nullptr) || written != chunk) {
					throw IoFailure("WriteFile", a_path, GetLastError());
				}
				offset += written;
			}
			if (!FlushFileBuffers(handle)) {
				throw IoFailure("FlushFileBuffers", a_path, GetLastError());
			}
		} catch (...) {
			CloseHandle(handle);
			throw;
		}
		CloseHandle(handle);
#else
		const int descriptor = ::open(a_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
		if (descriptor < 0) {
			throw IoFailure("open", a_path, static_cast<std::uint64_t>(errno));
		}
		std::size_t offset = 0;
		try {
			while (offset < a_contents.size()) {
				const auto written = ::write(descriptor, a_contents.data() + offset, a_contents.size() - offset);
				if (written < 0) {
					if (errno == EINTR) {
						continue;
					}
					throw IoFailure("write", a_path, static_cast<std::uint64_t>(errno));
				}
				offset += static_cast<std::size_t>(written);
			}
			if (::fsync(descriptor) != 0) {
				throw IoFailure("fsync", a_path, static_cast<std::uint64_t>(errno));
			}
		} catch (...) {
			::close(descriptor);
			throw;
		}
		::close(descriptor);
#endif
	}

	void DurableStateFileStore::SyncExistingFile(const std::filesystem::path& a_path)
	{
#ifdef _WIN32
		const HANDLE handle = CreateFileW(
			a_path.c_str(),
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ,
			nullptr,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
			nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			throw IoFailure("CreateFileW", a_path, GetLastError());
		}
		if (!FlushFileBuffers(handle)) {
			const auto code = GetLastError();
			CloseHandle(handle);
			throw IoFailure("FlushFileBuffers", a_path, code);
		}
		CloseHandle(handle);
#else
		const int descriptor = ::open(a_path.c_str(), O_RDONLY | O_CLOEXEC);
		if (descriptor < 0) {
			throw IoFailure("open", a_path, static_cast<std::uint64_t>(errno));
		}
		if (::fsync(descriptor) != 0) {
			const auto code = errno;
			::close(descriptor);
			throw IoFailure("fsync", a_path, static_cast<std::uint64_t>(code));
		}
		::close(descriptor);
#endif
	}

	void DurableStateFileStore::AtomicReplace(
		const std::filesystem::path& a_source,
		const std::filesystem::path& a_destination,
		bool* a_replaced)
	{
#ifdef _WIN32
		if (!MoveFileExW(
				a_source.c_str(),
				a_destination.c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			throw IoFailure("MoveFileExW", a_destination, GetLastError());
		}
		if (a_replaced != nullptr) {
			*a_replaced = true;
		}
#else
		if (::rename(a_source.c_str(), a_destination.c_str()) != 0) {
			throw IoFailure("rename", a_destination, static_cast<std::uint64_t>(errno));
		}
		// The rename is the commit point. The directory sync that follows makes
		// it durable across power loss, but the new state is already what a
		// reader sees, so a failure here must not be treated as "nothing
		// happened".
		if (a_replaced != nullptr) {
			*a_replaced = true;
		}
		SyncDirectory(a_destination.parent_path());
#endif
	}

	void DurableStateFileStore::SyncDirectory(const std::filesystem::path& a_directory)
	{
#ifndef _WIN32
		const int descriptor = ::open(a_directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (descriptor < 0) {
			throw IoFailure("open directory", a_directory, static_cast<std::uint64_t>(errno));
		}
		if (::fsync(descriptor) != 0) {
			const auto code = errno;
			::close(descriptor);
			throw IoFailure("fsync directory", a_directory, static_cast<std::uint64_t>(code));
		}
		::close(descriptor);
#else
		(void)a_directory;
#endif
	}

	std::string DurableStateFileStore::UniqueSuffix()
	{
		static std::atomic<std::uint64_t> sequence{ 0 };
		const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
		return std::to_string(static_cast<std::uint64_t>(ticks)) + "-" + std::to_string(++sequence);
	}

	std::map<std::uint32_t, nlohmann::json> DurableStateFileStore::GetCharacterSlotRecords(std::string_view a_account) const
	{
		const std::scoped_lock lock(_gate);
		return F4T::CharacterSlots::GetSlotRecords(_root, a_account);
	}

	bool DurableStateFileStore::SetCharacterSlotRecord(
		std::string_view a_account,
		std::uint32_t a_slot,
		const nlohmann::json& a_record)
	{
		const std::scoped_lock lock(_gate);
		return MutateAndPersistLocked([&](nlohmann::json& a_root) {
			return F4T::CharacterSlots::SetSlotRecord(a_root, a_account, a_slot, a_record);
		}, true);
	}

	bool DurableStateFileStore::EraseCharacterSlotRecord(std::string_view a_account, std::uint32_t a_slot)
	{
		const std::scoped_lock lock(_gate);
		return MutateAndPersistLocked([&](nlohmann::json& a_root) {
			return F4T::CharacterSlots::EraseSlotRecord(a_root, a_account, a_slot);
		}, true);
	}

	bool DurableStateFileStore::MigrateCharacterSlotsToSlotZero(std::string& a_conflictKey)
	{
		a_conflictKey.clear();
		const std::scoped_lock lock(_gate);
		try {
			// Constraint: migration runs inside the mutation so a failed persist rolls back.
			bool conflict = false;
			bool dirty = false;
			const auto committed = MutateAndPersistLocked([&](nlohmann::json& a_root) {
				const auto migrated = F4T::CharacterSlots::MigrateLegacyProfiles(a_root, _clock());
				if (migrated.conflict) {
					a_conflictKey = migrated.conflictKey;
					conflict = true;
					return false;
				}
				dirty = migrated.moved || migrated.alreadySlotted;
				return dirty;
			}, true);
			if (conflict) {
				return false;
			}
			return !dirty || committed;
		} catch (const std::exception&) {
			return false;
		}
	}
}

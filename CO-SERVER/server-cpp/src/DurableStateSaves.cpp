#include "DurableStateFileStore.h"
#include "DurableStateSections.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using namespace DurableStateSections;

		struct SectionBackup
		{
			std::string name;
			bool existed{};
			nlohmann::json value;
		};

		struct ProfileBackup
		{
			std::string id;
			std::optional<nlohmann::json> value;
		};

		using UnlistedFingerprints = std::map<std::string, std::size_t>;

		UnlistedFingerprints FingerprintUnlisted(const nlohmann::json& a_table, const std::vector<std::string>& a_listed)
		{
			UnlistedFingerprints fingerprints;
			for (const auto& entry : a_table.items()) {
				if (std::binary_search(a_listed.begin(), a_listed.end(), entry.key())) continue;
				fingerprints.emplace(entry.key(), std::hash<std::string>{}(entry.value().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)));
			}
			return fingerprints;
		}

		std::atomic<std::uint64_t> g_saves{ 0 };
		std::atomic<std::uint64_t> g_saveMicroseconds{ 0 };
		std::atomic<std::uint64_t> g_commitWaits{ 0 };
		std::atomic<std::uint64_t> g_commitWaitMicroseconds{ 0 };

		class ElapsedTally
		{
		public:
			ElapsedTally(std::atomic<std::uint64_t>& a_count, std::atomic<std::uint64_t>& a_microseconds) :
				_count(a_count), _microseconds(a_microseconds) {}
			ElapsedTally(const ElapsedTally&) = delete;
			ElapsedTally& operator=(const ElapsedTally&) = delete;
			~ElapsedTally()
			{
				const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - _started).count();
				_count.fetch_add(1, std::memory_order_relaxed);
				_microseconds.fetch_add(static_cast<std::uint64_t>(elapsed), std::memory_order_relaxed);
			}

		private:
			std::atomic<std::uint64_t>& _count;
			std::atomic<std::uint64_t>& _microseconds;
			std::chrono::steady_clock::time_point _started{ std::chrono::steady_clock::now() };
		};
	}

	DurableStateFileStore::MainThreadSaveTotals DurableStateFileStore::TakeMainThreadSaveTotals()
	{
		return MainThreadSaveTotals{
			.saves = g_saves.exchange(0, std::memory_order_relaxed),
			.saveSeconds = static_cast<double>(g_saveMicroseconds.exchange(0, std::memory_order_relaxed)) / 1.0e6,
			.commitWaits = g_commitWaits.exchange(0, std::memory_order_relaxed),
			.commitWaitSeconds = static_cast<double>(g_commitWaitMicroseconds.exchange(0, std::memory_order_relaxed)) / 1.0e6
		};
	}

	void DurableStateFileStore::WaitForCommits()
	{
		const ElapsedTally tally(g_commitWaits, g_commitWaitMicroseconds);
		_pipeline->WaitForCommits();
	}

	bool DurableStateFileStore::MutateAndPersistLocked(
		std::span<const std::string_view> a_sections,
		const std::function<bool(nlohmann::json&)>& a_mutate,
		bool a_awaitCommit, bool a_forceAwait)
	{
		return MutateLocked(nullptr, a_sections, a_mutate, a_awaitCommit, a_forceAwait);
	}

	bool DurableStateFileStore::MutateProfilesAndPersistLocked(
		std::vector<std::string> a_profileIds,
		std::span<const std::string_view> a_sections,
		const std::function<bool(nlohmann::json&)>& a_mutate,
		bool a_awaitCommit, bool a_forceAwait)
	{
		std::sort(a_profileIds.begin(), a_profileIds.end());
		a_profileIds.erase(std::unique(a_profileIds.begin(), a_profileIds.end()), a_profileIds.end());
		return MutateLocked(std::addressof(a_profileIds), a_sections, a_mutate, a_awaitCommit, a_forceAwait);
	}

	bool DurableStateFileStore::MutateLocked(
		const std::vector<std::string>* a_profileIds,
		std::span<const std::string_view> a_sections,
		const std::function<bool(nlohmann::json&)>& a_mutate,
		bool a_awaitCommit, bool a_forceAwait)
	{
		const ElapsedTally tally(g_saves, g_saveMicroseconds);
		const auto started = std::chrono::steady_clock::now();
		const auto profilesIt = _root.find(std::string{ kProfilesSection });
		const bool scoped = a_profileIds != nullptr && profilesIt != _root.end() && profilesIt->is_object();
		std::vector<std::string_view> sections(a_sections.begin(), a_sections.end());
		if (a_profileIds != nullptr && !scoped) {
			sections.insert(sections.begin(), kProfilesSection);
		}
		std::vector<SectionBackup> previous;
		previous.reserve(sections.size());
		for (const auto name : sections) {
			const auto found = _root.find(std::string{ name });
			previous.push_back(SectionBackup{
				.name = std::string{ name },
				.existed = found != _root.end(),
				.value = found == _root.end() ? nlohmann::json(nullptr) : *found
			});
		}
		std::vector<ProfileBackup> profiles;
		std::size_t profileCount = 0;
		if (scoped) {
			profileCount = profilesIt->size();
			profiles.reserve(a_profileIds->size());
			for (const auto& id : *a_profileIds) {
				const auto found = profilesIt->find(id);
				profiles.push_back(ProfileBackup{ id, found == profilesIt->end() ? std::nullopt : std::optional<nlohmann::json>{ *found } });
			}
		}
		UnlistedFingerprints unlistedBefore;
		if (scoped && _auditUnlistedProfiles) {
			unlistedBefore = FingerprintUnlisted(_root.at(std::string{ kProfilesSection }), *a_profileIds);
		}
		// constraint: restore runs at most once per save, so it moves the backed-up values back.
		const auto restore = [&] {
			for (auto& backup : previous) {
				if (backup.existed) {
					_root[backup.name] = std::move(backup.value);
				} else {
					_root.erase(backup.name);
				}
			}
			const auto table = _root.find(std::string{ kProfilesSection });
			if (profiles.empty() || table == _root.end() || !table->is_object()) {
				return;
			}
			for (auto& backup : profiles) {
				if (backup.value) {
					(*table)[backup.id] = std::move(*backup.value);
				} else {
					table->erase(backup.id);
				}
			}
		};

		bool changed = false;
		try {
			changed = a_mutate(_root);
		} catch (...) {
			restore();
			throw;
		}
		if (!changed) {
			restore();
			return false;
		}

		bool memberWrites = scoped;
		if (scoped) {
			const auto table = _root.find(std::string{ kProfilesSection });
			bool contained = table != _root.end() && table->is_object();
			if (contained) {
				std::size_t expected = profileCount;
				for (const auto& backup : profiles) {
					const bool present = table->contains(backup.id);
					if (present && !backup.value) ++expected;
					if (!present && backup.value) --expected;
				}
				contained = table->size() == expected;
			}
			const bool unlistedChanged = _auditUnlistedProfiles && contained && FingerprintUnlisted(*table, *a_profileIds) != unlistedBefore;
			if (!contained || unlistedChanged) {
				LogWidenedProfileSave(!contained);
			}
			// constraint: a quarantined commit drops every write it carried, so the next profile save resends the whole section.
			const bool healing = !a_profileIds->empty() && _pipeline->QuarantinedSince(_profilesPostedWholeThrough, _pipeline->LastPostedTicket());
			if (!contained || unlistedChanged || healing) {
				memberWrites = false;
				sections.insert(sections.begin(), kProfilesSection);
			}
		}

		try {
			PostLocked(memberWrites ? a_profileIds : nullptr, sections, a_awaitCommit, a_forceAwait);
		} catch (...) {
			restore();
			if (!_deferCommits || a_forceAwait) {
				throw;
			}
			return false;
		}
		const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
		if (elapsed > kSlowMainThreadSaveSeconds) {
			LogMainThreadSave(elapsed, sections.size() + (memberWrites ? 1 : 0), a_awaitCommit && !_deferCommits);
		}
		return true;
	}

	void DurableStateFileStore::PostSectionsLocked(std::span<const std::string_view> a_sections, bool a_awaitCommit, bool a_forceAwait)
	{
		if (std::find(a_sections.begin(), a_sections.end(), std::string_view{ "profiles" }) != a_sections.end())
			F4T::DeathBag::ValidateRecipientInstances(_root);
		PostLocked(nullptr, a_sections, a_awaitCommit, a_forceAwait);
	}

	void DurableStateFileStore::PostLocked(
		const std::vector<std::string>* a_profileIds,
		std::span<const std::string_view> a_sections,
		bool a_awaitCommit, bool a_forceAwait)
	{
		const auto validate = [&](std::string_view a_name, const nlohmann::json& a_value) {
			try {
				(void)a_value.dump();
			} catch (const std::exception& exception) {
				_pipeline->PostQuarantineNotice();
				LogQuarantineWarning(a_name, exception.what());
				throw;
			}
		};
		const nlohmann::json* table = nullptr;
		if (a_profileIds != nullptr) {
			table = std::addressof(_root.at(std::string{ kProfilesSection }));
			for (const auto& id : *a_profileIds) {
				if (const auto found = table->find(id); found != table->end()) {
					validate(kProfilesSection, *found);
				}
			}
		}
		for (const auto name : a_sections) {
			if (const auto found = _root.find(std::string{ name }); found != _root.end()) {
				validate(name, *found);
			}
		}
		std::vector<DurableSectionDelta> deltas;
		deltas.reserve(a_sections.size() + (a_profileIds != nullptr ? a_profileIds->size() : 0));
		if (table != nullptr) {
			for (const auto& id : *a_profileIds) {
				const auto found = table->find(id);
				deltas.push_back(DurableSectionDelta{
					.name = std::string{ kProfilesSection },
					.value = found == table->end() ? std::nullopt : std::optional<nlohmann::json>{ *found },
					.key = id
				});
			}
		}
		for (const auto name : a_sections) {
			const auto found = _root.find(std::string{ name });
			if (found == _root.end()) {
				deltas.push_back(DurableSectionDelta{ .name = std::string{ name }, .value = std::nullopt });
			} else {
				deltas.push_back(DurableSectionDelta{ .name = std::string{ name }, .value = std::optional<nlohmann::json>{ *found } });
			}
		}
		const bool wholeProfiles = std::find(a_sections.begin(), a_sections.end(), kProfilesSection) != a_sections.end();
		const auto postedBefore = _pipeline->LastPostedTicket();
		_pipeline->Post(std::move(deltas), a_awaitCommit && (!_deferCommits || a_forceAwait));
		if (wholeProfiles) {
			_profilesPostedWholeThrough = postedBefore;
		}
	}

	void DurableStateFileStore::LogWidenedProfileSave(bool a_membership)
	{
		static std::mutex s_mutex;
		static std::chrono::steady_clock::time_point s_last{};
		const std::scoped_lock lock(s_mutex);
		const auto now = std::chrono::steady_clock::now();
		if (s_last.time_since_epoch().count() != 0 && now - s_last < std::chrono::seconds(30)) {
			return;
		}
		s_last = now;
		const char* message = a_membership
			? "Durable save: a profile-scoped change added or removed other profiles; saved the whole profiles section\n"
			: "Durable save: a profile-scoped change modified an unlisted profile in place; saved the whole profiles section\n";
		std::cerr << message;
	}

	void DurableStateFileStore::SetDeferCommits(bool a_deferCommits)
	{
		_deferCommits = a_deferCommits;
	}

	std::uint64_t DurableStateFileStore::LastPostedTicket() const
	{
		return _pipeline->LastPostedTicket();
	}

	bool DurableStateFileStore::TicketDurable(std::uint64_t a_ticket) const
	{
		return _pipeline->Durable(a_ticket);
	}

	bool DurableStateFileStore::AllPostedDurable() const
	{
		return _pipeline->AllPostedDurable();
	}

	bool DurableStateFileStore::TicketFailed(std::uint64_t a_ticket) const
	{
		return _pipeline->TicketFailed(a_ticket);
	}

	bool DurableStateFileStore::TicketQuarantined(std::uint64_t a_ticket) const
	{
		return _pipeline->TicketQuarantined(a_ticket);
	}

	bool DurableStateFileStore::QuarantinedSince(std::uint64_t a_afterBatch, std::uint64_t a_throughBatch) const
	{
		return _pipeline->QuarantinedSince(a_afterBatch, a_throughBatch);
	}

	void DurableStateFileStore::LogQuarantineWarning(std::string_view a_section, std::string_view a_error)
	{
		static std::mutex s_mutex;
		static std::chrono::steady_clock::time_point s_last{};
		const std::scoped_lock lock(s_mutex);
		const auto now = std::chrono::steady_clock::now();
		if (s_last.time_since_epoch().count() != 0 && now - s_last < std::chrono::seconds(30)) {
			return;
		}
		s_last = now;
		std::cerr << "Durable save: refused a section that cannot be serialised [" << a_section << "]: " << a_error << '\n';
	}

	void DurableStateFileStore::LogMainThreadSave(double a_seconds, std::size_t a_sections, bool a_waited) const
	{
		std::cout << "Durable save: main loop spent "
		          << static_cast<long long>(a_seconds * 1000.0)
		          << " ms on " << a_sections << " section(s), waitedForCommit="
		          << (a_waited ? 1 : 0) << '\n'
		          << std::flush;
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
		a_migrated = F4T::DeathBag::MigrateLoaded(loaded.root, _clock()) || loaded.migrated;
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
		}
		PersistLocked();
	}

	void DurableStateFileStore::PersistLocked()
	{
		_pipeline->PersistDocument(_root);
	}
}

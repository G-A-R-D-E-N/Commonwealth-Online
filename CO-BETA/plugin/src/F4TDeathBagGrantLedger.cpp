#include "pch.h"
#include "F4TDeathBagGrantLedger.h"
#include "F4TDeathBagGrantFiles.h"
#include "F4TDeathBagJournal.h"
#include "F4TDeathBagReceipts.h"
#include "F4TContentManifest.h"
#include "F4TSaveLineageSession.h"
#include "F4TSessionSaveGuard.h"
#include "F4TServerProfileStore.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <nlohmann/json.hpp>

namespace
{
	constexpr std::uintmax_t kCompactBytes = 1024 * 1024;
	constexpr const char* kJournalName = "death-bag-grants.journal";
	constexpr const char* kLegacyName = "death-bag-grants.json";

	struct Ledger
	{
		bool loaded{ false };
		bool healthy{ false };
		bool dirty{ false };
		std::uint64_t nextSequence{ 1 };
		std::uint64_t loadGeneration{};
		std::uintmax_t checkpointBytes{};
		F4T::DeathBagJournal::State state;
		std::set<std::string> unsaved;
		std::set<std::string> saving;
	};

	std::mutex g_mutex;
	std::mutex g_applicationMutex;
	Ledger g_ledger;
	std::optional<F4T::DeathBagReceipts::ReceiptSet> g_receipts;
	std::optional<F4T::DeathBagReceipts::ReceiptSet> g_loadedReceipts;
	std::optional<F4T::DeathBagReceipts::ReceiptSet> g_savingReceipts;
	F4T::DeathBagReceipts::ReceiptChunkReader g_receiptChunks;
	std::optional<bool> g_loadSucceeded;
	F4T::DeathBagReceipts::SaveGrantBarrier g_saveBarrier;
	bool g_receiptWritten{};
	bool g_savePersistWarning{};
	bool g_saveProbeTimedOut{};
	std::string g_pendingSaveName;
	std::string g_preSaveName;
	F4T::DeathBagReceipts::SaveBaseline g_saveBaseline;
	std::uint64_t g_saveAttempt{};
	std::chrono::steady_clock::time_point g_localProbeDeadline{};
	std::chrono::steady_clock::time_point g_nextLocalProbe{};
	std::optional<std::string> g_loadedSaveHash;
	std::optional<std::string> g_pendingPreviousSaveHash;
	std::string g_loadedSaveName;
	std::set<std::string> g_volatileWithdrawalProofs;

	std::string ReceiptHash(const std::optional<F4T::DeathBagReceipts::ReceiptSet>& a_receipts)
	{
		try { return a_receipts ? F4T::ContentManifest::Sha256Hex(a_receipts->Encode()) : std::string{}; }
		catch (...) { return {}; }
	}

	using F4T::DeathBagGrantFiles::NowSeconds;
	using F4T::DeathBagGrantFiles::ReadBytes;
	using F4T::DeathBagGrantFiles::WriteBytes;
	using F4T::DeathBagGrantFiles::ReplaceBytes;
	using F4T::DeathBagGrantFiles::RetireTornTail;
	using F4T::DeathBagGrantFiles::RetireLegacy;
	using F4T::DeathBagGrantFiles::StorePath;

	bool WriteCheckpoint(const F4T::DeathBagJournal::State& a_state)
	{
		const auto unit = F4T::DeathBagJournal::Checkpoint(a_state);
		if (!unit) {
			return false;
		}
		const auto encoded = F4T::DeathBagJournal::Encode(1, *unit);
		if (!encoded || !ReplaceBytes(StorePath(kJournalName), encoded->bytes)) {
			return false;
		}
		g_ledger.nextSequence = 1 + encoded->frames;
		g_ledger.checkpointBytes = encoded->bytes.size();
		g_ledger.dirty = false;
		return true;
	}

	bool AppendUnit(const F4T::DeathBagJournal::Unit& a_unit)
	{
		const auto encoded = F4T::DeathBagJournal::Encode(g_ledger.nextSequence, a_unit);
		if (!encoded) {
			return false;
		}
		if (!WriteBytes(StorePath(kJournalName), encoded->bytes, std::ios::app)) {
			g_ledger.dirty = true;
			return false;
		}
		g_ledger.nextSequence += encoded->frames;
		return true;
	}

	void CompactIfLarge()
	{
		std::error_code ec;
		const auto size = std::filesystem::file_size(StorePath(kJournalName), ec);
		if (ec || size <= (std::max)(kCompactBytes, 2 * g_ledger.checkpointBytes)) {
			return;
		}
		if (!WriteCheckpoint(g_ledger.state)) {
			REX::WARN("Death bag grant journal could not be compacted; it will be retried on the next change.");
		}
	}

	bool Commit(const F4T::DeathBagJournal::Unit& a_unit)
	{
		if (!g_ledger.healthy) {
			return false;
		}
		auto next = g_ledger.state;
		if (!F4T::DeathBagJournal::Apply(next, a_unit)) {
			return false;
		}
		if (g_ledger.dirty ? !WriteCheckpoint(next) : !AppendUnit(a_unit)) {
			return false;
		}
		g_ledger.state = std::move(next);
		CompactIfLarge();
		return true;
	}

	bool CommitOrKeep(const F4T::DeathBagJournal::Unit& a_unit)
	{
		if (Commit(a_unit)) {
			return true;
		}
		if (F4T::DeathBagJournal::ApplyEncodable(g_ledger.state, a_unit, g_ledger.nextSequence)) {
			g_ledger.dirty = true;
		}
		return false;
	}

	bool Load()
	{
		const auto journal = StorePath(kJournalName);
		const auto legacy = StorePath(kLegacyName);
		if (journal.empty() || legacy.empty()) {
			return false;
		}
		std::error_code ec;
		const bool hasJournal = std::filesystem::exists(journal, ec);
		if (ec) {
			return false;
		}
		const auto bytes = hasJournal ? ReadBytes(journal) : std::optional<std::string>{ std::string{} };
		if (!bytes) {
			return false;
		}
		auto replay = F4T::DeathBagJournal::Replay(*bytes);
		if (!replay.valid) {
			return false;
		}
		if (replay.torn) {
			if (!RetireTornTail(journal, *bytes, replay.committedBytes)) {
				return false;
			}
			REX::WARN("Death bag grant journal had a damaged tail; the damaged bytes were copied aside and removed.");
		}
		const bool hasLegacy = std::filesystem::exists(legacy, ec);
		if (ec) {
			return false;
		}
		if (replay.committedBytes == 0 && hasLegacy) {
			const auto text = ReadBytes(legacy);
			if (!text) {
				return false;
			}
			std::size_t dropped = 0;
			auto migrated = F4T::DeathBagJournal::MigrateLegacy(nlohmann::json::parse(*text, nullptr, false), NowSeconds(), dropped);
			if (!migrated) {
				return false;
			}
			if (dropped > 0) {
				REX::WARN("Death bag place-failure records were malformed and were dropped during migration.");
			}
			g_ledger.state = std::move(*migrated);
			if (!WriteCheckpoint(g_ledger.state)) {
				return false;
			}
		} else {
			g_ledger.state = std::move(replay.state);
			g_ledger.nextSequence = replay.nextSequence;
		}
		if (hasLegacy) {
			RetireLegacy(legacy);
		}
		return true;
	}

	void EnsureLoaded()
	{
		if (g_ledger.loaded) {
			return;
		}
		g_ledger.loaded = true;
		g_ledger.healthy = Load();
		if (!g_ledger.healthy) {
			REX::WARN("Death bag grant ledger is unreadable; death bag grants are held until the file is repaired.");
		}
	}
}

namespace F4T::DeathBagGrantLedger
{
	std::mutex& ApplicationMutex()
	{
		return g_applicationMutex;
	}

	bool SaveInProgress()
	{
		return g_saveBarrier.Blocked();
	}

	ReceiptStatus ReceiptFor(const std::string& a_operation)
	{
		const std::scoped_lock lock(g_mutex);
		if (!g_receipts) return ReceiptStatus::Unknown;
		if (g_receipts->Contains(a_operation)) return ReceiptStatus::Present;
		return g_receipts->IsComplete() ? ReceiptStatus::Absent : ReceiptStatus::Unknown;
	}

	bool CanRecordReceipt(const std::string& a_operation)
	{
		const std::scoped_lock lock(g_mutex);
		if (!g_receipts) return false;
		auto candidate = *g_receipts;
		return candidate.Insert(a_operation);
	}

	bool BeginFreshReceipt()
	{
		const std::scoped_lock lock(g_mutex);
		if (!g_receipts && g_loadSucceeded && *g_loadSucceeded) g_receipts.emplace(false);
		return g_receipts.has_value();
	}

	bool PrepareWithdrawal(const std::string& a_operation)
	{
		try {
		const std::scoped_lock lock(g_mutex);
		EnsureLoaded();
		if (!g_ledger.healthy || a_operation.empty() || a_operation.size() > 128 || !g_receipts) return false;
		auto candidate = *g_receipts;
		if (!candidate.Insert(a_operation)) return false;
		if (g_receipts->IsComplete()) return true;
		if (!g_loadedSaveHash) return false;
		F4T::DeathBagJournal::Unit unit;
		unit.ops.push_back(F4T::DeathBagJournal::ProofOp(a_operation, *g_loadedSaveHash));
		return Commit(unit);
		} catch (...) { return false; }
	}

	bool HasWithdrawalProof(const std::string& a_operation)
	{
		try {
		const std::scoped_lock lock(g_mutex);
		EnsureLoaded();
		if (!g_ledger.healthy || !g_loadedSaveHash) return false;
		return g_volatileWithdrawalProofs.contains(a_operation) ||
			F4T::DeathBagReceipts::HasSaveProof(g_ledger.state.withdrawalProofs, a_operation, *g_loadedSaveHash);
		} catch (...) { return false; }
	}

	void SaveReceipts(const F4SE::SerializationInterface* a_intfc)
	{
		const std::scoped_lock lock(g_mutex);
		g_receiptWritten = false;
		g_pendingSaveName.clear();
		if (!a_intfc || !g_savingReceipts) return;
		for (const auto& chunk : F4T::DeathBagReceipts::EncodeChunks(g_savingReceipts->Encode())) {
			if (!a_intfc->WriteRecord(ReceiptRecordType, 2, chunk.data(), static_cast<std::uint32_t>(chunk.size()))) return;
		}
		g_receiptWritten = true;
	}

	void LoadReceipts(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length)
	{
		std::optional<std::string> payload;
		const std::size_t limit = a_version == 2 ? F4T::DeathBagReceipts::MaximumChunkRecordBytes : F4T::DeathBagReceipts::MaximumBytes;
		if (a_intfc && (a_version == 1 || a_version == 2) && a_length <= limit) {
			std::string bytes(a_length, '\0');
			if (a_intfc->ReadRecordData(bytes.data(), a_length) == a_length) payload = std::move(bytes);
		}
		const std::scoped_lock applicationLock(g_applicationMutex);
		const std::scoped_lock lock(g_mutex);
		EnsureLoaded();
		std::optional<F4T::DeathBagReceipts::ReceiptSet> decoded;
		if (a_version == 1 && payload) {
			decoded = F4T::DeathBagReceipts::ReceiptSet::Decode(*payload);
		} else if (a_version == 2) {
			(void)g_receiptChunks.Add(payload ? std::string_view(*payload) : std::string_view{});
			decoded = g_receiptChunks.Decode();
		}
		g_loadedReceipts = std::move(decoded);
		if (g_ledger.healthy && g_loadSucceeded && *g_loadSucceeded && g_loadedSaveHash && g_loadedReceipts &&
			F4T::DeathBagReceipts::IsReceiptBound(g_ledger.state.receiptBindings, *g_loadedSaveHash, ReceiptHash(g_loadedReceipts))) {
			if (g_receipts && !g_loadedReceipts->Merge(*g_receipts)) {
				g_receipts.reset();
				return;
			}
			g_receipts = g_loadedReceipts;
		}
	}

	void FinishReceiptLoad(bool a_success)
	{
		std::string saveName;
		std::optional<std::string> priorHash;
		std::uint64_t loadAttempt{};
		{
			const std::scoped_lock lock(g_mutex);
			saveName = g_loadedSaveName;
			priorHash = g_loadedSaveHash;
			loadAttempt = g_saveAttempt;
		}
		const auto loadedHash = a_success && !saveName.empty() ? F4T::SaveLineageSession::HashSaveForRecovery(saveName) : std::nullopt;
		const std::scoped_lock applicationLock(g_applicationMutex);
		const std::scoped_lock lock(g_mutex);
		if (loadAttempt != g_saveAttempt || saveName != g_loadedSaveName) return;
		EnsureLoaded();
		if (!priorHash || !loadedHash || !F4T::DeathBagReceipts::MatchesSave(*priorHash, *loadedHash)) g_loadedSaveHash.reset();
		g_loadSucceeded = a_success;
		if (a_success && g_ledger.healthy && g_loadedReceipts) {
			g_receipts = F4T::DeathBagReceipts::ForVerifiedSave(g_ledger.state.receiptBindings, g_loadedReceipts, priorHash.value_or(""), g_loadedSaveHash.value_or(""), ReceiptHash(g_loadedReceipts));
		} else if (a_success) g_receipts.emplace(false);
		else g_receipts.reset();
	}

	void NewGame()
	{
		const std::scoped_lock applicationLock(g_applicationMutex);
		const std::scoped_lock lock(g_mutex);
		EnsureLoaded();
		if (++g_saveAttempt == 0) ++g_saveAttempt;
		++g_ledger.loadGeneration;
		g_receipts.emplace();
		g_loadedReceipts.reset();
		g_savingReceipts.reset();
		g_receiptChunks.Reset();
		g_loadSucceeded.reset();
		g_loadedSaveHash.reset();
		g_pendingPreviousSaveHash.reset();
		g_loadedSaveName.clear();
		g_volatileWithdrawalProofs.clear();
		g_ledger.unsaved.clear();
		g_ledger.saving.clear();
		g_receiptWritten = false;
		g_pendingSaveName.clear();
		g_saveBarrier.Reset();
	}

	std::optional<Entry> Find(const std::string& a_operation)
	{
		try {
			const std::scoped_lock lock(g_mutex);
			EnsureLoaded();
			if (!g_ledger.healthy) {
				return std::nullopt;
			}
			const auto found = g_ledger.state.entries.find(a_operation);
			if (found == g_ledger.state.entries.end()) {
				if (g_receipts && g_receipts->Contains(a_operation)) return Entry{ std::nullopt, true, true, false, 0 };
				return std::nullopt;
			}
			auto entry = found->second;
			if (g_receipts && g_receipts->Contains(a_operation)) entry.added = true;
			entry.confirmed = g_receipts && g_receipts->Contains(a_operation) && !g_ledger.unsaved.contains(a_operation);
			entry.unsaved = g_ledger.unsaved.contains(a_operation);
			return entry;
		} catch (...) {
			return std::nullopt;
		}
	}

	bool Record(const std::string& a_operation, std::uint64_t a_baseline)
	{
		if (a_operation.empty()) {
			return false;
		}
		try {
			const std::scoped_lock lock(g_mutex);
			EnsureLoaded();
			if (!g_ledger.healthy) {
				return false;
			}
			const auto found = g_ledger.state.entries.find(a_operation);
			if (found != g_ledger.state.entries.end() && found->second.baseline) {
				return false;
			}
			auto entry = found != g_ledger.state.entries.end() ? found->second : Entry{};
			entry.baseline = a_baseline;
			F4T::DeathBagJournal::Unit unit;
			unit.ops.push_back(F4T::DeathBagJournal::EntryOp(a_operation, entry));
			return Commit(unit);
		} catch (...) {
			return false;
		}
	}

	bool MarkAdded(const std::string& a_operation)
	{
		try {
			const std::scoped_lock lock(g_mutex);
			EnsureLoaded();
			if (!g_ledger.healthy) {
				return false;
			}
			const auto found = g_ledger.state.entries.find(a_operation);
			if (found == g_ledger.state.entries.end()) {
				return false;
			}
			if (!g_receipts || !g_receipts->Insert(a_operation)) return false;
			auto entry = found->second;
			entry.added = true;
			entry.confirmed = false;
			entry.addedAt = NowSeconds();
			F4T::DeathBagJournal::Unit unit;
			unit.ops.push_back(F4T::DeathBagJournal::EntryOp(a_operation, entry));
			g_ledger.unsaved.insert(a_operation);
			if (!CommitOrKeep(unit)) REX::WARN("Death bag grant ledger could not persist the current receipt; save confirmation is required.");
			return true;
		} catch (...) {
			return false;
		}
	}

	void BeginSave(const std::string& a_saveName, const F4T::DeathBagReceipts::SaveBaseline& a_baseline)
	{
		try {
			const std::scoped_lock applicationLock(g_applicationMutex);
			const std::scoped_lock lock(g_mutex);
			if (++g_saveAttempt == 0) ++g_saveAttempt;
			const bool overlapping = g_saveBarrier.Blocked() || !g_pendingSaveName.empty();
			g_saveBarrier.Begin(g_saveAttempt, a_saveName, a_baseline);
			g_savingReceipts = overlapping ? std::nullopt : g_receipts;
			g_preSaveName = overlapping ? std::string{} : a_saveName;
			g_saveBaseline = overlapping ? F4T::DeathBagReceipts::SaveBaseline{} : a_baseline;
			g_receiptWritten = false;
			g_savePersistWarning = false;
			g_saveProbeTimedOut = false;
			g_pendingSaveName.clear();
			g_pendingPreviousSaveHash.reset();
			g_ledger.saving = g_ledger.unsaved;
		} catch (...) {
			return;
		}
	}

	void FinishSave(const std::string& a_saveName)
	{
		const std::scoped_lock applicationLock(g_applicationMutex);
		const std::scoped_lock lock(g_mutex);
		g_saveBarrier.Finish(a_saveName);
		if (g_saveBarrier.Blocked() && !g_saveBarrier.NeedsObservation()) REX::WARN("Death bag withdrawals are held because the save could not be verified. Load a completed save to resume.");
		g_nextLocalProbe = {};
		g_localProbeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
		if (!a_saveName.empty() && a_saveName == g_preSaveName && g_receiptWritten && g_savingReceipts) {
			g_pendingSaveName = a_saveName;
			g_pendingPreviousSaveHash = g_loadedSaveHash;
		}
	}

	std::optional<std::uint64_t> PendingSaveAttempt(const std::string& a_saveName)
	{
		const std::scoped_lock lock(g_mutex);
		if (a_saveName.empty() || a_saveName != g_pendingSaveName || !g_receiptWritten || !g_savingReceipts) return std::nullopt;
		return g_saveAttempt;
	}

	std::optional<std::uint64_t> PendingBarrierAttempt(const std::string& a_saveName)
	{
		const std::scoped_lock lock(g_mutex);
		if (!g_saveBarrier.NeedsObservation() || g_saveBarrier.Name() != a_saveName) return std::nullopt;
		return g_saveBarrier.Attempt();
	}

	void ObserveSaveBarrier(const std::string& a_saveName, const F4T::DeathBagReceipts::SaveBaseline& a_observed, std::uint64_t a_generation)
	{
		const std::scoped_lock applicationLock(g_applicationMutex);
		const std::scoped_lock lock(g_mutex);
		(void)g_saveBarrier.Observe(a_generation, a_saveName, a_observed);
	}

	std::optional<bool> SaveCandidateReady(const std::string& a_saveName, const F4T::DeathBagReceipts::SaveBaseline& a_observed, std::optional<std::uint64_t> a_generation)
	{
		if (!a_generation) return std::nullopt;
		const std::scoped_lock lock(g_mutex);
		if (!F4T::DeathBagReceipts::MatchesSaveAttempt(*a_generation, a_saveName, g_saveAttempt, g_pendingSaveName) || !g_receiptWritten || !g_savingReceipts) return false;
		return F4T::DeathBagReceipts::SaveChanged(g_saveBaseline, a_observed);
	}

	void ConfirmSaved(const std::string& a_saveName, const F4T::DeathBagReceipts::SaveBaseline& a_observed, std::optional<std::uint64_t> a_generation)
	{
		try {
			const std::scoped_lock applicationLock(g_applicationMutex);
			const std::scoped_lock lock(g_mutex);
			EnsureLoaded();
			if (a_generation && !F4T::DeathBagReceipts::MatchesSaveAttempt(*a_generation, a_saveName, g_saveAttempt, g_pendingSaveName)) return;
			if (!g_ledger.healthy || a_saveName.empty() || a_saveName != g_pendingSaveName || !g_receiptWritten || !g_savingReceipts ||
				!F4T::DeathBagReceipts::SaveChanged(g_saveBaseline, a_observed)) return;
			const auto& saveHash = a_observed.hash;
			const auto receiptHash = F4T::ContentManifest::Sha256Hex(g_savingReceipts->Encode());
			if (g_ledger.state.receiptBindings.contains(saveHash) && !F4T::DeathBagReceipts::IsReceiptBound(g_ledger.state.receiptBindings, saveHash, receiptHash)) {
				if (!g_savePersistWarning) REX::WARN("Death bag save receipts could not be bound to this save; manual reconciliation may be required.");
				g_savePersistWarning = true;
				return;
			}
			F4T::DeathBagJournal::Unit unit;
			if (!F4T::DeathBagReceipts::IsReceiptBound(g_ledger.state.receiptBindings, saveHash, receiptHash)) unit.ops.push_back(F4T::DeathBagJournal::BindOp(saveHash, receiptHash));
			const auto previousSaveHash = g_pendingPreviousSaveHash;
			g_loadedSaveHash = saveHash;
			if (previousSaveHash) {
				F4T::DeathBagReceipts::CarryVolatileProofs(g_ledger.state.withdrawalProofs, *previousSaveHash, *g_savingReceipts, g_volatileWithdrawalProofs);
				for (const auto& [operation, hashes] : g_ledger.state.withdrawalProofs) {
					if ((hashes.contains(*previousSaveHash) || g_volatileWithdrawalProofs.contains(operation)) && !g_savingReceipts->Contains(operation) && !hashes.contains(saveHash)) {
						unit.ops.push_back(F4T::DeathBagJournal::ProofOp(operation, saveHash));
					}
				}
			}
			const auto removals = F4T::DeathBagJournal::SavedRemovals(g_ledger.state.entries, g_ledger.saving, *g_savingReceipts);
			for (const auto& operation : removals) unit.ops.push_back(F4T::DeathBagJournal::RemoveOp(operation));
			if (!Commit(unit)) {
				if (!g_savePersistWarning) REX::WARN("Death bag save proofs could not be written; recovery after restarting may require manual reconciliation.");
				g_savePersistWarning = true;
				return;
			}
			for (const auto& operation : removals) g_ledger.unsaved.erase(operation);
			g_ledger.saving.clear();
			g_savingReceipts.reset();
			g_pendingSaveName.clear();
			g_pendingPreviousSaveHash.reset();
		} catch (...) {
			return;
		}
	}

	void TickPendingSave()
	{
		try {
			if (F4T::SessionSaveGuard::IsSessionExitPending() && !F4T::SessionSaveGuard::IsSessionExitLanded()) return;
			std::string name;
			std::uint64_t attempt{};
			bool observeBarrier{};
			{
				const std::scoped_lock lock(g_mutex);
				const auto now = std::chrono::steady_clock::now();
				observeBarrier = g_saveBarrier.NeedsObservation();
				name = observeBarrier ? g_saveBarrier.Name() : g_pendingSaveName;
				if (name.empty() || now < g_nextLocalProbe) return;
				if (now >= g_localProbeDeadline) {
					if (!g_saveProbeTimedOut) REX::WARN("Death bag save verification timed out. Load a completed save to resume withdrawals.");
					g_saveProbeTimedOut = true;
					return;
				}
				if (F4T::SaveLineageSession::HasPendingRecordFor(name)) return;
				attempt = observeBarrier ? g_saveBarrier.Attempt() : g_saveAttempt;
				g_nextLocalProbe = std::chrono::steady_clock::time_point::max();
			}
			const auto observed = F4T::SaveLineageSession::CaptureSaveBaseline(name);
			{
				const std::scoped_lock lock(g_mutex);
				const bool matchesBarrier = g_saveBarrier.NeedsObservation() && F4T::DeathBagReceipts::MatchesSaveAttempt(attempt, name, g_saveBarrier.Attempt(), g_saveBarrier.Name());
				const bool matchesPending = F4T::DeathBagReceipts::MatchesSaveAttempt(attempt, name, g_saveAttempt, g_pendingSaveName);
				if (!matchesBarrier && !matchesPending) return;
				g_nextLocalProbe = std::chrono::steady_clock::now() + std::chrono::seconds(2);
			}
			ConfirmSaved(name, observed, attempt);
			if (observeBarrier) ObserveSaveBarrier(name, observed, attempt);
		} catch (...) { return; }
	}

	void NoteLoad(const std::string& a_saveName)
	{
		try {
			auto name = a_saveName;
			const auto slash = name.find_last_of("/\\");
			if (slash != std::string::npos) name.erase(0, slash + 1);
			if (name.size() > 4 && (name.substr(name.size() - 4) == ".fos" || name.substr(name.size() - 4) == ".FOS")) name.resize(name.size() - 4);
			const std::scoped_lock applicationLock(g_applicationMutex);
			const auto hash = F4T::SaveLineageSession::HashSaveForRecovery(name);
			const std::scoped_lock lock(g_mutex);
			if (++g_saveAttempt == 0) ++g_saveAttempt;
			++g_ledger.loadGeneration;
			g_loadedSaveHash = hash;
			g_loadedSaveName = name;
			g_volatileWithdrawalProofs.clear();
			g_receipts.reset();
			g_loadedReceipts.reset();
			g_receiptChunks.Reset();
			g_loadSucceeded.reset();
			g_savingReceipts.reset();
			g_pendingSaveName.clear();
			g_pendingPreviousSaveHash.reset();
			g_saveBarrier.Reset();
			EnsureLoaded();
			g_ledger.unsaved.clear();
			g_ledger.saving.clear();
		} catch (...) {
			return;
		}
	}

	void RecordPlaceFailure(const std::string& a_deathId, bool a_restored, const nlohmann::json& a_remaining)
	{
		try {
			const std::scoped_lock lock(g_mutex);
			EnsureLoaded();
			if (g_ledger.state.placeFailures.contains(a_deathId)) {
				return;
			}
			F4T::DeathBagJournal::Unit unit;
			unit.ops.push_back(F4T::DeathBagJournal::PlaceOp(PlaceFailure{ a_deathId, a_restored, a_remaining, NowSeconds() }));
			if (!CommitOrKeep(unit) && g_ledger.healthy) {
				REX::WARN("Death bag place-failure record could not be recorded.");
			}
		} catch (...) {
			return;
		}
	}

	void ForgetPlaceFailure(const std::string& a_deathId)
	{
		try {
			const std::scoped_lock lock(g_mutex);
			EnsureLoaded();
			if (!g_ledger.state.placeFailures.contains(a_deathId)) {
				return;
			}
			F4T::DeathBagJournal::Unit unit;
			unit.ops.push_back(F4T::DeathBagJournal::ForgetPlaceOp(a_deathId));
			if (!CommitOrKeep(unit) && g_ledger.healthy) {
				REX::WARN("Death bag place-failure acknowledgement could not be saved; the record may be resent after a restart.");
			}
		} catch (...) {
			return;
		}
	}

	std::vector<PlaceFailure> PlaceFailures()
	{
		try {
			const std::scoped_lock lock(g_mutex);
			EnsureLoaded();
			std::vector<PlaceFailure> failures;
			for (const auto& failure : g_ledger.state.placeFailures) {
				failures.push_back(failure.second);
			}
			return failures;
		} catch (...) {
			return {};
		}
	}

	bool RecordPendingPlace(const std::string& a_deathId, const nlohmann::json& a_stacks)
	{
		try {
			if (!a_stacks.is_array() || a_stacks.empty()) return false;
			const std::scoped_lock lock(g_mutex);
			EnsureLoaded();
			F4T::DeathBagJournal::Unit unit;
			unit.ops.push_back(F4T::DeathBagJournal::PendOp(PendingPlace{ a_deathId, "pending", a_stacks, NowSeconds() }));
			return CommitOrKeep(unit);
		} catch (...) {
			return false;
		}
	}

	bool SetPendingPlacePhase(const std::string& a_deathId, const std::string& a_phase)
	{
		try {
			const std::scoped_lock lock(g_mutex);
			EnsureLoaded();
			const auto found = g_ledger.state.pendingPlaces.find(a_deathId);
			if (found == g_ledger.state.pendingPlaces.end()) return false;
			if (found->second.phase == a_phase) return true;
			F4T::DeathBagJournal::Unit unit;
			unit.ops.push_back(F4T::DeathBagJournal::PendOp(PendingPlace{ a_deathId, a_phase, found->second.stacks, found->second.createdAt }));
			if (CommitOrKeep(unit)) return true;
			if (g_ledger.healthy) REX::WARN("Death bag placement phase could not be saved.");
			return false;
		} catch (...) {
			return false;
		}
	}

	void ForgetPendingPlace(const std::string& a_deathId)
	{
		try {
			const std::scoped_lock lock(g_mutex);
			EnsureLoaded();
			if (!g_ledger.state.pendingPlaces.contains(a_deathId)) return;
			F4T::DeathBagJournal::Unit unit;
			unit.ops.push_back(F4T::DeathBagJournal::ForgetPendOp(a_deathId));
			if (!CommitOrKeep(unit) && g_ledger.healthy) {
				REX::WARN("Death bag placement record could not be cleared; it is resolved again after a restart.");
			}
		} catch (...) {
			return;
		}
	}

	std::vector<PendingPlace> PendingPlaces()
	{
		try {
			const std::scoped_lock lock(g_mutex);
			EnsureLoaded();
			std::vector<PendingPlace> entries;
			for (const auto& entry : g_ledger.state.pendingPlaces) {
				entries.push_back(entry.second);
			}
			return entries;
		} catch (...) {
			return {};
		}
	}

	std::uint64_t LoadGeneration()
	{
		const std::scoped_lock lock(g_mutex);
		return g_ledger.loadGeneration;
	}
}

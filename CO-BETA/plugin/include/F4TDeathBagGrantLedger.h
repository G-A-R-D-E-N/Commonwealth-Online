#pragma once
#include <cstdint>
#include <algorithm>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <set>
#include <vector>
#include <nlohmann/json.hpp>
#include "F4TDeathBagJournal.h"
#include "F4TDeathBagReceipts.h"
namespace F4SE { class SerializationInterface; }
namespace F4T::DeathBagGrantLedger
{
	inline constexpr std::uint32_t ReceiptRecordType = 'DBGR';
	using ReceiptStatus = F4T::DeathBagReceipts::ReceiptStatus;
	using Entry = F4T::DeathBagJournal::Entry;
	using PlaceFailure = F4T::DeathBagJournal::PlaceFailure;
	using PendingPlace = F4T::DeathBagJournal::PendingPlace;
	[[nodiscard]] std::optional<Entry> Find(const std::string& a_operation);
	[[nodiscard]] bool Record(const std::string& a_operation, std::uint64_t a_baseline);
	[[nodiscard]] bool MarkAdded(const std::string& a_operation);
	[[nodiscard]] ReceiptStatus ReceiptFor(const std::string& a_operation);
	[[nodiscard]] bool CanRecordReceipt(const std::string& a_operation);
	[[nodiscard]] bool BeginFreshReceipt();
	[[nodiscard]] bool PrepareWithdrawal(const std::string& a_operation);
	[[nodiscard]] bool HasWithdrawalProof(const std::string& a_operation);
	[[nodiscard]] std::mutex& ApplicationMutex();
	[[nodiscard]] bool SaveInProgress();
	void SaveReceipts(const F4SE::SerializationInterface* a_intfc);
	void LoadReceipts(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length);
	void FinishReceiptLoad(bool a_success);
	void NewGame();
	void BeginSave(const std::string& a_saveName, const F4T::DeathBagReceipts::SaveBaseline& a_baseline);
	void FinishSave(const std::string& a_saveName);
	[[nodiscard]] std::optional<std::uint64_t> PendingSaveAttempt(const std::string& a_saveName);
	[[nodiscard]] std::optional<std::uint64_t> PendingBarrierAttempt(const std::string& a_saveName);
	void ObserveSaveBarrier(const std::string& a_saveName, const F4T::DeathBagReceipts::SaveBaseline& a_observed, std::uint64_t a_generation);
	[[nodiscard]] std::optional<bool> SaveCandidateReady(const std::string& a_saveName, const F4T::DeathBagReceipts::SaveBaseline& a_observed, std::optional<std::uint64_t> a_generation);
	void ConfirmSaved(const std::string& a_saveName, const F4T::DeathBagReceipts::SaveBaseline& a_observed, std::optional<std::uint64_t> a_generation = std::nullopt);
	void TickPendingSave();
	void NoteLoad(const std::string& a_saveName);
	void RecordPlaceFailure(const std::string& a_deathId, bool a_restored, const nlohmann::json& a_remaining);
	void ForgetPlaceFailure(const std::string& a_deathId);
	[[nodiscard]] std::vector<PlaceFailure> PlaceFailures();
	[[nodiscard]] bool RecordPendingPlace(const std::string& a_deathId, const nlohmann::json& a_stacks);
	[[nodiscard]] bool SetPendingPlacePhase(const std::string& a_deathId, const std::string& a_phase);
	void ForgetPendingPlace(const std::string& a_deathId);
	[[nodiscard]] std::vector<PendingPlace> PendingPlaces();
	[[nodiscard]] std::uint64_t LoadGeneration();
}

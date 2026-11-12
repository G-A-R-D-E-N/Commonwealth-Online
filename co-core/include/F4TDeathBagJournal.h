#pragma once

#include "F4TDeathBagHistory.h"
#include "F4TDeathBagReceipts.h"
#include "F4TDeathBagWire.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

namespace F4T::DeathBagJournal
{
	inline constexpr int LegacyFirstVersion = 1;
	inline constexpr int LegacyLastVersion = 4;
	inline constexpr std::size_t MaximumPartBytes = F4T::DeathBagHistory::MaximumTransactionBytes - 512;
	inline constexpr std::size_t MaximumPartOpsBytes = MaximumPartBytes - 128;

	struct Entry
	{
		std::optional<std::uint64_t> baseline;
		bool added{ false };
		bool confirmed{ false };
		bool unsaved{ false };
		std::int64_t addedAt{};
	};

	struct PlaceFailure
	{
		std::string deathId;
		bool restored{ true };
		nlohmann::json remaining = nlohmann::json::array();
		std::int64_t createdAt{};
	};

	struct PendingPlace
	{
		std::string deathId;
		std::string phase{ "pending" };
		nlohmann::json stacks = nlohmann::json::array();
		std::int64_t createdAt{};
	};

	struct State
	{
		std::deque<std::string> order;
		std::map<std::string, Entry> entries;
		std::map<std::string, std::set<std::string>> withdrawalProofs;
		std::map<std::string, std::string> receiptBindings;
		std::map<std::string, PlaceFailure> placeFailures;
		std::map<std::string, PendingPlace> pendingPlaces;
	};

	struct Unit
	{
		bool reset{ false };
		std::vector<nlohmann::json> ops;
	};

	struct Encoded
	{
		std::string bytes;
		std::uint64_t frames{};
	};

	struct ReplayResult
	{
		bool valid{ true };
		bool torn{ false };
		std::size_t committedBytes{};
		std::uint64_t nextSequence{ 1 };
		State state;
	};

	[[nodiscard]] inline nlohmann::json EntryOp(const std::string& a_operation, const Entry& a_entry)
	{
		return nlohmann::json{ { "op", "entry" }, { "operation", a_operation },
			{ "baseline", a_entry.baseline ? nlohmann::json(*a_entry.baseline) : nlohmann::json(nullptr) },
			{ "added", a_entry.added }, { "confirmed", a_entry.confirmed }, { "addedAt", a_entry.addedAt } };
	}

	[[nodiscard]] inline nlohmann::json ProofOp(const std::string& a_operation, const std::string& a_hash)
	{
		return nlohmann::json{ { "op", "proof" }, { "operation", a_operation }, { "hash", a_hash } };
	}

	[[nodiscard]] inline nlohmann::json BindOp(const std::string& a_saveHash, const std::string& a_receiptHash)
	{
		return nlohmann::json{ { "op", "bind" }, { "saveHash", a_saveHash }, { "receiptHash", a_receiptHash } };
	}

	[[nodiscard]] inline nlohmann::json RemoveOp(const std::string& a_operation)
	{
		return nlohmann::json{ { "op", "remove" }, { "operation", a_operation } };
	}

	[[nodiscard]] inline nlohmann::json PlaceOp(const PlaceFailure& a_failure)
	{
		return nlohmann::json{ { "op", "place" }, { "deathId", a_failure.deathId }, { "restored", a_failure.restored }, { "remaining", a_failure.remaining }, { "createdAt", a_failure.createdAt } };
	}

	[[nodiscard]] inline nlohmann::json ForgetPlaceOp(const std::string& a_deathId)
	{
		return nlohmann::json{ { "op", "forgetPlace" }, { "deathId", a_deathId } };
	}

	[[nodiscard]] inline nlohmann::json PendOp(const PendingPlace& a_pending)
	{
		return nlohmann::json{ { "op", "pend" }, { "deathId", a_pending.deathId }, { "phase", a_pending.phase },
			{ "stacks", a_pending.stacks }, { "createdAt", a_pending.createdAt } };
	}

	[[nodiscard]] inline nlohmann::json ForgetPendOp(const std::string& a_deathId)
	{
		return nlohmann::json{ { "op", "forgetPend" }, { "deathId", a_deathId } };
	}

	[[nodiscard]] inline bool ReadOperation(const nlohmann::json& a_value, std::string& a_operation)
	{
		if (!a_value.is_string() || a_value.get_ref<const std::string&>().empty()) return false;
		a_operation = a_value.get<std::string>();
		return true;
	}

	[[nodiscard]] inline std::optional<PlaceFailure> ParsePlace(const nlohmann::json& a_record, std::int64_t a_createdAt)
	{
		if (!a_record.is_object() || !a_record.contains("deathId") || !a_record.at("deathId").is_string() || !a_record.contains("restored") ||
			!a_record.at("restored").is_boolean() || !a_record.contains("remaining")) return std::nullopt;
		const auto deathId = a_record.at("deathId").get<std::string>();
		const auto restored = a_record.at("restored").get<bool>();
		const auto& remaining = a_record.at("remaining");
		if (!F4T::DeathBagWire::EncodePlaceFailed(deathId, restored, remaining)) return std::nullopt;
		return PlaceFailure{ deathId, restored, remaining, a_createdAt };
	}

	[[nodiscard]] inline std::optional<PlaceFailure> ReadPlaceFailure(const nlohmann::json& a_record, std::int64_t a_now)
	{
		if (!a_record.is_object() || !a_record.contains("deathId") || !a_record.at("deathId").is_string()) return std::nullopt;
		if (a_record.contains("packet")) {
			const auto packet = F4T::DeathBagWire::DecodePlaceFailed(a_record.at("packet"));
			if (!packet) return std::nullopt;
			return PlaceFailure{ packet->deathId, packet->restored, packet->remaining, a_now };
		}
		const auto createdAt = (a_record.contains("createdAt") && a_record.at("createdAt").is_number_integer()) ? a_record.at("createdAt").get<std::int64_t>() : a_now;
		return ParsePlace(a_record, createdAt);
	}

	[[nodiscard]] inline bool ApplyOperation(State& a_state, const nlohmann::json& a_op)
	{
		if (!a_op.is_object() || !a_op.contains("op") || !a_op.at("op").is_string()) return false;
		const auto& kind = a_op.at("op").get_ref<const std::string&>();
		std::string operation;
		if (kind == "entry") {
			if (a_op.size() != 6 || !a_op.contains("operation") || !ReadOperation(a_op.at("operation"), operation) || !a_op.contains("baseline") ||
				!a_op.contains("added") || !a_op.contains("confirmed") || !a_op.contains("addedAt")) return false;
			const auto& baseline = a_op.at("baseline");
			if ((!baseline.is_null() && !baseline.is_number_unsigned()) || !a_op.at("added").is_boolean() || !a_op.at("confirmed").is_boolean() || !a_op.at("addedAt").is_number_integer()) return false;
			const Entry entry{ baseline.is_null() ? std::nullopt : std::optional<std::uint64_t>{ baseline.get<std::uint64_t>() }, a_op.at("added").get<bool>(), a_op.at("confirmed").get<bool>(), false, a_op.at("addedAt").get<std::int64_t>() };
			if (!a_state.entries.contains(operation)) a_state.order.push_back(operation);
			a_state.entries.insert_or_assign(operation, entry);
			return true;
		}
		if (kind == "proof") {
			if (a_op.size() != 3 || !a_op.contains("operation") || !ReadOperation(a_op.at("operation"), operation) || !a_op.contains("hash") || !a_op.at("hash").is_string()) return false;
			return F4T::DeathBagReceipts::AddSaveProof(a_state.withdrawalProofs[operation], a_op.at("hash").get<std::string>());
		}
		if (kind == "bind") {
			if (a_op.size() != 3 || !a_op.contains("saveHash") || !a_op.at("saveHash").is_string() || !a_op.contains("receiptHash") || !a_op.at("receiptHash").is_string()) return false;
			return F4T::DeathBagReceipts::BindReceipt(a_state.receiptBindings, a_op.at("saveHash").get<std::string>(), a_op.at("receiptHash").get<std::string>());
		}
		if (kind == "remove") {
			if (a_op.size() != 2 || !a_op.contains("operation") || !ReadOperation(a_op.at("operation"), operation)) return false;
			a_state.entries.erase(operation);
			std::erase(a_state.order, operation);
			return true;
		}
		if (kind == "place") {
			if (a_op.size() != 5 || !a_op.contains("createdAt") || !a_op.at("createdAt").is_number_integer()) return false;
			const auto failure = ParsePlace(a_op, a_op.at("createdAt").get<std::int64_t>());
			if (!failure) return false;
			a_state.placeFailures.insert_or_assign(failure->deathId, *failure);
			return true;
		}
		if (kind == "forgetPlace") {
			if (a_op.size() != 2 || !a_op.contains("deathId") || !a_op.at("deathId").is_string()) return false;
			a_state.placeFailures.erase(a_op.at("deathId").get<std::string>());
			return true;
		}
		if (kind == "pend") {
			if (a_op.size() != 5 || !a_op.contains("deathId") || !a_op.at("deathId").is_string() || !a_op.contains("phase") || !a_op.at("phase").is_string() ||
				!a_op.contains("stacks") || !a_op.contains("createdAt") || !a_op.at("createdAt").is_number_integer()) return false;
			const auto deathId = a_op.at("deathId").get<std::string>();
			const auto phase = a_op.at("phase").get<std::string>();
			if (!F4T::DeathBagWire::ValidDeathId(deathId) || (phase != "pending" && phase != "placing" && phase != "placed" && phase != "unplaced") ||
				!F4T::DeathBagWire::ValidStacks(a_op.at("stacks"))) return false;
			a_state.pendingPlaces.insert_or_assign(deathId, PendingPlace{ deathId, phase, a_op.at("stacks"), a_op.at("createdAt").get<std::int64_t>() });
			return true;
		}
		if (kind == "forgetPend") {
			if (a_op.size() != 2 || !a_op.contains("deathId") || !a_op.at("deathId").is_string()) return false;
			a_state.pendingPlaces.erase(a_op.at("deathId").get<std::string>());
			return true;
		}
		return false;
	}

	[[nodiscard]] inline bool ApplyOps(State& a_state, const std::vector<nlohmann::json>& a_ops)
	{
		for (const auto& op : a_ops) {
			if (!ApplyOperation(a_state, op)) return false;
		}
		return true;
	}

	[[nodiscard]] inline bool Apply(State& a_state, const Unit& a_unit)
	{
		auto next = a_unit.reset ? State{} : a_state;
		if (!ApplyOps(next, a_unit.ops)) return false;
		a_state = std::move(next);
		return true;
	}

	[[nodiscard]] inline std::optional<Unit> Checkpoint(const State& a_state)
	{
		if (a_state.order.size() != a_state.entries.size()) return std::nullopt;
		Unit unit;
		unit.reset = true;
		std::set<std::string> seen;
		for (const auto& operation : a_state.order) {
			const auto found = a_state.entries.find(operation);
			if (found == a_state.entries.end() || !seen.insert(operation).second) return std::nullopt;
			unit.ops.push_back(EntryOp(operation, found->second));
		}
		for (const auto& [operation, hashes] : a_state.withdrawalProofs) {
			for (const auto& hash : hashes) unit.ops.push_back(ProofOp(operation, hash));
		}
		for (const auto& [saveHash, receiptHash] : a_state.receiptBindings) unit.ops.push_back(BindOp(saveHash, receiptHash));
		for (const auto& failure : a_state.placeFailures) unit.ops.push_back(PlaceOp(failure.second));
		for (const auto& pending : a_state.pendingPlaces) unit.ops.push_back(PendOp(pending.second));
		return unit;
	}

	[[nodiscard]] inline std::vector<std::string> SavedRemovals(const std::map<std::string, Entry>& a_entries, const std::set<std::string>& a_saving,
		const F4T::DeathBagReceipts::ReceiptSet& a_frozen)
	{
		std::vector<std::string> removals;
		for (const auto& operation : a_frozen.SavedOperations(a_saving)) {
			const auto found = a_entries.find(operation);
			if (found != a_entries.end() && found->second.added) removals.push_back(operation);
		}
		return removals;
	}

	[[nodiscard]] inline std::optional<Encoded> Encode(std::uint64_t a_firstSequence, const Unit& a_unit)
	{
		try {
			std::vector<std::vector<nlohmann::json>> parts(1);
			std::size_t bytes = 0;
			for (const auto& op : a_unit.ops) {
				const auto size = op.dump().size() + 1;
				if (size > MaximumPartOpsBytes) return std::nullopt;
				if (bytes + size > MaximumPartOpsBytes && !parts.back().empty()) {
					parts.emplace_back();
					bytes = 0;
				}
				parts.back().push_back(op);
				bytes += size;
			}
			Encoded encoded;
			for (std::size_t index = 0; index < parts.size(); ++index) {
				const auto frame = F4T::DeathBagHistory::EncodeTransaction(a_firstSequence + index,
					nlohmann::json{ { "ops", parts[index] }, { "part", index }, { "parts", parts.size() }, { "reset", a_unit.reset } });
				if (!frame) return std::nullopt;
				encoded.bytes += *frame;
			}
			encoded.frames = parts.size();
			return encoded;
		} catch (...) {
			return std::nullopt;
		}
	}

	[[nodiscard]] inline bool ApplyEncodable(State& a_state, const Unit& a_unit, std::uint64_t a_sequence)
	{
		return Encode(a_sequence, a_unit) && Apply(a_state, a_unit);
	}

	[[nodiscard]] inline ReplayResult Replay(std::string_view a_bytes)
	{
		ReplayResult result;
		std::vector<nlohmann::json> pending;
		bool pendingReset = false;
		std::uint64_t nextPart = 0;
		std::uint64_t partCount = 0;
		std::uint64_t sequence = 1;
		std::size_t offset = 0;
		while (offset < a_bytes.size()) {
			const auto rest = a_bytes.substr(offset);
			if (rest.find_first_not_of('\0') == std::string_view::npos) {
				result.torn = true;
				break;
			}
			const auto decoded = F4T::DeathBagHistory::DecodeTransaction(rest, sequence);
			const bool reachesEnd = F4T::DeathBagHistory::DeclaredFrameSize(rest) == rest.size();
			if (decoded.status == F4T::DeathBagHistory::DecodeStatus::Incomplete ||
				(decoded.status == F4T::DeathBagHistory::DecodeStatus::Corrupt && reachesEnd)) {
				result.torn = true;
				break;
			}
			const auto& payload = decoded.payload;
			if (decoded.status != F4T::DeathBagHistory::DecodeStatus::Committed || payload.size() != 4 || !payload.contains("ops") || !payload.at("ops").is_array() ||
				!payload.contains("reset") || !payload.at("reset").is_boolean() || !payload.contains("part") || !payload.at("part").is_number_unsigned() ||
				!payload.contains("parts") || !payload.at("parts").is_number_unsigned()) {
				result.valid = false;
				return result;
			}
			const auto part = payload.at("part").get<std::uint64_t>();
			const auto parts = payload.at("parts").get<std::uint64_t>();
			const auto reset = payload.at("reset").get<bool>();
			if (parts == 0 || part >= parts || part != nextPart || (part != 0 && (parts != partCount || reset != pendingReset))) {
				result.valid = false;
				return result;
			}
			if (part == 0) {
				pending.clear();
				pendingReset = reset;
				partCount = parts;
			}
			for (const auto& op : payload.at("ops")) pending.push_back(op);
			offset += decoded.consumed;
			++sequence;
			nextPart = part + 1;
			if (nextPart < parts) continue;
			nextPart = 0;
			if (pendingReset) result.state = State{};
			if (!ApplyOps(result.state, pending)) {
				result.valid = false;
				return result;
			}
			result.committedBytes = offset;
			result.nextSequence = sequence;
		}
		if (nextPart != 0) result.torn = true;
		return result;
	}

	[[nodiscard]] inline std::optional<State> MigrateLegacy(const nlohmann::json& a_document, std::int64_t a_now, std::size_t& a_droppedPlaces)
	{
		try {
			const auto version = a_document.is_object() ? a_document.value("version", 0) : 0;
			if (version < LegacyFirstVersion || version > LegacyLastVersion || !a_document.contains("grants") ||
				!F4T::DeathBagReceipts::ValidLedgerOperations(a_document.at("grants"))) return std::nullopt;
			State state;
			std::size_t dropped = 0;
			for (const auto& entry : a_document.at("grants")) {
				const bool legacy = entry.is_string();
				const auto operation = legacy ? entry.get<std::string>() : entry.at("operation").get<std::string>();
				const auto baseline = (legacy || entry.at("baseline").is_null()) ? std::nullopt : std::optional<std::uint64_t>{ entry.at("baseline").get<std::uint64_t>() };
				const bool added = !legacy && entry.value("added", false);
				const auto addedAt = legacy ? std::int64_t{} : entry.value("addedAt", std::int64_t{});
				if (state.entries.try_emplace(operation, Entry{ baseline, added, added && entry.value("confirmed", false), false, addedAt }).second) state.order.push_back(operation);
			}
			if (a_document.contains("withdrawalProofs")) {
				const auto& proofs = a_document.at("withdrawalProofs");
				if (!proofs.is_object()) return std::nullopt;
				for (const auto& [operation, hashes] : proofs.items()) {
					if (operation.empty() || !hashes.is_array() || hashes.empty()) return std::nullopt;
					std::set<std::string> saved;
					for (const auto& hash : hashes) {
						if (!hash.is_string() || !F4T::DeathBagReceipts::AddSaveProof(saved, hash.get<std::string>())) return std::nullopt;
					}
					state.withdrawalProofs.emplace(operation, std::move(saved));
				}
			}
			if (a_document.contains("receiptBindings")) {
				const auto& bindings = a_document.at("receiptBindings");
				if (!bindings.is_object()) return std::nullopt;
				for (const auto& [saveHash, receiptHash] : bindings.items()) {
					if (!receiptHash.is_string() || !F4T::DeathBagReceipts::BindReceipt(state.receiptBindings, saveHash, receiptHash.get<std::string>())) return std::nullopt;
				}
			} else if (version >= LegacyLastVersion) {
				return std::nullopt;
			}
			if (a_document.contains("placeFailures")) {
				if (!a_document.at("placeFailures").is_array()) return std::nullopt;
				for (const auto& record : a_document.at("placeFailures")) {
					const auto failure = ReadPlaceFailure(record, a_now);
					if (failure) state.placeFailures.try_emplace(failure->deathId, *failure);
					else ++dropped;
				}
			}
			a_droppedPlaces = dropped;
			return state;
		} catch (...) {
			return std::nullopt;
		}
	}
}

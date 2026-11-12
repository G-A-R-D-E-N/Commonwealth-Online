#pragma once

#include "F4TContentManifest.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace F4T::DeathBagReceipts
{
	inline constexpr std::size_t MaximumBytes = 512 * 1024;
	inline constexpr std::size_t MaximumChunkBytes = 64 * 1024;
	inline constexpr std::size_t MaximumChunkRecordBytes = MaximumChunkBytes + 512;
	enum class ReceiptStatus { Unknown, Absent, Present };
	enum class GrantDisposition { Hold, Apply, Duplicate };

	[[nodiscard]] inline GrantDisposition DecideGrant(ReceiptStatus a_receipt, bool a_fresh, bool a_proof)
	{
		if (a_receipt == ReceiptStatus::Present) return GrantDisposition::Duplicate;
		if (a_receipt == ReceiptStatus::Absent || (a_fresh && a_proof)) return GrantDisposition::Apply;
		return GrantDisposition::Hold;
	}

	[[nodiscard]] inline bool MatchesSave(std::string_view a_expected, std::string_view a_current)
	{
		if (a_expected.size() != 64 || a_current != a_expected) return false;
		for (const char ch : a_expected) if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
		return true;
	}

	struct SaveBaseline
	{
		enum class State { Unknown, Absent, Present };
		State state{ State::Unknown };
		std::string hash;
		std::filesystem::path path;
	};

	[[nodiscard]] inline SaveBaseline::State InspectSaveTarget(const std::filesystem::path& a_path)
	{
		if (a_path.empty()) return SaveBaseline::State::Unknown;
		std::error_code ec;
		auto temporary = a_path;
		temporary += ".tmp";
		if (std::filesystem::exists(temporary, ec) || ec) return SaveBaseline::State::Unknown;
		const bool exists = std::filesystem::exists(a_path, ec);
		if (ec) return SaveBaseline::State::Unknown;
		return exists ? SaveBaseline::State::Present : SaveBaseline::State::Absent;
	}

	[[nodiscard]] inline bool SaveChanged(const SaveBaseline& a_before, const SaveBaseline& a_after)
	{
		if (a_before.path.empty() || a_before.path != a_after.path || a_after.state != SaveBaseline::State::Present || !MatchesSave(a_after.hash, a_after.hash)) return false;
		if (a_before.state == SaveBaseline::State::Absent) return true;
		return a_before.state == SaveBaseline::State::Present && MatchesSave(a_before.hash, a_before.hash) && a_before.hash != a_after.hash;
	}

	[[nodiscard]] inline bool MatchesSaveAttempt(std::uint64_t a_expected, std::string_view a_expectedName,
		std::uint64_t a_current, std::string_view a_currentName)
	{
		return a_expected != 0 && a_expected == a_current && !a_expectedName.empty() && a_expectedName == a_currentName;
	}

	class SaveGrantBarrier
	{
	public:
		void Begin(std::uint64_t a_attempt, const std::string& a_name, const SaveBaseline& a_baseline)
		{
			if (_blocked) { _ambiguous = true; return; }
			_blocked = true;
			_finished = false;
			_ambiguous = false;
			_attempt = a_attempt;
			_name = a_name;
			_baseline = a_baseline;
		}

		void Finish(std::string_view a_name)
		{
			if (!_blocked || _ambiguous || a_name.empty() || a_name != _name) { _ambiguous = true; return; }
			_finished = true;
		}

		[[nodiscard]] bool Observe(std::uint64_t a_attempt, std::string_view a_name, const SaveBaseline& a_observed)
		{
			if (!NeedsObservation() || !MatchesSaveAttempt(a_attempt, a_name, _attempt, _name) || !SaveChanged(_baseline, a_observed)) return false;
			Reset();
			return true;
		}

		void Reset() { _blocked = false; _finished = false; _ambiguous = false; _attempt = 0; _name.clear(); _baseline = {}; }
		[[nodiscard]] bool Blocked() const { return _blocked; }
		[[nodiscard]] bool NeedsObservation() const { return _blocked && _finished && !_ambiguous; }
		[[nodiscard]] const std::string& Name() const { return _name; }
		[[nodiscard]] std::uint64_t Attempt() const { return _attempt; }

	private:
		bool _blocked{};
		bool _finished{};
		bool _ambiguous{};
		std::uint64_t _attempt{};
		std::string _name;
		SaveBaseline _baseline;
	};

	[[nodiscard]] inline bool AddSaveProof(std::set<std::string>& a_proofs, const std::string& a_hash)
	{
		if (!MatchesSave(a_hash, a_hash)) return false;
		a_proofs.insert(a_hash);
		return true;
	}

	[[nodiscard]] inline bool HasSaveProof(const std::map<std::string, std::set<std::string>>& a_proofs,
		const std::string& a_operation, const std::string& a_hash)
	{
		if (!MatchesSave(a_hash, a_hash)) return false;
		const auto found = a_proofs.find(a_operation);
		return found != a_proofs.end() && found->second.contains(a_hash);
	}

	[[nodiscard]] inline bool ValidLedgerOperations(const nlohmann::json& a_entries)
	{
		if (!a_entries.is_array()) return false;
		std::set<std::string> operations;
		for (const auto& entry : a_entries) {
			const auto* operation = entry.is_string() ? &entry : (entry.is_object() && entry.contains("operation") ? &entry.at("operation") : nullptr);
			if (!operation || !operation->is_string() || operation->get_ref<const std::string&>().empty() ||
				!operations.insert(operation->get<std::string>()).second) return false;
		}
		return true;
	}

	class ReceiptSet
	{
	public:
		explicit ReceiptSet(bool a_complete = true) : _complete(a_complete) {}

		[[nodiscard]] bool IsComplete() const { return _complete; }

		[[nodiscard]] bool Insert(const std::string& a_operation)
		{
			if (a_operation.empty() || a_operation.size() > 128) return false;
			_operations.insert(a_operation);
			return true;
		}

		[[nodiscard]] bool Contains(const std::string& a_operation) const
		{
			return _operations.contains(a_operation);
		}

		[[nodiscard]] std::set<std::string> SavedOperations(const std::set<std::string>& a_candidates) const
		{
			std::set<std::string> saved;
			for (const auto& operation : a_candidates) if (Contains(operation)) saved.insert(operation);
			return saved;
		}

		[[nodiscard]] bool Merge(const ReceiptSet& a_other)
		{
			auto merged = *this;
			for (const auto& operation : a_other._operations) if (!merged.Insert(operation)) return false;
			*this = std::move(merged);
			return true;
		}

		[[nodiscard]] std::string Encode() const
		{
			return nlohmann::json{ { "complete", _complete }, { "operations", _operations } }.dump();
		}

		[[nodiscard]] static std::optional<ReceiptSet> Decode(std::string_view a_payload)
		{
			const auto parsed = nlohmann::json::parse(a_payload, nullptr, false);
			if (!parsed.is_object() || parsed.size() != 2 || !parsed.contains("complete") || !parsed.at("complete").is_boolean() ||
				!parsed.contains("operations") || !parsed.at("operations").is_array()) return std::nullopt;
			ReceiptSet result(parsed.at("complete").get<bool>());
			for (const auto& value : parsed.at("operations")) {
				if (!value.is_string()) return std::nullopt;
				const auto& operation = value.get_ref<const std::string&>();
				if (result.Contains(operation) || !result.Insert(operation)) return std::nullopt;
			}
			return result;
		}

	private:
		bool _complete;
		std::set<std::string> _operations;
	};

	[[nodiscard]] inline bool BindReceipt(std::map<std::string, std::string>& a_bindings, const std::string& a_saveHash, const std::string& a_receiptHash)
	{
		if (!MatchesSave(a_saveHash, a_saveHash) || !MatchesSave(a_receiptHash, a_receiptHash)) return false;
		const auto found = a_bindings.find(a_saveHash);
		if (found != a_bindings.end()) return found->second == a_receiptHash;
		a_bindings.emplace(a_saveHash, a_receiptHash);
		return true;
	}

	[[nodiscard]] inline ReceiptSet ForVerifiedSave(const std::map<std::string, std::string>& a_bindings,
		const std::optional<ReceiptSet>& a_receipts, std::string_view a_before, std::string_view a_after, std::string_view a_receiptHash)
	{
		if (!a_receipts || !MatchesSave(a_before, a_after) || !MatchesSave(a_receiptHash, a_receiptHash)) return ReceiptSet(false);
		const auto found = a_bindings.find(std::string(a_after));
		return found != a_bindings.end() && found->second == a_receiptHash ? *a_receipts : ReceiptSet(false);
	}

	[[nodiscard]] inline bool IsReceiptBound(const std::map<std::string, std::string>& a_bindings,
		std::string_view a_saveHash, std::string_view a_receiptHash)
	{
		if (!MatchesSave(a_saveHash, a_saveHash) || !MatchesSave(a_receiptHash, a_receiptHash)) return false;
		const auto found = a_bindings.find(std::string(a_saveHash));
		return found != a_bindings.end() && found->second == a_receiptHash;
	}

	inline void CarryVolatileProofs(const std::map<std::string, std::set<std::string>>& a_proofs,
		const std::string& a_previousHash, const ReceiptSet& a_savedReceipts, std::set<std::string>& a_volatileProofs)
	{
		for (const auto& [operation, hashes] : a_proofs) {
			if (hashes.contains(a_previousHash)) a_volatileProofs.insert(operation);
		}
		for (auto it = a_volatileProofs.begin(); it != a_volatileProofs.end();) {
			if (a_savedReceipts.Contains(*it)) it = a_volatileProofs.erase(it);
			else ++it;
		}
	}

	[[nodiscard]] inline std::vector<std::string> EncodeChunks(const std::string& a_payload)
	{
		const auto digest = F4T::ContentManifest::Sha256Hex(a_payload);
		const auto count = std::max<std::size_t>(1, (a_payload.size() + MaximumChunkBytes - 1) / MaximumChunkBytes);
		std::vector<std::string> chunks;
		for (std::size_t index = 0; index < count; ++index) {
			const auto slice = a_payload.substr(std::min(a_payload.size(), index * MaximumChunkBytes), MaximumChunkBytes);
			chunks.push_back(nlohmann::json{ { "count", count }, { "index", index }, { "sha256", digest } }.dump() + "\n" + slice);
		}
		return chunks;
	}

	class ReceiptChunkReader
	{
	public:
		[[nodiscard]] bool Add(std::string_view a_record)
		{
			const auto newline = a_record.find('\n');
			if (!_valid || newline == std::string_view::npos) return Invalidate();
			const auto header = nlohmann::json::parse(a_record.substr(0, newline), nullptr, false);
			if (!header.is_object() || header.size() != 3 || !header.contains("count") || !header.at("count").is_number_unsigned() ||
				!header.contains("index") || !header.at("index").is_number_unsigned() || !header.contains("sha256") || !header.at("sha256").is_string()) return Invalidate();
			const auto count = header.at("count").get<std::size_t>();
			const auto index = header.at("index").get<std::size_t>();
			const auto digest = header.at("sha256").get<std::string>();
			const auto slice = std::string(a_record.substr(newline + 1));
			if (count == 0 || index >= count || !MatchesSave(digest, digest) || slice.size() > MaximumChunkBytes) return Invalidate();
			if (_count == 0) {
				_count = count;
				_digest = digest;
			} else if (count != _count || digest != _digest) {
				return Invalidate();
			}
			const auto [found, inserted] = _slices.emplace(index, slice);
			if (!inserted && found->second != slice) return Invalidate();
			return true;
		}

		[[nodiscard]] std::optional<ReceiptSet> Decode() const
		{
			if (!_valid || _count == 0 || _slices.size() != _count) return std::nullopt;
			std::string payload;
			for (const auto& [index, slice] : _slices) payload += slice;
			if (!MatchesSave(_digest, F4T::ContentManifest::Sha256Hex(payload))) return std::nullopt;
			return ReceiptSet::Decode(payload);
		}

		void Reset() { *this = ReceiptChunkReader{}; }

	private:
		[[nodiscard]] bool Invalidate()
		{
			_valid = false;
			return false;
		}

		bool _valid{ true };
		std::size_t _count{};
		std::string _digest;
		std::map<std::size_t, std::string> _slices;
	};
}

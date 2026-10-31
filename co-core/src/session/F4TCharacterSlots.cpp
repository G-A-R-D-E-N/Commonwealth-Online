#include "F4TCharacterSlots.h"

#include <charconv>
#include <cmath>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace F4T::CharacterSlots
{
	namespace
	{
		[[nodiscard]] bool HasCharacterData(const nlohmann::json& a_profile) noexcept
		{
			return a_profile.is_object() &&
				(a_profile.contains("playerState") || a_profile.contains("inventory") || a_profile.contains("inventoryOperations"));
		}

		void RemapDropOwners(nlohmann::json& a_root, std::string_view a_source, std::string_view a_target)
		{
			const auto drops = a_root.find("drops");
			if (drops == a_root.end() || !drops->is_object()) {
				return;
			}
			const auto live = drops->find("live");
			if (live == drops->end() || !live->is_object()) {
				return;
			}
			for (auto& entry : live->items()) {
				if (!entry.value().is_object()) {
					continue;
				}
				const auto owner = entry.value().find("ownerProfileId");
				if (owner != entry.value().end() && owner->is_string() && owner->get_ref<const std::string&>() == a_source) {
					*owner = std::string{ a_target };
				}
			}
		}

		[[nodiscard]] bool AllDigits(std::string_view a_value) noexcept
		{
			if (a_value.empty() || a_value.size() > 10) {
				return false;
			}
			if (a_value.size() > 1 && a_value.front() == '0') {
				return false;
			}
			for (const char ch : a_value) {
				if (ch < '0' || ch > '9') {
					return false;
				}
			}
			return true;
		}
	}

	std::optional<std::string> AccountKey(std::string_view a_profileId)
	{
		if (!Identity::IsValidProfileId(a_profileId)) {
			return std::nullopt;
		}
		return std::string{ a_profileId };
	}

	bool IsValidSlotCount(std::size_t a_count) noexcept
	{
		return a_count >= MinimumCharacterSlots && a_count <= MaximumCharacterSlots;
	}

	bool IsValidSlot(std::uint32_t a_slot, std::size_t a_slotCount) noexcept
	{
		return IsValidSlotCount(a_slotCount) && static_cast<std::size_t>(a_slot) < a_slotCount;
	}

	std::string SlotProfileKey(std::string_view a_account, std::uint32_t a_slot)
	{
		if (!Identity::IsValidProfileId(a_account)) {
			return {};
		}
		std::string key{ a_account };
		key.push_back(SlotKeySeparator);
		char digits[11]{};
		const auto result = std::to_chars(digits, digits + sizeof(digits) - 1, a_slot);
		if (result.ec != std::errc{}) {
			return {};
		}
		key.append(digits, result.ptr);
		return key;
	}

	std::optional<std::pair<std::string, std::uint32_t>> ParseSlotKey(std::string_view a_key)
	{
		const auto split = a_key.find(SlotKeySeparator);
		if (split == std::string_view::npos) {
			return std::nullopt;
		}
		const auto account = a_key.substr(0, split);
		const auto slotText = a_key.substr(split + 1);
		if (!Identity::IsValidProfileId(account) || !AllDigits(slotText)) {
			return std::nullopt;
		}
		std::uint32_t slot{};
		const auto result = std::from_chars(slotText.data(), slotText.data() + slotText.size(), slot);
		if (result.ec != std::errc{} || result.ptr != slotText.data() + slotText.size()) {
			return std::nullopt;
		}
		return std::pair<std::string, std::uint32_t>{ std::string{ account }, slot };
	}

	MigrationResult MigrateLegacyProfiles(nlohmann::json& a_root, double a_updatedAt)
	{
		if (!a_root.is_object()) {
			throw std::invalid_argument("Character slot migration requires a JSON object root.");
		}
		const auto profilesIt = a_root.find("profiles");
		if (profilesIt == a_root.end() || !profilesIt->is_object()) {
			throw std::invalid_argument("Character slot migration requires a profiles object.");
		}
		if (!std::isfinite(a_updatedAt)) {
			throw std::invalid_argument("Character slot migration requires a finite timestamp.");
		}
		MigrationResult result;
		auto& profiles = *profilesIt;
		struct PendingMove
		{
			std::string source;
			std::string target;
		};
		std::vector<PendingMove> pending;
		for (auto it = profiles.begin(); it != profiles.end(); ++it) {
			if (ParseSlotKey(it.key()).has_value()) {
				++result.alreadySlotted;
			} else if (!Identity::IsValidProfileId(it.key())) {
				++result.skipped;
			} else if (!HasCharacterData(it.value())) {
				continue;
			} else {
				const std::string target = SlotProfileKey(it.key(), FirstSlot);
				const auto existing = profiles.find(target);
				if (existing != profiles.end() && HasCharacterData(*existing)) {
					result.conflict = true;
					result.conflictKey = it.key();
					return result;
				}
				pending.push_back({ it.key(), target });
			}
		}
		for (const auto& move : pending) {
			auto source = profiles.find(move.source);
			if (source == profiles.end()) {
				continue;
			}
			auto& slotNode = profiles[move.target];
			if (!slotNode.is_object()) {
				slotNode = nlohmann::json::object();
			}
			for (const char* key : { "playerState", "inventory", "inventoryOperations" }) {
				const auto value = source->find(key);
				if (value != source->end()) {
					slotNode[key] = std::move(*value);
				}
			}
			slotNode["updatedAt"] = a_updatedAt;
			for (const char* key : { "playerState", "inventory", "inventoryOperations" }) {
				source->erase(key);
			}
			RemapDropOwners(a_root, move.source, move.target);
			bool keep = false;
			for (auto field = source->begin(); field != source->end(); ++field) {
				if (field.key() != "updatedAt") {
					keep = true;
					break;
				}
			}
			if (!keep) {
				profiles.erase(source);
			}
			++result.moved;
		}
		return result;
	}

	std::map<std::uint32_t, nlohmann::json> GetSlotRecords(const nlohmann::json& a_root, std::string_view a_account)
	{
		std::map<std::uint32_t, nlohmann::json> records;
		const auto table = a_root.find("characterSlots");
		if (table == a_root.end() || !table->is_object()) {
			return records;
		}
		const auto account = table->find(std::string{ a_account });
		if (account == table->end() || !account->is_object()) {
			return records;
		}
		for (const auto& entry : account->items()) {
			if (!entry.value().is_object()) {
				continue;
			}
			const auto parsed = ParseSlotKey(std::string{ a_account } + SlotKeySeparator + entry.key());
			if (parsed && parsed->first == a_account) {
				records.emplace(parsed->second, entry.value());
			}
		}
		return records;
	}

	bool SetSlotRecord(nlohmann::json& a_root, std::string_view a_account, std::uint32_t a_slot, const nlohmann::json& a_record)
	{
		if (!a_record.is_object()) {
			return false;
		}
		a_root["characterSlots"][std::string{ a_account }][std::to_string(a_slot)] = a_record;
		return true;
	}

	bool EraseSlotRecord(nlohmann::json& a_root, std::string_view a_account, std::uint32_t a_slot)
	{
		const auto table = a_root.find("characterSlots");
		if (table == a_root.end() || !table->is_object()) {
			return false;
		}
		const auto account = table->find(std::string{ a_account });
		return account != table->end() && account->is_object() && account->erase(std::to_string(a_slot)) > 0;
	}

	bool EraseProfileNode(nlohmann::json& a_root, std::string_view a_profileKey)
	{
		const auto profiles = a_root.find("profiles");
		if (profiles == a_root.end() || !profiles->is_object()) {
			return false;
		}
		return profiles->erase(std::string{ a_profileKey }) > 0;
	}

}

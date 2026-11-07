#pragma once

#include "F4TInventoryContract.h"
#include "F4TContentManifest.h"
#include "F4TCharacterSlots.h"

#include <set>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace F4T::DeathIntent
{
	inline constexpr std::size_t MaximumRecords = 4096;
	enum class Outcome { Created, Replayed, Conflict, Rejected };
	struct Allocation { Outcome outcome{ Outcome::Rejected }; std::string deathId; std::uint64_t generation{}; };
	[[nodiscard]] inline bool ValidCharacter(const std::string& a_character)
	{
		return a_character.size() == 38 && a_character.starts_with("quest-") &&
			std::all_of(a_character.begin() + 6, a_character.end(), [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); });
	}

	[[nodiscard]] inline bool ValidRecord(const nlohmann::json& a_record)
	{
		try {
			if (!a_record.is_object() || a_record.size() != 10 || !a_record.contains("characterId") || !a_record.at("characterId").is_string() || !a_record.contains("profileId") ||
				!a_record.at("profileId").is_string() || !a_record.contains("inventoryRevision") ||
				!a_record.at("inventoryRevision").is_number_unsigned() || !a_record.contains("cellKey") ||
				!a_record.at("cellKey").is_string() || !a_record.contains("worldspaceKey") ||
				(!a_record.at("worldspaceKey").is_null() && !a_record.at("worldspaceKey").is_string()) ||
				!a_record.contains("position") || !a_record.at("position").is_array() || a_record.at("position").size() != 3 ||
				!a_record.contains("version") || !a_record.at("version").is_number_unsigned() || a_record.at("version") != 1 ||
				!a_record.contains("state") || (a_record.at("state") != "dead" && a_record.at("state") != "downed") ||
				!a_record.contains("deadlineUnix") ||
				!a_record.contains("locationSource") || a_record.at("locationSource") != "client-reported") return false;
			const auto& profile = a_record.at("profileId").get_ref<const std::string&>();
			const auto& character = a_record.at("characterId").get_ref<const std::string&>();
			if (!ValidCharacter(character) ||
				profile.empty() || profile.size() > 128 ||
				a_record.at("inventoryRevision").get<std::uint64_t>() > (std::numeric_limits<std::uint32_t>::max)() ||
				!F4T::InventoryContract::ParseItemKey(a_record.at("cellKey").get_ref<const std::string&>())) return false;
			const auto& world = a_record.at("worldspaceKey");
			if (!world.is_null() && !F4T::InventoryContract::ParseItemKey(world.get_ref<const std::string&>())) return false;
			const auto& deadline = a_record.at("deadlineUnix");
			if (a_record.at("state") == "dead") {
				if (!deadline.is_null()) return false;
			} else if (!deadline.is_number() || !std::isfinite(deadline.get<double>()) || deadline.get<double>() < 0.0) return false;
			for (const auto& number : a_record.at("position")) {
				if (!number.is_number() || !std::isfinite(number.get<double>())) return false;
			}
			return true;
		} catch (const nlohmann::json::exception&) { return false; }
	}

	[[nodiscard]] inline bool ValidId(const std::string& a_id)
	{
		if (a_id.size() != 32) return false;
		for (const auto ch : a_id) if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
		return true;
	}

	[[nodiscard]] inline std::string DerivedId(const std::string& a_serverId, const std::string& a_profile, std::uint64_t a_generation)
	{
		const auto material = a_serverId + ":" + std::to_string(a_profile.size()) + ":" + a_profile + ":" + std::to_string(a_generation);
		return F4T::ContentManifest::Sha256Hex(material).substr(0, 32);
	}

	inline void Validate(const nlohmann::json& a_root)
	{
		const auto records = a_root.find("deathIntents");
		if (records != a_root.end()) {
			if (!records->is_object() || records->size() > MaximumRecords) throw std::invalid_argument("Invalid death intent section.");
			for (const auto& [id, record] : records->items()) {
				if (!ValidId(id) || !ValidRecord(record)) throw std::invalid_argument("Invalid death intent record.");
			}
		}
		const auto generations = a_root.find("deathGenerations");
		if (generations == a_root.end()) return;
		if (!generations->is_object() || generations->size() > MaximumRecords || !a_root.contains("serverInstanceId") ||
			!a_root.at("serverInstanceId").is_string()) throw std::invalid_argument("Invalid death generation section.");
		for (const auto& [character, generation] : generations->items()) {
			const bool spent = generation.is_object() && generation.contains("bagSpent");
			if (!ValidCharacter(character) || !generation.is_object() || generation.size() != (spent ? 3u : 2u) ||
				!generation.contains("number") || !generation.at("number").is_number_unsigned() || generation.at("number") == 0 ||
				!generation.contains("activeId") || (!generation.at("activeId").is_null() && !generation.at("activeId").is_string()) ||
				(spent && (generation.at("bagSpent") != true || generation.at("activeId").is_null()))) throw std::invalid_argument("Invalid death generation.");
			if (generation.at("activeId").is_null()) continue;
			const auto& id = generation.at("activeId").get_ref<const std::string&>();
			if (!ValidId(id) || id != DerivedId(a_root.at("serverInstanceId").get_ref<const std::string&>(), character, generation.at("number").get<std::uint64_t>()) ||
				records == a_root.end() || !records->contains(id) || records->at(id).at("characterId") != character)
				throw std::invalid_argument("Death generation does not match its intent.");
		}
	}

	[[nodiscard]] inline bool MatchesCurrentCharacterInventory(const nlohmann::json& a_root, const nlohmann::json& a_record)
	{
		if (!a_root.contains("profiles") || !a_root.at("profiles").is_object()) return false;
		const auto profile = a_root.at("profiles").find(a_record.at("profileId").get_ref<const std::string&>());
		if (profile == a_root.at("profiles").end() || !profile->is_object() || !profile->contains("inventory")) return false;
		const auto slot = F4T::CharacterSlots::ParseSlotKey(a_record.at("profileId").get_ref<const std::string&>());
		if (!slot) return false;
		const auto slots = F4T::CharacterSlots::GetSlotRecords(a_root, slot->first);
		const auto selected = slots.find(slot->second);
		if (selected == slots.end() || selected->second.value("questProfile", nlohmann::json{}) != a_record.at("characterId")) return false;
		const auto& inventory = profile->at("inventory");
		return inventory.is_object() && inventory.contains("revision") && inventory.at("revision") == a_record.at("inventoryRevision");
	}

	[[nodiscard]] inline Outcome Record(nlohmann::json& a_root, const std::string& a_id, const nlohmann::json& a_record)
	{
		if (!ValidId(a_id) || !ValidRecord(a_record) || !a_root.is_object() ||
			!a_root.contains("inventoryAuthorityBlocked") || a_root.at("inventoryAuthorityBlocked") != false ||
			!a_root.contains("profiles") || !a_root.at("profiles").is_object()) return Outcome::Rejected;
		try { Validate(a_root); } catch (const std::invalid_argument&) { return Outcome::Rejected; }
		if (!MatchesCurrentCharacterInventory(a_root, a_record)) return Outcome::Rejected;
		const auto records = a_root.find("deathIntents");
		if (records != a_root.end()) {
			if (const auto existing = records->find(a_id); existing != records->end()) {
				return *existing == a_record ? Outcome::Replayed : Outcome::Conflict;
			}
			if (records->size() >= MaximumRecords) return Outcome::Rejected;
		}
		a_root["deathIntents"][a_id] = a_record;
		return Outcome::Created;
	}

	[[nodiscard]] inline Allocation Allocate(nlohmann::json& a_root, const nlohmann::json& a_record)
	{
		if (!ValidRecord(a_record) || !a_root.is_object() || !a_root.contains("serverInstanceId") ||
			!a_root.at("serverInstanceId").is_string() || !a_root.contains("inventoryAuthorityBlocked") ||
			a_root.at("inventoryAuthorityBlocked") != false) return {};
		try { Validate(a_root); } catch (const std::invalid_argument&) { return {}; }
		if (!MatchesCurrentCharacterInventory(a_root, a_record)) return {};
		const auto& profile = a_record.at("characterId").get_ref<const std::string&>();
		const auto generations = a_root.find("deathGenerations");
		std::uint64_t generation = 1;
		if (generations != a_root.end()) {
			const auto existing = generations->find(profile);
			if (existing != generations->end()) {
				const auto previous = existing->at("number").get<std::uint64_t>();
				if (!existing->at("activeId").is_null()) {
					const auto id = existing->at("activeId").get<std::string>();
					const auto outcome = a_root.at("deathIntents").at(id) == a_record ? Outcome::Replayed : Outcome::Conflict;
					return { outcome, id, previous };
				}
				if (previous == (std::numeric_limits<std::uint64_t>::max)()) return {};
				generation = previous + 1;
			} else if (generations->size() >= MaximumRecords) {
				return {};
			}
		}
		const auto id = DerivedId(a_root.at("serverInstanceId").get_ref<const std::string&>(), profile, generation);
		const auto outcome = Record(a_root, id, a_record);
		if (outcome != Outcome::Created) return {};
		a_root["deathGenerations"][profile] = nlohmann::json{ { "number", generation }, { "activeId", id } };
		return { Outcome::Created, id, generation };
	}

	[[nodiscard]] inline bool RetireUntransferred(nlohmann::json& a_root, const std::string& a_profile, const std::string& a_id)
	{
		if (!a_root.is_object() || !a_root.contains("inventoryAuthorityBlocked") || a_root.at("inventoryAuthorityBlocked") != false) return false;
		try { Validate(a_root); } catch (const std::invalid_argument&) { return false; }
		auto generations = a_root.find("deathGenerations");
		if (generations == a_root.end()) return false;
		auto generation = generations->find(a_profile);
		if (generation == generations->end() || generation->at("activeId") != a_id) return false;
		auto records = a_root.find("deathIntents");
		if (records == a_root.end() || !records->contains(a_id)) return false;
		records->erase(a_id);
		(*generation)["activeId"] = nullptr;
		generation->erase("bagSpent");
		return true;
	}

	[[nodiscard]] inline bool MarkDead(nlohmann::json& a_root, const std::string& a_character, const std::string& a_id)
	{
		if (!a_root.is_object() || !a_root.contains("inventoryAuthorityBlocked") || a_root.at("inventoryAuthorityBlocked") != false) return false;
		try { Validate(a_root); } catch (const std::invalid_argument&) { return false; }
		auto generations = a_root.find("deathGenerations");
		if (generations == a_root.end()) return false;
		auto generation = generations->find(a_character);
		if (generation == generations->end() || generation->at("activeId") != a_id) return false;
		auto& record = a_root.at("deathIntents").at(a_id);
		if (record.at("state") != "downed") return false;
		record["state"] = "dead";
		record["deadlineUnix"] = nullptr;
		const auto profiles = a_root.find("profiles");
		if (profiles == a_root.end() || !profiles->is_object()) return true;
		const auto profile = profiles->find(record.at("profileId").get_ref<const std::string&>());
		if (profile == profiles->end() || !profile->is_object() || !profile->contains("inventory") || !profile->at("inventory").is_object()) return true;
		const auto inventory = profile->at("inventory").find("revision");
		if (inventory != profile->at("inventory").end() && inventory->is_number_unsigned()) record["inventoryRevision"] = *inventory;
		return true;
	}

	[[nodiscard]] inline bool HasDueExpiry(const nlohmann::json& a_root, double a_nowUnix, const std::set<std::string>& a_onlineCharacters)
	{
		if (!std::isfinite(a_nowUnix) || a_nowUnix < 0.0 || !a_root.is_object()) return true;
		try {
			const auto generations = a_root.find("deathGenerations");
			if (generations == a_root.end()) return false;
			if (!generations->is_object()) return true;
			const auto records = a_root.find("deathIntents");
			if (records == a_root.end() || !records->is_object()) return true;
			for (const auto& [character, generation] : generations->items()) {
				if (a_onlineCharacters.contains(character)) continue;
				if (!generation.is_object() || !generation.contains("activeId")) return true;
				const auto& active = generation.at("activeId");
				if (!active.is_string()) continue;
				const auto record = records->find(active.get_ref<const std::string&>());
				if (record == records->end() || !record->is_object()) return true;
				if (record->value("state", std::string{}) != "downed") continue;
				const auto deadline = record->find("deadlineUnix");
				if (deadline == record->end() || !deadline->is_number()) continue;
				if (deadline->get<double>() <= a_nowUnix) return true;
			}
			return false;
		} catch (const nlohmann::json::exception&) { return true; }
	}

	[[nodiscard]] inline std::size_t ExpireDue(nlohmann::json& a_root, double a_nowUnix, const std::set<std::string>& a_onlineCharacters)
	{
		if (!std::isfinite(a_nowUnix) || a_nowUnix < 0.0 || !a_root.is_object() ||
			!a_root.contains("inventoryAuthorityBlocked") || a_root.at("inventoryAuthorityBlocked") != false) return 0;
		try { Validate(a_root); } catch (const std::invalid_argument&) { return 0; }
		auto generations = a_root.find("deathGenerations");
		if (generations == a_root.end()) return 0;
		std::size_t changed = 0;
		for (auto& [character, generation] : generations->items()) {
			if (a_onlineCharacters.contains(character)) continue;
			const auto& active = generation.at("activeId");
			if (!active.is_string()) continue;
			auto& record = a_root.at("deathIntents").at(active.get_ref<const std::string&>());
			if (record.at("state") != "downed" || record.at("deadlineUnix").get<double>() > a_nowUnix) continue;
			record["state"] = "dead";
			record["deadlineUnix"] = nullptr;
			++changed;
		}
		return changed;
	}
}

#pragma once
#include "F4TDeathIntent.h"
#include "F4TDurableState.h"
#include "F4TInventorySnapshot.h"
#include "F4TItemHealthSnapshot.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
namespace F4T::DeathBag
{
	[[nodiscard]] inline bool ValidGrantRecord(const std::string& a_key, const nlohmann::json& a_instance)
	{
		const auto& instance = a_instance;
		if (!instance.is_object() || instance.size() != 7 + instance.count("attemptedAt") + instance.count("freshProof") + instance.count("claimRequired") + instance.count("claimManaged") ||
			(instance.contains("freshProof") && (!instance.at("freshProof").is_boolean() || instance.at("freshProof") != true)) ||
			(instance.contains("claimRequired") && (!instance.at("claimRequired").is_boolean() || instance.at("claimRequired") != true)) ||
			(instance.contains("claimManaged") && (!instance.at("claimManaged").is_boolean() || instance.at("claimManaged") != true ||
				(instance.contains("claimRequired") == instance.contains("freshProof")))) ||
			!instance.contains("deathId") || !instance.at("deathId").is_string() ||
			!F4T::DeathIntent::ValidId(instance.at("deathId").get_ref<const std::string&>()) ||
			!instance.contains("operationId") || !instance.at("operationId").is_string() || instance.at("operationId").get_ref<const std::string&>().empty() ||
			a_key != "bag:" + instance.at("deathId").get<std::string>() + ":" + instance.at("operationId").get<std::string>() ||
			a_key.size() > F4T::Drops::kMaximumOperationIdChars || !instance.contains("characterId") || !instance.at("characterId").is_string() ||
			!F4T::DeathIntent::ValidCharacter(instance.at("characterId").get_ref<const std::string&>()) ||
			!instance.contains("itemKey") || !instance.at("itemKey").is_string() ||
			!F4T::InventoryContract::ParseItemKey(instance.at("itemKey").get_ref<const std::string&>()) ||
			!instance.contains("count") || !instance.at("count").is_number_unsigned() || instance.at("count") == 0 ||
			instance.at("count").get<std::uint64_t>() > (std::numeric_limits<std::uint32_t>::max)() ||
			!instance.contains("metadata") || (!instance.at("metadata").is_null() && !F4T::ItemHealthSnapshot::Decode(instance.at("metadata"))) ||
			!instance.contains("state") || (instance.at("state") != "pending" && instance.at("state") != "attempted") ||
			(instance.contains("claimRequired") && (instance.contains("freshProof") || !instance.contains("claimManaged") || instance.at("state") != "pending")) ||
			(instance.contains("attemptedAt") && (instance.at("state") != "attempted" || !instance.at("attemptedAt").is_number() ||
				!std::isfinite(instance.at("attemptedAt").get<double>()))))
			return false;
		return true;
	}
	inline void ValidateGrantHistory(const nlohmann::json& a_root)
	{
		const auto history = a_root.find("deathBagGrantHistory");
		if (history == a_root.end()) return;
		if (!history->is_object()) throw std::invalid_argument("Invalid grant history.");
		const auto profiles = a_root.find("profiles");
		for (const auto& [profileId, records] : history->items()) {
			if (!records.is_object()) throw std::invalid_argument("Invalid grant history.");
			const nlohmann::json* live = nullptr;
			if (profiles != a_root.end() && profiles->is_object() && profiles->contains(profileId) && profiles->at(profileId).is_object() &&
				profiles->at(profileId).contains("deathBagInstances"))
				live = &profiles->at(profileId).at("deathBagInstances");
			for (const auto& [key, record] : records.items()) {
				if (!ValidGrantRecord(key, record) || record.at("state") != "attempted" || !record.contains("attemptedAt") || (live && live->contains(key)))
					throw std::invalid_argument("Invalid grant history record.");
			}
		}
	}
	[[nodiscard]] inline const nlohmann::json* GrantHistory(const nlohmann::json& a_root, const std::string& a_profile)
	{
		const auto history = a_root.find("deathBagGrantHistory");
		if (history == a_root.end() || !history->is_object()) return nullptr;
		const auto records = history->find(a_profile);
		return records == history->end() || !records->is_object() ? nullptr : &*records;
	}
	[[nodiscard]] inline const nlohmann::json* FindGrant(const nlohmann::json& a_root, const std::string& a_profile, const std::string& a_operation)
	{
		if (const auto profiles = a_root.find("profiles"); profiles != a_root.end() && profiles->is_object()) {
			if (const auto profile = profiles->find(a_profile); profile != profiles->end() && profile->is_object()) {
				if (const auto live = profile->find("deathBagInstances"); live != profile->end() && live->is_object()) {
					if (const auto found = live->find(a_operation); found != live->end()) return &*found;
				}
			}
		}
		if (const auto* history = GrantHistory(a_root, a_profile)) {
			if (const auto found = history->find(a_operation); found != history->end()) return &*found;
		}
		return nullptr;
	}
	inline bool MigrateGrantHistory(nlohmann::json& a_root, double a_now)
	{
		const auto profiles = a_root.find("profiles");
		if (profiles == a_root.end() || !profiles->is_object()) return false;
		bool changed = false;
		for (auto& [profileId, profile] : profiles->items()) {
			if (!profile.is_object() || !profile.contains("deathBagInstances") || !profile.at("deathBagInstances").is_object()) continue;
			auto& records = profile.at("deathBagInstances");
			for (auto it = records.begin(); it != records.end();) {
				if (!it->is_object() || it->value("state", std::string{}) != "attempted") { ++it; continue; }
				auto record = *it;
				if (!record.contains("attemptedAt")) record["attemptedAt"] = a_now;
				a_root["deathBagGrantHistory"][profileId][it.key()] = std::move(record);
				it = records.erase(it);
				changed = true;
			}
		}
		return changed;
	}
}

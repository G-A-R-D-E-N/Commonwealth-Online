#pragma once
#include "F4TDeathIntent.h"
#include "F4TDurableState.h"
#include "F4TInventorySnapshot.h"
#include "F4TItemHealthSnapshot.h"
#include "F4TDeathBagLocalPlan.h"
#include "F4TDeathBagWire.h"
#include "F4TDeathBagGrantRecords.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
namespace F4T::DeathBag
{
	inline constexpr std::size_t MaximumBags = 4096;
	inline constexpr double CrashRefundSeconds = 120.0;
	inline constexpr double PlaceResultBoundSeconds = 86400.0;
	inline constexpr std::size_t MaximumPendingGrants = 64;
	inline constexpr std::size_t MaximumArchivedBags = 4096;
	inline constexpr std::size_t MaximumArchiveListing = 50;
	[[nodiscard]] inline bool MatchesStacks(const nlohmann::json& a_stacks, const nlohmann::json& a_items)
	{
		if (!a_stacks.is_array() || a_stacks.size() > 4096 || !a_items.is_object()) return false;
		std::map<std::string, std::uint64_t> counts;
		for (const auto& stack : a_stacks) {
			if (!stack.is_object() || stack.size() != 3 || !stack.contains("itemKey") || !stack.at("itemKey").is_string() ||
				!stack.contains("count") || !stack.at("count").is_number_unsigned() || !stack.contains("metadata")) return false;
			const auto& key = stack.at("itemKey").get_ref<const std::string&>();
			if (!F4T::InventoryContract::ParseItemKey(key)) return false;
			const auto count = stack.at("count").get<std::uint64_t>();
			if (!count || count > (std::numeric_limits<std::uint32_t>::max)()) return false;
			if (!stack.at("metadata").is_null() && !F4T::ItemHealthSnapshot::Decode(stack.at("metadata"))) return false;
			auto& total = counts[key];
			if (total > (std::numeric_limits<std::uint32_t>::max)() - count) return false;
			total += count;
		}
		if (counts.size() != a_items.size()) return false;
		for (const auto& [key, count] : counts) {
			if (!a_items.contains(key) || !a_items.at(key).contains("count") || a_items.at(key).at("count") != count) return false;
		}
		return true;
	}
	[[nodiscard]] inline std::size_t CoreKeyCount(const nlohmann::json& a_bag)
	{
		return a_bag.size() - a_bag.count("placeRequestedAt") - a_bag.count("unverified");
	}
	[[nodiscard]] inline bool ValidDeparture(const nlohmann::json& a_bag)
	{
		if (CoreKeyCount(a_bag) == 8) return true;
		return a_bag.contains("departure") && a_bag.at("departure").is_string() &&
			(a_bag.at("departure") == "none" || a_bag.at("departure") == "crash" || a_bag.at("departure") == "quit") &&
			a_bag.contains("departedAt") && a_bag.at("departedAt").is_number() && std::isfinite(a_bag.at("departedAt").get<double>());
	}
	inline void Validate(const nlohmann::json& a_root)
	{
		const auto archive = a_root.find("deathBagArchive");
		if (archive != a_root.end()) {
			if (!archive->is_object() || archive->size() > MaximumArchivedBags) throw std::invalid_argument("Invalid archived death bags.");
			for (const auto& [id, entry] : archive->items()) {
				if (!F4T::DeathIntent::ValidId(id) || !entry.is_object() || entry.size() != 2 || !entry.contains("reason") || !entry.contains("bag") ||
					!entry.at("reason").is_string() || (entry.at("reason") != "placeFailed" && entry.at("reason") != "placeFailedUnverified" && entry.at("reason") != "evicted")) throw std::invalid_argument("Invalid archived death bag.");
				Validate(nlohmann::json{ { "deathBags", nlohmann::json{ { id, entry.at("bag") } } } });
			}
		}
		const auto bags = a_root.find("deathBags");
		if (bags == a_root.end()) return;
		if (!bags->is_object() || bags->size() > MaximumBags) throw std::invalid_argument("Invalid death bags.");
		for (const auto& [id, bag] : bags->items()) {
			if (!F4T::DeathIntent::ValidId(id) || !bag.is_object() || (CoreKeyCount(bag) != 8 && CoreKeyCount(bag) != 10) ||
				!bag.contains("ownerProfileId") || !bag.at("ownerProfileId").is_string() ||
				bag.at("ownerProfileId").get_ref<const std::string&>().empty() ||
				!bag.contains("characterId") || !bag.at("characterId").is_string() ||
				!F4T::DeathIntent::ValidCharacter(bag.at("characterId").get_ref<const std::string&>()) ||
				!bag.contains("location") || !bag.at("location").is_object() ||
				!bag.at("location").contains("cellKey") || !bag.at("location").at("cellKey").is_string() ||
				!F4T::InventoryContract::ParseItemKey(bag.at("location").at("cellKey").get_ref<const std::string&>()) ||
				!bag.at("location").contains("worldspaceKey") ||
				(!bag.at("location").at("worldspaceKey").is_null() &&
				 (!bag.at("location").at("worldspaceKey").is_string() || !F4T::InventoryContract::ParseItemKey(bag.at("location").at("worldspaceKey").get_ref<const std::string&>()))) ||
				!bag.at("location").contains("position") || !bag.at("location").at("position").is_array() || bag.at("location").at("position").size() != 3 ||
				!bag.contains("items") || !bag.at("items").is_object() ||
				!bag.contains("deposited") || !bag.at("deposited").is_boolean() ||
				!bag.contains("placed") || !bag.at("placed").is_boolean() || (bag.at("placed") == true && bag.at("deposited") != true) ||
				!bag.contains("createdAt") || !bag.at("createdAt").is_number() || !std::isfinite(bag.at("createdAt").get<double>()) ||
				!ValidDeparture(bag) ||
				(bag.contains("placeRequestedAt") && (!bag.at("placeRequestedAt").is_number() || !std::isfinite(bag.at("placeRequestedAt").get<double>()))) ||
				(bag.contains("unverified") && (bag.at("unverified") != true || bag.at("deposited") != false)) ||
				!bag.contains("stacks") || !MatchesStacks(bag.at("stacks"), bag.at("items")))
				throw std::invalid_argument("Invalid death bag record.");
			F4T::DurableState::ValidateInventory(nlohmann::json{ { "initialized", true }, { "revision", 0u }, { "items", bag.at("items") } });
			for (const auto& value : bag.at("location").at("position")) {
				if (!value.is_number() || !std::isfinite(value.get<double>())) throw std::invalid_argument("Invalid death bag location.");
			}
		}
	}
	inline void ValidateRecipientInstances(const nlohmann::json& a_root)
	{
		if (const auto history = a_root.find("deathBagGrantHistory"); history != a_root.end()) {
			if (!history->is_object()) throw std::invalid_argument("Invalid grant history.");
			for (const auto& records : *history) {
				if (!records.is_object()) throw std::invalid_argument("Invalid grant history.");
			}
		}
		const auto profiles = a_root.find("profiles");
		if (profiles == a_root.end()) return;
		if (!profiles->is_object()) throw std::invalid_argument("Invalid recipient instances.");
		for (const auto& [profileId, profile] : profiles->items()) {
			if (!profile.is_object()) throw std::invalid_argument("Invalid recipient instances.");
			const auto instances = profile.find("deathBagInstances");
			if (instances == profile.end()) continue;
			if (!instances->is_object() || instances->size() > 4096 || !profile.contains("inventory")) throw std::invalid_argument("Invalid recipient instances.");
			const auto* past = GrantHistory(a_root, profileId);
			for (const auto& [key, instance] : instances->items()) {
				if (!ValidGrantRecord(key, instance) || (past && past->contains(key))) throw std::invalid_argument("Invalid recipient instance.");
			}
		}
	}

	[[nodiscard]] inline bool OwnedBy(const nlohmann::json& a_root, const std::string& a_id, const std::string& a_profile, const std::string& a_character)
	{
		if (!a_root.contains("deathBags") || !a_root.at("deathBags").contains(a_id)) return false;
		const auto& bag = a_root.at("deathBags").at(a_id);
		return bag.is_object() && bag.value("deposited", false) && bag.value("ownerProfileId", std::string{}) == a_profile &&
			bag.value("characterId", std::string{}) == a_character;
	}
	[[nodiscard]] inline bool MoveToArchive(nlohmann::json& a_root, const std::string& a_id, const std::string& a_reason)
	{
		try {
			Validate(a_root);
			if (!a_root.at("deathBags").contains(a_id)) return false;
			if (a_root.contains("deathBagArchive") && (a_root.at("deathBagArchive").contains(a_id) || a_root.at("deathBagArchive").size() >= MaximumArchivedBags)) return false;
			a_root["deathBagArchive"][a_id] = nlohmann::json{ { "reason", a_reason }, { "bag", a_root.at("deathBags").at(a_id) } };
			a_root.at("deathBags").erase(a_id);
			return true;
		} catch (const std::exception&) { return false; }
	}
	enum class HoldOutcome { Rejected, Archived, Parked };
	[[nodiscard]] inline HoldOutcome Hold(nlohmann::json& a_root, const std::string& a_id, const std::string& a_reason = "placeFailed")
	{
		try {
			Validate(a_root);
			auto& bag = a_root.at("deathBags").at(a_id);
			if (bag.at("deposited") != true || bag.at("placed") != false) return HoldOutcome::Rejected;
			bag.erase("placeRequestedAt");
		} catch (const std::exception&) { return HoldOutcome::Rejected; }
		if (MoveToArchive(a_root, a_id, a_reason)) return HoldOutcome::Archived;
		try {
			auto& parked = a_root.at("deathBags").at(a_id);
			parked["deposited"] = false;
			if (a_reason == "placeFailedUnverified") parked["unverified"] = true;
			return HoldOutcome::Parked;
		} catch (const std::exception&) { return HoldOutcome::Rejected; }
	}
	[[nodiscard]] inline bool EvictForCapacity(nlohmann::json& a_root)
	{
		try {
			std::optional<std::string> oldest;
			double oldestCreated{};
			const auto generations = a_root.find("deathGenerations");
			for (const auto& [id, bag] : a_root.at("deathBags").items()) {
				if (bag.at("placed") != true || bag.at("items").empty()) continue;
				const auto character = bag.at("characterId").get<std::string>();
				if (generations != a_root.end() && generations->contains(character) && generations->at(character).at("activeId") == id) continue;
				const auto created = bag.at("createdAt").get<double>();
				if (!oldest || created < oldestCreated) {
					oldest = id;
					oldestCreated = created;
				}
			}
			return oldest && MoveToArchive(a_root, *oldest, "evicted");
		} catch (const std::exception&) { return false; }
	}
	[[nodiscard]] inline bool CreateEmpty(nlohmann::json& a_root, const std::string& a_deathId, double a_now)
	{
		if (!a_root.is_object() || a_root.value("inventoryAuthorityBlocked", true) || !std::isfinite(a_now)) return false;
		try { F4T::DeathIntent::Validate(a_root); Validate(a_root); } catch (const std::exception&) { return false; }
		const auto intents = a_root.find("deathIntents");
		if (intents == a_root.end() || !intents->contains(a_deathId)) return false;
		const auto& intent = intents->at(a_deathId);
		if (intent.at("state") != "dead") return false;
		const auto gens = a_root.find("deathGenerations");
		const auto character = intent.at("characterId").get<std::string>();
		if (gens == a_root.end() || !gens->contains(character) || gens->at(character).at("activeId") != a_deathId || gens->at(character).contains("bagSpent")) return false;
		if (a_root.contains("deathBags") && a_root.at("deathBags").contains(a_deathId)) return false;
		if (a_root.contains("deathBagArchive") && a_root.at("deathBagArchive").contains(a_deathId)) return false;
		if (a_root.contains("deathBags") && a_root.at("deathBags").size() >= MaximumBags && !EvictForCapacity(a_root)) return false;
		a_root["deathBags"][a_deathId] = nlohmann::json{
			{ "ownerProfileId", intent.at("profileId") }, { "characterId", character },
			{ "location", nlohmann::json{ { "cellKey", intent.at("cellKey") }, { "worldspaceKey", intent.at("worldspaceKey") }, { "position", intent.at("position") } } },
			{ "items", nlohmann::json::object() }, { "stacks", nlohmann::json::array() }, { "deposited", false }, { "placed", false }, { "createdAt", a_now },
			{ "departure", "none" }, { "departedAt", 0.0 } };
		return true;
	}
	[[nodiscard]] inline bool Spent(const nlohmann::json& a_root, const std::string& a_id, const std::string& a_profile, const std::string& a_character)
	{
		const auto intents = a_root.find("deathIntents");
		const auto generations = a_root.find("deathGenerations");
		if (intents == a_root.end() || generations == a_root.end() || !intents->is_object() || !generations->is_object() || !intents->contains(a_id)) return false;
		const auto& intent = intents->at(a_id);
		if (intent.value("profileId", std::string{}) != a_profile || intent.value("characterId", std::string{}) != a_character) return false;
		const auto generation = generations->find(a_character);
		return generation != generations->end() && generation->contains("bagSpent") && generation->at("activeId") == a_id;
	}
	inline void RetireBag(nlohmann::json& a_root, const std::string& a_id)
	{
		const auto owner = a_root.at("deathBags").at(a_id).at("characterId").get<std::string>();
		a_root.at("deathBags").erase(a_id);
		auto generations = a_root.find("deathGenerations");
		if (generations == a_root.end()) return;
		auto generation = generations->find(owner);
		if (generation != generations->end() && generation->at("activeId") == a_id) (*generation)["bagSpent"] = true;
	}
	[[nodiscard]] inline bool MoveRetained(nlohmann::json& a_moved, nlohmann::json& a_retained, const nlohmann::json& a_list)
	{
		if (!a_list.is_array() || a_list.size() > 4096) return false;
		std::map<std::string, std::uint64_t> counts;
		for (const auto& entry : a_list) {
			if (!entry.is_object() || entry.size() != 2 || !entry.contains("itemKey") || !entry.at("itemKey").is_string() ||
				!entry.contains("count") || !entry.at("count").is_number_unsigned()) return false;
			const auto& key = entry.at("itemKey").get_ref<const std::string&>();
			const auto count = entry.at("count").get<std::uint64_t>();
			if (!F4T::InventoryContract::ParseItemKey(key) || !count || count > (std::numeric_limits<std::uint32_t>::max)()) return false;
			auto& total = counts[key];
			if (total > (std::numeric_limits<std::uint32_t>::max)() - count) return false;
			total += count;
		}
		for (const auto& [key, count] : counts) {
			if (!a_moved.contains(key) || a_retained.contains(key)) return false;
			const auto have = a_moved.at(key).at("count").get<std::uint64_t>();
			if (have < count) return false;
			auto kept = a_moved.at(key);
			kept["count"] = count;
			a_retained[key] = kept;
			if (have == count) a_moved.erase(key); else a_moved.at(key)["count"] = have - count;
		}
		return true;
	}
	[[nodiscard]] inline bool Deposit(nlohmann::json& a_root, const std::string& a_id, const nlohmann::json& a_stacks, const nlohmann::json& a_retained = nlohmann::json::array())
	{
		try {
			Validate(a_root);
			auto& bag = a_root.at("deathBags").at(a_id);
			if (!a_stacks.is_array() || a_stacks.size() > F4T::DeathBagWire::MaximumOwnerDiscoveryStacks) return false;
			if (F4T::DeathBagWire::DiscoveryFrameBytes() + F4T::DeathBagWire::DiscoveryEntryBytes(nlohmann::json{ { "deathId", a_id }, { "location", bag.at("location") },
				{ "canTake", true }, { "stacks", a_stacks } }) > F4T::DeathBagWire::MaximumDiscoveryBytes) return false;
			if (bag.at("deposited") != false || !bag.at("items").empty()) return false;
			const auto profile = bag.at("ownerProfileId").get<std::string>();
			const auto& intent = a_root.at("deathIntents").at(a_id);
			if (!F4T::DeathIntent::MatchesCurrentCharacterInventory(a_root, intent)) return false;
			const auto& inventory = a_root.at("profiles").at(profile).at("inventory");
			auto moved = inventory.at("items");
			auto retained = nlohmann::json::object();
			for (const auto base : F4T::DeathBagLocal::PipboyArmorBases) {
				const auto key = F4T::DeathBagLocal::PipboyArmorKey(base);
				if (moved.contains(key)) { retained[key] = moved.at(key); moved.erase(key); }
			}
			if (!MoveRetained(moved, retained, a_retained)) return false;
			if (!MatchesStacks(a_stacks, moved)) return false;
			if (!moved.empty()) {
				const auto request = nlohmann::json{ { "type", "inventorySnapshotRequest" }, { "operationId", "death:" + a_id },
					{ "baseRevision", inventory.at("revision") }, { "items", retained } };
				const auto outcome = F4T::InventorySnapshot::Reconcile(a_root, profile, request);
				if (!outcome.changed || outcome.packet.value("status", std::string{}) != "committed") return false;
			}
			bag["items"] = std::move(moved);
			bag["stacks"] = a_stacks;
			bag["deposited"] = true;
			return true;
		} catch (const std::exception&) { return false; }
	}
	[[nodiscard]] inline bool Place(nlohmann::json& a_root, const std::string& a_id, const std::string& a_profile, const std::string& a_character)
	{
		try {
			Validate(a_root);
			if (Spent(a_root, a_id, a_profile, a_character)) return true;
			auto& bag = a_root.at("deathBags").at(a_id);
			if (bag.at("deposited") != true || bag.at("placed") != false || bag.at("items").empty() ||
				bag.at("ownerProfileId") != a_profile || bag.at("characterId") != a_character) return false;
			const auto slot = F4T::CharacterSlots::ParseSlotKey(a_profile);
			if (!slot) return false;
			const auto slots = F4T::CharacterSlots::GetSlotRecords(a_root, slot->first);
			const auto selected = slots.find(slot->second);
			if (selected == slots.end() || selected->second.value("questProfile", nlohmann::json{}) != a_character) return false;
			bag["placed"] = true;
			bag.erase("placeRequestedAt");
			return true;
		} catch (const std::exception&) { return false; }
	}
	[[nodiscard]] inline bool RequestPlacement(nlohmann::json& a_root, const std::string& a_id, double a_now)
	{
		if (!std::isfinite(a_now)) return false;
		try {
			Validate(a_root);
			auto& bag = a_root.at("deathBags").at(a_id);
			if (bag.at("deposited") != true || bag.at("placed") != false) return false;
			if (!bag.at("items").empty()) bag["placeRequestedAt"] = a_now;
			return true;
		} catch (const std::exception&) { return false; }
	}
	[[nodiscard]] inline bool Refund(nlohmann::json& a_root, const std::string& a_id, const std::string& a_operationPrefix, nlohmann::json* a_stacksOut)
	{
		try {
			Validate(a_root);
			const auto& bag = a_root.at("deathBags").at(a_id);
			const auto profile = bag.at("ownerProfileId").get<std::string>();
			const auto slot = F4T::CharacterSlots::ParseSlotKey(profile);
			if (!slot) return false;
			const auto slots = F4T::CharacterSlots::GetSlotRecords(a_root, slot->first);
			const auto selected = slots.find(slot->second);
			if (selected == slots.end() || selected->second.value("questProfile", nlohmann::json{}) != bag.at("characterId")) return false;
			const auto items = bag.at("items");
			const auto stacks = bag.at("stacks");
			if (!items.empty()) {
				const auto& inventory = a_root.at("profiles").at(profile).at("inventory");
				auto merged = inventory.at("items");
				for (const auto& [key, descriptor] : items.items()) {
					const auto current = merged.contains(key) ? merged.at(key).at("count").get<std::uint64_t>() : std::uint64_t{ 0 };
					const auto count = descriptor.at("count").get<std::uint64_t>();
					if (current > (std::numeric_limits<std::uint32_t>::max)() - count) return false;
					auto restored = descriptor;
					restored["count"] = current + count;
					merged[key] = restored;
				}
				const auto request = nlohmann::json{ { "type", "inventorySnapshotRequest" }, { "operationId", a_operationPrefix + a_id },
					{ "baseRevision", inventory.at("revision") }, { "items", merged } };
				const auto outcome = F4T::InventorySnapshot::Reconcile(a_root, profile, request);
				if (!outcome.changed || outcome.packet.value("status", std::string{}) != "committed") return false;
			}
			if (a_stacksOut) *a_stacksOut = stacks;
			a_root.at("deathBags").erase(a_id);
			return true;
		} catch (const std::exception&) { return false; }
	}
	[[nodiscard]] inline bool Void(nlohmann::json& a_root, const std::string& a_id, nlohmann::json* a_stacksOut = nullptr)
	{
		try {
			const auto& bag = a_root.at("deathBags").at(a_id);
			if (bag.at("deposited") != true || bag.at("placed") != false || bag.contains("placeRequestedAt")) return false;
		} catch (const std::exception&) { return false; }
		return Refund(a_root, a_id, "void:", a_stacksOut);
	}
	[[nodiscard]] inline bool Discard(nlohmann::json& a_root, const std::string& a_id, nlohmann::json* a_stacksOut = nullptr)
	{
		try {
			const auto& bag = a_root.at("deathBags").at(a_id);
			if (bag.at("deposited") != true || bag.at("placed") != false) return false;
			if (a_stacksOut) *a_stacksOut = bag.at("stacks");
			a_root.at("deathBags").erase(a_id);
			return true;
		} catch (const std::exception&) { return false; }
	}

	[[nodiscard]] inline std::size_t PendingGrantCount(const nlohmann::json& a_records)
	{
		std::size_t pending{};
		for (const auto& [key, record] : a_records.items()) {
			(void)key;
			if (record.at("state") == "pending") ++pending;
		}
		return pending;
	}
	[[nodiscard]] inline bool QueueRefundGrant(nlohmann::json& a_root, const std::string& a_profile, const std::string& a_character,
		const std::string& a_deathId, const std::string& a_operationId, const std::string& a_itemKey, std::uint32_t a_count,
		const nlohmann::json& a_metadata)
	{
		if (a_count == 0 || a_operationId.empty() || a_operationId.size() > 32 || !F4T::DeathIntent::ValidId(a_deathId) ||
			!F4T::InventoryContract::ParseItemKey(a_itemKey)) return false;
		if (!a_metadata.is_null() && !F4T::ItemHealthSnapshot::Decode(a_metadata)) return false;
		try {
			if (a_root.value("inventoryAuthorityBlocked", true)) return false;
			Validate(a_root);
			ValidateRecipientInstances(a_root);
			const auto slot = F4T::CharacterSlots::ParseSlotKey(a_profile);
			if (!slot) return false;
			const auto slots = F4T::CharacterSlots::GetSlotRecords(a_root, slot->first);
			const auto selected = slots.find(slot->second);
			if (selected == slots.end() || selected->second.value("questProfile", nlohmann::json{}) != a_character) return false;
			const auto operation = "bag:" + a_deathId + ":" + a_operationId;
			if (operation.size() > F4T::Drops::kMaximumOperationIdChars) return false;
			const auto& profile = a_root.at("profiles").at(a_profile);
			const auto& inventory = profile.at("inventory");
			if (!inventory.is_object() || !inventory.contains("revision") || !inventory.contains("items") ||
				!inventory.at("items").is_object()) return false;
			if (const auto* existing = FindGrant(a_root, a_profile, operation)) {
				return existing->at("deathId") == a_deathId && existing->at("operationId") == a_operationId &&
					existing->at("characterId") == a_character && existing->at("itemKey") == a_itemKey &&
					existing->at("count") == a_count && existing->at("metadata") == a_metadata;
			}
			if (profile.contains("deathBagInstances")) {
				const auto& records = profile.at("deathBagInstances");
				if (!records.is_object() || records.size() >= 4096) return false;
				if (PendingGrantCount(records) >= MaximumPendingGrants) return false;
			}
			const auto colon = a_itemKey.rfind(':');
			if (colon == std::string::npos || colon == 0 || a_itemKey.size() - colon - 1 != 8) return false;
			auto plugin = a_itemKey.substr(0, colon);
			auto form = a_itemKey.substr(colon + 1);
			for (char& ch : plugin) {
				if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
			}
			for (char& ch : form) {
				if (ch >= 'a' && ch <= 'f') ch = static_cast<char>(ch - ('a' - 'A'));
			}
			if (plugin + ":" + form != a_itemKey || form == "00000000") return false;
			auto merged = inventory.at("items");
			const auto current = merged.contains(a_itemKey) ? merged.at(a_itemKey).at("count").get<std::uint64_t>() : std::uint64_t{ 0 };
			if (current > (std::numeric_limits<std::uint32_t>::max)() - a_count) return false;
			merged[a_itemKey] = nlohmann::json{ { "plugin", plugin }, { "localFormId", form }, { "count", current + a_count } };
			const auto request = nlohmann::json{ { "type", "inventorySnapshotRequest" }, { "operationId", "refund:" + a_operationId },
				{ "baseRevision", inventory.at("revision") }, { "items", merged } };
			const auto outcome = F4T::InventorySnapshot::Reconcile(a_root, a_profile, request);
			if (!outcome.changed || outcome.packet.value("status", std::string{}) != "committed") return false;
			a_root["profiles"][a_profile]["deathBagInstances"][operation] = nlohmann::json{
				{ "deathId", a_deathId }, { "operationId", a_operationId }, { "characterId", a_character },
				{ "itemKey", a_itemKey }, { "count", a_count }, { "metadata", a_metadata }, { "state", "pending" },
				{ "claimRequired", true }, { "claimManaged", true } };
			return true;
		} catch (const std::exception&) { return false; }
	}
	[[nodiscard]] inline bool IsParked(const nlohmann::json& a_bag)
	{
		return a_bag.is_object() && a_bag.value("deposited", true) == false && a_bag.contains("items") && !a_bag.at("items").empty();
	}
	[[nodiscard]] inline bool WithinStacks(const nlohmann::json& a_remaining, const nlohmann::json& a_stacks)
	{
		std::map<std::pair<std::string, std::string>, std::uint64_t> available;
		for (const auto& stack : a_stacks) available[{ stack.at("itemKey").get<std::string>(), stack.at("metadata").dump() }] += stack.at("count").get<std::uint64_t>();
		for (const auto& stack : a_remaining) {
			auto& left = available[{ stack.at("itemKey").get<std::string>(), stack.at("metadata").dump() }];
			const auto count = stack.at("count").get<std::uint64_t>();
			if (left < count) return false;
			left -= count;
		}
		return true;
	}
	[[nodiscard]] inline std::map<std::string, std::uint64_t> StackTotals(const nlohmann::json& a_stacks)
	{
		std::map<std::string, std::uint64_t> totals;
		for (const auto& stack : a_stacks) totals[stack.at("itemKey").get<std::string>()] += stack.at("count").get<std::uint64_t>();
		return totals;
	}
	inline void TrimToRemaining(nlohmann::json& a_bag, const nlohmann::json& a_remaining)
	{
		auto items = nlohmann::json::object();
		for (const auto& [key, count] : StackTotals(a_remaining)) {
			auto descriptor = a_bag.at("items").at(key);
			descriptor["count"] = count;
			items[key] = std::move(descriptor);
		}
		a_bag["stacks"] = a_remaining;
		a_bag["items"] = std::move(items);
	}
	enum class PlaceFailedOutcome { Rejected, Unchanged, Discarded, Archived, Parked };
	[[nodiscard]] inline PlaceFailedOutcome ApplyPlaceFailed(nlohmann::json& a_root, const std::string& a_id, const std::string& a_profile,
		const std::string& a_character, bool a_restored, const nlohmann::json& a_remaining, nlohmann::json* a_stacksOut = nullptr)
	{
		try {
			Validate(a_root);
			if (!F4T::DeathBagWire::ValidRemaining(a_restored, a_remaining)) return PlaceFailedOutcome::Rejected;
			const bool archived = a_root.contains("deathBagArchive") && a_root.at("deathBagArchive").contains(a_id);
			const bool active = a_root.contains("deathBags") && a_root.at("deathBags").contains(a_id);
			if (!archived && !active) return PlaceFailedOutcome::Unchanged;
			const auto& bag = archived ? a_root.at("deathBagArchive").at(a_id).at("bag") : a_root.at("deathBags").at(a_id);
			if (bag.at("ownerProfileId") != a_profile || bag.at("characterId") != a_character) return PlaceFailedOutcome::Unchanged;
			const bool open = !archived && bag.at("deposited") == true;
			if (!open && !archived && !IsParked(bag)) return PlaceFailedOutcome::Rejected;
			if (!WithinStacks(a_remaining, bag.at("stacks"))) return PlaceFailedOutcome::Rejected;
			if (archived) {
				if (bag.at("placed") == true || (!a_restored && (a_remaining.empty() || a_remaining == bag.at("stacks")))) return PlaceFailedOutcome::Unchanged;
				auto working = a_root;
				if (a_restored) {
					working.at("deathBagArchive").erase(a_id);
				} else {
					TrimToRemaining(working.at("deathBagArchive").at(a_id).at("bag"), a_remaining);
					if (working.at("deathBagArchive").at(a_id).at("reason") == "placeFailedUnverified") working.at("deathBagArchive").at(a_id)["reason"] = "placeFailed";
				}
				Validate(working);
				a_root = std::move(working);
				return PlaceFailedOutcome::Archived;
			}
			if (!open || bag.at("placed") != false) return PlaceFailedOutcome::Unchanged;
			if (a_restored) return Discard(a_root, a_id, a_stacksOut) ? PlaceFailedOutcome::Discarded : PlaceFailedOutcome::Rejected;
			auto working = a_root;
			if (!a_remaining.empty()) TrimToRemaining(working.at("deathBags").at(a_id), a_remaining);
			const auto held = Hold(working, a_id, a_remaining.empty() ? "placeFailedUnverified" : "placeFailed");
			if (held == HoldOutcome::Rejected) return PlaceFailedOutcome::Rejected;
			a_root = std::move(working);
			return held == HoldOutcome::Parked ? PlaceFailedOutcome::Parked : PlaceFailedOutcome::Archived;
		} catch (const std::exception&) { return PlaceFailedOutcome::Rejected; }
	}
	[[nodiscard]] inline std::string RecoveryOperation(const std::string& a_deathId, std::size_t a_remaining, std::size_t a_index)
	{
		return "r" + a_deathId.substr(0, 16) + "-" + std::to_string(a_remaining) + "-" + std::to_string(a_index);
	}
	[[nodiscard]] inline bool Unverified(const nlohmann::json& a_root, const std::string& a_id)
	{
		const auto archive = a_root.find("deathBagArchive");
		if (archive != a_root.end() && archive->contains(a_id)) return archive->at(a_id).at("reason") == "placeFailedUnverified";
		const auto bags = a_root.find("deathBags");
		return bags != a_root.end() && bags->contains(a_id) && bags->at(a_id).contains("unverified");
	}
	enum class Resolution { Full, None };
	[[nodiscard]] inline bool ResolveUnverified(nlohmann::json& a_root, const std::string& a_id, Resolution a_resolution)
	{
		try {
			Validate(a_root);
			if (!Unverified(a_root, a_id)) return false;
			auto working = a_root;
			const bool archived = working.contains("deathBagArchive") && working.at("deathBagArchive").contains(a_id);
			auto& owner = archived ? working.at("deathBagArchive") : working.at("deathBags");
			if (a_resolution == Resolution::None) owner.erase(a_id);
			else if (archived) owner.at(a_id)["reason"] = "placeFailed";
			else owner.at(a_id).erase("unverified");
			Validate(working);
			a_root = std::move(working);
			return true;
		} catch (const std::exception&) { return false; }
	}
	[[nodiscard]] inline bool RecoverArchived(nlohmann::json& a_root, const std::string& a_id)
	{
		try {
			Validate(a_root);
			const bool archived = a_root.contains("deathBagArchive") && a_root.at("deathBagArchive").contains(a_id);
			if (!archived && !(a_root.contains("deathBags") && a_root.at("deathBags").contains(a_id) && IsParked(a_root.at("deathBags").at(a_id)))) return false;
			if (Unverified(a_root, a_id)) return false;
			auto working = a_root;
			auto bag = archived ? working.at("deathBagArchive").at(a_id).at("bag") : working.at("deathBags").at(a_id);
			const auto profile = bag.at("ownerProfileId").get<std::string>();
			const auto character = bag.at("characterId").get<std::string>();
			const auto stacks = bag.at("stacks");
			const auto total = stacks.size();
			auto items = bag.at("items");
			const auto& profileRecord = working.at("profiles").at(profile);
			const auto instances = profileRecord.find("deathBagInstances");
			if (FindGrant(working, profile, "bag:" + a_id + ":" + RecoveryOperation(a_id, total, 0))) return false;
			const auto pending = instances != profileRecord.end() ? PendingGrantCount(*instances) : std::size_t{};
			const auto freeSlots = pending < MaximumPendingGrants ? MaximumPendingGrants - pending : std::size_t{};
			const auto chunk = (std::min)(total, freeSlots);
			if (total > 0 && chunk == 0) return false;
			auto remainder = nlohmann::json::array();
			for (std::size_t index = 0; index < total; ++index) {
				const auto& stack = stacks.at(index);
				if (index >= chunk) {
					remainder.push_back(stack);
					continue;
				}
				const auto itemKey = stack.at("itemKey").get<std::string>();
				const auto count = stack.at("count").get<std::uint32_t>();
				if (!QueueRefundGrant(working, profile, character, a_id, RecoveryOperation(a_id, total, index), itemKey, count, stack.at("metadata"))) return false;
				const auto left = items.at(itemKey).at("count").get<std::uint32_t>() - count;
				if (left == 0) items.erase(itemKey); else items.at(itemKey)["count"] = left;
			}
			if (remainder.empty()) {
				if (archived) working.at("deathBagArchive").erase(a_id); else working.at("deathBags").erase(a_id);
			} else {
				bag["stacks"] = std::move(remainder);
				bag["items"] = std::move(items);
				if (archived) working.at("deathBagArchive").at(a_id)["bag"] = std::move(bag); else working.at("deathBags").at(a_id) = std::move(bag);
			}
			a_root = std::move(working);
			return true;
		} catch (const std::exception&) { return false; }
	}
	[[nodiscard]] inline nlohmann::json ArchiveSummary(const nlohmann::json& a_root, std::size_t a_offset)
	{
		std::vector<std::pair<double, std::string>> order;
		const auto archive = a_root.find("deathBagArchive");
		if (archive != a_root.end()) {
			for (const auto& [id, entry] : archive->items()) order.emplace_back(entry.at("bag").at("createdAt").get<double>(), id);
		}
		const auto bags = a_root.find("deathBags");
		if (bags != a_root.end()) {
			for (const auto& [id, bag] : bags->items()) {
				if (IsParked(bag)) order.emplace_back(bag.at("createdAt").get<double>(), id);
			}
		}
		std::sort(order.begin(), order.end(), [](const auto& a_left, const auto& a_right) {
			return a_left.first != a_right.first ? a_left.first > a_right.first : a_left.second < a_right.second;
		});
		auto listing = nlohmann::json::array();
		for (std::size_t index = a_offset; index < order.size() && listing.size() < MaximumArchiveListing; ++index) {
			const auto& id = order[index].second;
			const bool archived = archive != a_root.end() && archive->contains(id);
			const auto& bag = archived ? archive->at(id).at("bag") : a_root.at("deathBags").at(id);
			listing.push_back(nlohmann::json{ { "deathId", id }, { "reason", archived ? archive->at(id).at("reason") : nlohmann::json(bag.contains("unverified") ? "parkedUnverified" : "parked") }, { "ownerProfileId", bag.at("ownerProfileId") },
				{ "characterId", bag.at("characterId") }, { "createdAt", bag.at("createdAt") }, { "stacks", bag.at("stacks") } });
		}
		return nlohmann::json{ { "total", order.size() }, { "bags", std::move(listing) } };
	}
	struct Expired
	{
		std::string deathId;
		std::string characterId;
		nlohmann::json stacks;
	};
	[[nodiscard]] inline std::vector<Expired> ExpirePending(nlohmann::json& a_root, double a_now)
	{
		std::vector<std::string> refund;
		try {
			Validate(a_root);
			for (const auto& [id, bag] : a_root.at("deathBags").items()) {
				if (bag.at("deposited") != true || bag.at("placed") == true || bag.contains("placeRequestedAt")) continue;
				const auto since = bag.at("departure") == "none" ? bag.at("createdAt").get<double>() : bag.at("departedAt").get<double>();
				if (since + CrashRefundSeconds <= a_now) refund.push_back(id);
			}
		} catch (const std::exception&) { return {}; }
		std::vector<Expired> expired;
		for (const auto& id : refund) {
			const auto character = a_root.at("deathBags").at(id).at("characterId").get<std::string>();
			nlohmann::json stacks;
			if (Void(a_root, id, &stacks)) expired.push_back(Expired{ id, character, std::move(stacks) });
		}
		return expired;
	}
	[[nodiscard]] inline std::size_t ArchiveOverdueClaims(nlohmann::json& a_root, double a_now)
	{
		std::vector<std::string> overdue;
		try {
			Validate(a_root);
			for (const auto& [id, bag] : a_root.at("deathBags").items()) {
				if (bag.at("deposited") != true || bag.at("placed") != false || !bag.contains("placeRequestedAt")) continue;
				if (bag.at("placeRequestedAt").get<double>() + PlaceResultBoundSeconds <= a_now) overdue.push_back(id);
			}
		} catch (const std::exception&) { return std::size_t{}; }
		std::size_t held{};
		for (const auto& id : overdue) {
			if (Hold(a_root, id, "placeFailedUnverified") != HoldOutcome::Rejected) ++held;
		}
		return held;
	}
	[[nodiscard]] inline bool MarkDeparted(nlohmann::json& a_root, const std::string& a_character, bool a_cleanGoodbye, double a_now)
	{
		if (!std::isfinite(a_now) || a_character.empty()) return false;
		try { Validate(a_root); } catch (const std::exception&) { return false; }
		if (!a_root.contains("deathBags")) return false;
		bool marked = false;
		for (auto& bag : a_root.at("deathBags")) {
			if (bag.at("deposited") != true || bag.at("placed") != false || bag.at("characterId") != a_character || bag.at("departure") != "none") continue;
			bag["departure"] = a_cleanGoodbye ? "quit" : "crash";
			bag["departedAt"] = a_now;
			marked = true;
		}
		return marked;
	}

	[[nodiscard]] inline bool OwnerSlotHolds(const nlohmann::json& a_root, const std::string& a_profile, const std::string& a_character)
	{
		const auto slot = F4T::CharacterSlots::ParseSlotKey(a_profile);
		if (!slot) return false;
		const auto slots = F4T::CharacterSlots::GetSlotRecords(a_root, slot->first);
		const auto selected = slots.find(slot->second);
		return selected != slots.end() && selected->second.value("questProfile", nlohmann::json{}) == a_character;
	}
	[[nodiscard]] inline bool HasExpiryWork(const nlohmann::json& a_root, double a_now)
	{
		if (!std::isfinite(a_now) || a_root.is_null()) return true;
		try {
			const auto bags = a_root.find("deathBags");
			if (bags == a_root.end()) return false;
			if (!bags->is_object()) return true;
			for (const auto& [id, bag] : bags->items()) {
				if (!bag.is_object() || bag.value("deposited", false) != true) continue;
				if (bag.value("placed", false) == false) {
					if (bag.contains("placeRequestedAt")) {
						if (bag.value("placeRequestedAt", 0.0) + PlaceResultBoundSeconds <= a_now) return true;
						continue;
					}
					const auto since = bag.value("departure", std::string{}) == "none" ? bag.value("createdAt", 0.0) : bag.value("departedAt", 0.0);
					if (since + CrashRefundSeconds <= a_now && OwnerSlotHolds(a_root, bag.value("ownerProfileId", std::string{}), bag.value("characterId", std::string{}))) return true;
					continue;
				}
				if (!bag.contains("items") || !bag.at("items").empty()) continue;
				return true;
			}
			return false;
		} catch (const std::exception&) { return true; }
	}

	[[nodiscard]] inline bool ClearDeparted(nlohmann::json& a_root, const std::string& a_character)
	{
		try { Validate(a_root); } catch (const std::exception&) { return false; }
		if (!a_root.contains("deathBags")) return false;
		bool cleared = false;
		for (auto& bag : a_root.at("deathBags")) {
			if (bag.at("deposited") != true || bag.at("placed") != false || bag.at("characterId") != a_character || bag.at("departure") == "none") continue;
			bag["departure"] = "none";
			bag["departedAt"] = 0.0;
			cleared = true;
		}
		return cleared;
	}
	inline bool MigrateLoaded(nlohmann::json& a_root, double a_now)
	{
		if (!a_root.is_object() || !std::isfinite(a_now)) return false;
		bool changed = MigrateGrantHistory(a_root, a_now);
		if (!a_root.contains("deathBags") || !a_root.at("deathBags").is_object()) return changed;
		for (auto& bag : a_root.at("deathBags")) {
			if (!bag.is_object()) continue;
			if (bag.size() == 8) {
				bag["departure"] = "none";
				bag["departedAt"] = 0.0;
				changed = true;
			}
			if (bag.value("deposited", false) == true && bag.value("placed", false) == false && bag.value("departure", std::string{}) == "none") {
				bag["departure"] = "crash";
				bag["departedAt"] = a_now;
				changed = true;
			}
		}
		return changed;
	}
	[[nodiscard]] inline std::size_t SweepEmptied(nlohmann::json& a_root)
	{
		try {
			Validate(a_root);
			const auto& bags = a_root.at("deathBags");
			std::vector<std::string> swept;
			for (const auto& [id, bag] : bags.items()) {
				if (bag.at("placed") == true && bag.at("items").empty()) swept.push_back(id);
			}
			for (const auto& id : swept) RetireBag(a_root, id);
			return swept.size();
		} catch (const std::exception&) { return std::size_t{}; }
	}

	[[nodiscard]] inline bool CanTake(const nlohmann::json& a_bag, const std::string& a_character, bool a_allowOthers)
	{
		return a_bag.is_object() && a_bag.contains("characterId") && a_bag.at("characterId").is_string() &&
			!a_character.empty() && (a_bag.at("characterId") == a_character || a_allowOthers);
	}
	[[nodiscard]] inline std::optional<nlohmann::json> Discovery(const nlohmann::json& a_root,
		const std::string& a_profile, const std::string& a_character, bool a_allowOthers)
	{
		try {
			Validate(a_root);
			const auto slot = F4T::CharacterSlots::ParseSlotKey(a_profile);
			if (!slot) return std::nullopt;
			const auto slots = F4T::CharacterSlots::GetSlotRecords(a_root, slot->first);
			const auto selected = slots.find(slot->second);
			if (selected == slots.end() || selected->second.value("questProfile", nlohmann::json{}) != a_character) return std::nullopt;
			nlohmann::json result = nlohmann::json::array();
			const auto bags = a_root.find("deathBags");
			if (bags == a_root.end()) return result;
			std::size_t bytes = F4T::DeathBagWire::DiscoveryFrameBytes();
			for (const bool ownerPass : { true, false }) {
				for (const auto& [id, bag] : bags->items()) {
					if (result.size() >= F4T::DeathBagWire::MaximumDiscoveryBags) break;
					if (bag.at("placed") != true || bag.at("items").empty() || (bag.at("characterId") == a_character) != ownerPass) continue;
					if (!CanTake(bag, a_character, a_allowOthers) || bag.at("stacks").size() > (ownerPass ? F4T::DeathBagWire::MaximumOwnerDiscoveryStacks : F4T::DeathBagWire::MaximumForeignDiscoveryStacks)) continue;
					auto entry = nlohmann::json{ { "deathId", id }, { "location", bag.at("location") }, { "canTake", true }, { "stacks", bag.at("stacks") } };
					const auto size = F4T::DeathBagWire::DiscoveryEntryBytes(entry);
					if (size > F4T::DeathBagWire::MaximumDiscoveryBytes - bytes) continue;
					bytes += size;
					result.push_back(std::move(entry));
				}
			}
			return result;
		} catch (const std::exception&) { return std::nullopt; }
	}

	[[nodiscard]] inline bool SelectedCharacter(const nlohmann::json& a_root, const std::string& a_profile, const std::string& a_character)
	{
		const auto slot = F4T::CharacterSlots::ParseSlotKey(a_profile);
		if (!slot) return false;
		const auto slots = F4T::CharacterSlots::GetSlotRecords(a_root, slot->first);
		const auto selected = slots.find(slot->second);
		return selected != slots.end() && selected->second.value("questProfile", nlohmann::json{}) == a_character;
	}
	[[nodiscard]] inline nlohmann::json GrantOffer(const nlohmann::json& a_record)
	{
		auto offer = a_record;
		offer.erase("claimRequired");
		offer.erase("claimManaged");
		offer.erase("attemptedAt");
		if (a_record.value("claimRequired", false)) offer["claimRequired"] = true;
		return offer;
	}
	[[nodiscard]] inline std::optional<nlohmann::json> ReconcileGrants(const nlohmann::json& a_root, const std::string& a_profile,
		const std::string& a_character, const std::optional<std::string>& a_cursor = std::nullopt,
		std::optional<std::string>* a_nextCursor = nullptr)
	{
		if (a_nextCursor) *a_nextCursor = std::nullopt;
		try {
			ValidateRecipientInstances(a_root);
			if (!SelectedCharacter(a_root, a_profile, a_character)) return std::nullopt;
			const auto empty = nlohmann::json::object();
			const auto& profile = a_root.at("profiles").at(a_profile);
			const auto live = profile.find("deathBagInstances");
			const auto& records = live == profile.end() ? empty : *live;
			const auto* past = GrantHistory(a_root, a_profile);
			const auto& history = past ? *past : empty;
			if (a_cursor) {
				if (a_cursor->size() > 128) return std::nullopt;
				const auto& liveMap = records.get_ref<const nlohmann::json::object_t&>();
				const auto& historyMap = history.get_ref<const nlohmann::json::object_t&>();
				auto left = liveMap.upper_bound(*a_cursor);
				auto right = historyMap.upper_bound(*a_cursor);
				nlohmann::json page = nlohmann::json::array();
				std::optional<std::string> last;
				while (left != liveMap.end() || right != historyMap.end()) {
					auto& next = right == historyMap.end() || (left != liveMap.end() && left->first < right->first) ? left : right;
					const auto& [key, record] = *next++;
					if (record.at("characterId") != a_character) continue;
					if (page.size() == MaximumPendingGrants) {
						if (a_nextCursor) *a_nextCursor = last;
						break;
					}
					page.push_back(GrantOffer(record));
					last = key;
				}
				return page;
			}
			nlohmann::json result = nlohmann::json::array();
			std::vector<std::tuple<double, std::string, const nlohmann::json*>> delivered;
			for (const auto* source : { &records, &history }) {
				for (const auto& [key, record] : source->items()) {
					if (record.at("characterId") != a_character || record.value("claimManaged", false)) continue;
					if (record.at("state") == "attempted") {
						delivered.emplace_back(record.value("attemptedAt", 0.0), key, &record);
					} else if (result.size() < MaximumPendingGrants) {
						result.push_back(GrantOffer(record));
					}
				}
			}
			const auto kept = (std::min)(MaximumPendingGrants - result.size(), delivered.size());
			std::partial_sort(delivered.begin(), delivered.begin() + kept, delivered.end(), [](const auto& a_left, const auto& a_right) { return std::get<0>(a_left) != std::get<0>(a_right) ? std::get<0>(a_left) > std::get<0>(a_right) : std::get<1>(a_left) < std::get<1>(a_right); });
			for (std::size_t index = 0; index < kept; ++index) result.push_back(GrantOffer(*std::get<2>(delivered[index])));
			return result;
		} catch (const std::exception&) { return std::nullopt; }
	}
	[[nodiscard]] inline bool ClaimQueuedGrant(nlohmann::json& a_root, const std::string& a_profile, const std::string& a_character,
		const std::string& a_deathId, const std::string& a_operationId)
	{
		try {
			ValidateRecipientInstances(a_root);
			if (!F4T::DeathIntent::ValidId(a_deathId) || !SelectedCharacter(a_root, a_profile, a_character)) return false;
			const auto operation = "bag:" + a_deathId + ":" + a_operationId;
			const auto* found = FindGrant(a_root, a_profile, operation);
			if (!found || found->at("characterId") != a_character) return false;
			if (found->at("state") == "attempted" || found->value("freshProof", false)) return found->value("claimManaged", false);
			if (!found->value("claimRequired", false)) return false;
			auto& record = a_root.at("profiles").at(a_profile).at("deathBagInstances").at(operation);
			record.erase("claimRequired");
			record["freshProof"] = true;
			return true;
		} catch (const std::exception&) { return false; }
	}

	[[nodiscard]] inline bool MarkGrantAttempted(nlohmann::json& a_root, const std::string& a_profile, const std::string& a_character,
		const std::string& a_deathId, const std::string& a_operationId, double a_now)
	{
		try {
			if (a_root.at("inventoryAuthorityBlocked") != false || !F4T::DeathIntent::ValidId(a_deathId) || !std::isfinite(a_now)) return false;
			ValidateRecipientInstances(a_root);
			if (!SelectedCharacter(a_root, a_profile, a_character)) return false;
			const auto operation = "bag:" + a_deathId + ":" + a_operationId;
			const auto* found = FindGrant(a_root, a_profile, operation);
			if (!found || found->at("characterId") != a_character || found->value("claimRequired", false)) return false;
			if (const auto* past = GrantHistory(a_root, a_profile); past && past->contains(operation)) return true;
			auto& records = a_root.at("profiles").at(a_profile).at("deathBagInstances");
			auto record = records.at(operation);
			record["state"] = "attempted";
			if (!record.contains("attemptedAt")) record["attemptedAt"] = a_now;
			if (!ValidGrantRecord(operation, record)) return false;
			a_root["deathBagGrantHistory"][a_profile][operation] = std::move(record);
			records.erase(operation);
			return true;
		} catch (const std::exception&) { return false; }
	}

	[[nodiscard]] inline std::uint32_t ConsumeStacks(nlohmann::json& a_stacks, const std::string& a_itemKey, std::uint32_t a_count)
	{
		std::uint32_t consumed{};
		for (auto it = a_stacks.begin(); it != a_stacks.end() && consumed < a_count;) {
			if (it->at("itemKey") != a_itemKey) { ++it; continue; }
			const auto count = it->at("count").get<std::uint32_t>();
			const auto used = (std::min)(a_count - consumed, count);
			consumed += used;
			if (used == count) it = a_stacks.erase(it); else { (*it)["count"] = count - used; ++it; }
		}
		return consumed;
	}
	[[nodiscard]] inline bool ReplayMatches(const nlohmann::json& a_record, const std::string& a_id, const std::string& a_operationId,
		const std::string& a_character, const std::string& a_itemKey, std::uint32_t a_count)
	{
		return a_record.at("deathId") == a_id && a_record.at("operationId") == a_operationId && a_record.at("characterId") == a_character &&
			a_record.at("itemKey") == a_itemKey && a_record.at("count") == a_count;
	}
	[[nodiscard]] inline bool Withdraw(nlohmann::json& a_root, const std::string& a_id, const std::string& a_profile,
		const std::string& a_character, bool a_allowOthers, const std::string& a_itemKey, std::uint32_t a_count,
		std::uint32_t a_expectedRevision, const std::string& a_operationId, double a_now, nlohmann::json* a_metadataOut = nullptr,
		bool a_receiptProof = false)
	{
		if (a_count == 0 || a_operationId.empty() || !F4T::InventoryContract::ParseItemKey(a_itemKey) || !std::isfinite(a_now)) return false;
		try {
			Validate(a_root);
			const auto slot = F4T::CharacterSlots::ParseSlotKey(a_profile);
			if (!slot) return false;
			const auto slots = F4T::CharacterSlots::GetSlotRecords(a_root, slot->first);
			const auto selected = slots.find(slot->second);
			if (selected == slots.end() || selected->second.value("questProfile", nlohmann::json{}) != a_character) return false;
			const auto operation = "bag:" + a_id + ":" + a_operationId;
			if (operation.size() > F4T::Drops::kMaximumOperationIdChars) return false;
			const auto& profile = a_root.at("profiles").at(a_profile);
			if (const auto* existing = FindGrant(a_root, a_profile, operation)) {
				if (!ReplayMatches(*existing, a_id, a_operationId, a_character, a_itemKey, a_count)) return false;
				if (a_metadataOut) *a_metadataOut = existing->at("metadata");
				return true;
			}
			auto& bag = a_root.at("deathBags").at(a_id);
			if (bag.at("placed") != true || !CanTake(bag, a_character, a_allowOthers)) return false;
			auto stacks = bag.at("stacks");
			nlohmann::json selectedMetadata = nullptr;
			std::size_t matching = 0;
			for (const auto& stack : stacks) {
				if (stack.at("itemKey") != a_itemKey) continue;
				++matching;
				if (!stack.at("metadata").is_null()) selectedMetadata = stack.at("metadata");
			}
			if (!selectedMetadata.is_null()) {
				if (!a_metadataOut || matching != 1) return false;
				for (const auto& stack : stacks) {
					if (stack.at("itemKey") == a_itemKey && stack.at("count") != a_count) return false;
				}
			}
			if (ConsumeStacks(stacks, a_itemKey, a_count) != a_count) return false;
			auto remaining = bag.at("items");
			const auto found = remaining.find(a_itemKey);
			if (found == remaining.end() || found->at("count").get<std::uint32_t>() < a_count) return false;
			const auto& inventory = a_root.at("profiles").at(a_profile).at("inventory");
			if (inventory.at("revision") != a_expectedRevision) return false;
			auto items = inventory.at("items");
			const auto current = items.contains(a_itemKey) ? items.at(a_itemKey).at("count").get<std::uint32_t>() : 0u;
			if (current > (std::numeric_limits<std::uint32_t>::max)() - a_count) return false;
			auto descriptor = *found;
			descriptor["count"] = current + a_count;
			items[a_itemKey] = descriptor;
			const auto left = found->at("count").get<std::uint32_t>() - a_count;
			if (left == 0) remaining.erase(found); else (*found)["count"] = left;
			if (profile.contains("deathBagInstances") && (!profile.at("deathBagInstances").is_object() ||
				profile.at("deathBagInstances").size() >= 4096 || PendingGrantCount(profile.at("deathBagInstances")) >= MaximumPendingGrants)) return false;
			const auto request = nlohmann::json{ { "type", "inventorySnapshotRequest" }, { "operationId", operation },
				{ "baseRevision", a_expectedRevision }, { "items", items } };
			const auto outcome = F4T::InventorySnapshot::Reconcile(a_root, a_profile, request);
			if (!outcome.changed || outcome.packet.value("status", std::string{}) != "committed") return false;
			auto& records = a_root["profiles"][a_profile]["deathBagInstances"];
			auto record = nlohmann::json{
				{ "deathId", a_id }, { "operationId", a_operationId }, { "characterId", a_character }, { "itemKey", a_itemKey },
				{ "count", a_count }, { "metadata", selectedMetadata }, { "state", "pending" } };
			if (a_receiptProof) record["freshProof"] = true;
			records[operation] = std::move(record);
			bag["items"] = std::move(remaining);
			bag["stacks"] = std::move(stacks);
			if (a_metadataOut) *a_metadataOut = std::move(selectedMetadata);
			if (bag.at("items").empty()) RetireBag(a_root, a_id);
			return true;
		} catch (const std::exception&) { return false; }
	}

}

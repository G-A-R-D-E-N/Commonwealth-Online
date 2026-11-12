#pragma once

#include "F4TDropLedger.h"
#include "F4TInventoryWire.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server::ContainerClaims
{
	inline constexpr std::string_view kSection = "containerClaims";
	inline constexpr double kWindowSeconds = 60.0;
	inline constexpr std::size_t kMaximumPerProfile = 16;
	inline constexpr std::size_t kMaximumOps = 16;
	inline constexpr std::size_t kMaximumTotal = 512;

	using Counts = std::map<std::string, std::uint32_t>;

	[[nodiscard]] inline bool Retriable(std::string_view a_reason)
	{
		return a_reason == "rateLimited" || a_reason == "inventoryPersistenceUnavailable" || a_reason == "startingInventoryPending";
	}

	[[nodiscard]] inline std::uint64_t Total(const nlohmann::json& a_claim)
	{
		std::uint64_t total = 0;
		for (const auto& op : a_claim.at("ops")) total += op.get<std::uint64_t>();
		return total;
	}

	[[nodiscard]] inline bool HasClaims(const nlohmann::json& a_root, const std::string& a_profileId)
	{
		const auto claims = a_root.find(std::string{ kSection });
		if (claims == a_root.end() || !claims->is_object()) return false;
		const auto profile = claims->find(a_profileId);
		return profile != claims->end() && profile->is_array() && !profile->empty();
	}

	inline bool SameMove(const nlohmann::json& a_claim, const std::string& a_ledgerKey, const std::string& a_itemKey)
	{
		return a_claim.value("itemKey", std::string{}) == a_itemKey &&
			F4T::Drops::DropLedger::ContainerLedgerKey(a_claim.at("container")) == a_ledgerKey;
	}

	inline bool DropEmpty(nlohmann::json& a_all, const std::string& a_profileId)
	{
		const auto found = a_all.find(a_profileId);
		if (found == a_all.end()) return false;
		const auto before = found->size();
		found->erase(std::remove_if(found->begin(), found->end(), [](const nlohmann::json& a_claim) { return a_claim.at("ops").empty(); }), found->end());
		const bool changed = found->size() != before;
		if (found->empty()) {
			a_all.erase(found);
			return true;
		}
		return changed;
	}

	inline bool ExpireAll(nlohmann::json& a_all, double a_now)
	{
		std::vector<std::string> profiles;
		for (const auto& entry : a_all.items()) profiles.push_back(entry.key());
		bool changed = false;
		for (const auto& profile : profiles) {
			for (auto& claim : a_all.at(profile)) {
				if (a_now - claim.value("at", 0.0) > kWindowSeconds) claim["ops"] = nlohmann::json::object();
			}
			changed = DropEmpty(a_all, profile) || changed;
		}
		return changed;
	}

	inline bool Record(nlohmann::json& a_root, const std::string& a_profileId, const nlohmann::json& a_packet, const F4T::Drops::InventoryPosition& a_position, double a_now)
	{
		const auto request = F4T::InventoryWire::NormalizeContainerRequest(a_packet);
		if (!request || request->at("direction") != "take" || !F4T::Drops::DropLedger::ContainerInScope(request->at("container"), a_position)) return false;
		const auto profile = a_root.at("profiles").find(a_profileId);
		if (profile == a_root.at("profiles").end() || !profile->contains("inventoryOperations")) return false;
		const auto operationId = request->at("operationId").get<std::string>();
		for (const auto& operation : profile->at("inventoryOperations")) {
			if (operation.at("request").value("operationId", std::string{}) == operationId) return false;
		}
		const auto itemKey = request->at("itemKey").get<std::string>();
		const auto count = request->at("count").get<std::uint32_t>();
		const auto& container = request->at("container");
		const auto ledgerKey = F4T::Drops::DropLedger::ContainerLedgerKey(container);
		const auto held = a_root.at("containers").find(ledgerKey);
		if (held == a_root.at("containers").end() || !held->at("items").contains(itemKey)) return false;
		auto& all = a_root[std::string{ kSection }];
		if (!all.is_object()) all = nlohmann::json::object();
		const bool changed = ExpireAll(all, a_now);
		std::size_t total = 0;
		for (const auto& entry : all.items()) total += entry.value().size();
		const auto mine = all.find(a_profileId);
		if (mine != all.end()) {
			for (auto& claim : *mine) {
				if (!SameMove(claim, ledgerKey, itemKey)) continue;
				auto& ops = claim.at("ops");
				if (ops.contains(operationId) || ops.size() >= kMaximumOps || Total(claim) + count > F4T::Drops::kMaximumDropCount) return changed;
				ops[operationId] = count;
				claim["at"] = a_now;
				return true;
			}
		}
		if ((mine != all.end() && mine->size() >= kMaximumPerProfile) || total >= kMaximumTotal) return changed;
		auto& claims = all[a_profileId];
		if (!claims.is_array()) claims = nlohmann::json::array();
		claims.push_back(nlohmann::json{ { "container", container }, { "itemKey", itemKey }, { "ops", nlohmann::json{ { operationId, count } } }, { "at", a_now } });
		return true;
	}

	inline bool Release(nlohmann::json& a_root, const std::string& a_profileId, const nlohmann::json& a_packet)
	{
		const auto request = F4T::InventoryWire::NormalizeContainerRequest(a_packet);
		const auto all = a_root.find(std::string{ kSection });
		if (!request || request->at("direction") != "take" || all == a_root.end() || !all->is_object() || !all->contains(a_profileId)) return false;
		const auto operationId = request->at("operationId").get<std::string>();
		const auto itemKey = request->at("itemKey").get<std::string>();
		const auto ledgerKey = F4T::Drops::DropLedger::ContainerLedgerKey(request->at("container"));
		bool changed = false;
		for (auto& claim : all->at(a_profileId)) {
			if (SameMove(claim, ledgerKey, itemKey)) changed = claim.at("ops").erase(operationId) != 0 || changed;
		}
		return DropEmpty(*all, a_profileId) || changed;
	}

	inline bool Settle(nlohmann::json& a_root, const std::string& a_profileId, const Counts& a_before, const Counts& a_after, double a_now)
	{
		const auto all = a_root.find(std::string{ kSection });
		if (all == a_root.end() || !all->is_object()) return false;
		bool changed = ExpireAll(*all, a_now);
		const auto mine = all->find(a_profileId);
		if (mine == all->end()) return changed;
		Counts gained;
		for (const auto& [itemKey, count] : a_after) {
			const auto before = a_before.find(itemKey);
			const std::uint32_t was = before == a_before.end() ? 0u : before->second;
			if (count > was) gained[itemKey] = count - was;
		}
		for (auto& claim : *mine) {
			const auto available = gained.find(claim.value("itemKey", std::string{}));
			if (available == gained.end() || available->second == 0) continue;
			const auto settled = static_cast<std::uint32_t>((std::min)(Total(claim), static_cast<std::uint64_t>(available->second)));
			(void)F4T::Drops::DropLedger::ApplyContainerClaim(a_root, claim.at("container"), claim.at("itemKey").get<std::string>(), settled);
			available->second -= settled;
			auto remaining = settled;
			std::vector<std::string> spent;
			auto& ops = claim.at("ops");
			for (auto op = ops.begin(); op != ops.end() && remaining != 0; ++op) {
				const auto value = op->get<std::uint32_t>();
				const auto used = (std::min)(value, remaining);
				remaining -= used;
				if (used == value) spent.push_back(op.key()); else *op = value - used;
			}
			for (const auto& id : spent) claim.at("ops").erase(id);
			changed = true;
		}
		return DropEmpty(*all, a_profileId) || changed;
	}
}

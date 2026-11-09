#pragma once
#include "F4TDeathBag.h"
namespace F4T::DeathBag
{
	enum class UnplacedOutcome { Refunded, Absent, Refused };
	[[nodiscard]] inline UnplacedOutcome ReportUnplaced(nlohmann::json& a_root, const std::string& a_id, const std::string& a_profile,
		const std::string& a_character, const std::string& a_operationId)
	{
		if (!F4T::DeathBagWire::ValidDeathId(a_id) || a_operationId.empty() || a_operationId.size() > 16) return UnplacedOutcome::Refused;
		try {
			Validate(a_root);
			if (a_root.value("inventoryAuthorityBlocked", true)) return UnplacedOutcome::Refused;
			if (a_root.contains("deathBagArchive") && a_root.at("deathBagArchive").contains(a_id)) return UnplacedOutcome::Refused;
			if (!a_root.contains("deathBags") || !a_root.at("deathBags").contains(a_id)) return UnplacedOutcome::Absent;
			const auto& bag = a_root.at("deathBags").at(a_id);
			if (bag.at("ownerProfileId") != a_profile || bag.at("characterId") != a_character || bag.at("deposited") != true || bag.at("placed") != false ||
				!bag.contains("placeRequestedAt") || bag.contains("unverified") || bag.at("items").empty() || !OwnerSlotHolds(a_root, a_profile, a_character)) return UnplacedOutcome::Refused;
			const auto stacks = bag.at("stacks");
			const auto& profileRecord = a_root.at("profiles").at(a_profile);
			const auto instances = profileRecord.find("deathBagInstances");
			const auto pending = instances != profileRecord.end() ? PendingGrantCount(*instances) : std::size_t{};
			if (pending > MaximumPendingGrants || stacks.size() > MaximumPendingGrants - pending) return UnplacedOutcome::Refused;
			auto working = a_root;
			for (std::size_t index = 0; index < stacks.size(); ++index) {
				const auto& stack = stacks.at(index);
				if (!QueueRefundGrant(working, a_profile, a_character, a_id, a_operationId + "-" + std::to_string(index), stack.at("itemKey").get<std::string>(),
					stack.at("count").get<std::uint32_t>(), stack.at("metadata"))) return UnplacedOutcome::Refused;
			}
			working.at("deathBags").erase(a_id);
			Validate(working);
			a_root = std::move(working);
			return UnplacedOutcome::Refunded;
		} catch (const std::exception&) { return UnplacedOutcome::Refused; }
	}
}

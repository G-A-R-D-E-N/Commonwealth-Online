#include "F4TSettlementAccessProtocol.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace F4T::SettlementAccessProtocol
{
	namespace
	{
		using Json = nlohmann::json;
		using SettlementAccess::ApplyStatus;
		using SettlementAccess::Settlement;
	}

	std::optional<Workshop::BuildZoneId> NormalizeGet(const Json& a_packet)
	{
		const auto found = a_packet.find("buildZoneId");
		if (found == a_packet.end() || !found->is_number_unsigned()) {
			return std::nullopt;
		}
		return found->get<Workshop::BuildZoneId>();
	}

	std::optional<Update> NormalizeUpdate(const Json& a_packet)
	{
		const auto zone = NormalizeGet(a_packet);
		const auto op = a_packet.find("op");
		const auto target = a_packet.find("targetProfileId");
		if (!zone || op == a_packet.end() || target == a_packet.end() || !op->is_string() || !target->is_string()) {
			return std::nullopt;
		}
		const auto& action = op->get_ref<const std::string&>();
		if (action != "grant" && action != "revoke") {
			return std::nullopt;
		}
		return Update{ *zone, action == "grant", target->get<std::string>() };
	}

	Json MakeState(const Settlement& a_settlement)
	{
		return Json{
			{ "type", "settlementAccessState" },
			{ "buildZoneId", a_settlement.buildZoneId },
			{ "ownerProfileId", a_settlement.ownerProfileId },
			{ "allowedProfileIds", a_settlement.allowedProfileIds }
		};
	}

	Json MakeReject(Workshop::BuildZoneId a_buildZoneId, ApplyStatus a_status)
	{
		return Json{
			{ "type", "settlementAccessReject" },
			{ "buildZoneId", a_buildZoneId },
			{ "status", StatusName(a_status) }
		};
	}

	std::string_view StatusName(ApplyStatus a_status) noexcept
	{
		switch (a_status) {
		case ApplyStatus::Committed:
			return "committed";
		case ApplyStatus::InvalidRequest:
			return "invalidRequest";
		case ApplyStatus::UnknownBuildZone:
			return "unknownBuildZone";
		case ApplyStatus::NotOwner:
			return "notOwner";
		case ApplyStatus::AlreadyAllowed:
			return "alreadyAllowed";
		case ApplyStatus::NotAllowed:
			return "notAllowed";
		case ApplyStatus::Full:
			return "full";
		case ApplyStatus::SettlementLimit:
			return "settlementLimit";
		case ApplyStatus::RateLimited:
			return "rateLimited";
		case ApplyStatus::PersistenceUnavailable:
			return "persistenceUnavailable";
		}
		return "invalidRequest";
	}

	Json MakeStoredJson(const std::vector<Settlement>& a_settlements)
	{
		auto stored = Json::array();
		for (const auto& settlement : a_settlements) {
			stored.push_back(Json{
				{ "buildZoneId", settlement.buildZoneId },
				{ "ownerProfileId", settlement.ownerProfileId },
				{ "allowedProfileIds", settlement.allowedProfileIds }
			});
		}
		return stored;
	}

	std::optional<std::vector<Settlement>> NormalizeStored(const Json& a_stored)
	{
		std::vector<Settlement> settlements;
		if (a_stored.is_null()) {
			return settlements;
		}
		if (!a_stored.is_array()) {
			return std::nullopt;
		}
		for (const auto& entry : a_stored) {
			const auto zone = NormalizeGet(entry);
			const auto owner = entry.find("ownerProfileId");
			const auto allowed = entry.find("allowedProfileIds");
			if (!entry.is_object() || !zone || owner == entry.end() || allowed == entry.end() ||
				!owner->is_string() || !allowed->is_array()) {
				return std::nullopt;
			}
			Settlement settlement{ *zone, owner->get<std::string>(), {} };
			for (const auto& profileId : *allowed) {
				if (!profileId.is_string()) {
					return std::nullopt;
				}
				settlement.allowedProfileIds.push_back(profileId.get<std::string>());
			}
			settlements.push_back(std::move(settlement));
		}
		return settlements;
	}
}

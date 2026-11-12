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
		using SettlementAccess::SettlementKind;
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

	Json MakeState(
		const Settlement& a_settlement,
		const std::vector<DestroyedStructure>& a_destroyedStructures)
	{
		Json state{
			{ "type", "settlementAccessState" },
			{ "buildZoneId", a_settlement.buildZoneId },
			{ "ownerProfileId", a_settlement.ownerProfileId },
			{ "allowedProfileIds", a_settlement.allowedProfileIds }
		};
		if (a_settlement.kind == SettlementKind::Camp) {
			state["kind"] = "camp";
		}
		state["workbenchMaxHp"] = SettlementAccess::WorkbenchMaxHpFor(a_settlement.kind);
		if (a_settlement.workbenchHp != 2000.0F) {
			state["workbenchHp"] = a_settlement.workbenchHp;
		}
		if (a_settlement.workbenchBroken) {
			state["workbenchBroken"] = true;
		}
		if (a_settlement.workbenchBrokenUnix != 0) {
			state["workbenchBrokenUnix"] = a_settlement.workbenchBrokenUnix;
		}
		if (a_settlement.protectedUntilUnix != 0) {
			state["protectedUntilUnix"] = a_settlement.protectedUntilUnix;
		}
		if (a_settlement.upkeepPaidUntil != 0) {
			state["upkeepPaidUntil"] = a_settlement.upkeepPaidUntil;
		}
		Json destroyed = Json::array();
		for (const auto& structure : a_destroyedStructures) {
			if (destroyed.size() >= kMaximumDestroyedStructures) {
				break;
			}
			destroyed.push_back(Json{ { "objectId", structure.objectId }, { "pieceType", structure.pieceType }, { "rebuildComponents", structure.rebuildComponents } });
		}
		state["destroyedStructures"] = std::move(destroyed);
		return state;
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
		case ApplyStatus::UnderAttack:
			return "underAttack";
		case ApplyStatus::InsufficientMaterials:
			return "insufficientMaterials";
		case ApplyStatus::NothingToRepair:
			return "nothingToRepair";
		case ApplyStatus::ChargeFailed:
			return "chargeFailed";
		case ApplyStatus::TooFar:
			return "tooFar";
		}
		return "invalidRequest";
	}

	Json MakeStoredJson(const std::vector<Settlement>& a_settlements)
	{
		auto stored = Json::array();
		for (const auto& settlement : a_settlements) {
			Json entry{
				{ "buildZoneId", settlement.buildZoneId },
				{ "ownerProfileId", settlement.ownerProfileId },
				{ "allowedProfileIds", settlement.allowedProfileIds },
				{ "kind", settlement.kind == SettlementKind::Camp ? "camp" : "vanilla" },
				{ "workbenchHp", settlement.workbenchHp },
				{ "workbenchBroken", settlement.workbenchBroken },
				{ "workbenchBrokenUnix", settlement.workbenchBrokenUnix },
				{ "protectedUntilUnix", settlement.protectedUntilUnix },
				{ "upkeepPaidUntil", settlement.upkeepPaidUntil }
			};
			stored.push_back(std::move(entry));
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
			if (const auto kind = entry.find("kind"); kind != entry.end()) {
				if (!kind->is_string()) {
					return std::nullopt;
				}
				const auto name = kind->get<std::string>();
				if (name == "camp") {
					settlement.kind = SettlementKind::Camp;
				} else if (name == "vanilla") {
					settlement.kind = SettlementKind::Vanilla;
				} else {
					return std::nullopt;
				}
			}
			if (const auto workbenchHp = entry.find("workbenchHp"); workbenchHp != entry.end()) {
				if (!workbenchHp->is_number()) {
					return std::nullopt;
				}
				settlement.workbenchHp = workbenchHp->get<float>();
			}
			if (const auto workbenchBroken = entry.find("workbenchBroken"); workbenchBroken != entry.end()) {
				if (!workbenchBroken->is_boolean()) {
					return std::nullopt;
				}
				settlement.workbenchBroken = workbenchBroken->get<bool>();
			}
			if (const auto brokenUnix = entry.find("workbenchBrokenUnix"); brokenUnix != entry.end()) {
				if (!brokenUnix->is_number_unsigned()) {
					return std::nullopt;
				}
				settlement.workbenchBrokenUnix = brokenUnix->get<std::uint64_t>();
			}
			if (const auto protectedUntil = entry.find("protectedUntilUnix"); protectedUntil != entry.end()) {
				if (!protectedUntil->is_number_unsigned()) {
					return std::nullopt;
				}
				settlement.protectedUntilUnix = protectedUntil->get<std::uint64_t>();
			}
			if (const auto upkeepPaidUntil = entry.find("upkeepPaidUntil"); upkeepPaidUntil != entry.end()) {
				if (!upkeepPaidUntil->is_number_unsigned()) {
					return std::nullopt;
				}
				settlement.upkeepPaidUntil = upkeepPaidUntil->get<std::uint64_t>();
			}
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

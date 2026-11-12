#include "F4TWorkshopProtocol.h"
#include "F4TJsonFloat.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace F4T::WorkshopProtocol
{
	namespace
	{
		using Json = nlohmann::json;

		std::optional<std::uint64_t> ReadU64(const Json& a_value)
		{
			if (a_value.is_boolean()) {
				return std::nullopt;
			}
			if (a_value.is_number_unsigned()) {
				return a_value.get<std::uint64_t>();
			}
			if (a_value.is_number_integer()) {
				const auto value = a_value.get<std::int64_t>();
				if (value >= 0) {
					return static_cast<std::uint64_t>(value);
				}
			}
			return std::nullopt;
		}

		using F4T::JsonFloat::ReadFloat;
	}

	nlohmann::json MakeDefenderState(
		Workshop::BuildZoneId a_buildZoneId,
		float a_defenseRating,
		bool a_underAttack,
		std::uint32_t a_attackerCount,
		WorkshopSettlers::DetailLevel a_detail)
	{
		const char* detail = "abstract";
		switch (a_detail) {
		case WorkshopSettlers::DetailLevel::Full:
			detail = "full";
			break;
		case WorkshopSettlers::DetailLevel::Coarse:
			detail = "coarse";
			break;
		case WorkshopSettlers::DetailLevel::Abstract:
			detail = "abstract";
			break;
		}
		return Json{
			{ "type", "defenderState" },
			{ "buildZoneId", a_buildZoneId },
			{ "defenseRating", a_defenseRating },
			{ "underAttack", a_underAttack },
			{ "attackerCount", a_attackerCount },
			{ "detail", detail }
		};
	}

	nlohmann::json MakeZoneAttackState(
		Workshop::BuildZoneId a_buildZoneId,
		bool a_underAttack,
		double a_sinceMonotonic,
		std::uint32_t a_attackerCount)
	{
		return Json{
			{ "type", "zoneAttackState" },
			{ "buildZoneId", a_buildZoneId },
			{ "underAttack", a_underAttack },
			{ "since", a_sinceMonotonic },
			{ "attackers", a_attackerCount }
		};
	}

	std::optional<WireOp> NormalizeWireOp(const nlohmann::json& a_packet)
	{
		const auto type = a_packet.find("type");
		if (type == a_packet.end() || !type->is_string() || type->get<std::string>() != "wireOp") {
			return std::nullopt;
		}
		const auto buildZoneId = a_packet.find("buildZoneId");
		const auto operationId = a_packet.find("operationId");
		const auto op = a_packet.find("op");
		if (buildZoneId == a_packet.end() || operationId == a_packet.end() || op == a_packet.end() ||
			!op->is_string()) {
			return std::nullopt;
		}
		const auto zoneValue = ReadU64(*buildZoneId);
		const auto operationValue = ReadU64(*operationId);
		if (!zoneValue || !operationValue || *zoneValue == 0 || *operationValue == 0) {
			return std::nullopt;
		}
		WireOp wire{ *zoneValue, *operationValue };
		const auto name = op->get<std::string>();
		if (name == "connect") {
			wire.kind = WireOpKind::Connect;
		} else if (name == "disconnect") {
			wire.kind = WireOpKind::Disconnect;
		} else if (name == "switch") {
			wire.kind = WireOpKind::Switch;
		} else {
			return std::nullopt;
		}
		if (wire.kind == WireOpKind::Switch) {
			const auto objectId = a_packet.find("objectId");
			const auto closed = a_packet.find("closed");
			if (objectId == a_packet.end() || closed == a_packet.end() || !closed->is_boolean()) {
				return std::nullopt;
			}
			const auto objectValue = ReadU64(*objectId);
			if (!objectValue || *objectValue == 0) {
				return std::nullopt;
			}
			wire.fromId = *objectValue;
			wire.switchClosed = closed->get<bool>();
			return wire;
		}
		const auto fromId = a_packet.find("fromId");
		const auto toId = a_packet.find("toId");
		if (fromId == a_packet.end() || toId == a_packet.end()) {
			return std::nullopt;
		}
		const auto fromValue = ReadU64(*fromId);
		const auto toValue = ReadU64(*toId);
		if (!fromValue || !toValue || *fromValue == 0 || *toValue == 0) {
			return std::nullopt;
		}
		wire.fromId = *fromValue;
		wire.toId = *toValue;
		if (wire.kind == WireOpKind::Connect) {
			if (const auto wireId = a_packet.find("wireId"); wireId != a_packet.end()) {
				const auto wireValue = ReadU64(*wireId);
				if (!wireValue || *wireValue == 0) {
					return std::nullopt;
				}
				wire.wireId = *wireValue;
			}
		}
		return wire;
	}

	std::optional<DoorOp> NormalizeDoorOp(const nlohmann::json& a_packet)
	{
		const auto type = a_packet.find("type");
		if (type == a_packet.end() || !type->is_string() || type->get<std::string>() != "doorOp") {
			return std::nullopt;
		}
		const auto buildZoneId = a_packet.find("buildZoneId");
		const auto objectId = a_packet.find("objectId");
		const auto op = a_packet.find("op");
		if (buildZoneId == a_packet.end() || objectId == a_packet.end() || op == a_packet.end() || !op->is_string()) {
			return std::nullopt;
		}
		const auto zoneValue = ReadU64(*buildZoneId);
		const auto objectValue = ReadU64(*objectId);
		if (!zoneValue || !objectValue || *zoneValue == 0 || *objectValue == 0) {
			return std::nullopt;
		}
		DoorOp door{ *zoneValue, *objectValue };
		const auto name = op->get<std::string>();
		if (name == "open") {
			door.kind = DoorOpKind::Open;
		} else if (name == "close") {
			door.kind = DoorOpKind::Close;
		} else if (name == "lock") {
			door.kind = DoorOpKind::Lock;
		} else if (name == "unlock") {
			door.kind = DoorOpKind::Unlock;
		} else {
			return std::nullopt;
		}
		return door;
	}

	std::optional<SnapshotRequest> NormalizeSnapshotRequest(const nlohmann::json& a_packet)
	{
		const auto type = a_packet.find("type");
		if (type == a_packet.end() || !type->is_string() || type->get<std::string>() != "workshopSnapshotRequest") {
			return std::nullopt;
		}
		const auto buildZoneId = a_packet.find("buildZoneId");
		const auto haveRevision = a_packet.find("haveRevision");
		if (buildZoneId == a_packet.end() || haveRevision == a_packet.end()) {
			return std::nullopt;
		}
		const auto zoneValue = ReadU64(*buildZoneId);
		const auto revisionValue = ReadU64(*haveRevision);
		if (!zoneValue || !revisionValue || *zoneValue == 0) {
			return std::nullopt;
		}
		return SnapshotRequest{ *zoneValue, *revisionValue };
	}

	std::optional<StructureRepair> NormalizeStructureRepair(const nlohmann::json& a_packet)
	{
		const auto type = a_packet.find("type");
		if (type == a_packet.end() || !type->is_string() || type->get<std::string>() != "structureRepair") {
			return std::nullopt;
		}
		const auto buildZoneId = a_packet.find("buildZoneId");
		const auto objectId = a_packet.find("objectId");
		if (buildZoneId == a_packet.end() || objectId == a_packet.end()) {
			return std::nullopt;
		}
		const auto zoneValue = ReadU64(*buildZoneId);
		const auto objectValue = ReadU64(*objectId);
		if (!zoneValue || !objectValue || *zoneValue == 0 || *objectValue == 0) {
			return std::nullopt;
		}
		return StructureRepair{ *zoneValue, *objectValue };
	}

	std::optional<StructureRebuild> NormalizeStructureRebuild(const nlohmann::json& a_packet)
	{
		const auto type = a_packet.find("type");
		if (type == a_packet.end() || !type->is_string() || type->get<std::string>() != "structureRebuild") {
			return std::nullopt;
		}
		const auto buildZoneId = a_packet.find("buildZoneId");
		const auto objectId = a_packet.find("objectId");
		if (buildZoneId == a_packet.end() || objectId == a_packet.end()) {
			return std::nullopt;
		}
		const auto zoneValue = ReadU64(*buildZoneId);
		const auto objectValue = ReadU64(*objectId);
		if (!zoneValue || !objectValue || *zoneValue == 0 || *objectValue == 0) {
			return std::nullopt;
		}
		return StructureRebuild{ *zoneValue, *objectValue };
	}

	nlohmann::json MakeUpkeepState(
		Workshop::BuildZoneId a_buildZoneId,
		std::uint64_t a_hourlyCost,
		std::uint64_t a_hoursCovered,
		bool a_decaying)
	{
		return Json{
			{ "type", "upkeepState" },
			{ "buildZoneId", a_buildZoneId },
			{ "hourlyCost", a_hourlyCost },
			{ "hoursCovered", a_hoursCovered },
			{ "decaying", a_decaying }
		};
	}

	std::optional<UpkeepState> ParseUpkeepState(const nlohmann::json& a_packet)
	{
		const auto type = a_packet.find("type");
		const auto zone = a_packet.find("buildZoneId");
		const auto hourly = a_packet.find("hourlyCost");
		const auto hours = a_packet.find("hoursCovered");
		const auto decaying = a_packet.find("decaying");
		if (type == a_packet.end() || !type->is_string() || type->get<std::string>() != "upkeepState" ||
			zone == a_packet.end() || hourly == a_packet.end() || hours == a_packet.end() ||
			decaying == a_packet.end() || !decaying->is_boolean()) {
			return std::nullopt;
		}
		const auto buildZoneId = ReadU64(*zone);
		const auto hourlyCost = ReadU64(*hourly);
		const auto hoursCovered = ReadU64(*hours);
		if (!buildZoneId || *buildZoneId == 0 || !hourlyCost || !hoursCovered) {
			return std::nullopt;
		}
		return UpkeepState{ *buildZoneId, *hourlyCost, *hoursCovered, decaying->get<bool>() };
	}

	std::optional<RaidDeclare> NormalizeRaidDeclare(const nlohmann::json& a_packet)
	{
		const auto type = a_packet.find("type");
		if (type == a_packet.end() || !type->is_string() || type->get<std::string>() != "raidDeclare") {
			return std::nullopt;
		}
		const auto buildZoneId = a_packet.find("buildZoneId");
		if (buildZoneId == a_packet.end()) {
			return std::nullopt;
		}
		const auto zoneValue = ReadU64(*buildZoneId);
		if (!zoneValue || *zoneValue == 0) {
			return std::nullopt;
		}
		return RaidDeclare{ *zoneValue };
	}

	std::optional<SettlementWindowSet> NormalizeSettlementWindowSet(const nlohmann::json& a_packet)
	{
		const auto type = a_packet.find("type");
		if (type == a_packet.end() || !type->is_string() || type->get<std::string>() != "settlementWindowSet") {
			return std::nullopt;
		}
		const auto buildZoneId = a_packet.find("buildZoneId");
		const auto start = a_packet.find("windowStartHour");
		const auto duration = a_packet.find("windowDurationHours");
		if (buildZoneId == a_packet.end() || start == a_packet.end() || duration == a_packet.end() ||
			!start->is_number() || !duration->is_number()) {
			return std::nullopt;
		}
		const auto zoneValue = ReadU64(*buildZoneId);
		const auto startHour = ReadFloat(*start);
		const auto durationHours = ReadFloat(*duration);
		std::optional<float> offsetHours = 0.0F;
		if (const auto offset = a_packet.find("timezoneOffsetHours"); offset != a_packet.end()) {
			if (!offset->is_number()) {
				return std::nullopt;
			}
			offsetHours = ReadFloat(*offset);
		}
		if (!zoneValue || *zoneValue == 0 || !startHour || !durationHours || !offsetHours ||
			!WorkshopRaids::ValidWindowSet(*startHour, *durationHours, *offsetHours)) {
			return std::nullopt;
		}
		return SettlementWindowSet{ *zoneValue, *startHour, *durationHours, *offsetHours };
	}

	nlohmann::json MakeSettlementRefused(
		Workshop::BuildZoneId a_buildZoneId,
		std::string_view a_op,
		std::string_view a_reason)
	{
		return Json{
			{ "type", "settlementRefused" },
			{ "buildZoneId", a_buildZoneId },
			{ "op", std::string{ a_op } },
			{ "reason", std::string{ a_reason } }
		};
	}

	nlohmann::json MakeRaidDeclareState(
		Workshop::BuildZoneId a_buildZoneId,
		std::string_view a_declarerProfileId,
		double a_readyMonotonic,
		double a_expiresMonotonic,
		double a_readyUnix,
		double a_expiresUnix)
	{
		return Json{
			{ "type", "raidDeclareState" },
			{ "buildZoneId", a_buildZoneId },
			{ "declarerProfileId", std::string{ a_declarerProfileId } },
			{ "readyMonotonic", a_readyMonotonic },
			{ "expiresMonotonic", a_expiresMonotonic },
			{ "readyUnix", a_readyUnix },
			{ "expiresUnix", a_expiresUnix }
		};
	}

	nlohmann::json MakeRaidDeclare(Workshop::BuildZoneId a_buildZoneId)
	{
		return Json{ { "type", "raidDeclare" }, { "buildZoneId", a_buildZoneId } };
	}

	nlohmann::json MakeSettlementWindowSet(
		Workshop::BuildZoneId a_buildZoneId,
		float a_windowStartHour,
		float a_windowDurationHours,
		float a_timezoneOffsetHours)
	{
		return Json{
			{ "type", "settlementWindowSet" },
			{ "buildZoneId", a_buildZoneId },
			{ "windowStartHour", a_windowStartHour },
			{ "windowDurationHours", a_windowDurationHours },
			{ "timezoneOffsetHours", a_timezoneOffsetHours }
		};
	}

	nlohmann::json MakeSettlerRecruit(Workshop::BuildZoneId a_buildZoneId)
	{
		return Json{ { "type", "settlerRecruit" }, { "buildZoneId", a_buildZoneId } };
	}

	nlohmann::json MakeSettlerAssign(
		Workshop::BuildZoneId a_buildZoneId,
		Workshop::ObjectId a_settlerId,
		std::string_view a_assignment)
	{
		return Json{
			{ "type", "settlerAssign" },
			{ "buildZoneId", a_buildZoneId },
			{ "settlerId", a_settlerId },
			{ "assignment", std::string{ a_assignment } }
		};
	}

	nlohmann::json MakeSettlerRecover(Workshop::BuildZoneId a_buildZoneId, Workshop::ObjectId a_settlerId)
	{
		return Json{ { "type", "settlerRecover" }, { "buildZoneId", a_buildZoneId }, { "settlerId", a_settlerId } };
	}

	nlohmann::json MakeRaidHitRefused(
		Workshop::BuildZoneId a_buildZoneId,
		WorkshopRaids::HitRefusal a_refusal,
		std::uint64_t a_secondsLeft)
	{
		Json packet{
			{ "type", "raidHitRefused" },
			{ "buildZoneId", a_buildZoneId },
			{ "reason", WorkshopRaids::HitRefusalName(a_refusal) }
		};
		if (a_secondsLeft > 0) {
			packet["secondsLeft"] = a_secondsLeft;
		}
		return packet;
	}


	nlohmann::json MakeSettlementWindowState(
		Workshop::BuildZoneId a_buildZoneId,
		float a_windowStartHour,
		float a_windowDurationHours,
		float a_timezoneOffsetHours)
	{
		return Json{
			{ "type", "settlementWindowState" },
			{ "buildZoneId", a_buildZoneId },
			{ "windowStartHour", a_windowStartHour },
			{ "windowDurationHours", a_windowDurationHours },
			{ "timezoneOffsetHours", a_timezoneOffsetHours }
		};
	}
}

#pragma once

#include "F4TWorkshopMaterials.h"
#include "F4TWorkshopRaids.h"
#include "F4TWorkshopSettlers.h"
#include "F4TWorkshopTransactions.h"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::WorkshopProtocol
{
	inline constexpr std::size_t MaximumSnapshotEntriesPerFrame = 256;

	struct Snapshot
	{
		Workshop::BuildZoneId buildZoneId{};
		Workshop::Revision revision{};
		std::vector<Workshop::ObjectState> objects;
		std::vector<Workshop::ObjectState> tombstones;
		bool complete{};
		bool unchanged{};
	};

	struct Commit
	{
		Workshop::PlayerId playerId{};
		Workshop::BuildZoneId buildZoneId{};
		Workshop::OperationId operationId{};
		Workshop::Revision revision{};
		Workshop::OperationKind kind{ Workshop::OperationKind::Place };
		Workshop::ObjectId objectId{};
		std::optional<Workshop::ObjectState> object;
	};

	struct Reject
	{
		Workshop::BuildZoneId buildZoneId{};
		Workshop::OperationId operationId{};
		Workshop::Revision revision{};
		Workshop::ApplyStatus status{ Workshop::ApplyStatus::InvalidRequest };
	};

	struct StructureHit
	{
		Workshop::BuildZoneId buildZoneId{};
		Workshop::ObjectId objectId{};
		WorkshopRaids::ExplosiveKind explosive{};
		WorkshopMaterials::WeaponKind weaponKind{ WorkshopMaterials::WeaponKind::Explosive };
	};

	enum class DoorOpKind : std::uint8_t
	{
		Open,
		Close,
		Lock,
		Unlock
	};

	struct DoorOp
	{
		Workshop::BuildZoneId buildZoneId{};
		Workshop::ObjectId objectId{};
		DoorOpKind kind{ DoorOpKind::Open };
	};

	struct StructureDamage
	{
		Workshop::BuildZoneId buildZoneId{};
		Workshop::ObjectId objectId{};
		float health{};
		float maxHealth{};
		Workshop::Revision revision{};
		std::uint8_t damageStage{};
	};

	struct StructureDestroyed
	{
		Workshop::BuildZoneId buildZoneId{};
		Workshop::ObjectId objectId{};
		std::uint64_t destroyedUnixSeconds{};
		Workshop::Revision revision{};
	};

	[[nodiscard]] std::optional<nlohmann::json> MakeOperation(const Workshop::Request& a_request);
	[[nodiscard]] std::optional<Commit> NormalizeCommit(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<Reject> NormalizeReject(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<Workshop::Request> NormalizeOperation(
		const nlohmann::json& a_packet,
		Workshop::PlayerId a_playerId);
	[[nodiscard]] std::optional<nlohmann::json> MakeCommit(
		const Workshop::Request& a_request,
		const Workshop::Result& a_result);
	[[nodiscard]] nlohmann::json MakeReject(
		const Workshop::Request& a_request,
		const Workshop::Result& a_result);
	[[nodiscard]] std::string_view StatusName(Workshop::ApplyStatus a_status) noexcept;
	[[nodiscard]] const char* RejectNotice(Workshop::ApplyStatus a_status) noexcept;
	[[nodiscard]] std::vector<nlohmann::json> MakeSnapshotPackets(
		Workshop::BuildZoneId a_buildZoneId,
		Workshop::Revision a_revision,
		const std::vector<Workshop::ObjectState>& a_objects,
		const std::vector<Workshop::ObjectState>& a_tombstones);
	[[nodiscard]] std::optional<Snapshot> NormalizeSnapshot(const nlohmann::json& a_packet);
	[[nodiscard]] nlohmann::json MakeZoneState(const Workshop::SettlementState& a_zone);
	[[nodiscard]] bool RestoreZoneState(Workshop::SettlementState& a_zone, const nlohmann::json& a_state);
	struct PowerWire
	{
		Workshop::ObjectId wireId{};
		Workshop::ObjectId fromId{};
		Workshop::ObjectId toId{};
	};

	[[nodiscard]] nlohmann::json MakePowerState(
		Workshop::BuildZoneId a_buildZoneId,
		const std::vector<Workshop::ObjectId>& a_poweredObjectIds,
		const std::vector<PowerWire>& a_wires = {});

	struct PowerState
	{
		Workshop::BuildZoneId buildZoneId{};
		std::vector<Workshop::ObjectId> poweredObjectIds;
		std::vector<PowerWire> wires;
		bool hasWires{};
	};

	[[nodiscard]] std::optional<PowerState> ParsePowerState(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<std::vector<Workshop::BuildZonePolicy>> ParseBuildZones(const nlohmann::json& a_file);
	[[nodiscard]] std::optional<StructureHit> NormalizeStructureHit(const nlohmann::json& a_packet);
	[[nodiscard]] nlohmann::json MakeStructureHit(const StructureHit& a_hit);
	[[nodiscard]] nlohmann::json MakeStructureDamage(const StructureDamage& a_damage);
	[[nodiscard]] nlohmann::json MakeStructureDestroyed(const StructureDestroyed& a_destroyed);
	[[nodiscard]] nlohmann::json MakeDefenderState(
		Workshop::BuildZoneId a_buildZoneId,
		float a_defenseRating,
		bool a_underAttack,
		std::uint32_t a_attackerCount,
		WorkshopSettlers::DetailLevel a_detail);
	[[nodiscard]] nlohmann::json MakeZoneAttackState(
		Workshop::BuildZoneId a_buildZoneId,
		bool a_underAttack,
		double a_sinceMonotonic,
		std::uint32_t a_attackerCount);

	enum class WireOpKind : std::uint8_t
	{
		Connect,
		Disconnect,
		Switch
	};

	struct WireOp
	{
		Workshop::BuildZoneId buildZoneId{};
		Workshop::OperationId operationId{};
		WireOpKind kind{ WireOpKind::Connect };
		Workshop::ObjectId fromId{};
		Workshop::ObjectId toId{};
		Workshop::ObjectId wireId{};
		bool switchClosed{ true };
	};

	[[nodiscard]] std::optional<WireOp> NormalizeWireOp(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<DoorOp> NormalizeDoorOp(const nlohmann::json& a_packet);

	struct SnapshotRequest
	{
		Workshop::BuildZoneId buildZoneId{};
		Workshop::Revision haveRevision{};
	};

	[[nodiscard]] std::optional<SnapshotRequest> NormalizeSnapshotRequest(const nlohmann::json& a_packet);

	struct StructureRepair
	{
		Workshop::BuildZoneId buildZoneId{};
		Workshop::ObjectId objectId{};
	};

	struct StructureRebuild
	{
		Workshop::BuildZoneId buildZoneId{};
		Workshop::ObjectId objectId{};
	};

	[[nodiscard]] std::optional<StructureRepair> NormalizeStructureRepair(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<StructureRebuild> NormalizeStructureRebuild(const nlohmann::json& a_packet);
	[[nodiscard]] nlohmann::json MakeUpkeepState(
		Workshop::BuildZoneId a_buildZoneId,
		std::uint64_t a_hourlyCost,
		std::uint64_t a_hoursCovered,
		bool a_decaying);

	struct UpkeepState
	{
		Workshop::BuildZoneId buildZoneId{};
		std::uint64_t hourlyCost{};
		std::uint64_t hoursCovered{};
		bool decaying{};
	};

	[[nodiscard]] std::optional<UpkeepState> ParseUpkeepState(const nlohmann::json& a_packet);

	struct RaidDeclare
	{
		Workshop::BuildZoneId buildZoneId{};
	};

	struct SettlementWindowSet
	{
		Workshop::BuildZoneId buildZoneId{};
		float windowStartHour{};
		float windowDurationHours{ 2.0F };
		float timezoneOffsetHours{};
	};

	[[nodiscard]] std::optional<RaidDeclare> NormalizeRaidDeclare(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<SettlementWindowSet> NormalizeSettlementWindowSet(const nlohmann::json& a_packet);
	[[nodiscard]] nlohmann::json MakeSettlementRefused(
		Workshop::BuildZoneId a_buildZoneId,
		std::string_view a_op,
		std::string_view a_reason);
	[[nodiscard]] nlohmann::json MakeRaidDeclareState(
		Workshop::BuildZoneId a_buildZoneId,
		std::string_view a_declarerProfileId,
		double a_readyMonotonic,
		double a_expiresMonotonic,
		double a_readyUnix,
		double a_expiresUnix);
	[[nodiscard]] nlohmann::json MakeSettlementWindowState(
		Workshop::BuildZoneId a_buildZoneId,
		float a_windowStartHour,
		float a_windowDurationHours,
		float a_timezoneOffsetHours);

	[[nodiscard]] nlohmann::json MakeRaidDeclare(Workshop::BuildZoneId a_buildZoneId);
	[[nodiscard]] nlohmann::json MakeSettlementWindowSet(
		Workshop::BuildZoneId a_buildZoneId,
		float a_windowStartHour,
		float a_windowDurationHours,
		float a_timezoneOffsetHours);
	[[nodiscard]] nlohmann::json MakeSettlerRecruit(Workshop::BuildZoneId a_buildZoneId);
	[[nodiscard]] nlohmann::json MakeSettlerAssign(
		Workshop::BuildZoneId a_buildZoneId,
		Workshop::ObjectId a_settlerId,
		std::string_view a_assignment);
	[[nodiscard]] nlohmann::json MakeSettlerRecover(Workshop::BuildZoneId a_buildZoneId, Workshop::ObjectId a_settlerId);
	[[nodiscard]] nlohmann::json MakeRaidHitRefused(
		Workshop::BuildZoneId a_buildZoneId,
		WorkshopRaids::HitRefusal a_refusal,
		std::uint64_t a_secondsLeft);

	namespace Codec
	{
		std::optional<std::uint64_t> ReadU64(const nlohmann::json& a_value);
		std::optional<std::uint32_t> ReadU32(const nlohmann::json& a_value);
		std::optional<Workshop::FormKey> ReadForm(const nlohmann::json& a_value);
		std::optional<Workshop::Transform> ReadTransform(const nlohmann::json& a_value);
		std::optional<Workshop::OperationKind> ReadKind(const nlohmann::json& a_value);
		nlohmann::json FormJson(const Workshop::FormKey& a_form);
		nlohmann::json TransformJson(const Workshop::Transform& a_transform);
		nlohmann::json ObjectJson(const Workshop::ObjectState& a_object);
		std::optional<Workshop::ObjectState> ReadObjectState(const nlohmann::json& a_value, Workshop::BuildZoneId a_buildZoneId);
		std::string_view KindName(Workshop::OperationKind a_kind) noexcept;
		std::optional<Workshop::ApplyStatus> ReadStatus(const nlohmann::json& a_value);
	}
}

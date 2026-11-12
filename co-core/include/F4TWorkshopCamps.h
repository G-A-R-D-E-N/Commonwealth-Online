#pragma once

#include "F4TTradeLedger.h"
#include "F4TWorkshopTransactions.h"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::WorkshopCamps
{
	inline constexpr std::uint64_t CampZoneIdHighBit = 1ull << 63;
	inline constexpr float UnitsPerMeter = 69.99125F; // TODO-verify: RE Docs 2026-10-31-commonwealth-online-re-closures.md
	inline constexpr float MinimumCampSeparationUnits = 150.0F * UnitsPerMeter;
	inline constexpr float MinimumSettlementSeparationUnits = 300.0F * UnitsPerMeter;
	inline constexpr float MaximumSlopeDegrees = 30.0F;
	inline constexpr float MaximumProbeDropUnits = 1.0F * UnitsPerMeter; // TODO-verify: ground drop tolerance under the placement point
	inline constexpr float DefaultCampRadiusUnits = 60.0F * UnitsPerMeter;
	inline constexpr std::uint32_t DefaultCampMaxObjects = 600;
	inline constexpr std::size_t MaximumCampsPerServer = 300;
	inline constexpr std::size_t MaximumCampsPerProfile = 1;
	inline constexpr std::uint64_t MoveCooldownSeconds = 3600;
	inline constexpr float DensityCellUnits = 500.0F * UnitsPerMeter;
	inline constexpr std::size_t MaximumCampsPerDensityCell = 4;
	inline constexpr float BrokenCampDecayPerHour = 0.10F;
	inline constexpr std::size_t MaximumCampRecords = 512;
	inline constexpr float MaximumPlacementOffsetUnits = 8.0F * UnitsPerMeter;
	inline constexpr float MaximumCoordinateMagnitude = 1000000.0F;
	inline constexpr std::uint64_t CampUpkeepGraceSeconds = 7ull * 24ull * 60ull * 60ull;
	inline constexpr std::string_view CampKitItemKey = "commonwealthonline.esp:0000997F";

	enum class FoundStatus : std::uint8_t
	{
		Ok,
		InvalidRequest,
		TooCloseToCamp,
		TooCloseToSettlement,
		NoBuildZone,
		Terrain,
		Limit,
		LimitProfile,
		LimitArea,
		UnderAttack,
		Cooldown,
		NotOwner,
		NotFound,
		DropLimit,
		GrantFailed,
		RefundUnavailable,
		TooFar
	};

	struct SeparationDetail
	{
		std::uint32_t distanceMeters{};
		std::uint32_t requiredMeters{};
		std::string subjectName;
	};

	struct GroundProbe
	{
		float height{};
		float slopeDegrees{};
		bool navmeshHit{};
	};

	struct CampRecord
	{
		Workshop::BuildZoneId buildZoneId{};
		std::string ownerProfileId;
		Workshop::FormKey space;
		float centerX{};
		float centerY{};
		float centerZ{};
		float radius{ DefaultCampRadiusUnits };
		std::uint32_t maxObjects{ DefaultCampMaxObjects };
		std::uint64_t foundedAtUnix{};
		std::uint64_t lastMovedAtUnix{};
		bool stored{};
	};

	struct FoundRequest
	{
		std::string ownerProfileId;
		std::uint64_t operationId{};
		Workshop::FormKey space;
		float x{};
		float y{};
		float z{};
		GroundProbe probe;
		std::uint64_t nowUnix{};
		std::uint32_t dropEntityId{};
	};

	struct MoveRequest
	{
		std::string ownerProfileId;
		std::uint64_t operationId{};
		Workshop::BuildZoneId buildZoneId{};
		Workshop::FormKey space;
		float x{};
		float y{};
		float z{};
		GroundProbe probe;
		std::uint64_t nowUnix{};
	};

	struct ZoneRequest
	{
		std::string ownerProfileId;
		std::uint64_t operationId{};
		Workshop::BuildZoneId buildZoneId{};
	};

	struct PlayerLocation
	{
		std::string worldspaceId;
		std::string cellId;
		double x{};
		double y{};
		double z{};
	};

	struct CampPlacement
	{
		Workshop::FormKey space;
		Workshop::FormKey cell;
		float x{};
		float y{};
		float z{};
	};

	[[nodiscard]] bool IsCampZoneId(Workshop::BuildZoneId a_buildZoneId) noexcept;
	[[nodiscard]] Workshop::BuildZoneId MakeCampZoneId(std::uint64_t a_sequence) noexcept;
	[[nodiscard]] float Distance(float a_ax, float a_ay, float a_az, float a_bx, float a_by, float a_bz) noexcept;
	[[nodiscard]] std::optional<CampPlacement> ResolvePlacement(
		const PlayerLocation& a_player,
		std::string_view a_plugin,
		const std::vector<Workshop::FormKey>& a_knownSpaces,
		float a_x,
		float a_y,
		float a_z);
	[[nodiscard]] FoundStatus ValidateFound(
		const FoundRequest& a_request,
		const std::vector<CampRecord>& a_camps,
		const std::vector<Workshop::BuildZonePolicy>& a_settlements,
		bool a_inNoBuildZone,
		Workshop::BuildZoneId a_excludeZoneId = 0, SeparationDetail* a_detail = nullptr);
	[[nodiscard]] FoundStatus CheckCampKitDrop(const nlohmann::json& a_drop, std::string_view a_profileId);
	[[nodiscard]] FoundStatus ValidateMove(
		const MoveRequest& a_request,
		const CampRecord& a_camp,
		const std::vector<CampRecord>& a_camps,
		const std::vector<Workshop::BuildZonePolicy>& a_settlements,
		bool a_inNoBuildZone,
		bool a_underAttack, SeparationDetail* a_detail = nullptr);
	[[nodiscard]] Workshop::BuildZonePolicy MakeCampPolicy(const CampRecord& a_camp) noexcept;
	[[nodiscard]] std::string_view FoundStatusName(FoundStatus a_status) noexcept;
	[[nodiscard]] std::optional<FoundRequest> NormalizeCampFound(const nlohmann::json& a_packet, std::string_view a_ownerProfileId);
	[[nodiscard]] std::optional<MoveRequest> NormalizeCampMove(const nlohmann::json& a_packet, std::string_view a_ownerProfileId);
	[[nodiscard]] std::optional<ZoneRequest> NormalizeCampZoneOp(
		const nlohmann::json& a_packet,
		std::string_view a_ownerProfileId,
		std::string_view a_type);
	[[nodiscard]] nlohmann::json MakeCampCreated(const CampRecord& a_camp, std::uint64_t a_operationId);
	[[nodiscard]] nlohmann::json MakeCampMoved(const CampRecord& a_camp, std::uint64_t a_operationId);
	[[nodiscard]] nlohmann::json MakeCampStored(Workshop::BuildZoneId a_buildZoneId, std::uint64_t a_operationId);
	[[nodiscard]] nlohmann::json MakeCampRemoved(Workshop::BuildZoneId a_buildZoneId, std::uint64_t a_operationId);
	[[nodiscard]] nlohmann::json MakeCampRejected(std::uint64_t a_operationId, FoundStatus a_status, const SeparationDetail& a_detail = {});
	[[nodiscard]] nlohmann::json MakeCampList(const std::vector<CampRecord>& a_camps);
	[[nodiscard]] nlohmann::json MakeCampRefund(
		Workshop::BuildZoneId a_buildZoneId,
		std::uint64_t a_operationId,
		std::size_t a_removedPieces,
		std::size_t a_unpricedPieces,
		const std::vector<Trade::Line>& a_granted);
	[[nodiscard]] nlohmann::json CampRecordJson(const CampRecord& a_camp);
	[[nodiscard]] std::optional<CampRecord> ReadCampRecord(const nlohmann::json& a_value);
}

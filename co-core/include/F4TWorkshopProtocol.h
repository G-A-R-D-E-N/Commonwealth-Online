#pragma once

#include "F4TWorkshopTransactions.h"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace F4T::WorkshopProtocol
{
	inline constexpr std::size_t MaximumSnapshotEntriesPerFrame = 32;

	struct Snapshot
	{
		Workshop::BuildZoneId buildZoneId{};
		Workshop::Revision revision{};
		std::vector<Workshop::ObjectState> objects;
		std::vector<Workshop::ObjectState> tombstones;
		bool complete{};
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
	[[nodiscard]] std::vector<nlohmann::json> MakeSnapshotPackets(
		Workshop::BuildZoneId a_buildZoneId,
		Workshop::Revision a_revision,
		const std::vector<Workshop::ObjectState>& a_objects,
		const std::vector<Workshop::ObjectState>& a_tombstones);
	[[nodiscard]] std::optional<Snapshot> NormalizeSnapshot(const nlohmann::json& a_packet);
	[[nodiscard]] nlohmann::json MakeZoneState(const Workshop::SettlementState& a_zone);
	[[nodiscard]] bool RestoreZoneState(Workshop::SettlementState& a_zone, const nlohmann::json& a_state);
	[[nodiscard]] std::optional<std::vector<Workshop::BuildZonePolicy>> ParseBuildZones(const nlohmann::json& a_file);
}

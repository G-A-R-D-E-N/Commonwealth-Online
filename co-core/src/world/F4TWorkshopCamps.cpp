#include "F4TWorkshopCamps.h"

#include "F4TDropLedger.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <system_error>

namespace F4T::WorkshopCamps
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

		std::optional<std::uint32_t> ReadU32(const Json& a_value)
		{
			const auto value = ReadU64(a_value);
			if (!value || *value > (std::numeric_limits<std::uint32_t>::max)()) {
				return std::nullopt;
			}
			return static_cast<std::uint32_t>(*value);
		}

		std::optional<float> ReadFloat(const Json& a_value)
		{
			if (!a_value.is_number() || a_value.is_boolean()) {
				return std::nullopt;
			}
			const auto value = a_value.get<double>();
			if (!std::isfinite(value) ||
				value < -(std::numeric_limits<float>::max)() ||
				value > (std::numeric_limits<float>::max)()) {
				return std::nullopt;
			}
			return static_cast<float>(value);
		}

		std::optional<Workshop::FormKey> ReadForm(const Json& a_value)
		{
			if (!a_value.is_object()) {
				return std::nullopt;
			}
			const auto plugin = a_value.find("plugin");
			const auto localFormId = a_value.find("localFormId");
			if (plugin == a_value.end() || localFormId == a_value.end() || !plugin->is_string()) {
				return std::nullopt;
			}
			auto name = plugin->get<std::string>();
			const auto id = ReadU32(*localFormId);
			if (name.empty() || name.size() > Workshop::MaximumPluginNameBytes || !id || *id == 0) {
				return std::nullopt;
			}
			return Workshop::FormKey{ std::move(name), *id };
		}

		bool CoordinatesInBounds(float a_x, float a_y, float a_z) noexcept
		{
			return std::fabs(a_x) <= MaximumCoordinateMagnitude && std::fabs(a_y) <= MaximumCoordinateMagnitude &&
				std::fabs(a_z) <= MaximumCoordinateMagnitude;
		}

		std::optional<std::uint32_t> ParseLocalFormId(std::string_view a_text) noexcept
		{
			if (a_text.size() > 2 && a_text[0] == '0' && (a_text[1] == 'x' || a_text[1] == 'X')) {
				a_text.remove_prefix(2);
			}
			std::uint32_t value{};
			const auto result = std::from_chars(a_text.data(), a_text.data() + a_text.size(), value, 16);
			if (a_text.empty() || result.ec != std::errc{} || result.ptr != a_text.data() + a_text.size() || value == 0 ||
				value > Workshop::MaximumLocalFormId) {
				return std::nullopt;
			}
			return value;
		}

		std::optional<std::string> PluginForWorldspace(
			std::uint32_t a_worldspace,
			std::string_view a_claimedPlugin,
			const std::vector<Workshop::FormKey>& a_knownSpaces)
		{
			std::optional<std::string> known;
			for (const auto& space : a_knownSpaces) {
				if (space.localFormId != a_worldspace) {
					continue;
				}
				if (known && *known != space.plugin) {
					return std::nullopt;
				}
				known = space.plugin;
			}
			if (!known) {
				return std::string{ a_claimedPlugin };
			}
			if (*known != a_claimedPlugin) {
				return std::nullopt;
			}
			return known;
		}

		constexpr double kMeterBoundarySlack = 1e-4;

		std::uint32_t WholeMetersDown(float a_units) noexcept
		{
			return static_cast<std::uint32_t>(std::floor(static_cast<double>(a_units) / static_cast<double>(UnitsPerMeter) + kMeterBoundarySlack));
		}

		std::uint32_t WholeMetersUp(float a_units) noexcept
		{
			return static_cast<std::uint32_t>(std::ceil(static_cast<double>(a_units) / static_cast<double>(UnitsPerMeter) - kMeterBoundarySlack));
		}

		Json FormJson(const Workshop::FormKey& a_form)
		{
			return Json{ { "plugin", a_form.plugin }, { "localFormId", a_form.localFormId } };
		}
	}

	bool IsCampZoneId(Workshop::BuildZoneId a_buildZoneId) noexcept
	{
		return (a_buildZoneId & CampZoneIdHighBit) != 0;
	}

	Workshop::BuildZoneId MakeCampZoneId(std::uint64_t a_sequence) noexcept
	{
		return CampZoneIdHighBit | (a_sequence & ~CampZoneIdHighBit);
	}

	bool MovedRecently(const CampRecord& a_camp, std::uint64_t a_nowUnix) noexcept
	{
		const std::uint64_t elapsed = a_nowUnix >= a_camp.lastMovedAtUnix ? a_nowUnix - a_camp.lastMovedAtUnix : 0;
		return a_camp.lastMovedAtUnix != 0 && elapsed < MoveCooldownSeconds;
	}

	float Distance(float a_ax, float a_ay, float a_az, float a_bx, float a_by, float a_bz) noexcept
	{
		const double dx = static_cast<double>(a_ax) - static_cast<double>(a_bx);
		const double dy = static_cast<double>(a_ay) - static_cast<double>(a_by);
		const double dz = static_cast<double>(a_az) - static_cast<double>(a_bz);
		return static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
	}

	std::optional<CampPlacement> ResolvePlacement(
		const PlayerLocation& a_player,
		std::string_view a_plugin,
		const std::vector<Workshop::FormKey>& a_knownSpaces,
		float a_x,
		float a_y,
		float a_z)
	{
		const auto worldspace = ParseLocalFormId(a_player.worldspaceId);
		const auto cell = ParseLocalFormId(a_player.cellId);
		if (!worldspace || !cell || a_plugin.empty() || a_plugin.size() > Workshop::MaximumPluginNameBytes ||
			!CoordinatesInBounds(a_x, a_y, a_z) ||
			!(std::fabs(a_player.x) <= MaximumCoordinateMagnitude && std::fabs(a_player.y) <= MaximumCoordinateMagnitude &&
				std::fabs(a_player.z) <= MaximumCoordinateMagnitude)) {
			return std::nullopt;
		}
		const auto offset = Distance(
			a_x,
			a_y,
			a_z,
			static_cast<float>(a_player.x),
			static_cast<float>(a_player.y),
			static_cast<float>(a_player.z));
		if (!(offset <= MaximumPlacementOffsetUnits)) {
			return std::nullopt;
		}
		const auto plugin = PluginForWorldspace(*worldspace, a_plugin, a_knownSpaces);
		if (!plugin) {
			return std::nullopt;
		}
		return CampPlacement{
			Workshop::FormKey{ *plugin, *worldspace },
			Workshop::FormKey{ *plugin, *cell },
			static_cast<float>(a_player.x),
			static_cast<float>(a_player.y),
			static_cast<float>(a_player.z)
		};
	}

	FoundStatus ValidateFound(
		const FoundRequest& a_request,
		const std::vector<CampRecord>& a_camps,
		const std::vector<Workshop::BuildZonePolicy>& a_settlements,
		bool a_inNoBuildZone,
		Workshop::BuildZoneId a_excludeZoneId, SeparationDetail* a_detail)
	{
		if (a_request.ownerProfileId.empty() ||
			a_request.space.plugin.empty() ||
			a_request.space.localFormId == 0 ||
			!CoordinatesInBounds(a_request.x, a_request.y, a_request.z) ||
			!std::isfinite(a_request.probe.height) ||
			!std::isfinite(a_request.probe.slopeDegrees) ||
			a_request.probe.slopeDegrees < 0.0F) {
			return FoundStatus::InvalidRequest;
		}
		if (a_inNoBuildZone) {
			return FoundStatus::NoBuildZone;
		}
		if (!a_request.probe.navmeshHit || a_request.probe.slopeDegrees > MaximumSlopeDegrees ||
			std::fabs(a_request.z - a_request.probe.height) > MaximumProbeDropUnits) {
			return FoundStatus::Terrain;
		}
		std::size_t records = 0;
		std::size_t active = 0;
		std::size_t owned = 0;
		std::size_t densityPeers = 0;
		const auto cellX = static_cast<std::int64_t>(std::floor(static_cast<double>(a_request.x) / static_cast<double>(DensityCellUnits)));
		const auto cellY = static_cast<std::int64_t>(std::floor(static_cast<double>(a_request.y) / static_cast<double>(DensityCellUnits)));
		for (const auto& camp : a_camps) {
			if (camp.buildZoneId == a_excludeZoneId) {
				if (MovedRecently(camp, a_request.nowUnix)) {
					return FoundStatus::Cooldown;
				}
				continue;
			}
			++records;
			if (camp.ownerProfileId == a_request.ownerProfileId) {
				++owned;
			}
			if (camp.stored) {
				continue;
			}
			++active;
			const float campDistance = Distance(a_request.x, a_request.y, a_request.z, camp.centerX, camp.centerY, camp.centerZ);
			if (campDistance < MinimumCampSeparationUnits) {
				if (a_detail) {
					*a_detail = SeparationDetail{ WholeMetersDown(campDistance), WholeMetersUp(MinimumCampSeparationUnits), {} };
				}
				return FoundStatus::TooCloseToCamp;
			}
			const auto otherX = static_cast<std::int64_t>(std::floor(static_cast<double>(camp.centerX) / static_cast<double>(DensityCellUnits)));
			const auto otherY = static_cast<std::int64_t>(std::floor(static_cast<double>(camp.centerY) / static_cast<double>(DensityCellUnits)));
			if (otherX == cellX && otherY == cellY) {
				++densityPeers;
			}
		}
		if (records >= MaximumCampRecords || active >= MaximumCampsPerServer) {
			return FoundStatus::Limit;
		}
		if (owned >= MaximumCampsPerProfile) {
			return FoundStatus::LimitProfile;
		}
		if (densityPeers >= MaximumCampsPerDensityCell) {
			return FoundStatus::LimitArea;
		}
		for (const auto& settlement : a_settlements) {
			if (IsCampZoneId(settlement.buildZoneId) || settlement.buildZoneId == a_excludeZoneId) {
				continue;
			}
			const float workbenchX = settlement.hasWorkbench ? settlement.workbenchX : settlement.centerX;
			const float workbenchY = settlement.hasWorkbench ? settlement.workbenchY : settlement.centerY;
			const float workbenchZ = settlement.hasWorkbench ? settlement.workbenchZ : settlement.centerZ;
			const float settlementDistance = Distance(a_request.x, a_request.y, a_request.z, workbenchX, workbenchY, workbenchZ);
			if (settlementDistance < MinimumSettlementSeparationUnits) {
				if (a_detail) {
					*a_detail = SeparationDetail{ WholeMetersDown(settlementDistance), WholeMetersUp(MinimumSettlementSeparationUnits), settlement.name };
				}
				return FoundStatus::TooCloseToSettlement;
			}
		}
		return FoundStatus::Ok;
	}

	FoundStatus ValidateMove(
		const MoveRequest& a_request,
		const CampRecord& a_camp,
		const std::vector<CampRecord>& a_camps,
		const std::vector<Workshop::BuildZonePolicy>& a_settlements,
		bool a_inNoBuildZone,
		bool a_underAttack, SeparationDetail* a_detail)
	{
		if (a_camp.buildZoneId != a_request.buildZoneId) {
			return FoundStatus::NotFound;
		}
		if (a_camp.ownerProfileId != a_request.ownerProfileId) {
			return FoundStatus::NotOwner;
		}
		if (a_camp.stored) {
			return FoundStatus::NotFound;
		}
		if (a_underAttack) {
			return FoundStatus::UnderAttack;
		}
		if (MovedRecently(a_camp, a_request.nowUnix)) {
			return FoundStatus::Cooldown;
		}
		FoundRequest found;
		found.ownerProfileId = a_request.ownerProfileId;
		found.operationId = a_request.operationId;
		found.space = a_request.space.plugin.empty() ? a_camp.space : a_request.space;
		found.x = a_request.x;
		found.y = a_request.y;
		found.z = a_request.z;
		found.probe = a_request.probe;
		found.nowUnix = a_request.nowUnix;
		return ValidateFound(found, a_camps, a_settlements, a_inNoBuildZone, a_request.buildZoneId, a_detail);
	}

	Workshop::BuildZonePolicy MakeCampPolicy(const CampRecord& a_camp) noexcept
	{
		Workshop::BuildZonePolicy policy;
		policy.buildZoneId = a_camp.buildZoneId;
		policy.space = a_camp.space;
		policy.centerX = a_camp.centerX;
		policy.centerY = a_camp.centerY;
		policy.centerZ = a_camp.centerZ;
		policy.radius = a_camp.radius > 0.0F ? a_camp.radius : DefaultCampRadiusUnits;
		policy.maxScale = 1.0F;
		policy.maxObjects = a_camp.maxObjects == 0 ? DefaultCampMaxObjects : a_camp.maxObjects;
		policy.hasWorkbench = true;
		policy.workbenchX = a_camp.centerX;
		policy.workbenchY = a_camp.centerY;
		policy.workbenchZ = a_camp.centerZ;
		policy.captureRadius = 0.0F;
		return policy;
	}

	std::string_view FoundStatusName(FoundStatus a_status) noexcept
	{
		switch (a_status) {
		case FoundStatus::Ok:
			return "ok";
		case FoundStatus::InvalidRequest:
			return "invalidRequest";
		case FoundStatus::TooCloseToCamp:
			return "tooCloseCamp";
		case FoundStatus::TooCloseToSettlement:
			return "tooCloseSettlement";
		case FoundStatus::NoBuildZone:
			return "noBuildZone";
		case FoundStatus::Terrain:
			return "terrain";
		case FoundStatus::Limit:
			return "limit";
		case FoundStatus::LimitProfile:
			return "limitProfile";
		case FoundStatus::LimitArea:
			return "limitArea";
		case FoundStatus::UnderAttack:
			return "underAttack";
		case FoundStatus::Cooldown:
			return "cooldown";
		case FoundStatus::NotOwner:
			return "notOwner";
		case FoundStatus::NotFound:
			return "notFound";
		case FoundStatus::DropLimit:
			return "dropLimit";
		case FoundStatus::GrantFailed:
			return "grantFailed";
		case FoundStatus::RefundUnavailable:
			return "refundUnavailable";
		}
		return "invalidRequest";
	}

	std::optional<FoundRequest> NormalizeCampFound(const Json& a_packet, std::string_view a_ownerProfileId)
	{
		if (!a_packet.is_object() || a_ownerProfileId.empty()) {
			return std::nullopt;
		}
		const auto type = a_packet.find("type");
		if (type == a_packet.end() || !type->is_string() || type->get<std::string>() != "campFound") {
			return std::nullopt;
		}
		const auto operation = a_packet.find("operationId");
		const auto space = a_packet.find("space");
		const auto x = a_packet.find("x");
		const auto y = a_packet.find("y");
		const auto z = a_packet.find("z");
		const auto probe = a_packet.find("probe");
		const auto dropEntity = a_packet.find("dropEntityId");
		if (operation == a_packet.end() || space == a_packet.end() || x == a_packet.end() || y == a_packet.end() ||
			z == a_packet.end() || probe == a_packet.end() || !probe->is_object() || dropEntity == a_packet.end()) {
			return std::nullopt;
		}
		const auto operationId = ReadU64(*operation);
		if (!operationId || *operationId == 0) {
			return std::nullopt;
		}
		const auto dropEntityId = ReadU64(*dropEntity);
		if (!dropEntityId || *dropEntityId < F4T::Drops::kFirstDroppedEntityId || *dropEntityId > std::numeric_limits<std::uint32_t>::max()) {
			return std::nullopt;
		}
		const auto form = ReadForm(*space);
		const auto xValue = ReadFloat(*x);
		const auto yValue = ReadFloat(*y);
		const auto zValue = ReadFloat(*z);
		const auto height = probe->find("height");
		const auto slope = probe->find("slopeDegrees");
		const auto navmesh = probe->find("navmeshHit");
		if (!form || !xValue || !yValue || !zValue || height == probe->end() || slope == probe->end() ||
			navmesh == probe->end() || !navmesh->is_boolean()) {
			return std::nullopt;
		}
		const auto heightValue = ReadFloat(*height);
		const auto slopeValue = ReadFloat(*slope);
		if (!heightValue || !slopeValue) {
			return std::nullopt;
		}
		FoundRequest request;
		request.ownerProfileId = std::string{ a_ownerProfileId };
		request.operationId = *operationId;
		request.space = *form;
		request.x = *xValue;
		request.y = *yValue;
		request.z = *zValue;
		request.probe.height = *heightValue;
		request.probe.slopeDegrees = *slopeValue;
		request.probe.navmeshHit = navmesh->get<bool>();
		request.dropEntityId = static_cast<std::uint32_t>(*dropEntityId);
		return request;
	}

	FoundStatus CheckCampKitDrop(const Json& a_drop, std::string_view a_profileId)
	{
		if (a_profileId.empty() || !a_drop.is_object() || !a_drop.contains("ownerProfileId") || !a_drop.at("ownerProfileId").is_string() ||
			a_drop.at("ownerProfileId").get_ref<const std::string&>() != a_profileId ||
			!a_drop.contains("itemKey") || !a_drop.at("itemKey").is_string() ||
			a_drop.at("itemKey").get_ref<const std::string&>() != CampKitItemKey) {
			return FoundStatus::NotFound;
		}
		const auto position = a_drop.find("position");
		if (!a_drop.contains("item") || !a_drop.at("item").is_object() || a_drop.at("item").value("count", 0) != 1 ||
			position == a_drop.end() || !position->is_object() || position->value("worldspaceId", std::string{}).empty()) {
			return FoundStatus::NotFound;
		}
		return FoundStatus::Ok;
	}

	Json MakeCampCreated(const CampRecord& a_camp, std::uint64_t a_operationId)
	{
		return Json{
			{ "type", "campCreated" },
			{ "operationId", a_operationId },
			{ "buildZoneId", a_camp.buildZoneId },
			{ "ownerProfileId", a_camp.ownerProfileId },
			{ "space", FormJson(a_camp.space) },
			{ "centerX", a_camp.centerX },
			{ "centerY", a_camp.centerY },
			{ "centerZ", a_camp.centerZ },
			{ "radius", a_camp.radius }
		};
	}

	Json MakeCampMoved(const CampRecord& a_camp, std::uint64_t a_operationId)
	{
		auto packet = MakeCampCreated(a_camp, a_operationId);
		packet["type"] = "campMoved";
		return packet;
	}

	Json MakeCampStored(Workshop::BuildZoneId a_buildZoneId, std::uint64_t a_operationId)
	{
		return Json{
			{ "type", "campStored" },
			{ "operationId", a_operationId },
			{ "buildZoneId", a_buildZoneId }
		};
	}

	Json MakeCampRemoved(Workshop::BuildZoneId a_buildZoneId, std::uint64_t a_operationId)
	{
		return Json{
			{ "type", "campRemoved" },
			{ "operationId", a_operationId },
			{ "buildZoneId", a_buildZoneId }
		};
	}

	Json MakeCampList(const std::vector<CampRecord>& a_camps)
	{
		Json camps = Json::array();
		for (const auto& camp : a_camps) {
			if (camp.stored) {
				continue;
			}
			camps.push_back(Json{
				{ "buildZoneId", camp.buildZoneId },
				{ "centerX", camp.centerX },
				{ "centerY", camp.centerY },
				{ "centerZ", camp.centerZ },
				{ "radius", camp.radius },
				{ "ownerProfileId", camp.ownerProfileId },
				{ "space", FormJson(camp.space) }
			});
		}
		return Json{ { "type", "campList" }, { "camps", std::move(camps) } };
	}

	Json MakeCampRefund(
		Workshop::BuildZoneId a_buildZoneId,
		std::uint64_t a_operationId,
		std::size_t a_removedPieces,
		std::size_t a_unpricedPieces,
		const std::vector<Trade::Line>& a_granted)
	{
		Json items = Json::array();
		for (const auto& line : a_granted) {
			items.push_back(Json{ { "itemKey", line.itemKey }, { "count", line.count } });
		}
		return Json{
			{ "type", "campRefund" },
			{ "operationId", a_operationId },
			{ "buildZoneId", a_buildZoneId },
			{ "removedPieces", a_removedPieces },
			{ "unpricedPieces", a_unpricedPieces },
			{ "items", std::move(items) }
		};
	}

	Json MakeCampRejected(std::uint64_t a_operationId, FoundStatus a_status, const SeparationDetail& a_detail)
	{
		auto packet = Json{
			{ "type", "campRejected" },
			{ "operationId", a_operationId },
			{ "status", FoundStatusName(a_status) }
		};
		if (a_detail.requiredMeters > 0) {
			packet["distanceMeters"] = a_detail.distanceMeters;
			packet["requiredMeters"] = a_detail.requiredMeters;
		}
		if (!a_detail.subjectName.empty()) {
			packet["subjectName"] = a_detail.subjectName;
		}
		return packet;
	}

	std::optional<MoveRequest> NormalizeCampMove(const Json& a_packet, std::string_view a_ownerProfileId)
	{
		if (!a_packet.is_object() || a_ownerProfileId.empty()) {
			return std::nullopt;
		}
		const auto type = a_packet.find("type");
		if (type == a_packet.end() || !type->is_string() || type->get<std::string>() != "campMove") {
			return std::nullopt;
		}
		const auto operation = a_packet.find("operationId");
		const auto zone = a_packet.find("buildZoneId");
		const auto x = a_packet.find("x");
		const auto y = a_packet.find("y");
		const auto z = a_packet.find("z");
		const auto probe = a_packet.find("probe");
		if (operation == a_packet.end() || zone == a_packet.end() || x == a_packet.end() || y == a_packet.end() ||
			z == a_packet.end() || probe == a_packet.end() || !probe->is_object()) {
			return std::nullopt;
		}
		const auto operationId = ReadU64(*operation);
		const auto buildZoneId = ReadU64(*zone);
		const auto xValue = ReadFloat(*x);
		const auto yValue = ReadFloat(*y);
		const auto zValue = ReadFloat(*z);
		const auto height = probe->find("height");
		const auto slope = probe->find("slopeDegrees");
		const auto navmesh = probe->find("navmeshHit");
		if (!operationId || *operationId == 0 || !buildZoneId || !IsCampZoneId(*buildZoneId) ||
			!xValue || !yValue || !zValue || height == probe->end() || slope == probe->end() ||
			navmesh == probe->end() || !navmesh->is_boolean()) {
			return std::nullopt;
		}
		const auto heightValue = ReadFloat(*height);
		const auto slopeValue = ReadFloat(*slope);
		if (!heightValue || !slopeValue) {
			return std::nullopt;
		}
		MoveRequest request;
		request.ownerProfileId = std::string{ a_ownerProfileId };
		request.operationId = *operationId;
		request.buildZoneId = *buildZoneId;
		request.x = *xValue;
		request.y = *yValue;
		request.z = *zValue;
		request.probe.height = *heightValue;
		request.probe.slopeDegrees = *slopeValue;
		request.probe.navmeshHit = navmesh->get<bool>();
		if (const auto space = a_packet.find("space"); space != a_packet.end()) {
			const auto form = ReadForm(*space);
			if (!form) {
				return std::nullopt;
			}
			request.space = *form;
		}
		return request;
	}

	std::optional<ZoneRequest> NormalizeCampZoneOp(
		const Json& a_packet,
		std::string_view a_ownerProfileId,
		std::string_view a_type)
	{
		if (!a_packet.is_object() || a_ownerProfileId.empty() || a_type.empty()) {
			return std::nullopt;
		}
		const auto type = a_packet.find("type");
		if (type == a_packet.end() || !type->is_string() || type->get<std::string>() != a_type) {
			return std::nullopt;
		}
		const auto operation = a_packet.find("operationId");
		const auto zone = a_packet.find("buildZoneId");
		if (operation == a_packet.end() || zone == a_packet.end()) {
			return std::nullopt;
		}
		const auto operationId = ReadU64(*operation);
		const auto buildZoneId = ReadU64(*zone);
		if (!operationId || *operationId == 0 || !buildZoneId || !IsCampZoneId(*buildZoneId)) {
			return std::nullopt;
		}
		ZoneRequest request;
		request.ownerProfileId = std::string{ a_ownerProfileId };
		request.operationId = *operationId;
		request.buildZoneId = *buildZoneId;
		return request;
	}

	Json CampRecordJson(const CampRecord& a_camp)
	{
		return Json{
			{ "buildZoneId", a_camp.buildZoneId },
			{ "ownerProfileId", a_camp.ownerProfileId },
			{ "space", FormJson(a_camp.space) },
			{ "centerX", a_camp.centerX },
			{ "centerY", a_camp.centerY },
			{ "centerZ", a_camp.centerZ },
			{ "radius", a_camp.radius },
			{ "maxObjects", a_camp.maxObjects },
			{ "foundedAtUnix", a_camp.foundedAtUnix },
			{ "lastMovedAtUnix", a_camp.lastMovedAtUnix },
			{ "stored", a_camp.stored }
		};
	}

	std::optional<CampRecord> ReadCampRecord(const Json& a_value)
	{
		if (!a_value.is_object()) {
			return std::nullopt;
		}
		const auto id = a_value.find("buildZoneId");
		const auto owner = a_value.find("ownerProfileId");
		const auto space = a_value.find("space");
		const auto x = a_value.find("centerX");
		const auto y = a_value.find("centerY");
		const auto z = a_value.find("centerZ");
		if (id == a_value.end() || owner == a_value.end() || space == a_value.end() ||
			x == a_value.end() || y == a_value.end() || z == a_value.end() || !owner->is_string()) {
			return std::nullopt;
		}
		const auto buildZoneId = ReadU64(*id);
		const auto form = ReadForm(*space);
		const auto xValue = ReadFloat(*x);
		const auto yValue = ReadFloat(*y);
		const auto zValue = ReadFloat(*z);
		if (!buildZoneId || !IsCampZoneId(*buildZoneId) || !form || !xValue || !yValue || !zValue) {
			return std::nullopt;
		}
		CampRecord camp;
		camp.buildZoneId = *buildZoneId;
		camp.ownerProfileId = owner->get<std::string>();
		camp.space = *form;
		camp.centerX = *xValue;
		camp.centerY = *yValue;
		camp.centerZ = *zValue;
		if (const auto radius = a_value.find("radius"); radius != a_value.end()) {
			const auto value = ReadFloat(*radius);
			if (!value) {
				return std::nullopt;
			}
			camp.radius = *value < UnitsPerMeter ? DefaultCampRadiusUnits : *value;
		}
		if (const auto maxObjects = a_value.find("maxObjects"); maxObjects != a_value.end()) {
			const auto value = ReadU32(*maxObjects);
			if (!value) {
				return std::nullopt;
			}
			camp.maxObjects = *value;
		}
		if (const auto founded = a_value.find("foundedAtUnix"); founded != a_value.end()) {
			const auto value = ReadU64(*founded);
			if (!value) {
				return std::nullopt;
			}
			camp.foundedAtUnix = *value;
		}
		if (const auto moved = a_value.find("lastMovedAtUnix"); moved != a_value.end()) {
			const auto value = ReadU64(*moved);
			if (!value) {
				return std::nullopt;
			}
			camp.lastMovedAtUnix = *value;
		}
		if (const auto stored = a_value.find("stored"); stored != a_value.end()) {
			if (!stored->is_boolean()) {
				return std::nullopt;
			}
			camp.stored = stored->get<bool>();
		}
		return camp;
	}
}

#include "F4TWorkshopProtocol.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace
{
	using Json = nlohmann::json;
	using namespace F4T::Workshop;

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

	std::optional<FormKey> ReadForm(const Json& a_value)
	{
		if (!a_value.is_object()) {
			return std::nullopt;
		}
		const auto plugin = a_value.find("plugin");
		const auto localFormId = a_value.find("localFormId");
		if (plugin == a_value.end() ||
			localFormId == a_value.end() ||
			!plugin->is_string()) {
			return std::nullopt;
		}
		auto name = plugin->get<std::string>();
		const auto id = ReadU32(*localFormId);
		if (name.empty() || name.size() > MaximumPluginNameBytes || !id || *id == 0) {
			return std::nullopt;
		}
		return FormKey{ std::move(name), *id };
	}

	std::optional<Transform> ReadTransform(const Json& a_value)
	{
		if (!a_value.is_object()) {
			return std::nullopt;
		}
		const char* names[]{ "x", "y", "z", "pitch", "yaw", "roll", "scale" };
		float values[7]{};
		for (std::size_t i = 0; i < 7; ++i) {
			const auto found = a_value.find(names[i]);
			if (found == a_value.end()) {
				return std::nullopt;
			}
			const auto value = ReadFloat(*found);
			if (!value) {
				return std::nullopt;
			}
			values[i] = *value;
		}
		if (!(values[6] > 0.0F)) {
			return std::nullopt;
		}
		return Transform{
			values[0],
			values[1],
			values[2],
			values[3],
			values[4],
			values[5],
			values[6]
		};
	}

	std::optional<OperationKind> ReadKind(const Json& a_value)
	{
		if (!a_value.is_string()) {
			return std::nullopt;
		}
		const auto kind = a_value.get<std::string>();
		if (kind == "place") {
			return OperationKind::Place;
		}
		if (kind == "move") {
			return OperationKind::Move;
		}
		if (kind == "scrap") {
			return OperationKind::Scrap;
		}
		if (kind == "scrapWorld") {
			return OperationKind::ScrapWorld;
		}
		return std::nullopt;
	}

	Json FormJson(const FormKey& a_form)
	{
		return Json{
			{ "plugin", a_form.plugin },
			{ "localFormId", a_form.localFormId }
		};
	}

	Json TransformJson(const Transform& a_transform)
	{
		return Json{
			{ "x", a_transform.x },
			{ "y", a_transform.y },
			{ "z", a_transform.z },
			{ "pitch", a_transform.pitch },
			{ "yaw", a_transform.yaw },
			{ "roll", a_transform.roll },
			{ "scale", a_transform.scale }
		};
	}

	Json ObjectJson(const ObjectState& a_object)
	{
		return Json{
			{ "objectId", a_object.objectId },
			{ "form", FormJson(a_object.form) },
			{ "space", FormJson(a_object.space) },
			{ "transform", TransformJson(a_object.transform) }
		};
	}

	std::optional<ObjectState> ReadObjectState(const Json& a_value, BuildZoneId a_buildZoneId)
	{
		if (!a_value.is_object()) {
			return std::nullopt;
		}
		const auto objectIt = a_value.find("objectId");
		const auto formIt = a_value.find("form");
		const auto spaceIt = a_value.find("space");
		const auto transformIt = a_value.find("transform");
		if (objectIt == a_value.end() || formIt == a_value.end() ||
			spaceIt == a_value.end() || transformIt == a_value.end()) {
			return std::nullopt;
		}
		const auto objectId = ReadU64(*objectIt);
		const auto form = ReadForm(*formIt);
		const auto space = ReadForm(*spaceIt);
		const auto transform = ReadTransform(*transformIt);
		if (!objectId || *objectId == 0 || !form || !space || !transform) {
			return std::nullopt;
		}
		return ObjectState{ *objectId, a_buildZoneId, *form, *space, *transform };
	}

	std::string_view KindName(OperationKind a_kind) noexcept
	{
		switch (a_kind) {
		case OperationKind::Place:
			return "place";
		case OperationKind::Move:
			return "move";
		case OperationKind::Scrap:
			return "scrap";
		case OperationKind::ScrapWorld:
			return "scrapWorld";
		}
		return "invalid";
	}

	std::optional<ApplyStatus> ReadStatus(const Json& a_value)
	{
		if (!a_value.is_string()) {
			return std::nullopt;
		}
		const auto status = a_value.get<std::string>();
		if (status == "staleRevision") return ApplyStatus::StaleRevision;
		if (status == "invalidRequest") return ApplyStatus::InvalidRequest;
		if (status == "unknownBuildZone") return ApplyStatus::UnknownBuildZone;
		if (status == "objectNotFound") return ApplyStatus::ObjectNotFound;
		if (status == "outsideBuildZone") return ApplyStatus::OutsideBuildZone;
		if (status == "budgetExceeded") return ApplyStatus::BudgetExceeded;
		if (status == "objectIdExhausted") return ApplyStatus::ObjectIdExhausted;
		if (status == "revisionExhausted") return ApplyStatus::RevisionExhausted;
		if (status == "notAuthorized") return ApplyStatus::NotAuthorized;
		if (status == "alreadyRemoved") return ApplyStatus::AlreadyRemoved;
		return std::nullopt;
	}
}

namespace F4T::WorkshopProtocol
{
	std::optional<nlohmann::json> MakeOperation(const Workshop::Request& a_request)
	{
		using namespace Workshop;
		Json packet{
			{ "type", "workshopOp" },
			{ "operationId", a_request.operationId },
			{ "expectedRevision", a_request.expectedRevision },
			{ "buildZoneId", a_request.buildZoneId },
			{ "kind", KindName(a_request.kind) }
		};

		switch (a_request.kind) {
		case OperationKind::Place:
		case OperationKind::ScrapWorld:
			packet["form"] = FormJson(a_request.form);
			packet["space"] = FormJson(a_request.space);
			packet["transform"] = TransformJson(a_request.transform);
			break;
		case OperationKind::Move:
			packet["objectId"] = a_request.objectId;
			packet["space"] = FormJson(a_request.space);
			packet["transform"] = TransformJson(a_request.transform);
			break;
		case OperationKind::Scrap:
			packet["objectId"] = a_request.objectId;
			break;
		default:
			return std::nullopt;
		}

		if (!NormalizeOperation(packet, 1)) {
			return std::nullopt;
		}
		return packet;
	}

	std::optional<Commit> NormalizeCommit(const nlohmann::json& a_packet)
	{
		using namespace Workshop;
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto type = a_packet.find("type");
		const auto player = a_packet.find("playerId");
		const auto operation = a_packet.find("operationId");
		const auto zone = a_packet.find("buildZoneId");
		const auto revision = a_packet.find("revision");
		const auto kindIt = a_packet.find("kind");
		const auto object = a_packet.find("objectId");
		if (type == a_packet.end() || !type->is_string() || type->get<std::string>() != "workshopCommit" ||
			player == a_packet.end() || operation == a_packet.end() || zone == a_packet.end() ||
			revision == a_packet.end() || kindIt == a_packet.end() || object == a_packet.end()) {
			return std::nullopt;
		}

		const auto playerId = ReadU32(*player);
		const auto operationId = ReadU64(*operation);
		const auto buildZoneId = ReadU64(*zone);
		const auto commitRevision = ReadU64(*revision);
		const auto kind = ReadKind(*kindIt);
		const auto objectId = ReadU64(*object);
		if (!playerId || *playerId == 0 || !operationId || *operationId == 0 ||
			!buildZoneId || *buildZoneId == 0 || !commitRevision || *commitRevision == 0 ||
			!kind || !objectId || *objectId == 0) {
			return std::nullopt;
		}

		Commit commit{
			*playerId,
			*buildZoneId,
			*operationId,
			*commitRevision,
			*kind,
			*objectId,
			std::nullopt
		};
		if (*kind == OperationKind::Scrap) {
			if (a_packet.contains("form") || a_packet.contains("space") || a_packet.contains("transform")) {
				return std::nullopt;
			}
			return commit;
		}

		const auto formIt = a_packet.find("form");
		const auto spaceIt = a_packet.find("space");
		const auto transformIt = a_packet.find("transform");
		if (formIt == a_packet.end() || spaceIt == a_packet.end() || transformIt == a_packet.end()) {
			return std::nullopt;
		}
		const auto form = ReadForm(*formIt);
		const auto space = ReadForm(*spaceIt);
		const auto transform = ReadTransform(*transformIt);
		if (!form || !space || !transform) {
			return std::nullopt;
		}
		commit.object = ObjectState{
			*objectId,
			*buildZoneId,
			*form,
			*space,
			*transform
		};
		return commit;
	}

	std::optional<Reject> NormalizeReject(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto type = a_packet.find("type");
		const auto operation = a_packet.find("operationId");
		const auto zone = a_packet.find("buildZoneId");
		const auto revision = a_packet.find("revision");
		const auto statusIt = a_packet.find("status");
		if (type == a_packet.end() || !type->is_string() || type->get<std::string>() != "workshopReject" ||
			operation == a_packet.end() || zone == a_packet.end() ||
			revision == a_packet.end() || statusIt == a_packet.end()) {
			return std::nullopt;
		}

		const auto operationId = ReadU64(*operation);
		const auto buildZoneId = ReadU64(*zone);
		const auto rejectRevision = ReadU64(*revision);
		const auto status = ReadStatus(*statusIt);
		if (!operationId || *operationId == 0 ||
			!buildZoneId || *buildZoneId == 0 ||
			!rejectRevision || !status) {
			return std::nullopt;
		}
		return Reject{ *buildZoneId, *operationId, *rejectRevision, *status };
	}

	std::optional<Workshop::Request> NormalizeOperation(
		const nlohmann::json& a_packet,
		Workshop::PlayerId a_playerId)
	{
		using namespace Workshop;
		if (a_playerId == 0 || !a_packet.is_object()) {
			return std::nullopt;
		}
		const auto typeIt = a_packet.find("type");
		if (typeIt == a_packet.end() || !typeIt->is_string() || typeIt->get<std::string>() != "workshopOp") {
			return std::nullopt;
		}

		const auto operationIt = a_packet.find("operationId");
		const auto revisionIt = a_packet.find("expectedRevision");
		const auto zoneIt = a_packet.find("buildZoneId");
		const auto kindIt = a_packet.find("kind");
		if (operationIt == a_packet.end() ||
			revisionIt == a_packet.end() ||
			zoneIt == a_packet.end() ||
			kindIt == a_packet.end()) {
			return std::nullopt;
		}

		const auto operationId = ReadU64(*operationIt);
		const auto expectedRevision = ReadU64(*revisionIt);
		const auto buildZoneId = ReadU64(*zoneIt);
		const auto kind = ReadKind(*kindIt);
		if (!operationId || *operationId == 0 ||
			!expectedRevision ||
			!buildZoneId || *buildZoneId == 0 ||
			!kind) {
			return std::nullopt;
		}

		Request request;
		request.playerId = a_playerId;
		request.operationId = *operationId;
		request.expectedRevision = *expectedRevision;
		request.buildZoneId = *buildZoneId;
		request.kind = *kind;

		if (*kind == OperationKind::Place || *kind == OperationKind::ScrapWorld) {
			const auto formIt = a_packet.find("form");
			const auto spaceIt = a_packet.find("space");
			const auto transformIt = a_packet.find("transform");
			if (formIt == a_packet.end() || spaceIt == a_packet.end() || transformIt == a_packet.end()) {
				return std::nullopt;
			}
			const auto form = ReadForm(*formIt);
			const auto space = ReadForm(*spaceIt);
			const auto transform = ReadTransform(*transformIt);
			if (!form || !space || !transform) {
				return std::nullopt;
			}
			request.form = *form;
			request.space = *space;
			request.transform = *transform;
			return request;
		}

		const auto objectIt = a_packet.find("objectId");
		if (objectIt == a_packet.end()) {
			return std::nullopt;
		}
		const auto objectId = ReadU64(*objectIt);
		if (!objectId || *objectId == 0) {
			return std::nullopt;
		}
		request.objectId = *objectId;

		if (*kind == OperationKind::Move) {
			const auto spaceIt = a_packet.find("space");
			const auto transformIt = a_packet.find("transform");
			if (spaceIt == a_packet.end() || transformIt == a_packet.end()) {
				return std::nullopt;
			}
			const auto space = ReadForm(*spaceIt);
			const auto transform = ReadTransform(*transformIt);
			if (!space || !transform) {
				return std::nullopt;
			}
			request.space = *space;
			request.transform = *transform;
		}
		return request;
	}

	std::optional<nlohmann::json> MakeCommit(
		const Workshop::Request& a_request,
		const Workshop::Result& a_result)
	{
		using namespace Workshop;
		if (a_result.status != ApplyStatus::Committed ||
			a_request.playerId == 0 ||
			a_request.buildZoneId == 0 ||
			a_request.operationId == 0 ||
			a_result.objectId == 0) {
			return std::nullopt;
		}

		Json packet{
			{ "type", "workshopCommit" },
			{ "playerId", a_request.playerId },
			{ "operationId", a_request.operationId },
			{ "buildZoneId", a_request.buildZoneId },
			{ "revision", a_result.revision },
			{ "kind", KindName(a_request.kind) },
			{ "objectId", a_result.objectId }
		};

		if (a_request.kind != OperationKind::Scrap) {
			if (!a_result.object ||
				a_result.object->objectId != a_result.objectId ||
				a_result.object->buildZoneId != a_request.buildZoneId) {
				return std::nullopt;
			}
			packet["form"] = FormJson(a_result.object->form);
			packet["space"] = FormJson(a_result.object->space);
			packet["transform"] = TransformJson(a_result.object->transform);
		}
		return packet;
	}

	nlohmann::json MakeReject(
		const Workshop::Request& a_request,
		const Workshop::Result& a_result)
	{
		return Json{
			{ "type", "workshopReject" },
			{ "operationId", a_request.operationId },
			{ "buildZoneId", a_request.buildZoneId },
			{ "revision", a_result.revision },
			{ "status", StatusName(a_result.status) }
		};
	}

	std::string_view StatusName(Workshop::ApplyStatus a_status) noexcept
	{
		using Workshop::ApplyStatus;
		switch (a_status) {
		case ApplyStatus::Committed:
			return "committed";
		case ApplyStatus::StaleRevision:
			return "staleRevision";
		case ApplyStatus::InvalidRequest:
			return "invalidRequest";
		case ApplyStatus::UnknownBuildZone:
			return "unknownBuildZone";
		case ApplyStatus::ObjectNotFound:
			return "objectNotFound";
		case ApplyStatus::OutsideBuildZone:
			return "outsideBuildZone";
		case ApplyStatus::BudgetExceeded:
			return "budgetExceeded";
		case ApplyStatus::ObjectIdExhausted:
			return "objectIdExhausted";
		case ApplyStatus::RevisionExhausted:
			return "revisionExhausted";
		case ApplyStatus::NotAuthorized:
			return "notAuthorized";
		case ApplyStatus::AlreadyRemoved:
			return "alreadyRemoved";
		}
		return "invalidRequest";
	}

	std::vector<nlohmann::json> MakeSnapshotPackets(
		Workshop::BuildZoneId a_buildZoneId,
		Workshop::Revision a_revision,
		const std::vector<Workshop::ObjectState>& a_objects,
		const std::vector<Workshop::ObjectState>& a_tombstones)
	{
		std::vector<Json> packets;
		std::size_t objectIndex = 0;
		std::size_t tombstoneIndex = 0;
		do {
			Json packet{
				{ "type", "workshopSnapshot" },
				{ "buildZoneId", a_buildZoneId },
				{ "revision", a_revision },
				{ "objects", Json::array() },
				{ "tombstones", Json::array() },
				{ "complete", false }
			};
			std::size_t budget = MaximumSnapshotEntriesPerFrame;
			for (; objectIndex < a_objects.size() && budget > 0; ++objectIndex, --budget) {
				packet["objects"].push_back(ObjectJson(a_objects[objectIndex]));
			}
			for (; tombstoneIndex < a_tombstones.size() && budget > 0; ++tombstoneIndex, --budget) {
				packet["tombstones"].push_back(ObjectJson(a_tombstones[tombstoneIndex]));
			}
			packets.push_back(std::move(packet));
		} while (objectIndex < a_objects.size() || tombstoneIndex < a_tombstones.size());
		packets.back()["complete"] = true;
		return packets;
	}

	std::optional<Snapshot> NormalizeSnapshot(const nlohmann::json& a_packet)
	{
		using namespace Workshop;
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto type = a_packet.find("type");
		const auto zone = a_packet.find("buildZoneId");
		const auto revision = a_packet.find("revision");
		const auto objects = a_packet.find("objects");
		const auto tombstones = a_packet.find("tombstones");
		const auto complete = a_packet.find("complete");
		if (type == a_packet.end() || !type->is_string() || type->get<std::string>() != "workshopSnapshot" ||
			zone == a_packet.end() || revision == a_packet.end() ||
			objects == a_packet.end() || tombstones == a_packet.end() || complete == a_packet.end() ||
			!objects->is_array() || !tombstones->is_array() || !complete->is_boolean() ||
			objects->size() + tombstones->size() > MaximumSnapshotEntriesPerFrame) {
			return std::nullopt;
		}

		const auto buildZoneId = ReadU64(*zone);
		const auto snapshotRevision = ReadU64(*revision);
		if (!buildZoneId || *buildZoneId == 0 || !snapshotRevision) {
			return std::nullopt;
		}

		Snapshot snapshot;
		snapshot.buildZoneId = *buildZoneId;
		snapshot.revision = *snapshotRevision;
		snapshot.complete = complete->get<bool>();
		for (const auto& entry : *objects) {
			const auto object = ReadObjectState(entry, *buildZoneId);
			if (!object) {
				return std::nullopt;
			}
			snapshot.objects.push_back(*object);
		}
		for (const auto& entry : *tombstones) {
			const auto tombstone = ReadObjectState(entry, *buildZoneId);
			if (!tombstone) {
				return std::nullopt;
			}
			snapshot.tombstones.push_back(*tombstone);
		}
		return snapshot;
	}

	nlohmann::json MakeZoneState(const Workshop::SettlementState& a_zone)
	{
		using namespace Workshop;
		Json state{
			{ "revision", a_zone.CurrentRevision() },
			{ "nextObjectId", a_zone.NextObjectId() },
			{ "objects", Json::array() },
			{ "tombstones", Json::array() }
		};
		for (const auto& object : a_zone.Objects()) {
			state["objects"].push_back(ObjectJson(object));
		}
		for (const auto& tombstone : a_zone.StoredTombstones()) {
			auto entry = ObjectJson(tombstone.state);
			entry["profileId"] = tombstone.profileId;
			state["tombstones"].push_back(std::move(entry));
		}
		return state;
	}

	bool RestoreZoneState(Workshop::SettlementState& a_zone, const nlohmann::json& a_state)
	{
		using namespace Workshop;
		if (!a_state.is_object()) {
			return false;
		}
		const auto revision = a_state.find("revision");
		const auto nextObjectId = a_state.find("nextObjectId");
		const auto objects = a_state.find("objects");
		const auto tombstones = a_state.find("tombstones");
		if (revision == a_state.end() || nextObjectId == a_state.end() ||
			objects == a_state.end() || tombstones == a_state.end() ||
			!objects->is_array() || !tombstones->is_array()) {
			return false;
		}
		const auto revisionValue = ReadU64(*revision);
		const auto nextValue = ReadU64(*nextObjectId);
		if (!revisionValue || !nextValue) {
			return false;
		}
		std::vector<ObjectState> restoredObjects;
		std::vector<StoredTombstone> restoredTombstones;
		for (const auto& entry : *objects) {
			const auto object = ReadObjectState(entry, a_zone.Id());
			if (!object) {
				return false;
			}
			restoredObjects.push_back(*object);
		}
		for (const auto& entry : *tombstones) {
			const auto tombstone = ReadObjectState(entry, a_zone.Id());
			const auto profile = entry.is_object() ? entry.find("profileId") : entry.end();
			if (!tombstone || profile == entry.end() || !profile->is_string()) {
				return false;
			}
			restoredTombstones.push_back({ *tombstone, profile->get<std::string>() });
		}
		return a_zone.Restore(*revisionValue, *nextValue, restoredObjects, restoredTombstones);
	}

	std::optional<std::vector<Workshop::BuildZonePolicy>> ParseBuildZones(const nlohmann::json& a_file)
	{
		using namespace Workshop;
		const auto zones = a_file.is_object() ? a_file.find("zones") : a_file.end();
		const auto version = a_file.is_object() ? a_file.find("version") : a_file.end();
		if (zones == a_file.end() || version == a_file.end() || !zones->is_array() ||
			ReadU64(*version) != std::optional<std::uint64_t>{ 1 }) {
			return std::nullopt;
		}
		std::vector<BuildZonePolicy> parsed;
		for (const auto& entry : *zones) {
			if (!entry.is_object()) {
				return std::nullopt;
			}
			const auto id = entry.find("buildZoneId");
			const auto space = entry.find("space");
			const auto x = entry.find("centerX");
			const auto y = entry.find("centerY");
			const auto z = entry.find("centerZ");
			const auto radius = entry.find("radius");
			const auto maxScale = entry.find("maxScale");
			const auto maxObjects = entry.find("maxObjects");
			if (id == entry.end() || space == entry.end() || x == entry.end() || y == entry.end() ||
				z == entry.end() || radius == entry.end() || maxScale == entry.end() || maxObjects == entry.end()) {
				return std::nullopt;
			}
			const auto idValue = ReadU64(*id);
			const auto spaceValue = ReadForm(*space);
			const auto xValue = ReadFloat(*x);
			const auto yValue = ReadFloat(*y);
			const auto zValue = ReadFloat(*z);
			const auto radiusValue = ReadFloat(*radius);
			const auto scaleValue = ReadFloat(*maxScale);
			const auto objectsValue = ReadU32(*maxObjects);
			if (!idValue || !spaceValue || !xValue || !yValue || !zValue || !radiusValue || !scaleValue || !objectsValue) {
				return std::nullopt;
			}
			BuildZonePolicy policy{ *idValue, *spaceValue, *xValue, *yValue, *zValue, *radiusValue, *scaleValue, *objectsValue };
			if (const auto bench = entry.find("workbench"); bench != entry.end()) {
				const auto bx = bench->is_object() ? bench->find("x") : bench->end();
				const auto by = bench->is_object() ? bench->find("y") : bench->end();
				const auto bz = bench->is_object() ? bench->find("z") : bench->end();
				if (bx == bench->end() || by == bench->end() || bz == bench->end()) {
					return std::nullopt;
				}
				const auto bxValue = ReadFloat(*bx);
				const auto byValue = ReadFloat(*by);
				const auto bzValue = ReadFloat(*bz);
				if (!bxValue || !byValue || !bzValue) {
					return std::nullopt;
				}
				policy.hasWorkbench = true;
				policy.workbenchX = *bxValue;
				policy.workbenchY = *byValue;
				policy.workbenchZ = *bzValue;
			}
			if (const auto captureRadius = entry.find("captureRadius"); captureRadius != entry.end()) {
				const auto captureValue = ReadFloat(*captureRadius);
				if (!captureValue) {
					return std::nullopt;
				}
				policy.captureRadius = *captureValue;
			}
			for (const auto& other : parsed) {
				if (other.buildZoneId == policy.buildZoneId) {
					return std::nullopt;
				}
			}
			if (!SettlementState{ policy }.PolicyValid()) {
				return std::nullopt;
			}
			parsed.push_back(std::move(policy));
		}
		return parsed;
	}
}

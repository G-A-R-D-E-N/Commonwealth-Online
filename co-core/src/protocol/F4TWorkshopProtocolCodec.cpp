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

using Json = nlohmann::json;
using namespace F4T::Workshop;

namespace F4T::WorkshopProtocol::Codec
{
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
		Json json{
			{ "objectId", a_object.objectId },
			{ "form", FormJson(a_object.form) },
			{ "space", FormJson(a_object.space) },
			{ "transform", TransformJson(a_object.transform) }
		};
		if (!a_object.placedBy.empty()) {
			json["placedBy"] = a_object.placedBy;
		}
		if (a_object.maxHealth > 0.0F) {
			json["health"] = a_object.health;
			json["maxHealth"] = a_object.maxHealth;
		}
		if (a_object.destroyedUnixSeconds != 0) {
			json["destroyedUnixSeconds"] = a_object.destroyedUnixSeconds;
		}
		if (a_object.door) {
			json["door"] = true;
		}
		if (a_object.locked) {
			json["locked"] = true;
		}
		if (a_object.foundation) {
			json["foundation"] = true;
		}
		if (!a_object.supportIds.empty()) {
			json["supportIds"] = a_object.supportIds;
		}
		return json;
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
		ObjectState object{ *objectId, a_buildZoneId, *form, *space, *transform };
		if (const auto placedBy = a_value.find("placedBy"); placedBy != a_value.end()) {
			if (!placedBy->is_string()) {
				return std::nullopt;
			}
			object.placedBy = placedBy->get<std::string>();
		}
		if (const auto health = a_value.find("health"); health != a_value.end()) {
			if (!health->is_number()) {
				return std::nullopt;
			}
			object.health = health->get<float>();
		}
		if (const auto maxHealth = a_value.find("maxHealth"); maxHealth != a_value.end()) {
			if (!maxHealth->is_number()) {
				return std::nullopt;
			}
			object.maxHealth = maxHealth->get<float>();
		}
		if (const auto destroyed = a_value.find("destroyedUnixSeconds"); destroyed != a_value.end()) {
			const auto value = ReadU64(*destroyed);
			if (!value) {
				return std::nullopt;
			}
			object.destroyedUnixSeconds = *value;
		}
		if (const auto door = a_value.find("door"); door != a_value.end()) {
			if (!door->is_boolean()) {
				return std::nullopt;
			}
			object.door = door->get<bool>();
		}
		if (const auto locked = a_value.find("locked"); locked != a_value.end()) {
			if (!locked->is_boolean()) {
				return std::nullopt;
			}
			object.locked = locked->get<bool>();
		}
		if (const auto foundation = a_value.find("foundation"); foundation != a_value.end()) {
			if (!foundation->is_boolean()) {
				return std::nullopt;
			}
			object.foundation = foundation->get<bool>();
		}
		if (const auto supportIds = a_value.find("supportIds"); supportIds != a_value.end()) {
			if (!supportIds->is_array()) {
				return std::nullopt;
			}
			for (const auto& entry : *supportIds) {
				const auto id = ReadU64(entry);
				if (!id || *id == 0) {
					return std::nullopt;
				}
				object.supportIds.push_back(*id);
			}
		}
		return object;
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
		if (status == "persistenceUnavailable") return ApplyStatus::PersistenceUnavailable;
		return std::nullopt;
	}
}

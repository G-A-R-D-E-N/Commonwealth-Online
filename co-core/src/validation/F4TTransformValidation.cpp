#include "F4TTransformValidation.h"

#include "F4TActionEventValidation.h"
#include "F4TNetworkValidation.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::TransformValidation
{
	namespace
	{
		using Json = nlohmann::json;

		std::optional<double> ReadDouble(const Json& a_value, double a_min, double a_max)
		{
			if (a_value.is_boolean() || !a_value.is_number()) {
				return std::nullopt;
			}
			try {
				const auto value = a_value.get<double>();
				if (std::isfinite(value) && value >= a_min && value <= a_max) {
					return value;
				}
			} catch (...) {
			}
			return std::nullopt;
		}

		std::optional<std::uint32_t> ReadUInt32(const Json& a_value)
		{
			if (a_value.is_boolean()) {
				return std::nullopt;
			}
			try {
				if (a_value.is_number_unsigned()) {
					const auto value = a_value.get<std::uint64_t>();
					if (value <= (std::numeric_limits<std::uint32_t>::max)()) {
						return static_cast<std::uint32_t>(value);
					}
				}
				if (a_value.is_number_integer()) {
					const auto value = a_value.get<std::int64_t>();
					if (value >= 0 && static_cast<std::uint64_t>(value) <= (std::numeric_limits<std::uint32_t>::max)()) {
						return static_cast<std::uint32_t>(value);
					}
				}
				if (a_value.is_number_float()) {
					const auto value = a_value.get<double>();
					if (std::isfinite(value) && value == std::trunc(value) && value >= 0.0 &&
						value <= static_cast<double>((std::numeric_limits<std::uint32_t>::max)())) {
						return static_cast<std::uint32_t>(value);
					}
				}
			} catch (...) {
			}
			return std::nullopt;
		}

		std::string NormalizeFormId(std::string_view a_value)
		{
			std::uint32_t parsed = 0;
			const auto result = std::from_chars(a_value.data(), a_value.data() + a_value.size(), parsed, 16);
			if (result.ec != std::errc{} || result.ptr != a_value.data() + a_value.size()) {
				return {};
			}
			char buffer[9]{};
			std::snprintf(buffer, sizeof(buffer), "%08X", parsed);
			return buffer;
		}
	}

	std::optional<nlohmann::json> NormalizeTransform(const nlohmann::json& a_packet)
	{
		using Json = nlohmann::json;
		if (!a_packet.is_object()) {
			return std::nullopt;
		}

		const auto xIt = a_packet.find("x");
		const auto yIt = a_packet.find("y");
		const auto zIt = a_packet.find("z");
		const auto angleIt = a_packet.find("angleZ");
		if (xIt == a_packet.end() || yIt == a_packet.end() || zIt == a_packet.end() || angleIt == a_packet.end()) {
			return std::nullopt;
		}
		const auto x = ReadDouble(*xIt, -F4T::NetworkValidation::MaximumAbsoluteCoordinate, F4T::NetworkValidation::MaximumAbsoluteCoordinate);
		const auto y = ReadDouble(*yIt, -F4T::NetworkValidation::MaximumAbsoluteCoordinate, F4T::NetworkValidation::MaximumAbsoluteCoordinate);
		const auto z = ReadDouble(*zIt, -F4T::NetworkValidation::MaximumAbsoluteCoordinate, F4T::NetworkValidation::MaximumAbsoluteCoordinate);
		const auto angleZ = ReadDouble(*angleIt, -F4T::NetworkValidation::MaximumAbsoluteCoordinate, F4T::NetworkValidation::MaximumAbsoluteCoordinate);
		if (!x || !y || !z || !angleZ) {
			return std::nullopt;
		}

		const auto cellIt = a_packet.find("cellId");
		if (cellIt == a_packet.end() || !cellIt->is_string()) {
			return std::nullopt;
		}
		const auto cell = cellIt->get<std::string>();
		std::string world;
		if (const auto worldIt = a_packet.find("worldspaceId"); worldIt != a_packet.end()) {
			if (!worldIt->is_string()) {
				return std::nullopt;
			}
			world = worldIt->get<std::string>();
		}
		if (!F4T::NetworkValidation::IsValidHexFormId(cell, false, false) ||
			!F4T::NetworkValidation::IsValidHexFormId(world, true, true)) {
			return std::nullopt;
		}

		std::string movementType = "normal";
		if (const auto movementIt = a_packet.find("movementType"); movementIt != a_packet.end()) {
			if (!movementIt->is_string()) {
				return std::nullopt;
			}
			movementType = movementIt->get<std::string>();
		}
		if (movementType != "normal" && !F4T::NetworkValidation::IsMovementTransitionType(movementType)) {
			return std::nullopt;
		}

		// Built from an allow-list rather than copied: anything a client invents
		// must not survive into the packets the server fans out to other players.
		Json normalized = Json::object();
		normalized["type"] = "transform";
		normalized["x"] = *x;
		normalized["y"] = *y;
		normalized["z"] = *z;
		normalized["angleZ"] = *angleZ;
		normalized["cellId"] = NormalizeFormId(cell);
		normalized["worldspaceId"] = world.empty() ? std::string{} : NormalizeFormId(world);
		normalized["movementType"] = movementType;

		if (const auto found = a_packet.find("clientTime"); found != a_packet.end()) {
			if (const auto value = ReadDouble(*found, 0.0, 1e12)) {
				normalized["clientTime"] = *value;
			}
		}

		for (const auto& [name, minimum] : { std::pair{ "movementSpeed", 0.0 }, std::pair{ "animationGraphSpeed", -1.0 } }) {
			const auto found = a_packet.find(name);
			if (found == a_packet.end()) {
				continue;
			}
			if (const auto value = ReadDouble(*found, minimum, F4T::NetworkValidation::MaximumMovementSpeed)) {
				normalized[name] = *value;
			}
		}

		struct BoundedField
		{
			const char* name;
			double minimum;
			double maximum;
		};
		for (const auto field : {
			BoundedField{ "animationDirection", -360.0, 360.0 },
			BoundedField{ "aimPitch", -180.0, 180.0 },
			BoundedField{ "turnDelta", -10000.0, 10000.0 }
		}) {
			const auto found = a_packet.find(field.name);
			if (found == a_packet.end()) {
				continue;
			}
			if (const auto value = ReadDouble(*found, field.minimum, field.maximum)) {
				normalized[field.name] = *value;
			}
		}

		for (const char* name : { "isMoving", "isSprinting", "isSneaking", "isJumping", "isCrouching", "weaponDrawn" }) {
			const auto found = a_packet.find(name);
			if (found != a_packet.end() && found->is_boolean()) {
				normalized[name] = found->get<bool>();
			}
		}

		for (const char* name : { "actorStateFlags1", "actorStateFlags2" }) {
			const auto found = a_packet.find(name);
			if (found == a_packet.end()) {
				continue;
			}
			if (const auto value = ReadUInt32(*found)) {
				normalized[name] = *value;
			}
		}

		if (const auto found = a_packet.find("actionEvents"); found != a_packet.end()) {
			normalized["actionEvents"] = F4T::ActionEventValidation::NormalizeActionEvents(*found, false);
		}

		return normalized;
	}
}

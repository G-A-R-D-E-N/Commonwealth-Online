#include "F4TAppearanceValidation.h"

#include "F4TNetworkValidation.h"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::AppearanceValidation
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr std::size_t kMaximumHeadParts = 64;
		constexpr std::size_t kMaximumMorphs = 128;
		constexpr std::size_t kMaximumMorphRegions = 128;
		constexpr std::size_t kMaximumFacialBoneMorphs = 128;
		constexpr std::size_t kMaximumTints = 128;

		std::optional<std::uint32_t> ReadUInt32(const Json& a_value, std::uint32_t a_min, std::uint32_t a_max)
		{
			if (a_value.is_boolean()) {
				return std::nullopt;
			}
			try {
				if (a_value.is_number_unsigned()) {
					const auto value = a_value.get<std::uint64_t>();
					if (value >= a_min && value <= a_max) {
						return static_cast<std::uint32_t>(value);
					}
				}
				if (a_value.is_number_integer()) {
					const auto value = a_value.get<std::int64_t>();
					if (value >= static_cast<std::int64_t>(a_min) && value <= static_cast<std::int64_t>(a_max)) {
						return static_cast<std::uint32_t>(value);
					}
				}
				if (a_value.is_number_float()) {
					const auto value = a_value.get<double>();
					if (std::isfinite(value) && value == std::trunc(value) &&
						value >= static_cast<double>(a_min) && value <= static_cast<double>(a_max)) {
						return static_cast<std::uint32_t>(value);
					}
				}
			} catch (...) {
			}
			return std::nullopt;
		}

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

		std::optional<std::string> ReadString(const Json& a_value)
		{
			if (!a_value.is_string()) {
				return std::nullopt;
			}
			return a_value.get<std::string>();
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

		std::optional<Json> NormalizeVec3(const Json& a_value, double a_min, double a_max)
		{
			if (!a_value.is_array() || a_value.size() != 3) {
				return std::nullopt;
			}
			Json output = Json::array();
			for (const auto& component : a_value) {
				const auto value = ReadDouble(component, a_min, a_max);
				if (!value) {
					return std::nullopt;
				}
				output.push_back(*value);
			}
			return output;
		}
	}

	std::optional<nlohmann::json> NormalizeAppearance(const nlohmann::json& a_value)
	{
		using Json = nlohmann::json;
		if (!a_value.is_object()) {
			return std::nullopt;
		}

		Json clean = Json::object();
		std::uint32_t version = 4;
		if (const auto found = a_value.find("version"); found != a_value.end()) {
			const auto parsed = ReadUInt32(*found, 1, 1000);
			if (!parsed) {
				return std::nullopt;
			}
			version = *parsed;
		}
		clean["version"] = version;

		for (const char* name : { "raceFormId", "hairColorFormId", "facialHairColorFormId", "complexionFormId" }) {
			const auto found = a_value.find(name);
			if (found == a_value.end()) {
				continue;
			}
			const auto form = ReadString(*found);
			if (!form || !F4T::NetworkValidation::IsValidHexFormId(*form, true, true)) {
				return std::nullopt;
			}
			clean[name] = form->empty() ? std::string{} : NormalizeFormId(*form);
		}

		if (const auto found = a_value.find("height"); found != a_value.end()) {
			const auto height = ReadDouble(*found, 0.25, 4.0);
			if (!height) {
				return std::nullopt;
			}
			clean["height"] = *height;
		}

		if (const auto found = a_value.find("isFemale"); found != a_value.end()) {
			if (!found->is_boolean()) {
				return std::nullopt;
			}
			clean["isFemale"] = found->get<bool>();
		}

		if (const auto found = a_value.find("morphWeight"); found != a_value.end()) {
			if (!found->is_object()) {
				return std::nullopt;
			}
			Json weights = Json::object();
			for (const char* name : { "thin", "muscular", "large" }) {
				const auto component = found->find(name);
				if (component == found->end()) {
					return std::nullopt;
				}
				const auto value = ReadDouble(*component, -100.0, 100.0);
				if (!value) {
					return std::nullopt;
				}
				weights[name] = *value;
			}
			clean["morphWeight"] = std::move(weights);
		}

		if (const auto found = a_value.find("bodyTintColor"); found != a_value.end()) {
			if (!found->is_object()) {
				return std::nullopt;
			}
			Json color = Json::object();
			for (const char* name : { "r", "g", "b", "a" }) {
				const auto component = found->find(name);
				if (component == found->end()) {
					return std::nullopt;
				}
				const auto value = ReadUInt32(*component, 0, 255);
				if (!value) {
					return std::nullopt;
				}
				color[name] = *value;
			}
			clean["bodyTintColor"] = std::move(color);
		}

		if (const auto found = a_value.find("headParts"); found != a_value.end()) {
			if (!found->is_array() || found->size() > kMaximumHeadParts) {
				return std::nullopt;
			}
			Json parts = Json::array();
			for (const auto& node : *found) {
				const auto form = ReadString(node);
				if (!form || !F4T::NetworkValidation::IsValidHexFormId(*form, false, true)) {
					return std::nullopt;
				}
				parts.push_back(NormalizeFormId(*form));
			}
			clean["headParts"] = std::move(parts);
		}

		if (const auto found = a_value.find("morphs"); found != a_value.end()) {
			if (!found->is_array() || found->size() > kMaximumMorphs) {
				return std::nullopt;
			}
			Json morphs = Json::array();
			for (const auto& node : *found) {
				if (!node.is_object()) {
					return std::nullopt;
				}
				const auto idNode = node.find("id");
				const auto valueNode = node.find("value");
				if (idNode == node.end() || valueNode == node.end()) {
					return std::nullopt;
				}
				const auto id = ReadString(*idNode);
				const auto amount = ReadDouble(*valueNode, -1000.0, 1000.0);
				if (!id || !amount || !F4T::NetworkValidation::IsValidHexFormId(*id, false, true)) {
					return std::nullopt;
				}
				morphs.push_back(Json{ { "id", NormalizeFormId(*id) }, { "value", *amount } });
			}
			clean["morphs"] = std::move(morphs);
		}

		if (const auto found = a_value.find("morphRegions"); found != a_value.end()) {
			if (!found->is_array() || found->size() > kMaximumMorphRegions) {
				return std::nullopt;
			}
			Json regions = Json::array();
			for (const auto& node : *found) {
				const auto amount = ReadDouble(node, -1000.0, 1000.0);
				if (!amount) {
					return std::nullopt;
				}
				regions.push_back(*amount);
			}
			clean["morphRegions"] = std::move(regions);
		}

		if (const auto found = a_value.find("facialBoneMorphs"); found != a_value.end()) {
			if (!found->is_array() || found->size() > kMaximumFacialBoneMorphs) {
				return std::nullopt;
			}
			Json morphs = Json::array();
			for (const auto& node : *found) {
				if (!node.is_object()) {
					return std::nullopt;
				}
				const auto idNode = node.find("id");
				const auto positionNode = node.find("position");
				const auto rotationNode = node.find("rotation");
				const auto scaleNode = node.find("scale");
				if (idNode == node.end() || positionNode == node.end() || rotationNode == node.end() || scaleNode == node.end()) {
					return std::nullopt;
				}
				const auto id = ReadString(*idNode);
				const auto position = NormalizeVec3(*positionNode, -10000.0, 10000.0);
				const auto rotation = NormalizeVec3(*rotationNode, -10000.0, 10000.0);
				const auto scale = NormalizeVec3(*scaleNode, -100.0, 100.0);
				if (!id || !position || !rotation || !scale || !F4T::NetworkValidation::IsValidHexFormId(*id, false, true)) {
					return std::nullopt;
				}
				morphs.push_back(Json{
					{ "id", NormalizeFormId(*id) },
					{ "position", *position },
					{ "rotation", *rotation },
					{ "scale", *scale }
				});
			}
			clean["facialBoneMorphs"] = std::move(morphs);
		}

		if (const auto found = a_value.find("tints"); found != a_value.end()) {
			if (!found->is_array() || found->size() > kMaximumTints) {
				return std::nullopt;
			}
			Json tints = Json::array();
			for (const auto& node : *found) {
				if (!node.is_object()) {
					return std::nullopt;
				}
				const auto idNode = node.find("id");
				const auto typeNode = node.find("type");
				const auto valueNode = node.find("value");
				if (idNode == node.end() || typeNode == node.end() || valueNode == node.end()) {
					return std::nullopt;
				}
				const auto id = ReadUInt32(*idNode, 0, 65535);
				const auto type = ReadUInt32(*typeNode, 0, (std::numeric_limits<std::uint32_t>::max)());
				const auto amount = ReadUInt32(*valueNode, 0, 255);
				if (!id || !type || !amount) {
					return std::nullopt;
				}
				Json tint{ { "id", *id }, { "type", *type }, { "value", *amount } };
				if (const auto colorNode = node.find("color"); colorNode != node.end()) {
					const auto color = ReadString(*colorNode);
					if (!color || !F4T::NetworkValidation::IsValidHexFormId(*color, false, true)) {
						return std::nullopt;
					}
					std::uint32_t swatch = 0;
					if (const auto swatchNode = node.find("swatch"); swatchNode != node.end()) {
						const auto parsed = ReadUInt32(*swatchNode, 0, 65535);
						if (!parsed) {
							return std::nullopt;
						}
						swatch = *parsed;
					}
					tint["color"] = NormalizeFormId(*color);
					tint["swatch"] = swatch;
				}
				tints.push_back(std::move(tint));
			}
			clean["tints"] = std::move(tints);
		}

		return clean;
	}
}

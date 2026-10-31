#include "F4TActionEventValidation.h"

#include "F4TNetworkValidation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace F4T::ActionEventValidation
{
	namespace
	{
		using Json = nlohmann::json;
		constexpr std::size_t kMaximumActionEvents = 16;

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
	}

	nlohmann::json NormalizeActionEvents(const nlohmann::json& a_value, bool a_strict)
	{
		using Json = nlohmann::json;
		Json output = Json::array();
		if (!a_value.is_array()) {
			return output;
		}

		const auto count = (std::min)(a_value.size(), kMaximumActionEvents);
		for (std::size_t index = 0; index < count; ++index) {
			const auto& item = a_value[index];
			if (!item.is_object()) {
				if (a_strict) {
					return Json::array();
				}
				continue;
			}

			const auto sequenceIt = item.find("sequence");
			const auto typeIt = item.find("type");
			const auto eventIt = item.find("eventName");
			const auto sequence = sequenceIt == item.end() ? std::nullopt : ReadUInt32(*sequenceIt);
			const auto type = typeIt == item.end() ? std::nullopt : ReadUInt32(*typeIt);
			const auto eventName = eventIt != item.end() && eventIt->is_string() ? std::optional{ eventIt->get<std::string>() } : std::nullopt;
			if (!sequence || *sequence == 0 || !type || *type == 0 || *type > 3 || !eventName ||
				!F4T::NetworkValidation::IsAllowedRemoteAction(*type, *eventName)) {
				if (a_strict) {
					return Json::array();
				}
				continue;
			}

			Json clean{
				{ "sequence", *sequence },
				{ "type", *type },
				{ "eventName", *eventName }
			};
			for (const char* name : { "actorStateFlags1", "actorStateFlags2" }) {
				const auto found = item.find(name);
				if (found == item.end()) {
					continue;
				}
				if (const auto flags = ReadUInt32(*found)) {
					clean[name] = *flags;
				}
			}
			output.push_back(std::move(clean));
		}
		return output;
	}
}

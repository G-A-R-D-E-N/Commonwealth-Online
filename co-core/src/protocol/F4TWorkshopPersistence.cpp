#include "F4TWorkshopPersistence.h"

#include <limits>
#include <optional>

namespace F4T::WorkshopPersistence
{
	namespace
	{
		using Json = nlohmann::json;

		std::optional<std::uint32_t> ReadUInt32(const Json& a_value)
		{
			if (a_value.is_boolean()) {
				return std::nullopt;
			}
			try {
				if (a_value.is_number_unsigned()) {
					const auto value = a_value.get<std::uint64_t>();
					if (value <= static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)())) {
						return static_cast<std::uint32_t>(value);
					}
				}
				if (a_value.is_number_integer()) {
					const auto value = a_value.get<std::int64_t>();
					if (value >= 0 &&
						static_cast<std::uint64_t>(value) <= static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)())) {
						return static_cast<std::uint32_t>(value);
					}
				}
			} catch (...) {
			}
			return std::nullopt;
		}
	}

	bool MigrateWorkshopRoot(Json& a_root)
	{
		if (!a_root.is_object()) {
			return false;
		}
		const auto zonesIt = a_root.find("zones");
		if (zonesIt != a_root.end() && !zonesIt->is_object()) {
			return false;
		}
		const auto versionIt = a_root.find("version");
		const auto version = versionIt == a_root.end() ? std::optional<std::uint32_t>{ 1 } : ReadUInt32(*versionIt);
		if (!version || *version == 0 || *version > CurrentSchemaVersion) {
			return false;
		}
		if (zonesIt == a_root.end()) {
			a_root["zones"] = Json::object();
		}
		a_root["version"] = CurrentSchemaVersion;
		if (!a_root.contains("meta") || !a_root["meta"].is_object()) {
			a_root["meta"] = Json::object();
		}
		a_root["meta"]["dirtyFlushSeconds"] = 1;
		return true;
	}
}

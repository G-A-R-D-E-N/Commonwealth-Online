#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>

namespace F4T::WorkshopPersistence
{
	inline constexpr std::uint32_t CurrentSchemaVersion = 2;

	[[nodiscard]] bool MigrateWorkshopRoot(nlohmann::json& a_root);
}

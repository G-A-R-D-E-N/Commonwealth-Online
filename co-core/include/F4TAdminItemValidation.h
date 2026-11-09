#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace F4T::AdminItemValidation
{
	inline constexpr std::size_t MaximumInventoryItems = 512;
	inline constexpr std::size_t MaximumInventoryReportBytes = 64 * 1024;
	inline constexpr std::size_t MaximumItemNameBytes = 64;
	inline constexpr std::size_t MaximumAdminResultErrorBytes = 256;
	inline constexpr std::size_t MaximumOutstandingAdminOps = 16;

	[[nodiscard]] std::optional<nlohmann::json> NormalizeInventoryReport(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeAdminItemResult(const nlohmann::json& a_packet);
}

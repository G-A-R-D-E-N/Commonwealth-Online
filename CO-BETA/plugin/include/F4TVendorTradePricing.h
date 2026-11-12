#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace F4T::VendorTradePricing
{
	inline constexpr std::string_view kTraderUnavailable = "The trader is unavailable.";

	[[nodiscard]] std::uint32_t BuyPrice(std::uint32_t a_formId);
	[[nodiscard]] std::uint32_t SellPrice(std::uint32_t a_formId);
	[[nodiscard]] std::string ItemName(std::uint32_t a_formId);
	[[nodiscard]] std::string ReasonText(const std::string& a_reason);
}

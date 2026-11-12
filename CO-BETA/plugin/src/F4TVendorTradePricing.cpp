#include "pch.h"

#include "F4TVendorTradePricing.h"

#include "F4TVendorTrade.h"

#include "RE/T/TESBoundObject.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESFullName.h"
#include "RE/T/TESValueForm.h"

#include <algorithm>
#include <format>
#include <string>

namespace F4T::VendorTradePricing
{
	namespace
	{
		std::uint32_t BaseValue(std::uint32_t a_formId)
		{
			const auto* form = RE::TESForm::GetFormByID(a_formId);
			const auto* valueForm = form ? form->As<RE::TESValueForm>() : nullptr;
			return valueForm ? static_cast<std::uint32_t>(std::max(0, valueForm->GetFormValue())) : 0U;
		}
	}

	std::uint32_t BuyPrice(std::uint32_t a_formId)
	{
		return F4T::VendorTrade::BuyPrice(BaseValue(a_formId));
	}

	std::uint32_t SellPrice(std::uint32_t a_formId)
	{
		return F4T::VendorTrade::SellPrice(BaseValue(a_formId));
	}

	std::string ItemName(std::uint32_t a_formId)
	{
		if (const auto* form = RE::TESForm::GetFormByID<RE::TESBoundObject>(a_formId)) {
			const auto fullName = RE::TESFullName::GetFullName(*form);
			if (!fullName.empty()) {
				return std::string{ fullName };
			}
		}
		return std::format("Item {:08X}", a_formId);
	}

	std::string ReasonText(const std::string& a_reason)
	{
		if (a_reason == "staleRevision") return "The trader's stock just changed. Review the cart and try again.";
		if (a_reason == "insufficientStock") return "The trader no longer has that much stock.";
		if (a_reason == "insufficientCaps") return "The trader cannot afford that.";
		if (a_reason == "stockLimit" || a_reason == "capsLimit") return "The trader cannot hold that much.";
		if (a_reason == "unseededVendor") return "The trader's stock is not known yet.";
		if (a_reason == "noProfile") return std::string{ kTraderUnavailable };
		if (a_reason == "mispriced") return "The caps do not match the price.";
		if (a_reason == "unknownItem") return "That item has no known value.";
		if (a_reason == "notOwned") return "You no longer hold that item. Try again in a moment.";
		return "The trade was refused.";
	}
}

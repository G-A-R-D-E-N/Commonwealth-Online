#pragma once
#include "RE/B/BGSInventoryItem.h"
#include "RE/E/ExtraDataList.h"
#include <nlohmann/json.hpp>
#include <optional>
namespace F4T::ItemHealthReadback
{
	[[nodiscard]] std::optional<nlohmann::json> Capture(const RE::BGSInventoryItem::Stack& a_stack);
	[[nodiscard]] RE::BSTSmartPointer<RE::ExtraDataList> CreateExtra(const nlohmann::json& a_snapshot);
}

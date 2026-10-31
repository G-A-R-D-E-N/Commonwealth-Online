#pragma once

#include "F4TRemotePlayerState.h"
#include "F4TPlayerStateValidation.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <span>
#include <tuple>
#include <vector>

namespace F4T::EquipmentState
{
	// Recipe presence must survive normalization; source:co-core/src/validation/F4TPlayerStateValidation.cpp.
	using Item = RemotePlayerState::RemoteEquippedItem;

	// Wire cap; source:co-core/src/validation/F4TPlayerStateValidation.cpp (kMaximumItemMods).
	inline constexpr std::size_t kMaximumItemMods = 32;

	inline bool SameMods(std::vector<RemotePlayerState::RemoteItemMod> a_left, std::vector<RemotePlayerState::RemoteItemMod> a_right)
	{
		const auto order = [](const RemotePlayerState::RemoteItemMod& a_a, const RemotePlayerState::RemoteItemMod& a_b) {
			return std::tie(a_a.formId, a_a.index, a_a.rank) < std::tie(a_b.formId, a_b.index, a_b.rank);
		};
		std::ranges::sort(a_left, order);
		std::ranges::sort(a_right, order);
		return a_left == a_right;
	}

	inline std::string DescribeMods(std::span<const RemotePlayerState::RemoteItemMod> a_mods)
	{
		if (a_mods.empty()) {
			return "none";
		}
		std::string text;
		for (const auto& mod : a_mods) {
			text += std::format("[{:08X}:{}:{}]", mod.formId, mod.index, mod.rank);
		}
		return text;
	}

	// TODO-verify(#407)
	inline std::string DescribeItems(std::span<const Item> a_items)
	{
		std::string text;
		for (const auto& item : a_items) {
			text += text.empty() ? "" : ", ";
			text += std::format("{}={:08X}", item.slot, item.formId);
			text += item.mods ? std::format("(mods={})", DescribeMods(*item.mods)) : std::string{ "(mods=unset)" };
		}
		return text;
	}

	inline std::optional<nlohmann::json> Encode(std::span<const Item> a_items)
	{
		auto items = nlohmann::json::array();
		for (const auto& item : a_items) {
			nlohmann::json node{
				{ "slot", item.slot },
				{ "formId", item.formId == 0 ? std::string{} : std::format("{:08X}", item.formId) }
			};
			if (item.mods) {
				auto mods = nlohmann::json::array();
				for (const auto& mod : *item.mods) {
					mods.push_back({ { "mod", std::format("{:08X}", mod.formId) }, { "index", mod.index }, { "rank", mod.rank } });
				}
				node["mods"] = std::move(mods);
			}
			items.push_back(std::move(node));
		}
		auto normalized = PlayerStateValidation::NormalizePlayerState({ { "equippedItems", std::move(items) } });
		return normalized ? std::optional{ std::move((*normalized)["equippedItems"]) } : std::nullopt;
	}

	inline std::optional<std::vector<Item>> Decode(const nlohmann::json& a_items)
	{
		const auto normalized = PlayerStateValidation::NormalizePlayerState({ { "equippedItems", a_items } });
		if (!normalized) {
			return std::nullopt;
		}
		for (const auto& node : a_items) {
			if (!node.contains("formId") || !node["formId"].is_string()) {
				return std::nullopt;
			}
		}
		const auto readForm = [](const std::string& text) {
			std::uint32_t value{};
			std::from_chars(text.data(), text.data() + text.size(), value, 16);
			return value;
		};
		std::vector<Item> items;
		for (const auto& node : (*normalized)["equippedItems"]) {
			Item item{ node["slot"].get<std::string>(), readForm(node["formId"].get<std::string>()), std::nullopt };
			if (const auto found = node.find("mods"); found != node.end()) {
				if (item.formId == 0) {
					return std::nullopt;
				}
				item.mods.emplace();
				for (const auto& mod : *found) {
					item.mods->push_back({ readForm(mod["mod"].get<std::string>()), mod["index"].get<std::uint8_t>(), mod["rank"].get<std::uint8_t>() });
				}
			}
			items.push_back(std::move(item));
		}
		return items;
	}
}

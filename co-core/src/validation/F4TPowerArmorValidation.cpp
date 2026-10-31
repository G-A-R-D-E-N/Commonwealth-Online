#include "F4TPowerArmorValidation.h"

#include "F4TNetworkValidation.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
// TODO-verify: concept-level wire and plan checks, engine facts unverified; source:docs/code-notes/power-armor-sync.md.

namespace F4T::PowerArmorValidation
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr std::size_t kMaximumPieces = 8;
		constexpr std::size_t kMaximumPieceMods = 32;
		constexpr std::size_t kMaximumSlotChars = 64;

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

		std::optional<std::uint32_t> ReadByte(const Json& a_node, const char* a_field)
		{
			const auto found = a_node.find(a_field);
			if (found == a_node.end() || !found->is_number_integer() || found->get<std::int64_t>() < 0 || found->get<std::int64_t>() > 0xFF) {
				return std::nullopt;
			}
			return static_cast<std::uint32_t>(found->get<std::int64_t>());
		}

		std::optional<Json> NormalizePieceMods(const Json& a_mods)
		{
			if (!a_mods.is_array() || a_mods.size() > kMaximumPieceMods) {
				return std::nullopt;
			}
			Json mods = Json::array();
			for (const auto& node : a_mods) {
				if (!node.is_object()) {
					return std::nullopt;
				}
				const auto mod = node.find("mod");
				const auto index = ReadByte(node, "index");
				const auto rank = ReadByte(node, "rank");
				if (mod == node.end() || !mod->is_string() || !index || !rank ||
					!F4T::NetworkValidation::IsValidHexFormId(mod->get<std::string>(), false, false)) {
					return std::nullopt;
				}
				mods.push_back(Json{ { "mod", NormalizeFormId(mod->get<std::string>()) }, { "index", *index }, { "rank", *rank } });
			}
			return mods;
		}
	}

	std::optional<nlohmann::json> NormalizePowerArmor(const nlohmann::json& a_value)
	{
		using Json = nlohmann::json;
		if (!a_value.is_object()) {
			return std::nullopt;
		}

		Json clean = Json::object();

		const auto inFrameIt = a_value.find("inFrame");
		if (inFrameIt == a_value.end() || !inFrameIt->is_boolean()) {
			return std::nullopt;
		}
		clean["inFrame"] = inFrameIt->get<bool>();

		const auto frameIt = a_value.find("frameBaseFormId");
		if (frameIt == a_value.end() || !frameIt->is_string()) {
			return std::nullopt;
		}
		const auto frame = frameIt->get<std::string>();
		if (!F4T::NetworkValidation::IsValidHexFormId(frame, true, true)) {
			return std::nullopt;
		}
		clean["frameBaseFormId"] = frame.empty() ? std::string{} : NormalizeFormId(frame);

		const auto piecesIt = a_value.find("pieces");
		if (piecesIt == a_value.end() || !piecesIt->is_array() || piecesIt->size() > kMaximumPieces) {
			return std::nullopt;
		}
		Json pieces = Json::array();
		{
			std::unordered_set<std::string> seenSlots;
			seenSlots.reserve(piecesIt->size());
			for (const auto& node : *piecesIt) {
				if (!node.is_object()) {
					return std::nullopt;
				}
				const auto slotIt = node.find("slot");
				const auto formIt = node.find("formId");
				if (slotIt == node.end() || !slotIt->is_string() || formIt == node.end() || !formIt->is_string()) {
					return std::nullopt;
				}
				const auto slot = slotIt->get<std::string>();
				const auto form = formIt->get<std::string>();
				if (slot.empty() || slot.size() > kMaximumSlotChars ||
					!seenSlots.insert(slot).second ||
					!F4T::NetworkValidation::IsValidHexFormId(form, false, false)) {
					return std::nullopt;
				}
				Json piece{
					{ "slot", slot },
					{ "formId", NormalizeFormId(form) }
				};
				if (const auto mods = node.find("mods"); mods != node.end()) {
					auto normalizedMods = NormalizePieceMods(*mods);
					if (!normalizedMods) {
						return std::nullopt;
					}
					piece["mods"] = std::move(*normalizedMods);
				}
				pieces.push_back(std::move(piece));
			}
		}
		clean["pieces"] = std::move(pieces);

		return clean;
	}
}

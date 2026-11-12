#include "F4TWorkshopPieces.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>

using Json = nlohmann::json;

namespace F4T::WorkshopPieces
{
	namespace
	{
		constexpr std::size_t MaximumEntries = 4096;

		std::string Lowercase(std::string a_name)
		{
			for (auto& ch : a_name) {
				ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			}
			return a_name;
		}

		std::optional<std::uint32_t> ReadLocalFormId(const Json& a_value)
		{
			if (!a_value.is_number_unsigned()) {
				return std::nullopt;
			}
			const auto value = a_value.get<std::uint64_t>();
			if (value == 0 || value > Workshop::MaximumLocalFormId) {
				return std::nullopt;
			}
			return static_cast<std::uint32_t>(value);
		}

		std::optional<WorkshopMaterials::MaterialTier> ReadTier(const Json& a_value)
		{
			if (!a_value.is_string()) {
				return std::nullopt;
			}
			const auto name = a_value.get<std::string>();
			if (name == "wood") {
				return WorkshopMaterials::MaterialTier::Wood;
			}
			if (name == "scrap") {
				return WorkshopMaterials::MaterialTier::ScrapMetal;
			}
			if (name == "concrete") {
				return WorkshopMaterials::MaterialTier::Concrete;
			}
			if (name == "reinforced") {
				return WorkshopMaterials::MaterialTier::Reinforced;
			}
			return std::nullopt;
		}

		std::optional<WorkshopPower::DeviceClass> ReadDeviceClass(const Json& a_value)
		{
			if (!a_value.is_string()) {
				return std::nullopt;
			}
			const auto name = a_value.get<std::string>();
			if (name == "other") {
				return WorkshopPower::DeviceClass::Other;
			}
			if (name == "light") {
				return WorkshopPower::DeviceClass::Light;
			}
			if (name == "water") {
				return WorkshopPower::DeviceClass::Water;
			}
			if (name == "door") {
				return WorkshopPower::DeviceClass::Door;
			}
			if (name == "turret") {
				return WorkshopPower::DeviceClass::Turret;
			}
			return std::nullopt;
		}

		std::optional<WorkshopPower::Node> ReadPower(const Json& a_value)
		{
			if (!a_value.is_object()) {
				return std::nullopt;
			}
			const auto output = a_value.find("outputWatts");
			const auto draw = a_value.find("drawWatts");
			const auto needsPower = a_value.find("needsPower");
			const auto deviceClass = a_value.find("deviceClass");
			if (output == a_value.end() || draw == a_value.end() || needsPower == a_value.end() ||
				deviceClass == a_value.end() || !output->is_number_unsigned() || !draw->is_number_unsigned() ||
				!needsPower->is_boolean()) {
				return std::nullopt;
			}
			const auto outputWatts = output->get<std::uint64_t>();
			const auto drawWatts = draw->get<std::uint64_t>();
			const auto device = ReadDeviceClass(*deviceClass);
			constexpr auto maxWatts = (std::numeric_limits<std::uint32_t>::max)();
			if (!device || outputWatts > maxWatts || drawWatts > maxWatts) {
				return std::nullopt;
			}
			WorkshopPower::Node node;
			node.outputWatts = static_cast<std::uint32_t>(outputWatts);
			node.drawWatts = static_cast<std::uint32_t>(drawWatts);
			node.needsPower = needsPower->get<bool>();
			node.deviceClass = *device;
			return node;
		}

		bool ReadFlag(const Json& a_object, const char* a_key, bool& a_value)
		{
			const auto found = a_object.find(a_key);
			if (found == a_object.end()) {
				return true;
			}
			if (!found->is_boolean()) {
				return false;
			}
			a_value = found->get<bool>();
			return true;
		}
	}

	bool Table::LoadFromJson(const Json& a_root)
	{
		const auto pieces = a_root.is_object() ? a_root.find("pieces") : a_root.end();
		if (pieces == a_root.end() || !pieces->is_array() || pieces->size() > MaximumEntries) {
			return false;
		}
		std::map<std::pair<std::string, std::uint32_t>, Piece> loaded;
		for (const auto& entry : *pieces) {
			if (!entry.is_object()) {
				return false;
			}
			const auto plugin = entry.find("plugin");
			const auto localFormId = entry.find("localFormId");
			const auto tier = entry.find("tier");
			if (plugin == entry.end() || localFormId == entry.end() || tier == entry.end() || !plugin->is_string()) {
				return false;
			}
			const auto name = Lowercase(plugin->get<std::string>());
			const auto id = ReadLocalFormId(*localFormId);
			const auto materialTier = ReadTier(*tier);
			if (name.empty() || name.size() > Workshop::MaximumPluginNameBytes || !id || !materialTier) {
				return false;
			}
			Piece piece;
			piece.tier = *materialTier;
			if (!ReadFlag(entry, "door", piece.door) || !ReadFlag(entry, "foundation", piece.foundation)) {
				return false;
			}
			if (const auto power = entry.find("power"); power != entry.end()) {
				piece.power = ReadPower(*power);
				if (!piece.power) {
					return false;
				}
			}
			if (!loaded.emplace(std::pair{ name, *id }, std::move(piece)).second) {
				return false;
			}
		}
		m_pieces = std::move(loaded);
		return true;
	}

	Piece Table::Find(const Workshop::FormKey& a_form) const
	{
		const auto found = m_pieces.find({ Lowercase(a_form.plugin), a_form.localFormId });
		return found == m_pieces.end() ? Piece{} : found->second;
	}
}

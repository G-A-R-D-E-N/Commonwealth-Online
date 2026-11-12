#include "F4TWeaponClasses.h"

#include <nlohmann/json.hpp>

#include <cctype>

using Json = nlohmann::json;

namespace F4T::WeaponClasses
{
	namespace
	{
		constexpr std::size_t MaximumEntries = 16384;

		std::string Lowercase(std::string a_name)
		{
			for (auto& ch : a_name) {
				ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			}
			return a_name;
		}

		bool ReadForm(const Json& a_entry, Workshop::FormKey& a_form)
		{
			const auto plugin = a_entry.find("plugin");
			const auto localFormId = a_entry.find("localFormId");
			if (plugin == a_entry.end() || localFormId == a_entry.end() || !plugin->is_string() || !localFormId->is_number_unsigned()) {
				return false;
			}
			const auto name = Lowercase(plugin->get<std::string>());
			const auto id = localFormId->get<std::uint64_t>();
			if (name.empty() || name.size() > Workshop::MaximumPluginNameBytes || id == 0 || id > Workshop::MaximumLocalFormId) {
				return false;
			}
			a_form = Workshop::FormKey{ name, static_cast<std::uint32_t>(id) };
			return true;
		}

		bool ReadClass(const Json& a_entry, std::optional<WorkshopMaterials::WeaponKind>& a_class)
		{
			const auto weaponClass = a_entry.find("class");
			if (weaponClass == a_entry.end() || !weaponClass->is_string()) {
				return false;
			}
			const auto name = weaponClass->get<std::string>();
			if (name == "None") {
				a_class = std::nullopt;
			} else if (name == "Melee") {
				a_class = WorkshopMaterials::WeaponKind::Melee;
			} else if (name == "Light") {
				a_class = WorkshopMaterials::WeaponKind::Light;
			} else if (name == "Rifle") {
				a_class = WorkshopMaterials::WeaponKind::Rifle;
			} else if (name == "Heavy") {
				a_class = WorkshopMaterials::WeaponKind::Heavy;
			} else {
				return false;
			}
			return true;
		}
	}

	bool Table::LoadFromJson(const Json& a_root)
	{
		const auto weapons = a_root.is_object() ? a_root.find("weapons") : a_root.end();
		if (weapons == a_root.end() || !weapons->is_array() || weapons->size() > MaximumEntries) {
			return false;
		}
		std::map<std::pair<std::string, std::uint32_t>, std::optional<WorkshopMaterials::WeaponKind>> loaded;
		for (const auto& entry : *weapons) {
			Workshop::FormKey form;
			std::optional<WorkshopMaterials::WeaponKind> weaponClass;
			if (!entry.is_object() || !ReadForm(entry, form) || !ReadClass(entry, weaponClass)) {
				return false;
			}
			if (!loaded.emplace(std::pair{ form.plugin, form.localFormId }, weaponClass).second) {
				return false;
			}
		}
		m_classes = std::move(loaded);
		return true;
	}

	const std::optional<WorkshopMaterials::WeaponKind>* Table::Find(const Workshop::FormKey& a_form) const
	{
		const auto found = m_classes.find({ Lowercase(a_form.plugin), a_form.localFormId });
		return found == m_classes.end() ? nullptr : &found->second;
	}
}

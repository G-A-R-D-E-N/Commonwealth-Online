#include "F4TWorkshopDefenseForms.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <string>

namespace F4T::WorkshopDefenseForms
{
	namespace
	{
		using Json = nlohmann::json;

		bool ReadPositiveFloat(const Json& a_value, float& a_out)
		{
			if (!a_value.is_number()) {
				return false;
			}
			const auto value = a_value.get<float>();
			if (!std::isfinite(value) || !(value > 0.0F)) {
				return false;
			}
			a_out = value;
			return true;
		}
	}

	std::string FormKeyString(const Workshop::FormKey& a_form)
	{
		return a_form.plugin + ":" + std::to_string(a_form.localFormId);
	}

	bool Table::Load(const nlohmann::json& a_root)
	{
		if (!a_root.is_object()) {
			return false;
		}
		const auto turrets = a_root.find("turrets");
		if (turrets == a_root.end()) {
			m_byForm.clear();
			return true;
		}
		if (!turrets->is_array()) {
			return false;
		}
		std::unordered_map<std::string, TurretForm> loaded;
		for (const auto& entry : *turrets) {
			if (!entry.is_object()) {
				return false;
			}
			const auto plugin = entry.find("plugin");
			const auto localFormId = entry.find("localFormId");
			if (plugin == entry.end() || localFormId == entry.end() || !plugin->is_string() ||
				!localFormId->is_number_unsigned()) {
				return false;
			}
			TurretForm form;
			if (const auto rating = entry.find("rating"); rating != entry.end() && !ReadPositiveFloat(*rating, form.ratingValue)) {
				return false;
			}
			if (const auto range = entry.find("rangeMeters"); range != entry.end() && !ReadPositiveFloat(*range, form.rangeMeters)) {
				return false;
			}
			if (const auto damage = entry.find("damage"); damage != entry.end() && !ReadPositiveFloat(*damage, form.damage)) {
				return false;
			}
			if (const auto interval = entry.find("fireIntervalSeconds"); interval != entry.end() &&
				!ReadPositiveFloat(*interval, form.fireIntervalSeconds)) {
				return false;
			}
			if (const auto needsPower = entry.find("needsPower"); needsPower != entry.end()) {
				if (!needsPower->is_boolean()) {
					return false;
				}
				form.needsPower = needsPower->get<bool>();
			}
			Workshop::FormKey key{ plugin->get<std::string>(), localFormId->get<std::uint32_t>() };
			loaded.emplace(FormKeyString(key), form);
		}
		m_byForm = std::move(loaded);
		return true;
	}

	std::optional<TurretForm> Table::Find(const Workshop::FormKey& a_form) const
	{
		const auto found = m_byForm.find(FormKeyString(a_form));
		if (found == m_byForm.end()) {
			return std::nullopt;
		}
		return found->second;
	}
}

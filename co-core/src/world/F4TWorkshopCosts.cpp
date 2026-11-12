#include "F4TWorkshopCosts.h"

#include "F4TDropLedger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>

using Json = nlohmann::json;

namespace F4T::WorkshopCosts
{
	namespace
	{
		constexpr std::size_t MaximumEntries = 16384;
		constexpr std::size_t MaximumComponents = 64;
		constexpr std::uint64_t MaximumComponentCount = 100000;

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

		std::string ItemKey(const Workshop::FormKey& a_item)
		{
			char form[9]{};
			std::snprintf(form, sizeof(form), "%08X", a_item.localFormId);
			return Lowercase(a_item.plugin) + ":" + form;
		}
	}

	bool Table::LoadFromJson(const Json& a_root)
	{
		const auto costs = a_root.is_object() ? a_root.find("costs") : a_root.end();
		if (costs == a_root.end() || !costs->is_array() || costs->size() > MaximumEntries) {
			return false;
		}
		std::map<std::pair<std::string, std::uint32_t>, std::vector<Component>> loaded;
		for (const auto& entry : *costs) {
			Workshop::FormKey created;
			const auto components = entry.is_object() ? entry.find("components") : entry.end();
			if (!entry.is_object() || !ReadForm(entry, created) || components == entry.end() || !components->is_array() ||
				components->size() > MaximumComponents) {
				return false;
			}
			std::vector<Component> list;
			for (const auto& component : *components) {
				Component parsed;
				const auto count = component.is_object() ? component.find("count") : component.end();
				if (!component.is_object() || !ReadForm(component, parsed.item) || count == component.end() ||
					!count->is_number_unsigned() || count->get<std::uint64_t>() == 0 || count->get<std::uint64_t>() > MaximumComponentCount) {
					return false;
				}
				parsed.count = static_cast<std::uint32_t>(count->get<std::uint64_t>());
				list.push_back(std::move(parsed));
			}
			if (!loaded.emplace(std::pair{ created.plugin, created.localFormId }, std::move(list)).second) {
				return false;
			}
		}
		m_costs = std::move(loaded);
		return true;
	}

	const std::vector<Component>* Table::Find(const Workshop::FormKey& a_form) const
	{
		const auto found = m_costs.find({ Lowercase(a_form.plugin), a_form.localFormId });
		return found == m_costs.end() ? nullptr : &found->second;
	}

	Refund HalfRefund(const Table& a_table, const std::vector<Workshop::ObjectState>& a_culled)
	{
		Refund refund;
		std::map<std::string, std::uint64_t> totals;
		for (const auto& object : a_culled) {
			const auto* components = a_table.Find(object.form);
			if (components == nullptr || components->empty()) {
				++refund.unpricedPieces;
				continue;
			}
			++refund.pricedPieces;
			for (const auto& component : *components) {
				totals[ItemKey(component.item)] += component.count;
			}
		}
		for (const auto& [key, total] : totals) {
			const auto half = (std::min)(total / 2, static_cast<std::uint64_t>(F4T::Drops::kMaximumDropCount));
			if (half != 0) {
				refund.lines.push_back(Trade::Line{ key, static_cast<std::uint32_t>(half) });
			}
		}
		return refund;
	}
}

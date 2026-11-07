#pragma once

#include "F4TWorkshopMaterials.h"
#include "F4TWorkshopTransactions.h"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace F4T::WeaponClasses
{
	class Table
	{
	public:
		[[nodiscard]] bool LoadFromJson(const nlohmann::json& a_root);
		[[nodiscard]] std::size_t Size() const noexcept { return m_classes.size(); }
		[[nodiscard]] const std::optional<WorkshopMaterials::WeaponKind>* Find(const Workshop::FormKey& a_form) const;

	private:
		std::map<std::pair<std::string, std::uint32_t>, std::optional<WorkshopMaterials::WeaponKind>> m_classes;
	};
}

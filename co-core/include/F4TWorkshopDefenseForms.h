#pragma once

#include "F4TWorkshopDefenses.h"
#include "F4TWorkshopTransactions.h"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace F4T::WorkshopDefenseForms
{
	struct TurretForm
	{
		float ratingValue{ WorkshopDefenses::DefaultTurretRating };
		float rangeMeters{ WorkshopDefenses::DefaultTurretRangeMeters };
		float damage{ WorkshopDefenses::DefaultTurretDamage };
		float fireIntervalSeconds{ WorkshopDefenses::DefaultTurretFireIntervalSeconds };
		bool needsPower{ true };
	};

	class Table
	{
	public:
		[[nodiscard]] bool Load(const nlohmann::json& a_root);
		[[nodiscard]] std::optional<TurretForm> Find(const Workshop::FormKey& a_form) const;

	private:
		std::unordered_map<std::string, TurretForm> m_byForm;
	};

	[[nodiscard]] std::string FormKeyString(const Workshop::FormKey& a_form);
}

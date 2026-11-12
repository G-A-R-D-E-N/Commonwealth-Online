#include "F4TWorkshopDefenseForms.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <limits>

int main()
{
	using Json = nlohmann::json;
	using F4T::WorkshopDefenseForms::Table;
	using F4T::Workshop::FormKey;

	const auto turrets = [](const char* a_field, const Json& a_value) {
		Json entry{ { "plugin", "Fallout4.esm" }, { "localFormId", 1U } };
		entry[a_field] = a_value;
		return Json{ { "turrets", Json::array({ entry }) } };
	};

	Table accepted;
	assert(accepted.Load(turrets("rating", 250.5)));
	const auto found = accepted.Find(FormKey{ "Fallout4.esm", 1 });
	assert(found && found->ratingValue == 250.5F);

	for (const char* field : { "rating", "rangeMeters", "damage", "fireIntervalSeconds" }) {
		for (const double hostile : { 1e300, -1e300, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() }) {
			Table rejected;
			assert(!rejected.Load(turrets(field, hostile)));
		}
	}
	return 0;
}

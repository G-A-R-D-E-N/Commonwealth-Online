#include "F4TQuestStoryOrder.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <string>
#include <utility>

namespace
{
	using Json = nlohmann::json;

	Json Requirement(const std::string& a_quest)
	{
		return Json{ { "quest", a_quest }, { "completed", true } };
	}

	Json Row(const std::string& a_quest, Json a_requires, const std::string& a_source = "test fixture")
	{
		return Json{
			{ "quest", a_quest },
			{ "label", "Fixture" },
			{ "requires", std::move(a_requires) },
			{ "startable", true },
			{ "source", a_source },
			{ "verified", false }
		};
	}

	Json Document(Json a_rows)
	{
		return Json{ { "quests", std::move(a_rows) } };
	}
}

int main()
{
	using F4T::QuestStory::Order;

	const auto valid = Order::Load(Document(Json::array({
		Row("0A1B2C:fallout4.esm", Json::array()),
		Row("0A1B2D:fallout4.esm", Json::array({ Requirement("0A1B2C:fallout4.esm") }))
	})));
	assert(valid.has_value());
	assert(valid->Size() == 2);
	assert(valid->Find(*F4T::QuestProtocol::ParseFormKey("0A1B2D:fallout4.esm")) != nullptr);

	assert(!Order::Load(Document(Json::array({
		Row("0A1B2C:fallout4.esm", Json::array()),
		Row("0A1B2C:Fallout4.esm", Json::array())
	}))));

	assert(!Order::Load(Document(Json::array({
		Row("0A1B2C:fallout4.esm", Json::array({ Requirement("0A1B2D:fallout4.esm") })),
		Row("0A1B2D:fallout4.esm", Json::array({ Requirement("0A1B2C:fallout4.esm") }))
	}))));

	assert(!Order::Load(Document(Json::array({
		Row("0A1B2C:fallout4.esm", Json::array({ Requirement("0A1B2E:fallout4.esm") }))
	}))));

	assert(!Order::Load(Document(Json::array({
		Row("0A1B2C:fallout4.esm", Json::array(), "")
	}))));

	assert(!Order::Load(Json{ { "rows", Json::array() } }));
	assert(Order::Load(Document(Json::array())).has_value());

	const auto shipped = Order::Default();
	assert(!shipped.Empty());
	assert(shipped.Find(*F4T::QuestProtocol::ParseFormKey("01CC2A:fallout4.esm")) != nullptr);

	return 0;
}

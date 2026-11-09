#include "F4TFurnitureSync.h"

#include <cassert>
#include <cstdint>
#include <limits>
#include <string>

namespace
{
	using Json = nlohmann::json;
	using namespace F4T::FurnitureSync;

	Json ClientUse(const char* a_action)
	{
		return Json{ { "type", "furnitureUse" }, { "action", a_action } };
	}
}

int main()
{
	assert(kCapabilityName == "furniture-sync-v1");
	assert(ParseAction("enter") == Action::Enter);
	assert(ParseAction("exit") == Action::Exit);
	assert(!ParseAction("Enter"));
	assert(!ParseAction("sit"));
	assert(!ParseAction(""));
	assert(ActionName(Action::Enter) == "enter");
	assert(ActionName(Action::Exit) == "exit");

	assert(ParseClientUse(ClientUse("enter")) == Action::Enter);
	assert(ParseClientUse(ClientUse("exit")) == Action::Exit);
	assert(!ParseClientUse(ClientUse("stand")));
	assert(!ParseClientUse(Json{ { "type", "furnitureUse" } }));
	assert(!ParseClientUse(Json{ { "type", "furnitureUse" }, { "action", 1 } }));
	assert(!ParseClientUse(Json{ { "type", "furnitureUse" }, { "action", Json::array({ "enter" }) } }));
	assert(!ParseClientUse(Json::array()));

	const auto relayed = ParseRelayedUse(Json{ { "type", "furnitureUse" }, { "playerId", 7 }, { "action", "exit" } });
	assert(relayed && relayed->playerId == 7 && relayed->action == Action::Exit);
	assert(!ParseRelayedUse(Json{ { "type", "furnitureUse" }, { "playerId", 0 }, { "action", "enter" } }));
	assert(!ParseRelayedUse(Json{ { "type", "furnitureUse" }, { "playerId", -1 }, { "action", "enter" } }));
	assert(!ParseRelayedUse(Json{ { "type", "furnitureUse" }, { "playerId", "7" }, { "action", "enter" } }));
	assert(!ParseRelayedUse(Json{ { "type", "furnitureUse" }, { "playerId", 1.5 }, { "action", "enter" } }));
	assert(!ParseRelayedUse(Json{ { "type", "furnitureUse" }, { "playerId", 7 }, { "action", "bogus" } }));
	assert(!ParseRelayedUse(Json{ { "type", "furnitureUse" }, { "action", "enter" } }));

	const auto maximum = ParseRelayedUse(Json{ { "playerId", static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)()) }, { "action", "enter" } });
	assert(maximum && maximum->playerId == (std::numeric_limits<std::uint32_t>::max)());
	assert(!ParseRelayedUse(Json{ { "playerId", static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)()) + 1 }, { "action", "enter" } }));

	return 0;
}

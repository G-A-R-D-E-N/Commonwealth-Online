#include "F4TSleepBedIdentity.h"
#include "F4TRemotePlayerCapacity.h"

#include <cassert>
#include <limits>
#include <variant>

int main()
{
	using namespace F4T::SleepBedIdentity;
	using Json = nlohmann::json;
	assert(IsEnabledMarker(1, 0));
	assert(!IsEnabledMarker(0, 0));
	assert(IsEnabledMarker(std::uint32_t{ 1 } << 21, 21));
	assert(!IsEnabledMarker(std::uint32_t{ 1 } << 22, 22));
	assert(!IsEnabledMarker(std::uint32_t{ 1 } << 31, 31));
	assert(!IsEnabledMarker((std::numeric_limits<std::uint32_t>::max)(), (std::numeric_limits<std::uint32_t>::max)()));
	const auto form = ParseBedIdentity(Json{ { "kind", "form" }, { "key", "001234:fallout4.esm" } });
	assert(form && std::holds_alternative<F4T::QuestProtocol::FormKey>(*form));
	const auto workshop = ParseBedIdentity(Json{ { "kind", "workshop" }, { "zone", 2u }, { "object", 3u } });
	assert(workshop && std::get<WorkshopObject>(*workshop) == (WorkshopObject{ 2, 3 }));
	assert(ParseBedIdentity(Json{ { "kind", "workshop" }, { "zone", 2 }, { "object", 3 } }));
	const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
	const auto large = ParseBedIdentity(Json{ { "kind", "workshop" }, { "zone", maximum }, { "object", maximum } });
	assert(large && std::get<WorkshopObject>(*large) == (WorkshopObject{ maximum, maximum }));
	const auto built = MakeSleepEnter(*large, 3);
	assert(built && (*built)["bed"]["zone"].get<std::uint64_t>() == maximum && (*built)["marker"] == 3);
	auto wire = Json::parse(built->dump());
	wire["playerId"] = 7;
	assert(ParseRelayedSleep(wire)->bed == large);
	assert(MakeSleepExit().at("action") == "exit");
	assert(!MakeSleepEnter(WorkshopObject{ 0, 3 }, 0));
	assert(!MakeSleepEnter(*large, (std::numeric_limits<std::uint32_t>::max)()));
	assert(MakeSleepEnter(*large, 21));
	assert(!MakeSleepEnter(*large, 22));
	assert(!MakeSleepEnter(F4T::QuestProtocol::FormKey{ "fallout4.esm", 0x01001234 }, 0));
	assert(!ParseBedIdentity(Json{ { "kind", "form" }, { "key", "bad" } }));
	assert(!ParseBedIdentity(Json{ { "kind", "workshop" }, { "zone", 0u }, { "object", 3u } }));
	assert(!ParseBedIdentity(Json{ { "kind", "workshop" }, { "zone", 2u }, { "object", -1 } }));
	assert(!ParseBedIdentity(Json{ { "kind", "workshop" }, { "zone", 2.5 }, { "object", 3u } }));
	assert(!ParseBedIdentity(Json{ { "kind", "other" } }));
	assert(!ParseBedIdentity(Json::array()));
	const Json enter{ { "playerId", 7 }, { "action", "enter" }, { "bed", { { "kind", "workshop" }, { "zone", 2 }, { "object", 3 } } }, { "marker", 0 } };
	const auto relayed = ParseRelayedSleep(enter);
	assert(relayed && relayed->playerId == 7 && relayed->bed && relayed->marker == 0);
	const auto exit = ParseRelayedSleep(Json{ { "playerId", 7 }, { "action", "exit" } });
	assert(exit && exit->playerId == 7 && !exit->bed);
	assert(!ParseRelayedSleep(Json{ { "playerId", 0 }, { "action", "exit" } }));
	assert(!ParseRelayedSleep(Json{ { "playerId", 7 }, { "action", "enter" }, { "bed", enter["bed"] } }));
	assert(!ParseRelayedSleep(Json{ { "playerId", 7 }, { "action", "enter" }, { "bed", enter["bed"] }, { "marker", -1 } }));
	assert(ParseRelayedSleep(Json{ { "playerId", 7 }, { "action", "enter" }, { "bed", enter["bed"] }, { "marker", 21 } }));
	assert(!ParseRelayedSleep(Json{ { "playerId", 7 }, { "action", "enter" }, { "bed", enter["bed"] }, { "marker", 22 } }));
	assert(!ParseRelayedSleep(Json{ { "playerId", 7 }, { "action", "enter" }, { "bed", enter["bed"] }, { "marker", (std::numeric_limits<std::uint32_t>::max)() } }));
	assert(!ParseRelayedSleep(Json{ { "playerId", 7 }, { "action", "other" } }));
	RemoteStates states;
	assert(!states.Apply(RelayedSleep{}));
	assert(states.Apply(*relayed));
	const auto enteredRevision = states.Revision(7);
	auto another = *relayed;
	another.playerId = 8;
	assert(states.Apply(another));
	assert(states.Revision(7) == enteredRevision);
	assert(states.Apply(*exit));
	assert(states.Apply(*relayed));
	assert(states.Revision(7) != enteredRevision);
	states.Erase(8);
	assert(states.Find(7) && states.Find(7)->marker == 0);
	auto moved = *relayed;
	moved.marker = 2;
	assert(states.Apply(moved) && states.Find(7)->marker == 2);
	assert(states.Apply(*exit) && !states.Find(7));
	for (std::uint32_t id = 1; id <= F4T::RemotePlayerCapacity::MaximumTrackedRemotePlayers; ++id) {
		moved.playerId = id;
		assert(states.Apply(moved));
	}
	moved.playerId = static_cast<std::uint32_t>(F4T::RemotePlayerCapacity::MaximumTrackedRemotePlayers + 1);
	assert(!states.Apply(moved));
	moved.playerId = 1;
	assert(states.Apply(moved));
	assert(states.Apply(RelayedSleep{ 1, std::nullopt, 0 }));
	assert(!states.Find(1));
	moved.playerId = static_cast<std::uint32_t>(F4T::RemotePlayerCapacity::MaximumTrackedRemotePlayers + 1);
	assert(states.Apply(moved));
	states.Erase(moved.playerId);
	assert(!states.Find(moved.playerId));
	states.Clear();
	assert(!states.Find(2));
}

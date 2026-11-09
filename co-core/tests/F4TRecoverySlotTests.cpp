#include "../src/protocol/F4TGnsProtocolAdapterSlots.h"

#include <nlohmann/json.hpp>
#include <cassert>

int main()
{
    using nlohmann::json;
    using F4T::GnsProtocolAdapter::Detail::ForwardCharacterSlotsToWelcome;
    json packet{ { "persistenceProfile", "co-12345678#1" }, { "characterSlots", { { "bound", true }, { "selected", 1 }, { "slots", 3 } } } };
    json welcome = json::object();
    assert(ForwardCharacterSlotsToWelcome(packet, welcome, "co-12345678"));
    assert(welcome.at("persistenceProfile") == "co-12345678#1");
    assert(!ForwardCharacterSlotsToWelcome(packet, welcome, "co-other000"));
    packet["characterSlots"]["selected"] = 2;
    assert(!ForwardCharacterSlotsToWelcome(packet, welcome, "co-12345678"));
    packet["characterSlots"]["selected"] = 1;
    packet["characterSlots"]["slots"] = 1;
    assert(!ForwardCharacterSlotsToWelcome(packet, welcome, "co-12345678"));
}

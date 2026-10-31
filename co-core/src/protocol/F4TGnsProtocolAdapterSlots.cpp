#include "F4TGnsProtocolAdapterSlots.h"

#include <nlohmann/json.hpp>

namespace F4T::GnsProtocolAdapter::Detail
{
	void AddSlotProtocolToHello(nlohmann::json& a_hello)
	{
		a_hello["slotProtocol"] = 1;
	}

	void ForwardCharacterSlotsToWelcome(const nlohmann::json& a_sessionReady, nlohmann::json& a_welcome)
	{
		if (const auto slots = a_sessionReady.find("characterSlots"); slots != a_sessionReady.end()) {
			a_welcome["characterSlots"] = *slots;
		}
	}
}

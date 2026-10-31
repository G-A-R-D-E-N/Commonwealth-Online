#pragma once

#include <nlohmann/json_fwd.hpp>

namespace F4T::GnsProtocolAdapter::Detail
{
	void AddSlotProtocolToHello(nlohmann::json& a_hello);
	void ForwardCharacterSlotsToWelcome(const nlohmann::json& a_sessionReady, nlohmann::json& a_welcome);
}

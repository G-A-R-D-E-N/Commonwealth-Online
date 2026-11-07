#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string_view>

namespace F4T::GnsProtocolAdapter::Detail
{
	void AddSlotProtocolToHello(nlohmann::json& a_hello);
	bool ForwardCharacterSlotsToWelcome(const nlohmann::json& a_sessionReady, nlohmann::json& a_welcome, std::string_view a_account);
}

#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace F4T::SettlementCaptureClient
{
	bool TryHandlePacket(std::string_view a_type, const nlohmann::json& a_packet);
	void Reset();
	void NoteLocalPosition(float a_x, float a_y, float a_z);
	bool HandleAction(std::string_view a_actionJson);
	std::string WithCaptureState(std::string a_stateJson);
}

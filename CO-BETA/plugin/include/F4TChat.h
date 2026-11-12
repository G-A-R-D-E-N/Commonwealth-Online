#pragma once

#include <nlohmann/json.hpp>

#include <string_view>

namespace F4T::Chat
{
	void NoteWelcome(const nlohmann::json& a_welcome);
	bool TryHandleChatPacket(std::string_view a_type, const nlohmann::json& a_packet);
	void Reset();
	void HandleAction(std::string_view a_actionJson);
}

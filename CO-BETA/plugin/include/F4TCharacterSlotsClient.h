#pragma once

#include "F4TSessionLaunch.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <string_view>

namespace F4T::CharacterSlotsClient
{
	void NoteWelcome(const nlohmann::json& a_packet);
	bool TryHandlePacket(std::string_view a_type, const nlohmann::json& a_packet);
	void Reset();
	bool HandleAction(std::string_view a_actionJson);
	std::string WithCharacterSlots(std::string a_stateJson);
	bool NeedsSelection();
	bool IsReconnecting();
	void DeferLaunch(const F4T::SessionLaunch::JoinRequest& a_request, std::function<void()> a_resume, std::function<void(bool, std::string_view)> a_cancel);
	bool CancelDeferredLaunch(std::string_view a_reason = "Connection cancelled.");
}

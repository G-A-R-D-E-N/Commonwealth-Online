#pragma once

#include "F4TSessionLaunch.h"

#include <nlohmann/json.hpp>

#include <string>

namespace F4T::FirstJoinAbandon
{
	F4T::SessionLaunch::LaunchOutcome BeginForSaveLessHold(
		const std::string& a_serverKey,
		F4T::SessionLaunch::JoinCompletion a_onComplete,
		std::string& a_error);
	void NoteWelcome(const nlohmann::json& a_packet);
	void NoteReplyReceived(bool a_ok, const std::string& a_reason);
	void Tick();
	bool CancelPending();
}

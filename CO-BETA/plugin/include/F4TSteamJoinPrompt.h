#pragma once

#include "F4TSteamTunnel.h"

#include <cstdint>
#include <stop_token>
#include <string_view>

namespace F4T::SteamJoinPrompt
{
	[[nodiscard]] SteamAdmission::JoinMode JoinModeFromPreference(std::uint8_t a_preference);
	[[nodiscard]] std::string_view JoinPromptMessage(SteamAdmission::FriendCheck a_friendCheck);
	[[nodiscard]] bool Ask(const SteamAdmission::TunnelRequest& a_request, std::stop_token a_stop);
	void Install();
}

#pragma once

#include <cstdint>
#include <string_view>

namespace F4T::VoiceRuntime
{
	void Start();
	void SubmitAction(std::string_view a_actionJson);
	[[nodiscard]] bool IsPlayerMuted(std::uint32_t a_playerId);
}

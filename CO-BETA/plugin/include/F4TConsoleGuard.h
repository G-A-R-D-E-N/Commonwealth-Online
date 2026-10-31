#pragma once

#include <nlohmann/json.hpp>

namespace F4T::ConsoleGuard
{
	void NoteWelcome(const nlohmann::json& a_packet);
	void Tick();
	void Reset();
	void ExecutePluginCommand(const char* a_command);
}

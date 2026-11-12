#pragma once

#include <nlohmann/json.hpp>

#include <string_view>

namespace RE::BSScript
{
	class IVirtualMachine;
}

namespace F4T::SurvivalNeedsClient
{
	bool TryHandlePacket(std::string_view a_type, const nlohmann::json& a_packet);
	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm);
	void FlushPendingSends();
	void Reset();
	void RegisterHud();
	void PushHudState();
	void ClearHud();
}

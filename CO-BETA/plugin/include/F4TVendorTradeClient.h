#pragma once

#include <nlohmann/json.hpp>

namespace F4T::VendorTradeClient
{
	void Attach();
	void HandlePacket(const nlohmann::json& a_packet);
	[[nodiscard]] bool HandleWindowAction(const nlohmann::json& a_action);
	[[nodiscard]] bool Active();
	void Tick();
	void Reset();
}

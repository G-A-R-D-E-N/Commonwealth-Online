#pragma once

#include <nlohmann/json.hpp>

#include "F4TVendorTradeEscrow.h"

namespace F4T::VendorTradeClient
{
	void Attach();
	void HandlePacket(const nlohmann::json& a_packet);
	[[nodiscard]] bool HandleWindowAction(const nlohmann::json& a_action);
	[[nodiscard]] bool Active();
	void Tick();
	[[nodiscard]] F4T::VendorTradeEscrow::Parked SnapshotParked();
	void RestoreParked(F4T::VendorTradeEscrow::Parked a_parked);
	void Reset();
}

#pragma once

#include "RE/B/BSPointerHandle.h"

#include <nlohmann/json.hpp>

#include <string_view>

namespace F4T::ExplosionSync
{
	void NoteWelcome(const nlohmann::json& a_welcome);
	void Tick(bool a_connected);
	void FlushOutbound();
	void DiscardOutbound();
	void HandleRelayedPacket(const nlohmann::json& a_packet);
	bool TryHandlePacket(std::string_view a_type, const nlohmann::json& a_packet);
	bool IsReplayedExplosion(const RE::ObjectRefHandle& a_ref);
}

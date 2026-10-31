#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::Factions
{

	void NoteWelcome(const nlohmann::json& a_welcome);
	bool TryHandlePacket(std::string_view a_type, const nlohmann::json& a_packet);
	void Reset();
	std::optional<std::string> MemberName(std::uint32_t a_playerId);
	bool InFaction();
	std::string BuildStateJson();
	void HandleAction(std::string_view a_actionJson);
	void QueueGameTask(std::function<void()> a_task);
	void QueueStateChanged();
	void QueueKnownSetChanged();
}

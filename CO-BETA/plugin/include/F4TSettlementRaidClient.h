#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace F4T::SettlementRaidClient
{
	bool TryHandlePacket(std::string_view a_type, const nlohmann::json& a_packet);
	void NoteCapabilities(const nlohmann::json& a_packet);
	void Reset();
	bool HandleAction(std::string_view a_actionJson);
	std::string WithSettlementRaid(std::string a_stateJson, std::size_t a_jsonOutSize);
	std::uint64_t PlayerProtectionSecondsLeft();
	void NoteServerTime(const nlohmann::json& a_welcome);
	std::uint64_t ServerUnixNow();
}

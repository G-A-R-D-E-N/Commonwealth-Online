#pragma once

#include "F4TPowerArmorSync.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace RE
{
	class PlayerCharacter;
}

namespace F4T::ParkedSuitSync
{
	struct Record
	{
		std::uint32_t suitId{};
		std::string ownerKey;
	};

	void PublishLocal(RE::PlayerCharacter& a_player, const std::optional<F4T::PowerArmorSync::PowerArmorState>& a_captured);
	[[nodiscard]] std::unordered_set<std::uint32_t> SnapshotFrameRefs();
	void ParkSpawnedFrames(const std::unordered_set<std::uint32_t>& a_before);
	void OnDisconnected();
	void OnWorldReset();
	void RemoveOwnSuit(std::uint32_t a_suitId);
	[[nodiscard]] bool TryHandlePacket(std::string_view a_packetType, const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<Record> Find(std::uint32_t a_suitId);
	[[nodiscard]] std::optional<bool> LastHackResult(std::uint32_t a_suitId);
	void HandleUiAction(std::string_view a_actionJson);
	[[nodiscard]] std::string BuildUiStateJson();
}

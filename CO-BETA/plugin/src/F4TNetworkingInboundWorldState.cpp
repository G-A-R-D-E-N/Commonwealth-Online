#include "pch.h"

#include "F4TNetworkingInboundWorldState.h"
#include "F4TNetworkingInternal.h"
#include "F4TDebugLogging.h"
#include "F4TWorldStateSync.h"

#include <format>
#include <utility>

namespace F4T::Networking::Detail
{
	void HandleWorldStatePacket(const Json& a_packet)
	{
		const auto gameHour = ReadDouble(a_packet, "gameHour");
		const auto gameDaysPassed = ReadDouble(a_packet, "gameDaysPassed");
		if (!gameHour || !gameDaysPassed) {
			LogThrottledWarning(
				"world_state_missing_fields",
				"Ignoring worldState packet missing required gameHour or gameDaysPassed.");
			return;
		}

		if (*gameHour < 0.0 || *gameHour > 24.0 || *gameDaysPassed < 0.0 || *gameDaysPassed > 1e9) {
			LogThrottledWarning("world_state_invalid_numbers", "Ignoring worldState packet with out-of-range time values.");
			return;
		}

		F4T::WorldStateSync::PendingWorldState pendingState{
			static_cast<float>(*gameHour),
			static_cast<float>(*gameDaysPassed),
			std::nullopt,
			false,
			ReadBool(a_packet, "timeSync").value_or(false)
		};

		const auto weatherFormIdText = ReadString(a_packet, "weatherFormId");
		if (weatherFormIdText && !weatherFormIdText->empty()) {
			if (const auto weatherFormId = ParseHexFormId(*weatherFormIdText)) {
				pendingState.weatherFormId = *weatherFormId;
				pendingState.hasWeatherFormId = true;
			} else if (ShouldLogWarning("world_state_weather_parse")) {
				LogWarningWithLocalPlayerPrefix(std::format(
					"Ignoring invalid weatherFormId '{}' in worldState packet.",
					F4T::DebugLogging::SanitizeLogText(*weatherFormIdText, 32)));
			}
		}

		if (ShouldLogRemoteUpdate(1)) {
			const auto timeSyncNote = pendingState.applyTime ? "timeSync=yes" : "timeSync=no";
			if (pendingState.hasWeatherFormId && pendingState.weatherFormId) {
				LogInfoWithLocalPlayerPrefix(std::format(
					"Queued host worldState ({}): gameHour={:.2f}, gameDaysPassed={:.3f}, weatherFormId=0x{:08X}.",
					timeSyncNote,
					pendingState.gameHour,
					pendingState.gameDaysPassed,
					*pendingState.weatherFormId));
			} else {
				LogInfoWithLocalPlayerPrefix(std::format(
					"Queued host worldState ({}): gameHour={:.2f}, gameDaysPassed={:.3f}, weatherFormId=<none>.",
					timeSyncNote,
					pendingState.gameHour,
					pendingState.gameDaysPassed));
			}
		}

		F4T::WorldStateSync::SetPendingWorldState(std::move(pendingState));
	}

	void HandleWorldStateHostPacket(const Json& a_packet)
	{
		const auto hostPlayerId = ReadPlayerId(a_packet, "worldStateHostPlayerId");
		if (!hostPlayerId) {
			LogThrottledWarning(
				"world_state_host_missing",
				"Ignoring worldStateHost packet without a valid worldStateHostPlayerId.");
			return;
		}

		F4T::WorldStateSync::QueueWorldStateHostPlayerId(*hostPlayerId);
	}
}

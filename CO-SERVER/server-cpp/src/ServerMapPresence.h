#pragma once

#include "F4TRemotePlayerCapacity.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server
{
	inline constexpr double kMapPresenceIntervalSeconds = 1.0;
	inline constexpr std::size_t kMapPresenceMaximumPlayers = F4T::RemotePlayerCapacity::MaximumSessionPlayers;

	struct MapPresencePeer
	{
		std::uint32_t playerId{};
		std::string   name;
		double        x{};
		double        y{};
		double        z{};
		double        angleZ{};
		std::string   worldspaceId;
		std::string   cellId;
		std::string   tag;
		bool          downed{};
		std::string   steamId;
		std::string   teamId;
		std::string   socialKey;
		bool          showLocation{ true };
		std::string   profileId;
		bool          friendOfViewer{};
	};

	struct MapViewer
	{
		std::uint32_t playerId{};
		std::string   tag;
		std::string   teamId;
	};

	[[nodiscard]] bool CanSeeOnMap(bool a_publicVisible, const MapViewer& a_viewer, const MapPresencePeer& a_subject, bool a_friends);
	[[nodiscard]] nlohmann::json MapPresencePacket(const std::vector<MapPresencePeer>& a_peers, double a_unixTimeSeconds, bool a_publicVisible = true);
}

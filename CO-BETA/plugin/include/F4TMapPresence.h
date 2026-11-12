#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace F4T::MapPresence
{
	struct Entry
	{
		std::uint32_t playerId{ 0 };
		std::string name;
		double x{ 0.0 };
		double y{ 0.0 };
		double z{ 0.0 };
		double angleZ{ 0.0 };
		std::string worldspaceId;
		std::string cellId;
		std::string tag;
		bool downed{ false };
		bool friendOfViewer{ false };
	};

	void HandlePacket(const nlohmann::json& a_packet);
	void Clear();
	bool PublicPlayersVisible();
	std::vector<Entry> Snapshot();
	nlohmann::json BuildMapPlayers();
}

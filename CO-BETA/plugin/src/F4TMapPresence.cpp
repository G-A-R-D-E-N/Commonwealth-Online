#include "pch.h"

#include "F4TMapPresence.h"

#include "F4TFactionPayload.h"
#include "F4TFactions.h"
#include "F4TFireteamClient.h"
#include "F4TNetworkingInternal.h"
#include "F4TRemotePlayerState.h"
#include "F4TRemotePlayerCapacity.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace F4T::MapPresence
{
	using namespace F4T::Networking::Detail;

	namespace
	{
		constexpr std::size_t kMaximumPlayers = F4T::RemotePlayerCapacity::MaximumSessionPlayers;
		constexpr std::size_t kMaximumNameBytes = 128;
		constexpr std::size_t kMaximumFormIdBytes = 32;
		constexpr std::size_t kMaximumTagBytes = 8;
		constexpr auto kStaleAfter = std::chrono::seconds{ 5 };

		std::mutex g_mutex;
		std::vector<Entry> g_entries;
		std::chrono::steady_clock::time_point g_receivedAt{};
		bool g_publicVisible = true;

		std::string ReadText(const Json& a_json, const char* a_field, std::size_t a_maximumBytes)
		{
			return F4T::Factions::Payload::SanitizeText(ReadString(a_json, a_field).value_or(""), a_maximumBytes);
		}

		std::optional<Entry> ParseEntry(const Json& a_player)
		{
			const auto playerId = ReadPlayerId(a_player, "playerId");
			const auto x = ReadDouble(a_player, "x");
			const auto y = ReadDouble(a_player, "y");
			const auto z = ReadDouble(a_player, "z").value_or(0.0);
			const auto angleZ = ReadDouble(a_player, "angleZ");
			if (!playerId || *playerId == 0 || !x || !y || !angleZ) {
				return std::nullopt;
			}

			auto name = ReadText(a_player, "name", kMaximumNameBytes);
			if (name.empty()) {
				name = "Unknown";
			}
			return Entry{
				*playerId,
				std::move(name),
				*x,
				*y,
				z,
				*angleZ,
				ReadText(a_player, "worldspaceId", kMaximumFormIdBytes),
				ReadText(a_player, "cellId", kMaximumFormIdBytes),
				ReadText(a_player, "tag", kMaximumTagBytes),
				ReadBool(a_player, "downed").value_or(false),
				ReadBool(a_player, "friend").value_or(false)
			};
		}
	}

	void HandlePacket(const Json& a_packet)
	{
		const auto players = a_packet.find("players");
		if (players == a_packet.end() || !players->is_array() || players->size() > kMaximumPlayers) {
			LogThrottledWarning("map_presence_malformed", "Ignoring malformed mapPresence packet.");
			return;
		}

		std::vector<Entry> entries;
		for (const auto& player : *players) {
			if (auto entry = ParseEntry(player)) {
				entries.push_back(std::move(*entry));
			} else {
				LogThrottledWarning("map_presence_entry", "Ignoring a malformed mapPresence entry.");
			}
		}

		std::scoped_lock lock(g_mutex);
		g_entries = std::move(entries);
		g_publicVisible = ReadBool(a_packet, "publicPlayersVisible").value_or(true);
		g_receivedAt = std::chrono::steady_clock::now();
	}

	void Clear()
	{
		std::scoped_lock lock(g_mutex);
		g_entries.clear();
		g_publicVisible = true;
	}

	bool PublicPlayersVisible()
	{
		std::scoped_lock lock(g_mutex);
		return g_publicVisible;
	}

	std::vector<Entry> Snapshot()
	{
		std::scoped_lock lock(g_mutex);
		if (g_entries.empty() || std::chrono::steady_clock::now() - g_receivedAt > kStaleAfter) {
			return {};
		}
		return g_entries;
	}

	nlohmann::json BuildMapPlayers()
	{
		nlohmann::json players = nlohmann::json::array();
		const auto entries = Snapshot();
		const auto localPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId().value_or(0);
		if (entries.empty() || localPlayerId == 0) {
			if (!PublicPlayersVisible()) {
				return players;
			}
			for (const auto& remote : F4T::RemotePlayerState::GetRemotePlayerSnapshot()) {
				nlohmann::json player{
					{ "id", remote.playerId },
					{ "x", remote.x },
					{ "y", remote.y },
					{ "z", remote.z },
					{ "angleZ", remote.angleZ },
					{ "worldspaceId", remote.worldspaceId },
					{ "cellId", remote.cellId },
				};
				if (!remote.characterName.empty()) {
					player["name"] = remote.characterName;
				}
				if (remote.vitals && remote.vitals->isDowned && !remote.vitals->isDead) {
					player["downed"] = true;
				}
				if (const auto memberName = F4T::Factions::MemberName(remote.playerId)) {
					player["faction"] = true;
					player["name"] = *memberName;
					player["factionName"] = F4T::Factions::OwnFactionTag();
				}
				players.push_back(std::move(player));
			}
			return players;
		}
		const auto ownTag = F4T::Factions::OwnFactionTag();
		std::vector<std::uint32_t> partyIds;
		for (const auto& member : F4T::FireteamClient::Current(F4T::FireteamClient::MonotonicMilliseconds()).team.members) {
			partyIds.push_back(member.playerId);
		}
		for (const auto& entry : entries) {
			if (entry.playerId == localPlayerId) {
				continue;
			}
			const bool friendly = entry.friendOfViewer;
			const bool faction = F4T::Factions::MemberName(entry.playerId).has_value() || (!entry.tag.empty() && entry.tag == ownTag);
			players.push_back(nlohmann::json{
				{ "id", entry.playerId },
				{ "x", entry.x },
				{ "y", entry.y },
				{ "z", entry.z },
				{ "angleZ", entry.angleZ },
				{ "worldspaceId", entry.worldspaceId },
				{ "cellId", entry.cellId },
				{ "name", entry.name },
				{ "factionName", entry.tag },
				{ "downed", entry.downed },
				{ "faction", faction },
				{ "friend", friendly },
				{ "party", std::find(partyIds.begin(), partyIds.end(), entry.playerId) != partyIds.end() } });
		}
		return players;
	}
}

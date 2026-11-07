#include "ServerMapPresence.h"

#include "ServerProtocolImpl.h"

#include "F4TChatValidation.h"

#include <utility>

namespace CommonwealthOnline::Server
{
	bool CanSeeOnMap(bool a_publicVisible, const MapViewer& a_viewer, const MapPresencePeer& a_subject, bool a_friends)
	{
		if (a_viewer.playerId == a_subject.playerId) return true;
		if (a_subject.showLocation && (a_publicVisible || a_friends)) return true;
		if (!a_viewer.tag.empty() && a_viewer.tag == a_subject.tag) return true;
		return !a_viewer.teamId.empty() && a_viewer.teamId == a_subject.teamId;
	}

	nlohmann::json MapPresencePacket(const std::vector<MapPresencePeer>& a_peers, double a_unixTimeSeconds, bool a_publicVisible)
	{
		nlohmann::json players = nlohmann::json::array();
		for (const auto& peer : a_peers) {
			players.push_back(nlohmann::json{
				{ "playerId", peer.playerId },
				{ "name", peer.name },
				{ "x", peer.x },
				{ "y", peer.y },
				{ "z", peer.z },
				{ "angleZ", peer.angleZ },
				{ "worldspaceId", peer.worldspaceId },
				{ "cellId", peer.cellId },
				{ "tag", peer.tag },
				{ "downed", peer.downed },
				{ "steamId", peer.steamId },
				{ "friend", peer.friendOfViewer }
			});
		}
		return nlohmann::json{ { "type", "mapPresence" }, { "serverTime", a_unixTimeSeconds }, { "publicPlayersVisible", a_publicVisible }, { "players", std::move(players) } };
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::PollNpcLootTakes(double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		return SweepNpcLootTakes(a_monotonicSeconds, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::PollPresence(double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		if (sessions.empty() || a_monotonicSeconds < nextPresenceMonotonic) return {};
		nextPresenceMonotonic = a_monotonicSeconds + kMapPresenceIntervalSeconds;
		std::vector<MapPresencePeer> peers;
		for (const auto& [connectionId, session] : sessions) {
			if (!session.active || !session.lastMovement) continue;
			if (peers.size() >= kMapPresenceMaximumPlayers) break;
			const auto& movement = *session.lastMovement;
			peers.push_back(MapPresencePeer{
				session.playerId,
				F4T::ChatValidation::SanitizeChatDisplayName(SessionNameView(session)),
				movement.x,
				movement.y,
				movement.z,
				movement.angleZ,
				movement.worldspaceId,
				movement.cellId,
				factionService.TagForProfile(session.profileId),
				SessionVitals(session).downed,
				session.steamId,
				fireteamService.TeamIdOfPlayer(session.playerId),
				std::string{ SessionSocialKeyView(session) },
				session.privacy.showLocation });
		}
		std::vector<OutboundFrame> frames;
		for (const auto& [connectionId, recipient] : sessions) {
			if (!recipient.active) continue;
			const MapViewer viewer{ recipient.playerId, factionService.TagForProfile(recipient.profileId), fireteamService.TeamIdOfPlayer(recipient.playerId) };
			const auto viewerSocialKey = std::string{ SessionSocialKeyView(recipient) };
			std::vector<MapPresencePeer> visible;
			for (const auto& peer : peers) {
				const auto friends = socialGraph.AreFriends(viewerSocialKey, peer.socialKey);
				if (!CanSeeOnMap(mapPublicPlayers, viewer, peer, friends)) continue;
				auto visiblePeer = peer;
				visiblePeer.friendOfViewer = friends;
				if (peer.playerId != recipient.playerId) visiblePeer.steamId.clear();
				visible.push_back(std::move(visiblePeer));
			}
			frames.push_back(ReliableFrame(connectionId, MapPresencePacket(visible, a_unixTimeSeconds, mapPublicPlayers)));
		}
		return frames;
	}
}

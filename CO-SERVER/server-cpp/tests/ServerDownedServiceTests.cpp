#include "ServerDowned.h"
#include "ServerOutboundFrames.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <iostream>
#include <string>

int main()
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::AppendFrames;
	using CommonwealthOnline::Server::DownedPeer;
	using CommonwealthOnline::Server::DownedService;
	using CommonwealthOnline::Server::DownedSessionState;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolStats;
	using CommonwealthOnline::Server::ReliableFrame;

	ProtocolStats stats;
	DownedService service{ stats };
	DownedService::Peers peers{
		DownedPeer{ 10, 1, std::nullopt },
		DownedPeer{ 20, 2, std::nullopt }
	};
	service.Join(peers, 20, 2, "profile-victim", 1.0);

	const auto downed = service.ObserveVitals([&peers] { return peers; }, 2, true, false, 2.0, true);
	const Json downedPacket{
		{ "type", "downedState" },
		{ "playerId", 2 },
		{ "downed", true },
		{ "remainingMs", 60000 }
	};
	assert(downed.size() == peers.size());
	std::vector<OutboundFrame> folded;
	for (const auto& peer : peers) {
		AppendFrames(folded, { ReliableFrame(peer.connectionId, downedPacket) });
	}
	assert(folded.size() == downed.size());
	for (std::size_t index = 0; index < downed.size(); ++index) {
		assert(downed[index].connectionId == folded[index].connectionId);
		assert(downed[index].delivery == folded[index].delivery);
		assert(downed[index].payload == folded[index].payload);
	}

	DownedSessionState state;
	const Json request{
		{ "type", "respawnRequest" },
		{ "point", "sanctuary" }
	};
	const auto early = service.Dispatch("respawnRequest", peers, 2, state, request, 2.1, 5.0);
	assert(early.has_value());
	assert(early->empty());
	assert(stats.respawnRequestsRejected == 1);

	assert(!service.Poll(peers, 62.0).empty());
	const auto granted = service.Dispatch("respawnRequest", peers, 2, state, request, 62.1, 5.0);
	assert(granted.has_value());
	const auto expectedGrant = ReliableFrame(20, Json{
		{ "type", "respawnGrant" },
		{ "point", "sanctuary" }
	});
	assert(granted->size() == 1);
	assert(granted->front().connectionId == expectedGrant.connectionId);
	assert(granted->front().delivery == expectedGrant.delivery);
	assert(granted->front().payload == expectedGrant.payload);

	std::cout
		<< "downed service contract: respawn point=sanctuary frames=" << granted->size()
		<< " rejected=" << stats.respawnRequestsRejected
		<< "\n";
	return 0;
}

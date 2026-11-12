#include "ServerDowned.h"
#include "ServerOutboundFrames.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>

void TestRefusedVitalsDeathKeepsTracker()
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::DownedPeer;
	using CommonwealthOnline::Server::DownedService;
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
	bool accept = false;
	int attempts = 0;
	service.SetDeathHandler([&accept, &attempts](std::uint32_t, std::string_view) {
		++attempts;
		return accept;
	});
	const auto expectedDied = ReliableFrame(20, Json{ { "type", "died" }, { "playerId", 2 } });
	const auto hasDied = [&expectedDied](const std::vector<OutboundFrame>& a_frames) {
		return std::any_of(a_frames.begin(), a_frames.end(), [&expectedDied](const auto& frame) {
			return frame.connectionId == expectedDied.connectionId && frame.payload == expectedDied.payload;
		});
	};

	const auto refused = service.ObserveVitals([&peers] { return peers; }, 2, false, true, 2.0, true);
	assert(refused.empty());
	assert(attempts == 1);
	assert(service.StateOf(2) == F4T::DownedState::State::Alive);

	accept = true;
	const auto accepted = service.ObserveVitals([&peers] { return peers; }, 2, false, true, 3.0, true);
	assert(attempts == 2);
	assert(hasDied(accepted));
	assert(service.StateOf(2) == F4T::DownedState::State::Dead);
	std::cout << "downed vitals refusal: attempts=" << attempts << " frames=" << accepted.size() << "\n";
}

void TestRefusedJoinDeathIsRetriedOnPoll()
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::DownedPeer;
	using CommonwealthOnline::Server::DownedService;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolStats;
	using CommonwealthOnline::Server::ReliableFrame;

	ProtocolStats stats;
	DownedService service{ stats };
	const DownedService::Peers peers{ DownedPeer{ 20, 2, std::nullopt } };
	assert(service.RestoreOffline("profile-victim", F4T::DownedState::State::Dead, 0.0, 1.0, ""));
	bool accept = false;
	service.SetDeathHandler([&accept](std::uint32_t, std::string_view) { return accept; });
	const auto expectedDied = ReliableFrame(20, Json{ { "type", "died" }, { "playerId", 2 } });
	const auto hasDied = [&expectedDied](const std::vector<OutboundFrame>& a_frames) {
		return std::any_of(a_frames.begin(), a_frames.end(), [&expectedDied](const auto& frame) {
			return frame.connectionId == expectedDied.connectionId && frame.payload == expectedDied.payload;
		});
	};

	const auto joined = service.Join(peers, 20, 2, "profile-victim", 1.0);
	assert(!hasDied(joined));
	assert(service.StateOf(2) == F4T::DownedState::State::Dead);

	accept = true;
	const auto retried = service.Poll(peers, 2.0);
	assert(hasDied(retried));
	const auto settled = service.Poll(peers, 3.0);
	assert(!hasDied(settled));
	std::cout << "downed join retry: joinFrames=" << joined.size() << " retryFrames=" << retried.size() << "\n";
}

void TestRefusedPeerDoesNotStallOthers()
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::DownedPeer;
	using CommonwealthOnline::Server::DownedService;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolStats;
	using CommonwealthOnline::Server::ReliableFrame;

	ProtocolStats stats;
	DownedService service{ stats };
	const DownedService::Peers peers{
		DownedPeer{ 10, 1, std::nullopt },
		DownedPeer{ 20, 2, std::nullopt }
	};
	service.Join(peers, 10, 1, "profile-a", 1.0);
	service.Join(peers, 20, 2, "profile-b", 1.0);
	bool acceptFirst = false;
	int firstAttempts = 0;
	service.SetDeathHandler([&acceptFirst, &firstAttempts](std::uint32_t a_playerId, std::string_view) {
		if (a_playerId != 1) return true;
		++firstAttempts;
		return acceptFirst;
	});
	const auto hasDied = [](const std::vector<OutboundFrame>& a_frames, std::uint32_t a_playerId) {
		const auto expected = ReliableFrame(a_playerId * 10, Json{ { "type", "died" }, { "playerId", a_playerId } });
		return std::any_of(a_frames.begin(), a_frames.end(), [&expected](const auto& frame) {
			return frame.connectionId == expected.connectionId && frame.payload == expected.payload;
		});
	};
	const auto getPeers = [&peers] { return peers; };
	(void)service.ObserveVitals(getPeers, 1, true, false, 2.0, true);
	(void)service.ObserveVitals(getPeers, 2, true, false, 2.0, true);

	const auto stalled = service.Poll(peers, 62.5);
	assert(hasDied(stalled, 2) && !hasDied(stalled, 1));
	assert(service.StateOf(2) == F4T::DownedState::State::Dead);
	assert(service.StateOf(1) == F4T::DownedState::State::Downed);
	assert(firstAttempts == 1);

	acceptFirst = true;
	const auto recovered = service.Poll(peers, 63.0);
	assert(hasDied(recovered, 1) && !hasDied(recovered, 2));
	assert(service.StateOf(1) == F4T::DownedState::State::Dead);
	std::cout << "downed refused peer: attempts=" << firstAttempts << "\n";
}

int main()
{
	TestRefusedVitalsDeathKeepsTracker();
	TestRefusedJoinDeathIsRetriedOnPoll();
	TestRefusedPeerDoesNotStallOthers();
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
		{ "remainingMs", 60000 },
		{ "playerIncarnation", 0 },
		{ "serverDowned", Json{ { "revision", 1 }, { "downed", true }, { "dead", false } } }
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
	const auto grant = std::find_if(granted->begin(), granted->end(), [&](const auto& frame) {
		return frame.connectionId == expectedGrant.connectionId && frame.payload == expectedGrant.payload;
	});
	assert(grant != granted->end());
	assert(grant->delivery == expectedGrant.delivery);
	assert(granted->size() == peers.size() + 1);

	std::cout
		<< "downed service contract: respawn point=sanctuary frames=" << granted->size()
		<< " rejected=" << stats.respawnRequestsRejected
		<< "\n";
	return 0;
}

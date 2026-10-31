#include "F4TSteamTunnel.h"

#include "F4TTransportEndpoint.h"
#include "F4TSteamTunnelFakes.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	void TestPolicy()
	{
		const auto friendsOnly = MakeFriendsOnlyPolicy([](std::uint64_t a_steamId) { return a_steamId == kFriend; });
		assert(friendsOnly(kFriend));
		assert(!friendsOnly(kStranger));
		assert(!friendsOnly(0));

		const auto allowed = MakeFriendsOnlyPolicy(nullptr, { kStranger });
		assert(allowed(kStranger));
		assert(!allowed(kFriend));
		assert(!allowed(0));

		const auto both = MakeFriendsOnlyPolicy([](std::uint64_t a_steamId) { return a_steamId == kFriend; }, { kStranger });
		assert(both(kFriend));
		assert(both(kStranger));
		assert(!both(1));

		const auto empty = MakeFriendsOnlyPolicy(nullptr);
		assert(!empty(kFriend));
		assert(!empty(0));

		assert(!F4T::Transport::SteamAdmissionPolicy{});
	}

	void TestEndpointHelpers()
	{
		assert(ReadSteamHostId("steam:76561198000000042").value() == kFriend);
		assert(!ReadSteamHostId("127.0.0.1:7777").has_value());
		assert(!ReadSteamHostId("steam:0").has_value());
		assert(!ReadSteamHostId("").has_value());

		assert(FormatLoopbackEndpoint(kFirstFakePort) == "127.0.0.1:40000");
		const auto parsed = F4T::Transport::ParseEndpoint(FormatLoopbackEndpoint(65535));
		assert(parsed.has_value());
		assert(parsed->host == "127.0.0.1");
		assert(parsed->port == 65535);
	}

	void TestHostAcceptsFriendAndRelaysBothWays()
	{
		FakeSteam steam;
		FakeUdp udp;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };

		assert(host.Start(kServerPort, FriendOnly()) == TunnelError::None);
		assert(host.IsRunning());
		assert(steam.listenVirtualPorts.size() == 1);
		assert(steam.listenVirtualPorts.front() == kSteamTunnelVirtualPort);

		const auto connection = QueueIncoming(steam, kFriend);
		steam.events.push_back(SteamConnectionEvent{ connection, kFriend, TunnelState::Connecting });
		host.Pump();

		assert(steam.callbackRuns == 1);
		assert(steam.connections[connection].accepted);
		assert(!steam.connections[connection].closed);
		assert(udp.opened.size() == 1);
		assert(host.Stats().connectionsAccepted == 1);
		assert(host.Stats().activeConnections == 1);

		const auto localPort = udp.opened.front();
		const std::vector<std::uint8_t> fromGuest{ 1, 2, 3, 250 };
		steam.connections[connection].inbox.push_back(fromGuest);
		host.Pump();

		assert(udp.sockets[localPort].sent.size() == 1);
		assert(udp.sockets[localPort].sent.front().first == kServerPort);
		assert(udp.sockets[localPort].sent.front().second == fromGuest);
		assert(host.Stats().datagramsToLocal == 1);

		const std::vector<std::uint8_t> fromServer{ 9, 8, 7 };
		udp.sockets[localPort].inbox.push_back(UdpDatagram{ kServerPort, fromServer });
		host.Pump();

		assert(steam.connections[connection].sent.size() == 1);
		assert(steam.connections[connection].sent.front().first == fromServer);
		assert(steam.connections[connection].sent.front().second == kSendUnreliableNoNagle);
		assert(host.Stats().datagramsToSteam == 1);

		host.Stop();
		assert(!host.IsRunning());
	}

	void TestHostRejectsNonFriend()
	{
		FakeSteam steam;
		FakeUdp udp;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.Start(kServerPort, FriendOnly()) == TunnelError::None);

		const auto stranger = QueueIncoming(steam, kStranger);
		host.Pump();

		assert(!steam.connections[stranger].accepted);
		assert(steam.connections[stranger].closed);
		assert(steam.connections[stranger].closeReason == F4T::SteamAdmission::kEndFriendsOnly);
		assert(!steam.connections[stranger].closeDebug.empty());
		assert(host.Stats().connectionsRejected == 1);
		assert(host.Stats().connectionsAccepted == 0);
		assert(host.Stats().activeConnections == 0);
		assert(udp.opened.empty());

		steam.connections[stranger].inbox.push_back(std::vector<std::uint8_t>{ 1, 2, 3 });
		host.Pump();
		assert(host.Stats().datagramsToLocal == 0);
		assert(udp.opened.empty());

		const auto anonymous = QueueIncoming(steam, 0);
		host.Pump();
		assert(!steam.connections[anonymous].accepted);
		assert(host.Stats().connectionsRejected == 2);

		host.Stop();
	}

	void TestHostRefusesSeventeenthConnection()
	{
		FakeSteam steam;
		FakeUdp udp;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.Start(kServerPort, MakeFriendsOnlyPolicy([](std::uint64_t) { return true; })) == TunnelError::None);

		std::vector<SteamConnectionHandle> admitted;
		for (std::size_t index = 0; index < kMaximumTunnelConnections; ++index) {
			admitted.push_back(QueueIncoming(steam, kFriend + index));
		}
		host.Pump();

		assert(host.Stats().connectionsAccepted == kMaximumTunnelConnections);
		assert(host.Stats().activeConnections == kMaximumTunnelConnections);
		assert(host.Stats().connectionsRefusedCapacity == 0);
		assert(udp.opened.size() == kMaximumTunnelConnections);
		for (const auto connection : admitted) {
			assert(steam.connections[connection].accepted);
		}

		const auto extra = QueueIncoming(steam, kFriend + kMaximumTunnelConnections);
		host.Pump();

		assert(!steam.connections[extra].accepted);
		assert(steam.connections[extra].closed);
		assert(steam.connections[extra].closeReason == kConnectionEndServerFull);
		assert(host.Stats().connectionsRefusedCapacity == 1);
		assert(host.Stats().activeConnections == kMaximumTunnelConnections);
		assert(udp.opened.size() == kMaximumTunnelConnections);

		host.Stop();
	}

	void TestHostHonoursMaximumConnections()
	{
		FakeSteam steam;
		FakeUdp udp;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.Start(kServerPort, MakeFriendsOnlyPolicy([](std::uint64_t) { return true; }), 2) == TunnelError::None);
		const auto first = QueueIncoming(steam, kFriend);
		const auto second = QueueIncoming(steam, kFriend + 1);
		const auto third = QueueIncoming(steam, kFriend + 2);
		host.Pump();
		assert(steam.connections[first].accepted && steam.connections[second].accepted);
		assert(!steam.connections[third].accepted && steam.connections[third].closeReason == kConnectionEndServerFull);
		assert(host.Stats().connectionsRefusedCapacity == 1 && host.Stats().activeConnections == 2);
		host.Stop();
	}

	void TestHostRefusesWhenLoopbackFails()
	{
		FakeSteam steam;
		FakeUdp udp;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.Start(kServerPort, FriendOnly()) == TunnelError::None);

		udp.openFails = true;
		const auto connection = QueueIncoming(steam, kFriend);
		host.Pump();

		assert(!steam.connections[connection].accepted);
		assert(steam.connections[connection].closed);
		assert(steam.connections[connection].closeReason == kConnectionEndLocalSocketFailed);
		assert(udp.opened.empty());
		assert(host.Stats().connectionsRejected == 1);
		assert(host.Stats().connectionsAccepted == 0);
		assert(host.Stats().activeConnections == 0);

		host.Stop();
	}

	void TestHostClosesConnectionAcceptedAfterStop()
	{
		FakeSteam steam;
		FakeUdp udp;
		SteamApi api = MakeSteamApi(steam);
		SteamHostTunnel* owner = nullptr;
		api.acceptConnection = [&owner](SteamConnectionHandle) {
			owner->Stop();
			return true;
		};
		SteamHostTunnel tunnel{ std::move(api), MakeUdpApi(udp) };
		owner = &tunnel;
		assert(tunnel.Start(kServerPort, FriendOnly()) == TunnelError::None);

		const auto connection = QueueIncoming(steam, kFriend);
		tunnel.Pump();

		assert(steam.connections[connection].closed);
		assert(udp.opened.size() == 1);
		assert(udp.closed == udp.opened);
		assert(tunnel.Stats().connectionsAccepted == 0);
		assert(tunnel.Stats().activeConnections == 0);
	}

	void TestHostDropsInvalidDatagrams()
	{
		FakeSteam steam;
		FakeUdp udp;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.Start(kServerPort, FriendOnly()) == TunnelError::None);

		const auto connection = QueueIncoming(steam, kFriend);
		host.Pump();
		const auto localPort = udp.opened.front();

		steam.connections[connection].inbox.push_back(std::vector<std::uint8_t>{});
		host.Pump();
		assert(host.Stats().datagramsDroppedInvalid == 1);
		assert(udp.sockets[localPort].sent.empty());

		steam.connections[connection].inbox.push_back(std::vector<std::uint8_t>(kMaximumRelayedDatagramBytes + 1, 7));
		host.Pump();
		assert(host.Stats().datagramsDroppedInvalid == 2);
		assert(udp.sockets[localPort].sent.empty());

		steam.connections[connection].inbox.push_back(std::vector<std::uint8_t>(kMaximumRelayedDatagramBytes, 7));
		host.Pump();
		assert(host.Stats().datagramsDroppedInvalid == 2);
		assert(udp.sockets[localPort].sent.size() == 1);
		assert(udp.sockets[localPort].sent.front().second.size() == kMaximumRelayedDatagramBytes);

		steam.connections[connection].inbox.push_back(std::vector<std::uint8_t>(kGnsMaximumUdpPacketBytes, 7));
		host.Pump();
		assert(host.Stats().datagramsDroppedInvalid == 2);
		assert(udp.sockets[localPort].sent.size() == 2);
		assert(udp.sockets[localPort].sent.back().second.size() == kGnsMaximumUdpPacketBytes);

		udp.sockets[localPort].inbox.push_back(UdpDatagram{ kServerPort, std::vector<std::uint8_t>(kMaximumRelayedDatagramBytes + 1, 3) });
		udp.sockets[localPort].inbox.push_back(UdpDatagram{ kServerPort, std::vector<std::uint8_t>{} });
		udp.sockets[localPort].inbox.push_back(UdpDatagram{ kServerPort - 1, std::vector<std::uint8_t>{ 4 } });
		host.Pump();
		assert(host.Stats().datagramsToSteam == 0);
		assert(host.Stats().datagramsDroppedInvalid == 4);
		assert(steam.connections[connection].sent.empty());

		host.Stop();
	}

	void TestHostReportsAdmittedPeers()
	{
		FakeSteam steam;
		FakeUdp udp;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.Start(kServerPort, FriendOnly()) == TunnelError::None);
		assert(host.PeerSteamIds().empty());

		QueueIncoming(steam, kStranger);
		host.Pump();
		assert(host.PeerSteamIds().empty());

		const auto friendConnection = QueueIncoming(steam, kFriend);
		host.Pump();
		assert(host.PeerSteamIds() == std::vector<std::uint64_t>{ kFriend });

		steam.events.push_back(SteamConnectionEvent{ friendConnection, kFriend, TunnelState::Closed });
		host.Pump();
		assert(host.PeerSteamIds().empty());

		const auto older = QueueIncoming(steam, kFriend);
		const auto newer = QueueIncoming(steam, kFriend);
		host.Pump();
		assert(host.PeerSteamIds() == std::vector<std::uint64_t>{ kFriend });
		assert(steam.connections[older].closed && !steam.connections[newer].closed);

		host.Stop();
		assert(host.PeerSteamIds().empty());
	}

	void TestHostStopClosesEverything()
	{
		FakeSteam steam;
		FakeUdp udp;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.Start(kServerPort, MakeFriendsOnlyPolicy(nullptr, { kFriend, kStranger })) == TunnelError::None);

		const auto first = QueueIncoming(steam, kFriend);
		const auto second = QueueIncoming(steam, kStranger);
		host.Pump();
		assert(host.Stats().activeConnections == 2);
		const auto callbacksBeforeStop = steam.callbackRuns;

		host.Stop();

		assert(!host.IsRunning());
		assert(host.Stats().activeConnections == 0);
		assert(steam.connections[first].closed);
		assert(steam.connections[second].closed);
		assert(steam.closedListenSockets.size() == 1);
		assert(steam.closedListenSockets.front() == 100);
		assert(udp.closed.size() == 2);

		host.Stop();
		assert(steam.closedListenSockets.size() == 1);

		const auto afterStop = QueueIncoming(steam, kFriend);
		steam.connections[first].inbox.push_back(std::vector<std::uint8_t>{ 1 });
		udp.sockets[udp.opened.front()].inbox.push_back(UdpDatagram{ kServerPort, std::vector<std::uint8_t>{ 2 } });
		host.Pump();

		assert(steam.callbackRuns == callbacksBeforeStop);
		assert(!steam.connections[afterStop].accepted);
		assert(host.Stats().connectionsAccepted == 2);
		assert(host.Stats().datagramsToLocal == 0);
		assert(host.Stats().datagramsToSteam == 0);
	}

	void TestHostStartFailures()
	{
		FakeSteam steam;
		FakeUdp udp;

		SteamApi incomplete;
		incomplete.getSteamId = [] { return kHostSteamId; };
		SteamHostTunnel missingSteam{ incomplete, MakeUdpApi(udp) };
		assert(!missingSteam.IsRunning());
		assert(missingSteam.Start(kServerPort, FriendOnly()) == TunnelError::SteamUnavailable);

		FakeUdp udpWithoutSockets;
		UdpSocketApi emptyUdp;
		SteamHostTunnel missingUdp{ MakeSteamApi(steam), emptyUdp };
		assert(missingUdp.Start(kServerPort, FriendOnly()) == TunnelError::LoopbackSocketFailed);

		SteamHostTunnel invalidPort{ MakeSteamApi(steam), MakeUdpApi(udpWithoutSockets) };
		assert(invalidPort.Start(0, FriendOnly()) == TunnelError::InvalidArgument);
		assert(invalidPort.Start(kServerPort, FriendOnly(), 0) == TunnelError::InvalidArgument);
		assert(invalidPort.Start(kServerPort, FriendOnly(), kMaximumTunnelConnections + 1) == TunnelError::InvalidArgument);

		steam.listenOk = false;
		SteamHostTunnel noListen{ MakeSteamApi(steam), MakeUdpApi(udpWithoutSockets) };
		assert(noListen.Start(kServerPort, FriendOnly()) == TunnelError::ListenSocketFailed);
		assert(!noListen.IsRunning());

		steam.listenOk = true;
		steam.relayReady = false;
		const auto listensBeforeRelay = steam.listenVirtualPorts.size();
		SteamHostTunnel noRelay{ MakeSteamApi(steam), MakeUdpApi(udpWithoutSockets) };
		assert(noRelay.Start(kServerPort, FriendOnly()) == TunnelError::RelayNotReady);
		assert(!noRelay.IsRunning());
		assert(steam.listenVirtualPorts.size() == listensBeforeRelay);
		steam.relayReady = true;

		SteamHostTunnel twice{ MakeSteamApi(steam), MakeUdpApi(udpWithoutSockets) };
		assert(twice.Start(kServerPort, FriendOnly()) == TunnelError::None);
		assert(twice.Start(kServerPort, FriendOnly()) == TunnelError::None);
		assert(steam.listenVirtualPorts.size() == 2);
		twice.Stop();
	}

	void TestConnectionEndDescriptions()
	{
		assert(F4T::Transport::DescribeConnectionEnd(kConnectionEndNotPermitted) == "That host only accepts Steam friends.");
		assert(F4T::Transport::DescribeConnectionEnd(kConnectionEndServerFull) == "That host is full.");
		assert(F4T::Transport::DescribeConnectionEnd(kConnectionEndLocalSocketFailed) == "The host could not open a relay port for you.");
		assert(F4T::Transport::DescribeConnectionEnd(F4T::Transport::kConnectionEndIdentityUnlinked) == "The host could not confirm your Steam identity.");
		assert(F4T::Transport::DescribeConnectionEnd(F4T::Transport::kConnectionEndFriendsOnly) == "That host only accepts Steam friends.");
		assert(F4T::Transport::DescribeConnectionEnd(F4T::Transport::kConnectionEndFriendCheckUnavailable) == "The host's Steam could not check whether you are friends. Try again in a moment.");
		assert(F4T::Transport::DescribeConnectionEnd(F4T::Transport::kConnectionEndUndeliverable).find("older Commonwealth Online build") != std::string_view::npos);
		assert(F4T::Transport::DescribeConnectionEnd(0) == "The Steam link ended before the host answered.");
		assert(F4T::Transport::DescribeConnectionEnd(1234) == "The Steam link ended before the host answered.");

		assert(F4T::Transport::DescribeTunnelState(TunnelState::Closed) == "closed");
		assert(F4T::Transport::DescribeTunnelState(TunnelState::Connecting) == "still connecting");
		assert(F4T::Transport::DescribeTunnelState(TunnelState::Connected) == "connected");
		assert(F4T::Transport::DescribeTunnelState(TunnelState::Failed) == "failed");
	}

	void TestHostRecordsEveryEvent()
	{
		FakeSteam steam;
		FakeUdp udp;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.DrainEvents().empty());

		assert(host.Start(kServerPort, FriendOnly()) == TunnelError::None);
		auto events = host.DrainEvents();
		assert(events.size() == 1);
		assert(events.front().kind == TunnelEventKind::TunnelStarted);
		assert(events.front().port == kServerPort);
		assert(events.front().detail == "guests join unverified");

		const auto friendConnection = QueueIncoming(steam, kFriend);
		host.Pump();
		events = host.DrainEvents();
		assert(events.size() == 1);
		assert(events.front().kind == TunnelEventKind::PeerAccepted);
		assert(events.front().peerSteamId == kFriend);
		assert(events.front().connection == friendConnection);
		assert(events.front().port == kFirstFakePort);

		steam.connections[friendConnection].inbox.push_back(std::vector<std::uint8_t>{ 5 });
		host.Pump();
		events = host.DrainEvents();
		assert(events.size() == 1);
		assert(events.front().kind == TunnelEventKind::FirstDatagramForwarded);
		assert(events.front().peerSteamId == kFriend);

		steam.connections[friendConnection].inbox.push_back(std::vector<std::uint8_t>{ 6 });
		host.Pump();
		assert(host.DrainEvents().empty());

		const auto strangerConnection = QueueIncoming(steam, kStranger);
		host.Pump();
		events = host.DrainEvents();
		assert(events.size() == 1);
		assert(events.front().kind == TunnelEventKind::PeerRefused);
		assert(events.front().peerSteamId == kStranger);
		assert(events.front().connection == strangerConnection);
		assert(events.front().code == F4T::SteamAdmission::kEndFriendsOnly);

		steam.events.push_back(SteamConnectionEvent{ friendConnection, kFriend, TunnelState::Closed });
		host.Pump();
		events = host.DrainEvents();
		assert(events.size() == 1);
		assert(events.front().kind == TunnelEventKind::PeerDropped);
		assert(events.front().peerSteamId == kFriend);

		host.Stop();
		events = host.DrainEvents();
		assert(events.size() == 1);
		assert(events.front().kind == TunnelEventKind::TunnelStopped);

		FakeSteam fullSteam;
		FakeUdp fullUdp;
		SteamHostTunnel full{ MakeSteamApi(fullSteam), MakeUdpApi(fullUdp) };
		assert(full.Start(kServerPort, MakeFriendsOnlyPolicy([](std::uint64_t) { return true; }), 1) == TunnelError::None);
		(void)QueueIncoming(fullSteam, kFriend);
		full.Pump();
		(void)full.DrainEvents();
		(void)QueueIncoming(fullSteam, kFriend + 1);
		full.Pump();
		events = full.DrainEvents();
		assert(events.size() == 1);
		assert(events.front().kind == TunnelEventKind::PeerRefused);
		assert(events.front().code == kConnectionEndServerFull);
		full.Stop();

		FakeSteam brokenUdpSteam;
		FakeUdp brokenUdp;
		brokenUdp.openFails = true;
		SteamHostTunnel noLoopback{ MakeSteamApi(brokenUdpSteam), MakeUdpApi(brokenUdp) };
		assert(noLoopback.Start(kServerPort, FriendOnly()) == TunnelError::None);
		(void)QueueIncoming(brokenUdpSteam, kFriend);
		noLoopback.Pump();
		events = noLoopback.DrainEvents();
		assert(events.size() == 2);
		assert(events[1].kind == TunnelEventKind::PeerRefused);
		assert(events[1].code == kConnectionEndLocalSocketFailed);
		noLoopback.Stop();
	}


	void TestErrorDescriptions()
	{
		const TunnelError errors[] = {
			TunnelError::None,
			TunnelError::InvalidArgument,
			TunnelError::SteamUnavailable,
			TunnelError::RelayNotReady,
			TunnelError::ListenSocketFailed,
			TunnelError::LoopbackSocketFailed,
			TunnelError::ConnectionFailed,
			TunnelError::NotRunning,
		};
		for (const auto error : errors) {
			assert(!DescribeTunnelError(error).empty());
		}
	}

	void TestHostMaximumConnectionsClamp()
	{
		assert(F4T::Transport::ClampHostMaximumConnections(4, 2) == 4);
		assert(F4T::Transport::ClampHostMaximumConnections(2, 5) == 5);
		assert(F4T::Transport::ClampHostMaximumConnections(0, 0) == 0);
		assert(F4T::Transport::ClampHostMaximumConnections(3, 3) == 3);
		assert(F4T::Transport::ClampHostMaximumConnections(99, 3) == kMaximumTunnelConnections);
		assert(F4T::Transport::ClampHostMaximumConnections(kMaximumTunnelConnections + 1, kMaximumTunnelConnections + 1) == kMaximumTunnelConnections);
	}
}


int main()
{
	TestPolicy();
	TestEndpointHelpers();
	TestHostAcceptsFriendAndRelaysBothWays();
	TestHostRejectsNonFriend();
	TestHostRefusesSeventeenthConnection();
	TestHostHonoursMaximumConnections();
	TestHostRefusesWhenLoopbackFails();
	TestHostClosesConnectionAcceptedAfterStop();
	TestHostDropsInvalidDatagrams();
	TestHostReportsAdmittedPeers();
	TestHostStopClosesEverything();
	TestHostStartFailures();
	TestConnectionEndDescriptions();
	TestHostRecordsEveryEvent();
	TestErrorDescriptions();
	TestHostMaximumConnectionsClamp();
	return 0;
}

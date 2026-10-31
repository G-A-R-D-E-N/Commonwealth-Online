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
	void TestGuestForwardsBothWays()
	{
		FakeSteam steam;
		FakeUdp udp;
		SteamGuestTunnel guest{ MakeSteamApi(steam), MakeUdpApi(udp) };

		const auto connected = guest.Connect(kHostSteamId);
		assert(connected.ok());
		assert(connected.error == TunnelError::None);
		assert(guest.IsRunning());
		assert(guest.State() == TunnelState::Connecting);
		assert(steam.connectRequests.size() == 1);
		assert(steam.connectRequests.front().first == kHostSteamId);
		assert(steam.connectRequests.front().second == kSteamTunnelVirtualPort);
		assert(udp.opened.size() == 1);
		assert(connected.loopbackPort == udp.opened.front());
		assert(FormatLoopbackEndpoint(connected.loopbackPort) == "127.0.0.1:40000");

		const auto connection = kGuestConnection;
		steam.connections[connection] = FakeSteam::Connection{};
		steam.events.push_back(SteamConnectionEvent{ connection, kHostSteamId, TunnelState::Connected });
		guest.Pump();
		assert(guest.State() == TunnelState::Connected);

		const std::vector<std::uint8_t> fromGns{ 11, 22, 33 };
		udp.sockets[connected.loopbackPort].inbox.push_back(UdpDatagram{ kGnsPort, fromGns });
		guest.Pump();

		assert(steam.connections[connection].sent.size() == 1);
		assert(steam.connections[connection].sent.front().first == fromGns);
		assert(steam.connections[connection].sent.front().second == kSendUnreliableNoNagle);
		assert(guest.Stats().datagramsToSteam == 1);

		const std::vector<std::uint8_t> fromHost{ 44, 55 };
		steam.connections[connection].inbox.push_back(fromHost);
		guest.Pump();

		assert(udp.sockets[connected.loopbackPort].sent.size() == 1);
		assert(udp.sockets[connected.loopbackPort].sent.front().first == kGnsPort);
		assert(udp.sockets[connected.loopbackPort].sent.front().second == fromHost);
		assert(guest.Stats().datagramsToLocal == 1);

		guest.Stop();
		assert(!guest.IsRunning());
		assert(guest.State() == TunnelState::Closed);
		assert(steam.connections[connection].closed);
		assert(udp.closed.size() == 1);
		assert(udp.closed.front() == connected.loopbackPort);
	}

	void TestGuestBuffersUntilTheGnsClientSpeaks()
	{
		FakeSteam steam;
		FakeUdp udp;
		SteamGuestTunnel guest{ MakeSteamApi(steam), MakeUdpApi(udp) };
		const auto connected = guest.Connect(kHostSteamId);
		assert(connected.ok());

		steam.connections[kGuestConnection] = FakeSteam::Connection{};
		for (std::uint8_t index = 0; index < 3; ++index) {
			steam.connections[kGuestConnection].inbox.push_back(std::vector<std::uint8_t>{ index });
		}
		guest.Pump();

		assert(udp.sockets[connected.loopbackPort].sent.empty());
		assert(guest.Stats().datagramsToLocal == 0);
		assert(guest.Stats().datagramsDroppedBufferFull == 0);

		udp.sockets[connected.loopbackPort].inbox.push_back(UdpDatagram{ kGnsPort, std::vector<std::uint8_t>{ 99 } });
		guest.Pump();
		guest.Pump();

		assert(udp.sockets[connected.loopbackPort].sent.size() == 3);
		for (std::size_t index = 0; index < 3; ++index) {
			assert(udp.sockets[connected.loopbackPort].sent[index].first == kGnsPort);
			assert(udp.sockets[connected.loopbackPort].sent[index].second.front() == static_cast<std::uint8_t>(index));
		}
		assert(guest.Stats().datagramsToLocal == 3);
	}

	void TestGuestRejectsInvalidDatagrams()
	{
		FakeSteam steam;
		FakeUdp udp;
		SteamGuestTunnel guest{ MakeSteamApi(steam), MakeUdpApi(udp) };
		const auto connected = guest.Connect(kHostSteamId);
		assert(connected.ok());
		steam.connections[1] = FakeSteam::Connection{};

		udp.sockets[connected.loopbackPort].inbox.push_back(UdpDatagram{ kGnsPort, std::vector<std::uint8_t>{} });
		udp.sockets[connected.loopbackPort].inbox.push_back(UdpDatagram{ kGnsPort, std::vector<std::uint8_t>(kMaximumRelayedDatagramBytes + 1, 5) });
		udp.sockets[connected.loopbackPort].inbox.push_back(UdpDatagram{ kGnsPort, std::vector<std::uint8_t>{ 6 } });
		guest.Pump();

		assert(steam.connections[1].sent.empty());
		assert(guest.Stats().datagramsDroppedInvalid == 2);
		assert(guest.Stats().datagramsToSteam == 0);

		steam.events.push_back(SteamConnectionEvent{ 1, kHostSteamId, TunnelState::Connected });
		guest.Pump();

		assert(steam.connections[1].sent.size() == 1);
		assert(steam.connections[1].sent.front().first == std::vector<std::uint8_t>{ 6 });
		assert(guest.Stats().datagramsToSteam == 1);

		steam.connections[1].inbox.push_back(std::vector<std::uint8_t>{});
		steam.connections[1].inbox.push_back(std::vector<std::uint8_t>(kMaximumRelayedDatagramBytes + 1, 5));
		guest.Pump();
		assert(udp.sockets[connected.loopbackPort].sent.empty());
		assert(guest.Stats().datagramsDroppedInvalid == 4);

		steam.connections[1].inbox.push_back(std::vector<std::uint8_t>(kGnsMaximumUdpPacketBytes, 9));
		guest.Pump();
		assert(udp.sockets[connected.loopbackPort].sent.size() == 1);
		assert(udp.sockets[connected.loopbackPort].sent.front().second.size() == kGnsMaximumUdpPacketBytes);
		assert(guest.Stats().datagramsDroppedInvalid == 4);
	}

	void TestGuestFailures()
	{
		FakeSteam steam;
		FakeUdp udp;

		steam.relayReady = false;
		SteamGuestTunnel notReady{ MakeSteamApi(steam), MakeUdpApi(udp) };
		const auto refused = notReady.Connect(kHostSteamId);
		assert(refused.error == TunnelError::RelayNotReady);
		assert(!refused.ok());
		assert(refused.loopbackPort == 0);
		assert(!notReady.IsRunning());
		assert(steam.connectRequests.empty());
		assert(udp.opened.empty());
		assert(!DescribeTunnelError(refused.error).empty());

		steam.relayReady = true;
		FakeUdp failingUdp;
		failingUdp.openFails = true;
		SteamGuestTunnel noLoopback{ MakeSteamApi(steam), MakeUdpApi(failingUdp) };
		assert(noLoopback.Connect(kHostSteamId).error == TunnelError::LoopbackSocketFailed);

		SteamApi incomplete;
		incomplete.getSteamId = [] { return kHostSteamId; };
		SteamGuestTunnel missingSteam{ incomplete, MakeUdpApi(udp) };
		assert(missingSteam.Connect(kHostSteamId).error == TunnelError::SteamUnavailable);

		SteamGuestTunnel noHost{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(noHost.Connect(0).error == TunnelError::InvalidArgument);
		assert(noHost.Connect(kHostSteamId).ok());

		steam.nextConnection = 0;
		SteamGuestTunnel noConnection{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(noConnection.Connect(kHostSteamId).error == TunnelError::ConnectionFailed);
		assert(udp.closed.size() == 1);

		steam.nextConnection = 1;
		SteamGuestTunnel dropped{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(dropped.Connect(kHostSteamId).ok());
		const auto callbacksBefore = steam.callbackRuns;
		steam.events.push_back(SteamConnectionEvent{ 1, kHostSteamId, TunnelState::Closed });
		dropped.Pump();
		assert(dropped.State() == TunnelState::Failed);
		assert(!dropped.IsRunning());

		steam.events.push_back(SteamConnectionEvent{ 1, kHostSteamId, TunnelState::Connecting });
		dropped.Pump();
		assert(steam.callbackRuns == callbacksBefore + 1);
		assert(dropped.State() == TunnelState::Failed);
	}

	void TestGuestRecordsHostRefusal()
	{
		FakeSteam steam;
		FakeUdp udp;
		SteamGuestTunnel guest{ MakeSteamApi(steam), MakeUdpApi(udp) };

		const auto connected = guest.Connect(kHostSteamId);
		assert(connected.ok());
		assert(guest.FailureReason().empty());
		assert(guest.EndReason() == 0);

		steam.connections[kGuestConnection] = FakeSteam::Connection{};
		SteamConnectionEvent refused{ kGuestConnection, kHostSteamId, TunnelState::Failed };
		refused.endReason = kConnectionEndServerFull;
		refused.endDebug = "server full";
		steam.events.push_back(refused);
		guest.Pump();

		assert(guest.State() == TunnelState::Failed);
		assert(!guest.IsRunning());
		assert(guest.EndReason() == kConnectionEndServerFull);
		assert(guest.FailureReason() == "server full");

		steam.events.push_back(SteamConnectionEvent{ kGuestConnection, kHostSteamId, TunnelState::Closed });
		guest.Pump();
		assert(guest.FailureReason() == "server full");

		const auto nextHandle = steam.nextConnection;
		SteamGuestTunnel stopped{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(stopped.Connect(kHostSteamId).ok());
		steam.connections[nextHandle] = FakeSteam::Connection{};
		steam.events.push_back(SteamConnectionEvent{ nextHandle, kHostSteamId, TunnelState::Connected });
		stopped.Pump();
		assert(stopped.State() == TunnelState::Connected);
		assert(stopped.FailureReason().empty());
		stopped.Stop();
		assert(!stopped.IsRunning());
		assert(stopped.FailureReason().empty());
		assert(stopped.EndReason() == 0);
	}

	void TestGuestRecordsEveryState()
	{
		FakeSteam steam;
		FakeUdp udp;
		SteamGuestTunnel guest{ MakeSteamApi(steam), MakeUdpApi(udp) };

		const auto connected = guest.Connect(kHostSteamId);
		assert(connected.ok());
		auto events = guest.DrainEvents();
		assert(events.size() == 1);
		assert(events.front().kind == TunnelEventKind::LinkStateChanged);
		assert(events.front().state == TunnelState::Connecting);
		assert(events.front().peerSteamId == kHostSteamId);
		assert(events.front().port == connected.loopbackPort);

		steam.connections[kGuestConnection] = FakeSteam::Connection{};
		steam.events.push_back(SteamConnectionEvent{ kGuestConnection, kHostSteamId, TunnelState::Connected });
		guest.Pump();
		events = guest.DrainEvents();
		assert(events.size() == 1);
		assert(events.front().state == TunnelState::Connected);
		assert(events.front().code == 0);

		udp.sockets[connected.loopbackPort].inbox.push_back(UdpDatagram{ kGnsPort, std::vector<std::uint8_t>{ 1 } });
		guest.Pump();
		events = guest.DrainEvents();
		assert(events.size() == 1);
		assert(events.front().kind == TunnelEventKind::FirstDatagramForwarded);

		udp.sockets[connected.loopbackPort].inbox.push_back(UdpDatagram{ kGnsPort, std::vector<std::uint8_t>{ 2 } });
		guest.Pump();
		assert(guest.DrainEvents().empty());

		SteamConnectionEvent refused{ kGuestConnection, kHostSteamId, TunnelState::Failed };
		refused.endReason = kConnectionEndServerFull;
		refused.endDebug = "server full";
		steam.events.push_back(refused);
		guest.Pump();
		events = guest.DrainEvents();
		assert(events.size() == 1);
		assert(events.front().state == TunnelState::Failed);
		assert(events.front().code == kConnectionEndServerFull);
		assert(events.front().detail == "server full");

		const std::string formatted = FormatTunnelEvent(events.front());
		assert(!formatted.empty());
		assert(!DescribeTunnelEventKind(TunnelEventKind::LinkStateChanged).empty());
		assert(DescribeTunnelEventKind(TunnelEventKind::PeerRefused) != DescribeTunnelEventKind(TunnelEventKind::PeerAccepted));

		guest.Stop();
		assert(guest.DrainEvents().empty());
	}

}

int main()
{
	TestGuestForwardsBothWays();
	TestGuestBuffersUntilTheGnsClientSpeaks();
	TestGuestRejectsInvalidDatagrams();
	TestGuestFailures();
	TestGuestRecordsHostRefusal();
	TestGuestRecordsEveryState();
	return 0;
}

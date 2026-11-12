#include "F4TSteamTunnel.h"

#include "F4TSteamTunnelFakes.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
	using F4T::SteamAdmission::JoinMode;
	using F4T::SteamAdmission::TunnelRequest;
	using F4T::Transport::kConnectionEndHostNotAccepting;
	using F4T::Transport::kConnectionEndIdentityUnlinked;
	using F4T::Transport::SteamIdentityBinder;

	struct BinderSnapshot
	{
		std::vector<std::pair<std::uint16_t, std::uint64_t>> bound;
		std::vector<std::uint16_t> unbound;
		std::vector<bool> acceptedAtBind;
		std::vector<bool> passwordRequired;
		std::vector<bool> socketOpenAtUnbind;
	};

	struct BinderLog
	{
		mutable std::mutex mutex;
		bool bindSucceeds{ true };
		std::vector<std::pair<std::uint16_t, std::uint64_t>> bound;
		std::vector<std::uint16_t> unbound;
		std::vector<bool> acceptedAtBind;
		std::vector<bool> passwordRequired;
		std::vector<bool> socketOpenAtUnbind;

		[[nodiscard]] BinderSnapshot Snapshot() const
		{
			std::lock_guard lock{ mutex };
			return BinderSnapshot{ bound, unbound, acceptedAtBind, passwordRequired, socketOpenAtUnbind };
		}
	};

	SteamIdentityBinder MakeBinder(BinderLog& a_log, FakeSteam& a_steam, FakeUdp& a_udp)
	{
		return SteamIdentityBinder{
			.bind = [&a_log, &a_steam](std::uint16_t a_port, std::uint64_t a_steamId, bool a_passwordRequired, std::uint64_t) {
				const bool accepted = a_steam.IsPeerAccepted(a_steamId);
				bool succeeds = false;
			{
					std::lock_guard lock{ a_log.mutex };
					a_log.acceptedAtBind.push_back(accepted);
					a_log.bound.emplace_back(a_port, a_steamId);
					a_log.passwordRequired.push_back(a_passwordRequired);
					succeeds = a_log.bindSucceeds;
				}
				return succeeds;
			},
			.unbind = [&a_log, &a_udp](std::uint16_t a_port, std::uint64_t) {
				const bool open = a_udp.IsOpen(a_port);
				std::lock_guard lock{ a_log.mutex };
				a_log.socketOpenAtUnbind.push_back(open);
				a_log.unbound.push_back(a_port);
				return true;
			},
			.hostPolicy = {}
		};
	}

	template <class Predicate>
	bool WaitFor(Predicate a_predicate, std::chrono::milliseconds a_timeout = std::chrono::seconds{ 2 })
	{
		const auto deadline = std::chrono::steady_clock::now() + a_timeout;
		while (!a_predicate()) {
			if (std::chrono::steady_clock::now() >= deadline) return false;
			std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
		}
		return true;
	}

	template <class Predicate>
	bool PumpUntil(SteamHostTunnel& a_host, Predicate a_predicate, std::chrono::milliseconds a_timeout = std::chrono::seconds{ 2 })
	{
		const auto deadline = std::chrono::steady_clock::now() + a_timeout;
		while (!a_predicate()) {
			if (std::chrono::steady_clock::now() >= deadline) return false;
			a_host.Pump();
			std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
		}
		return true;
	}

	void TestAcceptsBeforeBindingWithoutRelaying()
	{
		FakeSteam steam;
		FakeUdp udp;
		BinderLog log;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.Start(kServerPort, FriendOnly(), kMaximumTunnelConnections, MakeBinder(log, steam, udp)) == TunnelError::None);

		const auto connection = QueueIncoming(steam, kFriend);
		host.Pump();

		assert(steam.IsAccepted(connection));
		assert(WaitFor([&log] { return log.Snapshot().bound.size() == 1; }));
		const auto view = log.Snapshot();
		assert(view.bound.front().first == kFirstFakePort);
		assert(view.bound.front().second == kFriend);
		assert(view.acceptedAtBind.front());
		assert(host.Stats().activeConnections == 1);
		assert(view.unbound.empty());
	}

	void TestBindFailureRefusesConnection()
	{
		FakeSteam steam;
		FakeUdp udp;
		BinderLog log;
		log.bindSucceeds = false;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.Start(kServerPort, FriendOnly(), kMaximumTunnelConnections, MakeBinder(log, steam, udp)) == TunnelError::None);

		const auto connection = QueueIncoming(steam, kFriend);
		host.Pump();

		assert(steam.IsAccepted(connection));
		assert(WaitFor([&steam, connection] { return steam.IsClosed(connection); }));
		assert(WaitFor([&udp] { return !udp.ClosedPorts().empty(); }));
		assert(log.Snapshot().bound.size() == 1);
		assert(steam.CloseReason(connection) == kConnectionEndIdentityUnlinked);
		const auto closed = udp.ClosedPorts();
		assert(closed.size() == 1 && closed.front() == kFirstFakePort);
		assert(host.Stats().activeConnections == 0);
		assert(host.Stats().identityBindFailures == 1);
		assert(log.Snapshot().unbound.size() == 1 && log.Snapshot().unbound.front() == kFirstFakePort);
	}

	void TestPolicyRejectionNeverBinds()
	{
		FakeSteam steam;
		FakeUdp udp;
		BinderLog log;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.Start(kServerPort, FriendOnly(), kMaximumTunnelConnections, MakeBinder(log, steam, udp)) == TunnelError::None);

		(void)QueueIncoming(steam, kStranger);
		host.Pump();

		assert(log.Snapshot().bound.empty());
		assert(host.Stats().activeConnections == 0);
	}

	void TestAcceptFailureNeverBinds()
	{
		FakeSteam steam;
		FakeUdp udp;
		BinderLog log;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.Start(kServerPort, FriendOnly(), kMaximumTunnelConnections, MakeBinder(log, steam, udp)) == TunnelError::None);

		const auto connection = QueueIncoming(steam, kFriend);
		steam.Find(connection)->closed = true;
		host.Pump();

		const auto view = log.Snapshot();
		assert(view.bound.empty());
		assert(view.unbound.empty());
		const auto closed = udp.ClosedPorts();
		assert(closed.size() == 1 && closed.front() == kFirstFakePort);
		assert(host.Stats().activeConnections == 0);
	}

	void TestDropUnbindsBeforeSocketCloses()
	{
		FakeSteam steam;
		FakeUdp udp;
		BinderLog log;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.Start(kServerPort, FriendOnly(), kMaximumTunnelConnections, MakeBinder(log, steam, udp)) == TunnelError::None);

		const auto connection = QueueIncoming(steam, kFriend);
		host.Pump();
		assert(WaitFor([&host, connection] {
			const auto events = host.DrainEvents();
			return std::any_of(events.begin(), events.end(), [connection](const auto& event) {
				return event.kind == TunnelEventKind::IdentityBound && event.connection == connection;
			});
		}));
		steam.events.push_back(SteamConnectionEvent{ connection, kFriend, TunnelState::Closed });
		host.Pump();

		const auto view = log.Snapshot();
		assert(view.unbound.size() == 1 && view.unbound.front() == kFirstFakePort);
		assert(view.socketOpenAtUnbind.front());
		assert(!udp.IsOpen(kFirstFakePort));
		assert(host.Stats().activeConnections == 0);
	}

	void TestStopUnbindsEveryRelayAndForgetsBinder()
	{
		FakeSteam steam;
		FakeUdp udp;
		BinderLog log;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		auto policy = MakeFriendsOnlyPolicy(nullptr, { kFriend, kStranger });
		assert(host.Start(kServerPort, std::move(policy), kMaximumTunnelConnections, MakeBinder(log, steam, udp)) == TunnelError::None);

		(void)QueueIncoming(steam, kFriend);
		(void)QueueIncoming(steam, kStranger);
		host.Pump();
		assert(WaitFor([&log] { return log.Snapshot().bound.size() == 2; }));
		std::size_t identityBound = 0;
		assert(WaitFor([&host, &identityBound] {
			for (const auto& event : host.DrainEvents()) identityBound += event.kind == TunnelEventKind::IdentityBound ? 1 : 0;
			return identityBound == 2;
		}));

		host.Stop();
		const auto stoppedView = log.Snapshot();
		assert(stoppedView.unbound.size() == 2);
		assert(stoppedView.socketOpenAtUnbind[0] && stoppedView.socketOpenAtUnbind[1]);

		assert(host.Start(kServerPort, FriendOnly()) == TunnelError::None);
		(void)QueueIncoming(steam, kFriend);
		host.Pump();
		assert(log.Snapshot().bound.size() == 2);
		assert(host.Stats().activeConnections == 1);
	}

	void TestPartialBinderIsRejected()
	{
		FakeSteam steam;
		FakeUdp udp;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		SteamIdentityBinder bindOnly{ .bind = [](std::uint16_t, std::uint64_t, bool, std::uint64_t) { return true; }, .unbind = {}, .hostPolicy = {} };
		assert(host.Start(kServerPort, FriendOnly(), kMaximumTunnelConnections, std::move(bindOnly)) == TunnelError::InvalidArgument);
		assert(!host.IsRunning());
	}

	void TestBlockedBindDoesNotStallOtherGuests()
	{
		FakeSteam steam;
		FakeUdp udp;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };

		std::mutex gateMutex;
		std::condition_variable gate;
		bool released = false;
		std::atomic<int> bindCalls{ 0 };
		SteamIdentityBinder binder;
		binder.bind = [&gateMutex, &gate, &released, &bindCalls](std::uint16_t, std::uint64_t, bool, std::uint64_t) {
			std::unique_lock lock{ gateMutex };
			++bindCalls;
			gate.wait(lock, [&released] { return released; });
			return true;
		};
		binder.unbind = [](std::uint16_t, std::uint64_t) { return true; };
		assert(host.Start(kServerPort, MakeFriendsOnlyPolicy([](std::uint64_t) { return true; }), kMaximumTunnelConnections, std::move(binder)) == TunnelError::None);

		const auto first = QueueIncoming(steam, kFriend);
		host.Pump();
		assert(steam.IsAccepted(first));
		assert(WaitFor([&bindCalls] { return bindCalls.load() == 1; }));

		const auto second = QueueIncoming(steam, kFriend + 1);
		host.Pump();
		assert(steam.IsAccepted(second));
		assert(udp.OpenedPorts().size() == 2);

		steam.Find(first)->inbox.push_back(std::vector<std::uint8_t>{ 7 });
		host.Pump();
		host.Pump();
		const auto relayPort = udp.OpenedPorts().front();
		assert(udp.SentTo(relayPort).empty());

		{
			std::lock_guard lock{ gateMutex };
			released = true;
		}
		gate.notify_all();

		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 2 };
		while (udp.SentTo(relayPort).empty() && std::chrono::steady_clock::now() < deadline) {
			host.Pump();
			std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
		}
		const auto relayed = udp.SentTo(relayPort);
		assert(relayed.size() == 1);
		assert(relayed.front().first == kServerPort);
		assert(relayed.front().second == std::vector<std::uint8_t>{ 7 });
		host.Stop();
	}

	void TestDropDuringPendingBindReleasesPortAfterBind()
	{
		FakeSteam steam;
		FakeUdp udp;
		std::mutex gateMutex;
		std::condition_variable gate;
		bool released = false;
		std::atomic<int> bindCalls{ 0 };
		std::atomic<std::uint64_t> bindGeneration{ 0 };
		std::atomic<std::uint64_t> unbindGeneration{ 0 };
		std::atomic<int> unbinds{ 0 };
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		SteamIdentityBinder binder;
		binder.bind = [&gateMutex, &gate, &released, &bindCalls, &bindGeneration](std::uint16_t, std::uint64_t, bool, std::uint64_t a_generation) {
			++bindCalls;
			std::unique_lock lock{ gateMutex };
			gate.wait(lock, [&released] { return released; });
			bindGeneration = a_generation;
			return true;
		};
		binder.unbind = [&unbinds, &unbindGeneration](std::uint16_t, std::uint64_t a_generation) {
			unbindGeneration = a_generation;
			++unbinds;
			return true;
		};
		assert(host.Start(kServerPort, FriendOnly(), kMaximumTunnelConnections, std::move(binder)) == TunnelError::None);

		const auto connection = QueueIncoming(steam, kFriend);
		host.Pump();
		assert(steam.IsAccepted(connection));
		assert(WaitFor([&bindCalls] { return bindCalls.load() == 1; }));

		steam.events.push_back(SteamConnectionEvent{ connection, kFriend, TunnelState::Closed });
		host.Pump();
		assert(udp.ClosedPorts().empty());
		assert(unbinds.load() == 0);

		{
			std::lock_guard lock{ gateMutex };
			released = true;
		}
		gate.notify_all();
		assert(WaitFor([&udp] { return !udp.ClosedPorts().empty(); }));
		assert(udp.ClosedPorts().front() == kFirstFakePort);
		assert(unbinds.load() == 1);
		assert(bindGeneration.load() != 0 && unbindGeneration.load() == bindGeneration.load());
		assert(host.Stats().activeConnections == 0);
	}

	void TestLivePasswordChangeAppliesToNextAdmission()
	{
		FakeSteam steam;
		FakeUdp udp;
		BinderLog log;
		std::atomic<bool> passwordSet{ false };
		auto binder = MakeBinder(log, steam, udp);
		binder.hostPolicy = F4T::SteamAdmission::HostPolicy{ false, false };
		binder.refreshHostPolicy = [&passwordSet]() -> std::optional<F4T::SteamAdmission::HostPolicy> {
			return F4T::SteamAdmission::HostPolicy{ false, passwordSet.load() };
		};
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.Start(kServerPort, FriendOnly(), kMaximumTunnelConnections, std::move(binder)) == TunnelError::None);

		const auto refused = QueueIncoming(steam, kStranger);
		host.Pump();
		assert(WaitFor([&steam, refused] { return steam.IsClosed(refused); }));
		assert(log.Snapshot().bound.empty());

		passwordSet = true;
		(void)QueueIncoming(steam, kStranger);
		host.Pump();
		assert(WaitFor([&log] { return log.Snapshot().bound.size() == 1; }));
		assert(log.Snapshot().passwordRequired.front());

		passwordSet = false;
		const auto refusedAgain = QueueIncoming(steam, kStranger + 1);
		host.Pump();
		assert(WaitFor([&steam, refusedAgain] { return steam.IsClosed(refusedAgain); }));
		assert(log.Snapshot().bound.size() == 1);
	}

	void TestNoBinderKeepsUnverifiedRelay()
	{
		FakeSteam steam;
		FakeUdp udp;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.Start(kServerPort, FriendOnly()) == TunnelError::None);

		const auto connection = QueueIncoming(steam, kFriend);
		host.Pump();
		assert(steam.Find(connection)->accepted);
		assert(host.Stats().activeConnections == 1);

		const std::vector<std::uint8_t> fromGuest{ 1, 2, 3 };
		steam.Find(connection)->inbox.push_back(fromGuest);
		host.Pump();
		const auto localPort = udp.opened.front();
		assert(udp.sockets[localPort].sent.size() == 1);
		assert(udp.sockets[localPort].sent.front().first == kServerPort);
		assert(udp.sockets[localPort].sent.front().second == fromGuest);
		assert(host.Stats().datagramsToLocal == 1);
	}

	void TestAskFirstAcceptedAdmitsWithPasswordRule()
	{
		FakeSteam steam;
		FakeUdp udp;
		BinderLog log;
		std::atomic<int> asks{ 0 };
		auto binder = MakeBinder(log, steam, udp);
		binder.hostPolicy = F4T::SteamAdmission::HostPolicy{ false, true, JoinMode::AskFirst };
		binder.askHost = [&asks](const TunnelRequest& a_request, std::stop_token) {
			++asks;
			return a_request.peerSteamId == kStranger;
		};
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.Start(kServerPort, FriendOnly(), kMaximumTunnelConnections, std::move(binder)) == TunnelError::None);

		const auto connection = QueueIncoming(steam, kStranger);
		assert(PumpUntil(host, [&steam, connection] { return steam.IsAccepted(connection); }));
		assert(WaitFor([&log] { return log.Snapshot().bound.size() == 1; }));
		const auto view = log.Snapshot();
		assert(asks.load() == 1);
		assert(view.bound.front().second == kStranger);
		assert(view.passwordRequired.front());
		assert(view.acceptedAtBind.front());
		assert(host.Stats().activeConnections == 1);
	}

	void TestAskFirstDeclineRefusesEveryJoiner()
	{
		FakeSteam steam;
		FakeUdp udp;
		BinderLog log;
		std::atomic<int> asks{ 0 };
		auto binder = MakeBinder(log, steam, udp);
		binder.hostPolicy = F4T::SteamAdmission::HostPolicy{ true, false, JoinMode::AskFirst };
		binder.askHost = [&asks](const TunnelRequest&, std::stop_token) {
			++asks;
			return false;
		};
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.Start(kServerPort, FriendOnly(), kMaximumTunnelConnections, std::move(binder)) == TunnelError::None);

		const auto declined = QueueIncoming(steam, kFriend);
		assert(PumpUntil(host, [&steam, declined] { return steam.IsClosed(declined); }));
		assert(steam.CloseReason(declined) == kConnectionEndHostNotAccepting);
		assert(asks.load() == 1);
		assert(log.Snapshot().bound.empty());
		assert(host.Stats().connectionsRejected == 1);
		const auto events = host.DrainEvents();
		assert(std::any_of(events.begin(), events.end(), [declined](const TunnelEvent& a_event) {
			return a_event.kind == TunnelEventKind::PeerRefused && a_event.connection == declined && a_event.detail == "host_declined";
		}));
	}

	void TestAskFirstWithoutCallbackRefusesImmediately()
	{
		FakeSteam steam;
		FakeUdp udp;
		BinderLog log;
		auto binder = MakeBinder(log, steam, udp);
		binder.hostPolicy = F4T::SteamAdmission::HostPolicy{ true, false, JoinMode::AskFirst };
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.Start(kServerPort, FriendOnly(), kMaximumTunnelConnections, std::move(binder)) == TunnelError::None);

		const auto connection = QueueIncoming(steam, kFriend);
		host.Pump();
		assert(steam.IsClosed(connection));
		assert(steam.CloseReason(connection) == kConnectionEndHostNotAccepting);
		assert(log.Snapshot().bound.empty());
		assert(host.Stats().activeConnections == 0);
	}

	void TestAskFirstTimeoutRefusesAndCancelsPrompt()
	{
		FakeSteam steam;
		FakeUdp udp;
		BinderLog log;
		std::atomic<double> now{ 0.0 };
		std::atomic<bool> asked{ false };
		auto binder = MakeBinder(log, steam, udp);
		binder.hostPolicy = F4T::SteamAdmission::HostPolicy{ true, false, JoinMode::AskFirst };
		binder.askHost = [&asked](const TunnelRequest&, std::stop_token a_stop) {
			asked = true;
			while (!a_stop.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
			return false;
		};
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		host.SetClock([&now] { return now.load(); });
		assert(host.Start(kServerPort, FriendOnly(), kMaximumTunnelConnections, std::move(binder)) == TunnelError::None);

		const auto connection = QueueIncoming(steam, kFriend);
		host.Pump();
		assert(WaitFor([&asked] { return asked.load(); }));
		assert(!steam.IsClosed(connection));
		now = F4T::Transport::kHostApprovalTimeoutSeconds + 1.0;
		assert(PumpUntil(host, [&steam, connection] { return steam.IsClosed(connection); }));
		assert(steam.CloseReason(connection) == kConnectionEndHostNotAccepting);
		assert(!steam.IsAccepted(connection));
		assert(log.Snapshot().bound.empty());
	}

	void TestStopClosesPendingApprovals()
	{
		FakeSteam steam;
		FakeUdp udp;
		BinderLog log;
		std::atomic<bool> asked{ false };
		auto binder = MakeBinder(log, steam, udp);
		binder.hostPolicy = F4T::SteamAdmission::HostPolicy{ true, false, JoinMode::AskFirst };
		binder.askHost = [&asked](const TunnelRequest&, std::stop_token a_stop) {
			asked = true;
			while (!a_stop.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
			return false;
		};
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.Start(kServerPort, FriendOnly(), kMaximumTunnelConnections, std::move(binder)) == TunnelError::None);

		const auto connection = QueueIncoming(steam, kFriend);
		host.Pump();
		assert(WaitFor([&asked] { return asked.load(); }));
		host.Stop();
		assert(steam.IsClosed(connection));
		assert(!steam.IsAccepted(connection));
		assert(log.Snapshot().bound.empty());
	}

	void TestPeerClosingDuringApprovalCancelsPrompt()
	{
		FakeSteam steam;
		FakeUdp udp;
		BinderLog log;
		std::atomic<bool> asked{ false };
		auto binder = MakeBinder(log, steam, udp);
		binder.hostPolicy = F4T::SteamAdmission::HostPolicy{ true, false, JoinMode::AskFirst };
		binder.askHost = [&asked](const TunnelRequest&, std::stop_token a_stop) {
			asked = true;
			while (!a_stop.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
			return true;
		};
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.Start(kServerPort, FriendOnly(), kMaximumTunnelConnections, std::move(binder)) == TunnelError::None);

		const auto connection = QueueIncoming(steam, kFriend);
		host.Pump();
		assert(WaitFor([&asked] { return asked.load(); }));
		steam.events.push_back(SteamConnectionEvent{ connection, kFriend, TunnelState::Closed });
		host.Pump();
		assert(!steam.IsAccepted(connection));
		assert(log.Snapshot().bound.empty());
		assert(host.Stats().activeConnections == 0);
	}

	void TestPolicyRefreshChangesJoinModeBetweenAdmissions()
	{
		FakeSteam steam;
		FakeUdp udp;
		BinderLog log;
		std::atomic<bool> never{ true };
		auto binder = MakeBinder(log, steam, udp);
		binder.hostPolicy = F4T::SteamAdmission::HostPolicy{ true, false, JoinMode::Always };
		binder.refreshHostPolicy = [&never]() -> std::optional<F4T::SteamAdmission::HostPolicy> {
			return F4T::SteamAdmission::HostPolicy{ true, false, never.load() ? JoinMode::Never : JoinMode::Always };
		};
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.Start(kServerPort, FriendOnly(), kMaximumTunnelConnections, std::move(binder)) == TunnelError::None);

		const auto refused = QueueIncoming(steam, kFriend);
		host.Pump();
		assert(steam.IsClosed(refused) && steam.CloseReason(refused) == kConnectionEndHostNotAccepting);
		assert(log.Snapshot().bound.empty());

		never = false;
		const auto admitted = QueueIncoming(steam, kFriend);
		host.Pump();
		assert(WaitFor([&log] { return log.Snapshot().bound.size() == 1; }));
		assert(steam.IsAccepted(admitted));
	}
}

int main()
{
	TestAcceptsBeforeBindingWithoutRelaying();
	TestBindFailureRefusesConnection();
	TestPolicyRejectionNeverBinds();
	TestAcceptFailureNeverBinds();
	TestDropUnbindsBeforeSocketCloses();
	TestStopUnbindsEveryRelayAndForgetsBinder();
	TestPartialBinderIsRejected();
	TestBlockedBindDoesNotStallOtherGuests();
	TestDropDuringPendingBindReleasesPortAfterBind();
	TestLivePasswordChangeAppliesToNextAdmission();
	TestNoBinderKeepsUnverifiedRelay();
	TestAskFirstAcceptedAdmitsWithPasswordRule();
	TestAskFirstDeclineRefusesEveryJoiner();
	TestAskFirstWithoutCallbackRefusesImmediately();
	TestAskFirstTimeoutRefusesAndCancelsPrompt();
	TestStopClosesPendingApprovals();
	TestPeerClosingDuringApprovalCancelsPrompt();
	TestPolicyRefreshChangesJoinModeBetweenAdmissions();
	return 0;
}

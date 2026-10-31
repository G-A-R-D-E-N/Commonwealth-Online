#include "F4TSteamAdmission.h"
#include "F4TSteamTunnel.h"

#include "F4TSteamTunnelFakes.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
	namespace Admission = F4T::SteamAdmission;
	using Admission::FriendCheck;
	using Admission::HostPolicy;
	using Admission::LockoutOptions;
	using Admission::Outcome;
	using Admission::PasswordLockout;
	using Admission::TunnelRequest;
	using F4T::Transport::SteamIdentityBinder;

	constexpr std::uint64_t kOther = 76561198000000123ull;

	TunnelRequest Request(FriendCheck a_check, std::size_t a_relayed = 0, bool a_linked = true, std::uint64_t a_peer = kStranger)
	{
		return TunnelRequest{ a_peer, a_check, a_relayed, kMaximumTunnelConnections, a_linked };
	}

	bool ContainsSteamIdDigits(const std::string& a_line)
	{
		return a_line.find("7656119") != std::string::npos;
	}

	void TestDefaultsAreFriendsOnly()
	{
		const HostPolicy policy;
		assert(policy.friendsOnly && !policy.serverPasswordSet);
		const auto stranger = Admission::DecideTunnel(policy, Request(FriendCheck::NotFriend));
		assert(!stranger.Admitted() && stranger.outcome == Outcome::NotFriend && stranger.closeReason == Admission::kEndFriendsOnly);
		const auto withPassword = Admission::DecideTunnel(HostPolicy{ .friendsOnly = true, .serverPasswordSet = true }, Request(FriendCheck::NotFriend));
		assert(withPassword.outcome == Outcome::NotFriend);
	}

	void TestFriendsAreAdmittedWithoutTunnelPassword()
	{
		for (const auto& policy : { HostPolicy{}, HostPolicy{ false, true }, HostPolicy{ false, false } }) {
			const auto decision = Admission::DecideTunnel(policy, Request(FriendCheck::Friend, 0, false, kFriend));
			assert(decision.Admitted() && !decision.passwordRequired && decision.closeReason == 0);
		}
	}

	void TestFriendsOnlyOffNeedsAnEnforceablePassword()
	{
		const HostPolicy open{ .friendsOnly = false, .serverPasswordSet = true };
		const auto admitted = Admission::DecideTunnel(open, Request(FriendCheck::NotFriend));
		assert(admitted.Admitted() && admitted.passwordRequired);

		const auto noPassword = Admission::DecideTunnel(HostPolicy{ false, false }, Request(FriendCheck::NotFriend));
		assert(noPassword.outcome == Outcome::PasswordNotEnforceable && noPassword.closeReason == Admission::kEndFriendsOnly);
		const auto unlinked = Admission::DecideTunnel(open, Request(FriendCheck::NotFriend, 0, false));
		assert(unlinked.outcome == Outcome::PasswordNotEnforceable);
	}

	void TestUnavailableFriendCheckFailsClosed()
	{
		const auto closed = Admission::DecideTunnel(HostPolicy{}, Request(FriendCheck::Unavailable));
		assert(closed.outcome == Outcome::FriendCheckUnavailable && closed.closeReason == Admission::kEndFriendCheckUnavailable);
		const auto open = Admission::DecideTunnel(HostPolicy{ false, true }, Request(FriendCheck::Unavailable));
		assert(open.Admitted() && open.passwordRequired);
	}

	void TestInvalidIdentityIsRefusedFirst()
	{
		for (const std::uint64_t peer : { 0ull, 76561197960265728ull, 103582791429521408ull }) {
			const auto decision = Admission::DecideTunnel(HostPolicy{ false, true }, Request(FriendCheck::Friend, 0, true, peer));
			assert(decision.outcome == Outcome::InvalidIdentity && decision.closeReason == Admission::kEndNotPermitted);
		}
	}

	void TestCapacity()
	{
		const auto below = Admission::DecideTunnel(HostPolicy{}, Request(FriendCheck::Friend, kMaximumTunnelConnections - 1, true, kFriend));
		assert(below.Admitted());
		const auto full = Admission::DecideTunnel(HostPolicy{}, Request(FriendCheck::Friend, kMaximumTunnelConnections, true, kFriend));
		assert(full.outcome == Outcome::ServerFull && full.closeReason == Admission::kEndServerFull);
		const auto fullStranger = Admission::DecideTunnel(HostPolicy{}, Request(FriendCheck::NotFriend, kMaximumTunnelConnections));
		assert(fullStranger.outcome == Outcome::NotFriend);
		static_assert(kMaximumTunnelConnections == 16);
	}

	void TestReasonsAndMessages()
	{
		const std::vector<int> codes{ Admission::kEndNotPermitted, Admission::kEndServerFull, Admission::kEndFriendsOnly,
			Admission::kEndFriendCheckUnavailable, Admission::kEndLocalSocketFailed, Admission::kEndIdentityUnlinked };
		std::set<int> distinct;
		std::set<std::string> distinctMessages;
		for (const auto code : codes) {
			assert(code >= 1000 && code <= 2999);
			distinct.insert(code);
			assert(!Admission::GuestMessage(code).empty());
			distinctMessages.insert(std::string{ Admission::GuestMessage(code) });
		}
		assert(distinct.size() == codes.size() && distinctMessages.size() == codes.size() - 1);
		assert(Admission::GuestMessage(0).empty());
		assert(Admission::GuestMessage(Admission::kEndUndeliverable).find("older Commonwealth Online build") != std::string_view::npos);
		assert(Admission::GuestMessage(Admission::kLegacyEndServerFull) == Admission::GuestMessage(Admission::kEndServerFull));
		assert(Admission::GuestMessage(Admission::kLegacyEndNotPermitted) == Admission::GuestMessage(Admission::kEndNotPermitted));
		assert(Admission::GuestMessage(Admission::kLegacyEndLocalSocketFailed) == Admission::GuestMessage(Admission::kEndLocalSocketFailed));
		assert(Admission::GuestMessage(Admission::kLegacyEndIdentityUnlinked) == Admission::GuestMessage(Admission::kEndIdentityUnlinked));
		assert(Admission::GuestMessage(4002) == "The host's game is full.");
		assert(Admission::GuestMessage(Admission::kEndServerFull) != Admission::GuestMessage(Admission::kEndFriendsOnly));

		const auto request = Request(FriendCheck::NotFriend, 3);
		const auto decision = Admission::DecideTunnel(HostPolicy{}, request);
		const auto line = Admission::DescribeTunnelDecision(7, HostPolicy{}, request, decision);
		for (const auto* part : { "connection=7", "steamid=valid", "friends_only=on", "friend=not_friend", "server_password=none", "relayed=3/16", "outcome=refused", "reason=not_friend" }) {
			assert(line.find(part) != std::string::npos);
		}
		assert(!ContainsSteamIdDigits(line));
		const auto openRequest = Request(FriendCheck::NotFriend);
		const auto openLine = Admission::DescribeTunnelDecision(8, HostPolicy{ false, true }, openRequest, Admission::DecideTunnel(HostPolicy{ false, true }, openRequest));
		assert(openLine.find("outcome=admitted") != std::string::npos && openLine.find("password=required_by_server") != std::string::npos);
	}

	void TestLockoutThresholdAndCooldown()
	{
		PasswordLockout lockout{ LockoutOptions{ .threshold = 3, .cooldownSeconds = 60.0, .maximumTracked = 16 } };
		assert(!lockout.Check(kStranger, 0.0).locked);
		assert(lockout.RecordFailure(kStranger, 1.0).failures == 1);
		const auto second = lockout.RecordFailure(kStranger, 2.0);
		assert(!second.locked && second.failures == 2 && !lockout.Check(kStranger, 2.5).locked);

		const auto third = lockout.RecordFailure(kStranger, 3.0);
		assert(third.locked && third.lockedNow && third.failures == 3 && third.remainingSeconds == 60.0);

		auto blocked = lockout.Check(kStranger, 10.0);
		assert(blocked.locked && !blocked.lockedNow && blocked.blockedAttempts == 1 && blocked.remainingSeconds == 53.0);
		assert(lockout.Check(kStranger, 20.0).blockedAttempts == 2);
		const auto duringLock = lockout.RecordFailure(kStranger, 30.0);
		assert(duringLock.locked && duringLock.failures == 3 && !duringLock.lockedNow);

		assert(!lockout.Check(kOther, 30.0).locked);
		assert(!lockout.RecordFailure(kOther, 30.0).locked);

		const auto expired = lockout.Check(kStranger, 63.0);
		assert(!expired.locked && expired.expired);
		assert(!lockout.Check(kStranger, 63.0).expired);
		assert(lockout.RecordFailure(kStranger, 64.0).failures == 1);
	}

	void TestLockoutResetPolicy()
	{
		PasswordLockout lockout{ LockoutOptions{ .threshold = 2, .cooldownSeconds = 60.0, .maximumTracked = 16 } };
		(void)lockout.RecordFailure(kStranger, 0.0);
		lockout.RecordSuccess(kStranger);
		assert(!lockout.RecordFailure(kStranger, 1.0).locked);

		PasswordLockout decaying{ LockoutOptions{ .threshold = 2, .cooldownSeconds = 60.0, .maximumTracked = 16 } };
		(void)decaying.RecordFailure(kStranger, 0.0);
		const auto later = decaying.RecordFailure(kStranger, 61.0);
		assert(!later.locked && later.failures == 1);

		assert(decaying.RecordFailure(0, 0.0).failures == 0 && !decaying.Check(0, 0.0).locked);
		PasswordLockout sanitized{ LockoutOptions{ .threshold = 0, .cooldownSeconds = -1.0, .maximumTracked = 0 } };
		assert(sanitized.Options().threshold == 1 && sanitized.Options().cooldownSeconds == Admission::kDefaultLockoutSeconds);
		assert(sanitized.RecordFailure(kStranger, 0.0).lockedNow);
		const PasswordLockout defaults;
		assert(defaults.Options().threshold == Admission::kDefaultLockoutThreshold && defaults.Options().cooldownSeconds == 300.0);
	}

	void TestLockoutStateIsBounded()
	{
		PasswordLockout lockout{ LockoutOptions{ .threshold = 2, .cooldownSeconds = 60.0, .maximumTracked = 3 } };
		(void)lockout.RecordFailure(kStranger, 0.0);
		(void)lockout.RecordFailure(kStranger, 0.0);
		for (std::uint64_t index = 0; index < 10; ++index) (void)lockout.RecordFailure(kOther + index, 1.0 + static_cast<double>(index));
		assert(lockout.TrackedCount() == 3);
		assert(lockout.Check(kStranger, 20.0).locked);

		assert(lockout.Sweep(30.0) == 0);
		assert(lockout.Sweep(200.0) == 3 && lockout.TrackedCount() == 0);

		PasswordLockout full{ LockoutOptions{ .threshold = 1, .cooldownSeconds = 60.0, .maximumTracked = 2 } };
		assert(full.RecordFailure(kStranger, 5.0).lockedNow);
		assert(full.RecordFailure(kOther, 5.0).lockedNow);
		const auto refused = full.RecordFailure(kOther + 99, 6.0);
		assert(refused.tableFull && !refused.locked);
		assert(full.TrackedCount() == 2);
		assert(full.Check(kStranger, 7.0).locked && full.Check(kOther, 7.0).locked);
		assert(full.Sweep(66.0) == 2);

		PasswordLockout partial{ LockoutOptions{ .threshold = 3, .cooldownSeconds = 60.0, .maximumTracked = 2 } };
		(void)partial.RecordFailure(kStranger, 4.0);
		(void)partial.RecordFailure(kStranger, 4.0);
		(void)partial.RecordFailure(kOther, 5.0);
		const auto replaced = partial.RecordFailure(kOther + 99, 6.0);
		assert(!replaced.tableFull && partial.TrackedCount() == 2);
		assert(partial.Check(kStranger, 7.0).failures == 0);
		assert(partial.Check(kOther, 7.0).failures == 1);
	}

	struct BindSnapshot
	{
		std::vector<std::pair<std::uint64_t, bool>> bound;
		std::vector<std::uint16_t> unbound;
	};

	struct BindRecord
	{
		mutable std::mutex mutex;
		std::vector<std::pair<std::uint64_t, bool>> bound;
		std::vector<std::uint16_t> unbound;

		[[nodiscard]] BindSnapshot Snapshot() const
		{
			std::lock_guard lock{ mutex };
			return BindSnapshot{ bound, unbound };
		}
	};

	struct LogSink
	{
		std::mutex mutex;
		std::vector<std::string> lines;

		void Add(std::string_view a_line)
		{
			std::lock_guard lock{ mutex };
			lines.emplace_back(a_line);
		}

		[[nodiscard]] std::vector<std::string> Snapshot()
		{
			std::lock_guard lock{ mutex };
			return lines;
		}
	};

	SteamIdentityBinder Binder(BindRecord& a_record, std::optional<HostPolicy> a_policy)
	{
		return SteamIdentityBinder{
			.bind = [&a_record](std::uint16_t, std::uint64_t a_steamId, bool a_passwordRequired) {
				std::lock_guard lock{ a_record.mutex };
				a_record.bound.emplace_back(a_steamId, a_passwordRequired);
				return true;
			},
			.unbind = [&a_record](std::uint16_t a_port) {
				std::lock_guard lock{ a_record.mutex };
				a_record.unbound.push_back(a_port);
			},
			.hostPolicy = a_policy
		};
	}

	bool WaitFor(const std::function<bool()>& a_predicate)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 2 };
		while (!a_predicate()) {
			if (std::chrono::steady_clock::now() >= deadline) return false;
			std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
		}
		return true;
	}

	std::size_t CountLines(const std::vector<std::string>& a_lines, std::string_view a_text)
	{
		return static_cast<std::size_t>(std::count_if(a_lines.begin(), a_lines.end(), [a_text](const std::string& a_line) {
			return a_line.find(a_text) != std::string::npos;
		}));
	}

	void TestTunnelFriendsOnlyOff()
	{
		FakeSteam steam;
		FakeUdp udp;
		BindRecord record;
		LogSink sink;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		host.SetLog([&sink](std::string_view a_line) { sink.Add(a_line); });
		assert(host.Start(kServerPort, FriendOnly(), kMaximumTunnelConnections, Binder(record, HostPolicy{ false, true })) == TunnelError::None);
		const auto stranger = QueueIncoming(steam, kStranger);
		const auto friendPeer = QueueIncoming(steam, kFriend);
		host.Pump();
		assert(steam.IsAccepted(stranger) && steam.IsAccepted(friendPeer));
		assert(WaitFor([&sink] { return sink.Snapshot().size() == 4; }));
		const auto boundView = record.Snapshot();
		assert(boundView.bound.size() == 2);
		assert(std::find(boundView.bound.begin(), boundView.bound.end(), std::pair<std::uint64_t, bool>{ kStranger, true }) != boundView.bound.end());
		assert(std::find(boundView.bound.begin(), boundView.bound.end(), std::pair<std::uint64_t, bool>{ kFriend, false }) != boundView.bound.end());
		const auto log = sink.Snapshot();
		assert(CountLines(log, "friends_only=off") == 2 && CountLines(log, "password=required_by_server") == 1);
		assert(CountLines(log, "outcome=relay_ready relayed=") == 2);
		for (const auto& line : log) assert(!ContainsSteamIdDigits(line));
		host.Stop();
	}

	void TestTunnelFriendsOnlyOffFailsClosedWithoutServerCheck()
	{
		for (const auto& binderPolicy : { std::optional<HostPolicy>{}, std::optional<HostPolicy>{ HostPolicy{ false, false } } }) {
			FakeSteam steam;
			FakeUdp udp;
			BindRecord record;
			SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
			assert(host.Start(kServerPort, FriendOnly(), kMaximumTunnelConnections, Binder(record, binderPolicy)) == TunnelError::None);
			const auto stranger = QueueIncoming(steam, kStranger);
			host.Pump();
			assert(!steam.IsAccepted(stranger) && steam.CloseReason(stranger) == Admission::kEndFriendsOnly);
			assert(record.Snapshot().bound.empty() && udp.OpenedPorts().empty());
			host.Stop();
		}

		FakeSteam steam;
		FakeUdp udp;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.Start(kServerPort, FriendOnly()) == TunnelError::None);
		const auto stranger = QueueIncoming(steam, kStranger);
		host.Pump();
		assert(steam.CloseReason(stranger) == Admission::kEndFriendsOnly);
		host.Stop();
	}

	void TestTunnelUnavailableFriendCheckAndPolicyErrors()
	{
		FakeSteam steam;
		FakeUdp udp;
		LogSink sink;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		host.SetLog([&sink](std::string_view a_line) { sink.Add(a_line); });
		assert(host.Start(kServerPort, nullptr) == TunnelError::None);
		const auto peer = QueueIncoming(steam, kFriend);
		host.Pump();
		assert(steam.CloseReason(peer) == Admission::kEndFriendCheckUnavailable);
		const auto log = sink.Snapshot();
		assert(log.size() == 1 && CountLines(log, "reason=friend_check_unavailable") == 1);
		host.Stop();

		FakeSteam throwingSteam;
		FakeUdp throwingUdp;
		LogSink errors;
		SteamHostTunnel throwing{ MakeSteamApi(throwingSteam), MakeUdpApi(throwingUdp) };
		throwing.SetLog([&errors](std::string_view a_line) {
			errors.Add(a_line);
			throw std::runtime_error("sink failure");
		});
		assert(throwing.Start(kServerPort, [](std::uint64_t) -> bool { throw std::runtime_error("friend lookup failed"); }) == TunnelError::None);
		const auto failing = QueueIncoming(throwingSteam, kFriend);
		throwing.Pump();
		assert(!throwingSteam.IsAccepted(failing) && throwingSteam.CloseReason(failing) == Admission::kEndNotPermitted);
		const auto errorLines = errors.Snapshot();
		assert(errorLines.size() == 1 && CountLines(errorLines, "reason=policy_error") == 1);
		throwing.Stop();
	}

	void TestTunnelLastSlotAndDuplicates()
	{
		FakeSteam steam;
		FakeUdp udp;
		LogSink sink;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		host.SetLog([&sink](std::string_view a_line) { sink.Add(a_line); });
		assert(host.Start(kServerPort, MakeFriendsOnlyPolicy([](std::uint64_t) { return true; }), 3) == TunnelError::None);
		const auto first = QueueIncoming(steam, kFriend);
		const auto second = QueueIncoming(steam, kFriend + 1);
		const auto lastA = QueueIncoming(steam, kFriend + 2);
		const auto lastB = QueueIncoming(steam, kFriend + 3);
		host.Pump();
		assert(steam.IsAccepted(lastA) && !steam.IsAccepted(lastB));
		assert(steam.CloseReason(lastB) == Admission::kEndServerFull);
		assert(host.Stats().activeConnections == 3 && host.Stats().connectionsRefusedCapacity == 1);
		const auto log = sink.Snapshot();
		assert(log.back().find("reason=server_full") != std::string::npos && log.back().find("relayed=3/3") != std::string::npos);

		const auto rejoin = QueueIncoming(steam, kFriend);
		host.Pump();
		assert(steam.IsAccepted(rejoin));
		assert(steam.IsClosed(first) && !steam.IsClosed(second));
		assert(host.Stats().activeConnections == 3);
		assert(CountLines(sink.Snapshot(), "from the same Steam account") == 1);
		host.Stop();
	}

	void TestSupersededRelayKeptWhenNewRelayFails()
	{
		FakeSteam steam;
		FakeUdp udp;
		BindRecord record;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		assert(host.Start(kServerPort, FriendOnly(), kMaximumTunnelConnections, Binder(record, HostPolicy{ false, true })) == TunnelError::None);
		const auto first = QueueIncoming(steam, kStranger);
		host.Pump();
		assert(steam.IsAccepted(first) && host.Stats().activeConnections == 1);

		udp.openFails = true;
		const auto socketFailed = QueueIncoming(steam, kStranger);
		host.Pump();
		assert(!steam.IsAccepted(socketFailed));
		assert(!steam.IsClosed(first) && host.Stats().activeConnections == 1);
		host.Stop();

		FakeSteam bindSteam;
		FakeUdp bindUdp;
		BindRecord bindRecord;
		std::atomic<int> bindCalls{ 0 };
		SteamIdentityBinder failing{
			.bind = [&bindCalls](std::uint16_t, std::uint64_t, bool) { return ++bindCalls == 1; },
			.unbind = [&bindRecord](std::uint16_t a_port) {
				std::lock_guard lock{ bindRecord.mutex };
				bindRecord.unbound.push_back(a_port);
			},
			.hostPolicy = HostPolicy{ false, true }
		};
		SteamHostTunnel bindHost{ MakeSteamApi(bindSteam), MakeUdpApi(bindUdp) };
		assert(bindHost.Start(kServerPort, FriendOnly(), kMaximumTunnelConnections, failing) == TunnelError::None);
		const auto kept = QueueIncoming(bindSteam, kStranger);
		bindHost.Pump();
		assert(bindSteam.IsAccepted(kept));
		assert(WaitFor([&bindCalls] { return bindCalls == 1; }));
		const auto bindFailed = QueueIncoming(bindSteam, kStranger);
		bindHost.Pump();
		assert(bindSteam.IsAccepted(bindFailed));
		assert(WaitFor([&bindSteam, bindFailed] { return bindSteam.IsClosed(bindFailed); }));
		assert(!bindSteam.IsClosed(kept) && bindHost.Stats().activeConnections == 1);
		bindHost.Stop();
	}

	void TestRelayWelcomeTimeoutReclaimsSlot()
	{
		FakeSteam steam;
		FakeUdp udp;
		BindRecord record;
		double clock = 100.0;
		LogSink sink;
		SteamHostTunnel host{ MakeSteamApi(steam), MakeUdpApi(udp) };
		host.SetLog([&sink](std::string_view a_line) { sink.Add(a_line); });
		host.SetClock([&clock] { return clock; });
		assert(host.Start(kServerPort, FriendOnly(), kMaximumTunnelConnections, Binder(record, HostPolicy{ false, true })) == TunnelError::None);

		const auto silent = QueueIncoming(steam, kStranger);
		host.Pump();
		assert(steam.IsAccepted(silent) && host.Stats().activeConnections == 1);
		assert(WaitFor([&sink] { return CountLines(sink.Snapshot(), "outcome=relay_ready") == 1; }));

		clock += F4T::Transport::kRelayWelcomeTimeoutSeconds + 1.0;
		host.Pump();
		assert(steam.IsClosed(silent) && host.Stats().activeConnections == 0);
		assert(CountLines(sink.Snapshot(), "reason=welcome_timeout") == 1);
		assert(record.Snapshot().unbound.size() == 1);

		const auto answered = QueueIncoming(steam, kStranger + 1);
		host.Pump();
		assert(steam.IsAccepted(answered) && host.Stats().activeConnections == 1);
		assert(WaitFor([&sink] { return CountLines(sink.Snapshot(), "outcome=relay_ready") == 2; }));
		const auto relayPort = udp.OpenedPorts().back();
		udp.sockets[relayPort].inbox.push_back(UdpDatagram{ kServerPort, { 0x01, 0x02, 0x03 } });
		assert(WaitFor([&host] {
			host.Pump();
			return host.Stats().datagramsToSteam == 1;
		}));
		clock += F4T::Transport::kRelayWelcomeTimeoutSeconds + 1.0;
		host.Pump();
		assert(!steam.IsClosed(answered) && host.Stats().activeConnections == 1);
		host.Stop();
	}

	void TestGuestKeepsHostCloseReason()
	{
		FakeSteam steam;
		FakeUdp udp;
		SteamGuestTunnel guest{ MakeSteamApi(steam), MakeUdpApi(udp) };
		auto connected = guest.Connect(kHostSteamId);
		assert(connected.ok() && guest.EndReason() == 0);
		const auto connection = steam.nextConnection - 1;
		steam.events.push_back(SteamConnectionEvent{ connection, kHostSteamId, TunnelState::Failed, Admission::kEndServerFull });
		guest.Pump();
		assert(!guest.IsRunning() && guest.EndReason() == Admission::kEndServerFull);
		assert(Admission::GuestMessage(guest.EndReason()) == "The host's game is full.");
		guest.Stop();
		assert(guest.EndReason() == Admission::kEndServerFull);

		connected = guest.Connect(kHostSteamId);
		assert(connected.ok() && guest.EndReason() == 0);
		guest.Stop();
		assert(guest.EndReason() == 0);
	}
}

int main()
{
	TestDefaultsAreFriendsOnly();
	TestFriendsAreAdmittedWithoutTunnelPassword();
	TestFriendsOnlyOffNeedsAnEnforceablePassword();
	TestUnavailableFriendCheckFailsClosed();
	TestInvalidIdentityIsRefusedFirst();
	TestCapacity();
	TestReasonsAndMessages();
	TestLockoutThresholdAndCooldown();
	TestLockoutResetPolicy();
	TestLockoutStateIsBounded();
	TestTunnelFriendsOnlyOff();
	TestTunnelFriendsOnlyOffFailsClosedWithoutServerCheck();
	TestTunnelUnavailableFriendCheckAndPolicyErrors();
	TestTunnelLastSlotAndDuplicates();
	TestSupersededRelayKeptWhenNewRelayFails();
	TestRelayWelcomeTimeoutReclaimsSlot();
	TestGuestKeepsHostCloseReason();
	return 0;
}

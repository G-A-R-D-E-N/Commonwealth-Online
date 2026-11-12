#include "F4TSteamTunnelServiceTestSupport.h"

namespace ServiceTest
{
namespace
{
	void CheckGuestPresence()
	{
		const auto fake = std::make_shared<FakeSteam>();
		const auto friends = std::make_shared<FakeFriends>();
		Initialize([fake, friends] {
			auto result = MakeAvailable(fake);
			FillFriends(result, friends);
			return result;
		});
		RecordedLog::Clear();
		F4T::Steam::ClearGuestPresence();
		assert(friends->clears == 0);

		F4T::Steam::SetGuestPresence("Hosty", 39734273);
		assert(friends->Has("status", "Playing Commonwealth Online - Hosty"));
		assert(friends->Has("connect", "39734273"));
		assert(!friends->HasKey("commonwealth"));
		assert(RecordedLog::Contains("rich presence set status=\"Playing Commonwealth Online - Hosty\""));

		F4T::Steam::ClearGuestPresence();
		assert(friends->clears == 1);
		assert(RecordedLog::Contains("rich presence cleared for guest"));
		F4T::Steam::ClearGuestPresence();
		assert(friends->clears == 1);

		F4T::Steam::SetGuestPresence(std::string_view{ "Bad\nName" });
		assert(friends->Has("status", "Playing Commonwealth Online - BadName"));
		F4T::Steam::SetGuestPresence("");
		assert(friends->Has("status", "Playing Commonwealth Online"));
		F4T::Steam::ClearGuestPresence();
		assert(friends->clears == 2);

		assert(StartHost(kServerPort, 4) == TunnelError::None);
		AnnounceHosting("Vault");
		F4T::Steam::SetGuestPresence("Other");
		F4T::Steam::ClearGuestPresence();
		assert(friends->clears == 2);
		StopHost();
		assert(friends->clears == 3);
		Shutdown();
	}

	void CheckGuestLinkWait()
	{
		const auto fake = std::make_shared<FakeSteam>();
		Initialize([fake] { return MakeAvailable(fake); });

		const auto connected = ConnectGuest(kHostSteamId);
		assert(connected.ok());
		assert(GuestState() == F4T::Transport::TunnelState::Connecting);

		std::string failure;
		assert(!WaitForGuestConnected(std::chrono::milliseconds{ 40 }, failure));
		assert(failure == "The Steam link was still connecting after 40 milliseconds.");
		assert(GuestState() == F4T::Transport::TunnelState::Connecting);

		SteamConnectionEvent linked{};
		linked.connection = kGuestHandle;
		linked.peerSteamId = kHostSteamId;
		linked.state = F4T::Transport::TunnelState::Connected;
		fake->PushEvent(linked);

		failure.clear();
		assert(WaitForGuestConnected(std::chrono::seconds{ 3 }, failure));
		assert(failure.empty());
		assert(GuestState() == F4T::Transport::TunnelState::Connected);

		SteamConnectionEvent refused{};
		refused.connection = kGuestHandle;
		refused.peerSteamId = kHostSteamId;
		refused.state = F4T::Transport::TunnelState::Failed;
		refused.endReason = F4T::Transport::kConnectionEndServerFull;
		refused.endDebug = "server full";
		fake->PushEvent(refused);

		assert(WaitForGuestState(F4T::Transport::TunnelState::Failed, std::chrono::seconds{ 10 }));
		failure.clear();
		assert(!WaitForGuestConnected(std::chrono::seconds{ 3 }, failure));
		assert(failure == "That host is full. (server full)");
		assert(GuestState() == F4T::Transport::TunnelState::Failed);

		StopGuest();
		assert(GuestState() == F4T::Transport::TunnelState::Failed);
		Shutdown();
	}

	void CheckGuestRefusalMessages()
	{
		const auto fake = std::make_shared<FakeSteam>();
		Initialize([fake] { return MakeAvailable(fake); });

		struct RefusalCase
		{
			int code;
			std::string debug;
			std::string message;
		};
		const RefusalCase cases[] = {
			{ F4T::Transport::kConnectionEndNotPermitted, "not a permitted peer",
				"The host refused the connection. (not a permitted peer)" },
			{ F4T::Transport::kConnectionEndServerFull, "server full",
				"That host is full. (server full)" },
			{ F4T::Transport::kConnectionEndLocalSocketFailed, "local socket failed",
				"The host could not open a relay port for you. (local socket failed)" },
			{ F4T::Transport::kConnectionEndIdentityUnlinked, "identity link failed",
				"The host could not confirm your Steam identity. (identity link failed)" },
			{ F4T::Transport::kConnectionEndFriendsOnly, "not_friend",
				"That host only accepts Steam friends. (not_friend)" },
			{ F4T::Transport::kConnectionEndUndeliverable, "Invalid numeric reason code",
				std::string{ F4T::Transport::DescribeConnectionEnd(F4T::Transport::kConnectionEndUndeliverable) } },
		};

		for (const auto& refusal : cases) {
			const auto handle = fake->nextConnection;
			const auto connected = ConnectGuest(kHostSteamId);
			assert(connected.ok());

			SteamConnectionEvent refused{};
			refused.connection = handle;
			refused.peerSteamId = kHostSteamId;
			refused.state = F4T::Transport::TunnelState::Failed;
			refused.endReason = refusal.code;
			refused.endDebug = refusal.debug;
			fake->PushEvent(refused);

			assert(WaitForGuestState(F4T::Transport::TunnelState::Failed, std::chrono::seconds{ 10 }));
			std::string failure;
			assert(!WaitForGuestConnected(std::chrono::seconds{ 2 }, failure));
			assert(failure == refusal.message);
			StopGuest();
		}

		Shutdown();
	}

	void CheckHostIdentityLinkFlag()
	{
		const auto fake = std::make_shared<FakeSteam>();
		Initialize([fake] { return MakeAvailable(fake); });

		F4T::Steam::SetIdentityBinderFactory([](std::uint16_t, std::uint64_t) {
			return F4T::Transport::SteamIdentityBinder{};
		});
		assert(StartHost(kServerPort, 4) == TunnelError::None);
		assert(!F4T::Steam::HostIdentityLinked());
		StopHost();
		assert(!F4T::Steam::HostIdentityLinked());

		F4T::Steam::SetIdentityBinderFactory([](std::uint16_t, std::uint64_t) {
			return F4T::Transport::SteamIdentityBinder{
				.bind = [](std::uint16_t, std::uint64_t, bool, std::uint64_t) { return true; },
				.unbind = [](std::uint16_t, std::uint64_t) { return true; },
				.hostProof = std::string(64, 'a')
			};
		});
		assert(StartHost(kServerPort, 4) == TunnelError::None);
		assert(F4T::Steam::HostIdentityLinked());
		StopHost();
		assert(!F4T::Steam::HostIdentityLinked());

		F4T::Steam::SetIdentityBinderFactory({});
		Shutdown();
	}


}

	void RunGuestChecks()
	{
		CheckGuestPresence();
		CheckGuestLinkWait();
		CheckGuestRefusalMessages();
		CheckHostIdentityLinkFlag();
	}
}

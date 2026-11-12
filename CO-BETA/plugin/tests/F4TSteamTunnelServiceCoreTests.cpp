#include "F4TSteamTunnelServiceTestSupport.h"

namespace ServiceTest
{
namespace
{
	void CheckUnavailable()
	{
		assert(!HostRunning());
		assert(!GuestRunning());

		int calls = 0;
		const auto loader = [&calls] {
			++calls;
			return MakeUnavailable("no steam_api64.dll in this install");
		};

		Initialize(loader);
		Initialize(loader);
		assert(calls == 2);

		const ServiceStatus status = Status();
		assert(!status.available);
		assert(status.reason == "no steam_api64.dll in this install");
		assert(MySteamId() == 0);

		assert(StartHost(kServerPort, 4) == TunnelError::SteamUnavailable);
		assert(ConnectGuest(kHostSteamId).error == TunnelError::SteamUnavailable);

		StopHost();
		StopGuest();
		assert(!HostRunning());
		assert(!GuestRunning());
		Shutdown();
	}

	void CheckAvailable()
	{
		const auto fake = std::make_shared<FakeSteam>();
		int calls = 0;
		const auto loader = [&calls, fake] {
			++calls;
			return MakeAvailable(fake);
		};

		Initialize(loader);
		Initialize(loader);
		assert(calls == 1);

		const ServiceStatus status = Status();
		assert(status.available);
		assert(status.reason.empty());
		assert(MySteamId() == kMySteamId);

		std::vector<std::pair<std::uint16_t, std::uint64_t>> binderRequests;
		F4T::Steam::SetIdentityBinderFactory([&binderRequests](std::uint16_t a_port, std::uint64_t a_hostSteamId) {
			binderRequests.emplace_back(a_port, a_hostSteamId);
			return F4T::Transport::SteamIdentityBinder{};
		});

		assert(!HostRunning());
		assert(StartHost(0, 4) == TunnelError::InvalidArgument);
		assert(StartHost(kServerPort, 1) == TunnelError::InvalidArgument);
		assert(!HostRunning());

		assert(StartHost(kServerPort, 4) == TunnelError::None);
		assert(HostRunning());
		assert(fake->listenSocketsCreated.load() == 1);
		assert(binderRequests.size() == 1);
		assert(binderRequests.front().first == kServerPort);
		assert(binderRequests.front().second == kMySteamId);
		F4T::Steam::SetIdentityBinderFactory({});
		assert(StartHost(kServerPort, 4) == TunnelError::InvalidArgument);
		assert(fake->listenSocketsCreated.load() == 1);

		WaitForCallbacks(fake);
		assert(fake->callbackRuns.load() > 0);

		StopHost();
		assert(!HostRunning());
		assert(WaitFor([&] { return fake->closedListenSockets.load() == 1; }));
		StopHost();
		assert(!HostRunning());
		assert(fake->closedListenSockets.load() == 1);

		const int settled = fake->callbackRuns.load();
		std::this_thread::sleep_for(std::chrono::milliseconds{ 50 });
		assert(fake->callbackRuns.load() == settled);

		assert(!GuestRunning());
		const auto connected = ConnectGuest(kHostSteamId);
		assert(connected.ok());
		assert(connected.loopbackPort != 0);
		assert(GuestRunning());

		StopGuest();
		assert(!GuestRunning());
		assert(fake->closedConnections.load() == 1);
		StopGuest();
		assert(fake->closedConnections.load() == 1);

		Shutdown();
		assert(!HostRunning());
		assert(!GuestRunning());
	}

	void CheckFailedLoadRetries()
	{
		int calls = 0;
		const auto fake = std::make_shared<FakeSteam>();
		const auto loader = [&calls, fake] {
			++calls;
			if (calls == 1) {
				auto result = MakeAvailable(fake);
				result.available = false;
				result.reason = "Steam is still starting";
				return result;
			}
			return MakeAvailable(fake);
		};

		Initialize(loader);
		assert(calls == 1);
		assert(!Status().available);
		assert(Status().reason == "Steam is still starting");

		Initialize(loader);
		assert(calls == 2);
		assert(Status().available);
		assert(MySteamId() == kMySteamId);
		assert(StartHost(kServerPort, 4) == TunnelError::None);
		StopHost();
		assert(StartHost(kServerPort, 100) == TunnelError::None);
		assert(HostRunning());
		StopHost();

		Initialize(loader);
		assert(calls == 2);
		Shutdown();
	}

	void CheckResetAfterShutdown()
	{
		int calls = 0;
		Initialize([&calls] {
			++calls;
			return MakeUnavailable("second run");
		});
		assert(calls == 1);
		const ServiceStatus status = Status();
		assert(!status.available);
		assert(status.reason == "second run");
		Shutdown();
	}


	void CheckRetryIfUnavailable()
	{
		int calls = 0;
		Initialize([&calls] {
			++calls;
			return MakeUnavailable(calls == 1 ? "first" : "second");
		});
		assert(calls == 1);
		assert(RetryIfUnavailable());
		assert(calls == 2);
		assert(Status().reason == "second");
		assert(!RetryIfUnavailable());
		assert(calls == 2);
		Shutdown();

		const auto fake = std::make_shared<FakeSteam>();
		int availableCalls = 0;
		Initialize([&availableCalls, fake] {
			++availableCalls;
			return MakeAvailable(fake);
		});
		assert(!RetryIfUnavailable());
		assert(availableCalls == 1);
		assert(Status().available);
		Shutdown();
	}

}

	void RunCoreChecks()
	{
		CheckUnavailable();
		CheckAvailable();
		CheckFailedLoadRetries();
		CheckResetAfterShutdown();
		CheckRetryIfUnavailable();
	}
}

#include "F4TSteamInvite.h"
#include "F4TSteamTunnelService.h"

#include <atomic>
#include <cassert>
#include <cstring>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace RecordedLog
{
	std::mutex g_mutex;
	std::vector<std::string> g_lines;

	void Clear()
	{
		std::lock_guard lock{ g_mutex };
		g_lines.clear();
	}

	bool Contains(std::string_view a_text)
	{
		std::lock_guard lock{ g_mutex };
		for (const auto& line : g_lines) {
			if (line.find(a_text) != std::string::npos) return true;
		}
		return false;
	}
}

namespace F4T::DebugLogging
{
	bool IsEnabled()
	{
		return true;
	}

	void Write(std::string_view a_component, std::string_view a_message)
	{
		std::lock_guard lock{ RecordedLog::g_mutex };
		RecordedLog::g_lines.push_back(std::string{ a_component } + ": " + std::string{ a_message });
	}
}

namespace
{
	using F4T::Steam::AnnounceHosting;
	using F4T::Steam::ConnectGuest;
	using F4T::Steam::FindHost;
	using F4T::Steam::FriendCode;
	using F4T::Steam::GuestRunning;
	using F4T::Steam::GuestState;
	using F4T::Steam::WaitForGuestConnected;
	using F4T::Steam::HostRunning;
	using F4T::Steam::Initialize;
	using F4T::Steam::MySteamId;
	using F4T::Steam::RetryIfUnavailable;
	using F4T::Steam::ServiceStatus;
	using F4T::Steam::Shutdown;
	using F4T::Steam::StartHost;
	using F4T::Steam::Status;
	using F4T::Steam::StopGuest;
	using F4T::Steam::StopHost;
	using F4T::Transport::SteamApi;
	using F4T::Transport::SteamConnectionEvent;
	using F4T::Transport::SteamConnectionHandle;
	using F4T::Transport::SteamListenSocketHandle;
	using F4T::Transport::TunnelError;

	constexpr std::uint64_t kMySteamId = 76561198000000001ull;
	constexpr std::uint64_t kFriend = 76561198000000042ull;
	constexpr std::uint64_t kHostSteamId = 76561198000000007ull;
	constexpr std::uint16_t kServerPort = 7777;
	constexpr F4T::Transport::SteamConnectionHandle kGuestHandle = 1;
	std::string* g_acceptedConnect = nullptr;
	constexpr std::uint64_t kLobbyBase = (1ull << 56) | (8ull << 52) | (0x40000ull << 32);
	constexpr const char* kTestBuild = "0.1.0 / Fallout 4 1.11.240";
	std::vector<std::string> g_connects;
	std::vector<std::string> g_failures;

	void AcceptConnect(const char* a_connect)
	{
		if (g_acceptedConnect != nullptr && a_connect != nullptr) *g_acceptedConnect = a_connect;
	}

	void RecordConnect(const char* a_connect)
	{
		g_connects.emplace_back(a_connect ? a_connect : "<null>");
	}

	void RecordFailure(const char* a_message)
	{
		g_failures.emplace_back(a_message ? a_message : "<null>");
	}

	void ResetJoinRecords()
	{
		g_connects.clear();
		g_failures.clear();
		F4T::Steam::SetJoinConnectHandler(&RecordConnect);
		F4T::Steam::SetJoinFailureHandler(&RecordFailure);
		F4T::Steam::SetLobbyJoinHandler(&F4T::Steam::JoinLobby);
	}

	struct FakeSteam
	{
		std::atomic<int> callbackRuns{ 0 };
		std::atomic<int> listenSocketsCreated{ 0 };
		std::atomic<int> closedListenSockets{ 0 };
		std::atomic<int> closedConnections{ 0 };
		std::atomic<int> connectCalls{ 0 };
		SteamListenSocketHandle nextListen{ 100 };
		SteamConnectionHandle nextConnection{ 1 };

		void PushEvent(const SteamConnectionEvent& a_event)
		{
			std::scoped_lock lock(_mutex);
			_events.push_back(a_event);
		}

		std::vector<SteamConnectionEvent> PollEvents()
		{
			std::scoped_lock lock(_mutex);
			auto events = std::move(_events);
			_events.clear();
			return events;
		}

	private:
		std::mutex _mutex;
		std::vector<SteamConnectionEvent> _events;
	};

	SteamApi MakeSteamApi(const std::shared_ptr<FakeSteam>& a_fake)
	{
		SteamApi api;
		api.getSteamId = [] { return kMySteamId; };
		api.initRelay = [] { return true; };
		api.createListenSocketP2P = [a_fake](std::uint16_t) {
			++a_fake->listenSocketsCreated;
			return a_fake->nextListen++;
		};
		api.connectP2P = [a_fake](std::uint64_t, std::uint16_t) {
			++a_fake->connectCalls;
			return a_fake->nextConnection++;
		};
		api.acceptConnection = [](SteamConnectionHandle) { return true; };
		api.closeConnection = [a_fake](SteamConnectionHandle, int, std::string_view) { ++a_fake->closedConnections; };
		api.closeListenSocket = [a_fake](SteamListenSocketHandle) { ++a_fake->closedListenSockets; };
		api.sendMessage = [](SteamConnectionHandle, std::span<const std::uint8_t>, int) { return true; };
		api.receiveMessages = [](SteamConnectionHandle, std::size_t) { return std::vector<std::vector<std::uint8_t>>{}; };
		api.pollConnectionEvents = [a_fake] { return a_fake->PollEvents(); };
		api.runCallbacks = [a_fake] { ++a_fake->callbackRuns; };
		return api;
	}

	F4T::Steam::SteamApiLoadResult MakeUnavailable(std::string a_reason)
	{
		F4T::Steam::SteamApiLoadResult result;
		result.available = false;
		result.reason = std::move(a_reason);
		return result;
	}

	F4T::Steam::SteamApiLoadResult MakeAvailable(const std::shared_ptr<FakeSteam>& a_fake)
	{
		F4T::Steam::SteamApiLoadResult result;
		result.available = true;
		result.mySteamId = kMySteamId;
		result.isFriend = [](std::uint64_t a_steamId) { return a_steamId == kFriend; };
		result.api = MakeSteamApi(a_fake);
		assert(result.api.IsComplete());
		return result;
	}

	void WaitForCallbacks(const std::shared_ptr<FakeSteam>& a_fake)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 5 };
		while (a_fake->callbackRuns.load() == 0 && std::chrono::steady_clock::now() < deadline) {
			std::this_thread::sleep_for(std::chrono::milliseconds{ 5 });
		}
	}

	bool WaitForGuestState(F4T::Transport::TunnelState a_expected, std::chrono::milliseconds a_timeout)
	{
		const auto deadline = std::chrono::steady_clock::now() + a_timeout;
		for (;;) {
			if (GuestState() == a_expected) return true;
			if (std::chrono::steady_clock::now() >= deadline) return false;
			std::this_thread::sleep_for(std::chrono::milliseconds{ 5 });
		}
	}

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
		assert(fake->closedListenSockets.load() == 1);
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

	struct FakePerson
	{
		std::uint64_t id{};
		std::string name;
		bool playing{ true };
		std::string presence;
		std::map<std::string, std::string> keys;
	};

	struct FakeFriends
	{
		std::vector<FakePerson> people;
		std::vector<std::pair<std::string, std::string>> presence;
		int clears{ 0 };
		std::mutex mutex;

		std::vector<std::pair<std::string, std::string>> Snapshot()
		{
			std::lock_guard lock{ mutex };
			return presence;
		}

		bool Has(std::string_view a_key, std::string_view a_value)
		{
			std::lock_guard lock{ mutex };
			for (const auto& entry : presence) {
				if (entry.first == a_key && entry.second == a_value) return true;
			}
			return false;
		}

		bool HasKey(std::string_view a_key)
		{
			std::lock_guard lock{ mutex };
			for (const auto& entry : presence) {
				if (entry.first == a_key) return true;
			}
			return false;
		}
	};

	struct FakeMatchmaking
	{
		std::mutex mutex;
		std::uint64_t nextLobby{ kLobbyBase + 1000 };
		int createCalls{ 0 };
		int createMembers{ 0 };
		bool failCreate{ false };
		bool deferCreate{ false };
		std::vector<std::pair<F4T::Steam::LobbyCreatedFn, std::uint64_t>> deferredAll;
		std::uint64_t deferredLobby{ 0 };
		std::vector<std::uint64_t> joins;
		std::vector<std::uint64_t> left;
		bool failJoin{ false };
		std::uint32_t joinResponse{ 1 };
		std::uint64_t owner{ kHostSteamId };
		std::map<std::uint64_t, std::map<std::string, std::string>> data;
		std::map<std::uint64_t, int> limits;
		std::map<std::uint64_t, bool> joinable;

		std::string Get(std::uint64_t a_lobby, const std::string& a_key)
		{
			std::lock_guard lock{ mutex };
			const auto lobby = data.find(a_lobby);
			if (lobby == data.end()) return {};
			const auto value = lobby->second.find(a_key);
			return value == lobby->second.end() ? std::string{} : value->second;
		}

		bool Left(std::uint64_t a_lobby)
		{
			std::lock_guard lock{ mutex };
			for (const auto lobby : left) {
				if (lobby == a_lobby) return true;
			}
			return false;
		}

		void CompleteDeferred(std::size_t a_index = 0)
		{
			F4T::Steam::LobbyCreatedFn done;
			std::uint64_t lobby = 0;
			{
				std::lock_guard lock{ mutex };
				if (a_index >= deferredAll.size()) return;
				done = std::move(deferredAll[a_index].first);
				lobby = deferredAll[a_index].second;
				deferredAll[a_index].first = {};
			}
			if (done) done(true, 1, lobby);
		}

		std::uint64_t DeferredLobby(std::size_t a_index)
		{
			std::lock_guard lock{ mutex };
			return deferredAll.at(a_index).second;
		}
	};

	void FillMatchmaking(F4T::Steam::SteamApiLoadResult& a_result, const std::shared_ptr<FakeMatchmaking>& a_mm)
	{
		a_result.matchmakingAvailable = true;
		auto& api = a_result.matchmaking;
		api.createLobby = [a_mm](int a_members, F4T::Steam::LobbyCreatedFn a_done) {
			std::uint64_t lobby = 0;
			bool fail = false;
			{
				std::lock_guard lock{ a_mm->mutex };
				++a_mm->createCalls;
				a_mm->createMembers = a_members;
				lobby = a_mm->nextLobby++;
				fail = a_mm->failCreate;
				if (a_mm->deferCreate) {
					a_mm->deferredAll.emplace_back(a_done, lobby);
					a_mm->deferredLobby = lobby;
					return;
				}
			}
			a_done(!fail, fail ? 2 : 1, fail ? 0 : lobby);
		};
		api.joinLobby = [a_mm](std::uint64_t a_lobby, F4T::Steam::LobbyEnteredFn a_done) {
			bool fail = false;
			std::uint32_t response = 1;
			{
				std::lock_guard lock{ a_mm->mutex };
				a_mm->joins.push_back(a_lobby);
				fail = a_mm->failJoin;
				response = a_mm->joinResponse;
			}
			a_done(!fail, response, a_lobby);
		};
		api.leaveLobby = [a_mm](std::uint64_t a_lobby) {
			std::lock_guard lock{ a_mm->mutex };
			a_mm->left.push_back(a_lobby);
		};
		api.setLobbyData = [a_mm](std::uint64_t a_lobby, const char* a_key, const char* a_value) {
			std::lock_guard lock{ a_mm->mutex };
			a_mm->data[a_lobby][a_key] = a_value;
			return true;
		};
		api.getLobbyData = [a_mm](std::uint64_t a_lobby, const char* a_key) { return a_mm->Get(a_lobby, a_key); };
		api.lobbyOwner = [a_mm](std::uint64_t) {
			std::lock_guard lock{ a_mm->mutex };
			return a_mm->owner;
		};
		api.memberCount = [](std::uint64_t) { return 1; };
		api.setMemberLimit = [a_mm](std::uint64_t a_lobby, int a_limit) {
			std::lock_guard lock{ a_mm->mutex };
			a_mm->limits[a_lobby] = a_limit;
			return true;
		};
		api.setJoinable = [a_mm](std::uint64_t a_lobby, bool a_open) {
			std::lock_guard lock{ a_mm->mutex };
			a_mm->joinable[a_lobby] = a_open;
			return true;
		};
	}

	template <class Predicate>
	bool WaitFor(Predicate a_predicate)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 5 };
		while (!a_predicate()) {
			if (std::chrono::steady_clock::now() >= deadline) return false;
			std::this_thread::sleep_for(std::chrono::milliseconds{ 5 });
		}
		return true;
	}

	void FillFriends(F4T::Steam::SteamApiLoadResult& a_result, const std::shared_ptr<FakeFriends>& a_friends)
	{
		a_result.friends.friendCount = [a_friends] { return static_cast<int>(a_friends->people.size()); };
		a_result.friends.friendByIndex = [a_friends](int a_index) {
			if (a_index < 0 || a_index >= static_cast<int>(a_friends->people.size())) return std::uint64_t{ 0 };
			return a_friends->people[static_cast<std::size_t>(a_index)].id;
		};
		a_result.friends.friendName = [a_friends](std::uint64_t a_steamId) {
			for (const auto& person : a_friends->people) {
				if (person.id == a_steamId) return person.name;
			}
			return std::string{};
		};
		a_result.friends.playingFallout = [a_friends](std::uint64_t a_steamId) {
			for (const auto& person : a_friends->people) {
				if (person.id == a_steamId) return person.playing;
			}
			return false;
		};
		a_result.friends.richPresence = [a_friends](std::uint64_t a_steamId, const char* a_key) {
			if (a_key == nullptr) return std::string{};
			for (const auto& person : a_friends->people) {
				if (person.id != a_steamId) continue;
				if (std::string_view{ a_key } == "commonwealth") return person.presence;
				const auto found = person.keys.find(a_key);
				return found == person.keys.end() ? std::string{} : found->second;
			}
			return std::string{};
		};
		a_result.friends.requestRichPresence = [](std::uint64_t) {};
		a_result.friends.setRichPresence = [a_friends](const char* a_key, const char* a_value) {
			std::lock_guard lock{ a_friends->mutex };
			a_friends->presence.emplace_back(a_key ? a_key : "", a_value ? a_value : "");
			return true;
		};
		a_result.friends.clearRichPresence = [a_friends] {
			std::lock_guard lock{ a_friends->mutex };
			++a_friends->clears;
		};
	}

	void CheckFriendCodeAndPresence()
	{
		constexpr std::uint64_t kAccount = 39734273ull;
		constexpr std::uint64_t kSam = 76561198000000201ull;
		constexpr std::uint64_t kSammy = 76561198000000202ull;
		assert(FriendCode(kMySteamId) == kAccount);
		assert(FriendCode(0) == 0);

		const auto fake = std::make_shared<FakeSteam>();
		const auto friends = std::make_shared<FakeFriends>();
		friends->people = {
			{ kFriend, "Ada", true, "" },
			{ kHostSteamId, "Hosty", true, "host" },
			{ 76561198000000099ull, "Away", false, "host" },
			{ kSam, "Sam", true, "" },
			{ kSammy, "Sammy", true, "" },
		};

		Initialize([fake, friends] {
			auto result = MakeAvailable(fake);
			FillFriends(result, friends);
			return result;
		});

		const auto byCode = FindHost(std::to_string(kAccount));
		assert(byCode.ok());
		assert(byCode.steamId == kMySteamId);

		const auto byFullId = FindHost(std::to_string(kMySteamId));
		assert(byFullId.ok());
		assert(byFullId.steamId == kMySteamId);

		const auto ada = FindHost(std::to_string(FriendCode(kFriend)));
		assert(ada.ok());
		assert(ada.steamId == kFriend);
		assert(ada.name == "Ada");

		assert(!FindHost("0").ok());
		assert(!FindHost("039734273").ok());

		const auto automatic = FindHost("Auto");
		assert(automatic.ok());
		assert(automatic.steamId == kHostSteamId);
		assert(automatic.name == "Hosty");
		assert(FindHost("  ").steamId == kHostSteamId);

		const auto named = FindHost("ada");
		assert(named.ok());
		assert(named.steamId == kFriend);

		const auto hostingName = FindHost("hosty");
		assert(hostingName.ok());
		assert(hostingName.steamId == kHostSteamId);

		const auto missing = FindHost("nope");
		assert(!missing.ok());
		assert(missing.error == "No Steam friend matching that name is playing Fallout 4.");

		const auto ambiguous = FindHost("sam");
		assert(!ambiguous.ok());
		assert(ambiguous.error == "More than one Steam friend matches that name. Enter the Friend Code.");

		friends->people[1].presence.clear();
		const auto nobody = FindHost("auto");
		assert(!nobody.ok());
		assert(nobody.error == "No Steam friend is hosting right now. Start the host first, or enter their Friend Code.");
		friends->people[1].presence = "host";

		assert(StartHost(kServerPort, 4) == TunnelError::None);
		AnnounceHosting("Vault");
		assert(friends->presence.size() >= 2);
		assert(friends->presence[0].first == "commonwealth");
		assert(friends->presence[0].second == "host");
		assert(friends->presence[1].first == "status");
		assert(friends->presence[1].second == "Hosting Commonwealth Online: Vault");

		StopHost();
		assert(friends->clears == 1);
		assert(ConnectGuest(kHostSteamId).ok());
		StopGuest();
		assert(friends->clears == 1);
		Shutdown();

		Initialize([] { return MakeAvailable(std::make_shared<FakeSteam>()); });
		const auto noList = FindHost("Nomad");
		assert(!noList.ok());
		assert(noList.error == "Steam did not provide the friend list. Enter the host's Friend Code.");
		const auto stillNumeric = FindHost(std::to_string(kAccount));
		assert(stillNumeric.ok());
		assert(stillNumeric.steamId == kMySteamId);
		Shutdown();
	}

	void CheckPresenceArrivesDuringWait()
	{
		const auto fake = std::make_shared<FakeSteam>();
		const auto friends = std::make_shared<FakeFriends>();
		friends->people = { { kHostSteamId, "Late", true, "host" } };
		const auto visibleAt = std::chrono::steady_clock::now() + std::chrono::milliseconds{ 60 };

		Initialize([fake, friends, visibleAt] {
			auto result = MakeAvailable(fake);
			FillFriends(result, friends);
			result.friends.richPresence = [friends, visibleAt](std::uint64_t a_steamId, const char* a_key) {
				if (a_key == nullptr || std::string_view{ a_key } != "commonwealth") return std::string{};
				if (std::chrono::steady_clock::now() < visibleAt) return std::string{};
				for (const auto& person : friends->people) {
					if (person.id == a_steamId) return person.presence;
				}
				return std::string{};
			};
			return result;
		});

		const auto found = FindHost("auto");
		assert(found.ok());
		assert(found.steamId == kHostSteamId);
		assert(found.name == "Late");
		assert(fake->callbackRuns.load() == 0);
		Shutdown();
	}

	void CheckInviteOverlay()
	{
		const auto fake = std::make_shared<FakeSteam>();
		const auto friends = std::make_shared<FakeFriends>();
		int registeredId = 0;
		std::string opened;
		F4T::Steam::BindInvite({
			friends.get(),
			[&opened](void*, const char* a_connect) { opened = a_connect ? a_connect : ""; },
			[&registeredId](void*, int a_id) { registeredId = a_id; },
			[](void*) {}
		});
		assert(registeredId == F4T::Steam::kRichPresenceJoinCallback);
		assert(F4T::Steam::LastRegisteredCallbackId() == F4T::Steam::kRichPresenceJoinCallback);
		assert(F4T::Steam::InviteCallbackBytes() == static_cast<int>(sizeof(F4T::Steam::RichPresenceJoinRequest)));
		assert(!F4T::Steam::OpenFriendInvite().empty());

		Initialize([fake, friends] {
			auto result = MakeAvailable(fake);
			FillFriends(result, friends);
			return result;
		});
		assert(StartHost(kServerPort, 4) == TunnelError::None);
		const auto code = FriendCode(kMySteamId);
		AnnounceHosting("Vault");
		F4T::Steam::RememberInviteCode(code);
		bool sawConnect = false;
		for (const auto& entry : friends->presence) {
			if (entry.first == "connect" && entry.second == std::to_string(code)) sawConnect = true;
		}
		assert(sawConnect);
		assert(F4T::Steam::OpenFriendInvite().empty());
		assert(opened == std::to_string(code));
		F4T::Steam::BindInvite({
			friends.get(),
			[&opened](void*, const char*) { opened = "overlay-off"; },
			[](void*, int) {},
			[](void*) {},
			[] { return false; }
		});
		assert(F4T::Steam::OpenFriendInvite().find("Steam overlay is turned off") != std::string::npos);
		assert(opened == std::to_string(code));
		opened.clear();
		F4T::Steam::BindInvite({
			friends.get(),
			[&opened](void*, const char*) { opened = "overlay-on"; },
			[](void*, int) {},
			[](void*) {},
			[] { return true; }
		});
		assert(F4T::Steam::OpenFriendInvite().empty());
		assert(opened == "overlay-on");

		std::string accepted;
		g_acceptedConnect = &accepted;
		F4T::Steam::SetJoinConnectHandler(&AcceptConnect);
		F4T::Steam::RichPresenceJoinRequest request{};
		const auto text = std::string{ "99" };
		std::memcpy(request.connect, text.c_str(), text.size());
		F4T::Steam::RunInviteCallbackForTest(&request);
		assert(accepted == "99");
		g_acceptedConnect = nullptr;

		StopHost();
		assert(!F4T::Steam::OpenFriendInvite().empty());
		assert(friends->clears == 1);
		Shutdown();
	}

	void CheckLobbyHosting()
	{
		F4T::Steam::SetLocalBuildTag(kTestBuild);
		const auto fake = std::make_shared<FakeSteam>();
		const auto friends = std::make_shared<FakeFriends>();
		const auto mm = std::make_shared<FakeMatchmaking>();
		friends->people = { { kFriend, "Ada", true, "" } };
		Initialize([fake, friends, mm] {
			auto result = MakeAvailable(fake);
			FillFriends(result, friends);
			FillMatchmaking(result, mm);
			return result;
		});
		assert(Status().matchmaking);
		assert(F4T::Steam::MatchmakingAvailable());
		assert(F4T::Steam::HostLobby() == 0);
		RecordedLog::Clear();

		assert(StartHost(kServerPort, 4) == TunnelError::None);
		AnnounceHosting("Vault");
		const auto lobby = F4T::Steam::HostLobby();
		assert(lobby == kLobbyBase + 1000);
		assert(mm->createCalls == 1 && mm->createMembers == 4);
		assert(mm->Get(lobby, "co_build") == kTestBuild);
		assert(mm->Get(lobby, "co_host") == "Vault");
		assert(mm->Get(lobby, "co_players") == "1");
		assert(mm->Get(lobby, "co_max") == "4");
		assert(mm->joinable[lobby]);
		assert(friends->Has("commonwealth", "host"));
		assert(friends->Has("status", "Hosting Commonwealth Online: Vault"));
		assert(friends->Has("co_build", kTestBuild));
		assert(friends->Has("co_host", "Vault"));
		assert(friends->Has("co_players", "1"));
		assert(friends->Has("co_max", "4"));
		AnnounceHosting("Vault2");
		assert(mm->createCalls == 1);
		assert(mm->Get(lobby, "co_host") == "Vault2");
		assert(friends->Has("co_host", "Vault2"));
		assert(RecordedLog::Contains("lobby host name refreshed"));
		assert(RecordedLog::Contains("SteamTunnel: lobby create requested type=friends-only members=4"));
		assert(RecordedLog::Contains("SteamTunnel: lobby created lobby=" + std::to_string(lobby)));
		assert(RecordedLog::Contains("SteamTunnel: lobby data lobby=" + std::to_string(lobby)));
		assert(RecordedLog::Contains("SteamSession: rich presence set commonwealth=host"));

		SteamConnectionEvent incoming{};
		incoming.connection = 900;
		incoming.peerSteamId = kFriend;
		incoming.state = F4T::Transport::TunnelState::Connecting;
		fake->PushEvent(incoming);
		assert(WaitFor([&] { return mm->Get(lobby, "co_players") == "2"; }));
		assert(WaitFor([&] { return friends->Has("co_players", "2"); }));
		SteamConnectionEvent closing{};
		closing.connection = 900;
		closing.peerSteamId = kFriend;
		closing.state = F4T::Transport::TunnelState::Closed;
		fake->PushEvent(closing);
		assert(WaitFor([&] { return mm->Get(lobby, "co_players") == "1"; }));

		std::string opened;
		std::uint64_t openedLobby = 0;
		F4T::Steam::BindInvite({
			friends.get(),
			[&opened](void*, const char* a_connect) { opened = a_connect; },
			[](void*, int) {},
			[](void*) {},
			{},
			[&openedLobby](void*, std::uint64_t a_lobby) { openedLobby = a_lobby; },
			{},
			{}
		});
		F4T::Steam::RememberInviteCode(FriendCode(kMySteamId));
		assert(F4T::Steam::OpenFriendInvite().empty());
		assert(openedLobby == lobby);
		assert(opened.empty());
		assert(RecordedLog::Contains("invite dialog opened for lobby " + std::to_string(lobby)));
		F4T::Steam::BindInvite({
			friends.get(),
			[&opened](void*, const char* a_connect) { opened = a_connect; },
			[](void*, int) {},
			[](void*) {}
		});
		assert(F4T::Steam::OpenFriendInvite().empty());
		assert(opened == std::to_string(FriendCode(kMySteamId)));
		assert(RecordedLog::Contains("invite dialog opened with connect string"));
		F4T::Steam::BindInvite({ nullptr, {}, [](void*, int) {}, [](void*) {} });
		assert(F4T::Steam::OpenFriendInvite() == "Steam invite overlay is not available.");
		assert(RecordedLog::Contains("invite not opened: Steam invite overlay is not available."));

		F4T::Steam::SetHostMemberLimit(6);
		assert(mm->Get(lobby, "co_max") == "6");
		assert(mm->limits[lobby] == 6);
		assert(friends->Has("co_max", "6"));

		StopHost();
		assert(F4T::Steam::HostLobby() == 0);
		assert(mm->Left(lobby));
		assert(RecordedLog::Contains("SteamTunnel: lobby left lobby=" + std::to_string(lobby)));
		assert(friends->clears == 1);
		const auto snapshotSize = friends->Snapshot().size();
		std::this_thread::sleep_for(std::chrono::milliseconds{ 40 });
		assert(friends->Snapshot().size() == snapshotSize);
		Shutdown();
		F4T::Steam::SetLocalBuildTag({});
	}

	void CheckLobbyFailureModes()
	{
		F4T::Steam::SetLocalBuildTag(kTestBuild);
		{
			const auto fake = std::make_shared<FakeSteam>();
			const auto friends = std::make_shared<FakeFriends>();
			const auto mm = std::make_shared<FakeMatchmaking>();
			mm->failCreate = true;
			Initialize([fake, friends, mm] {
				auto result = MakeAvailable(fake);
				FillFriends(result, friends);
				FillMatchmaking(result, mm);
				return result;
			});
			RecordedLog::Clear();
			assert(StartHost(kServerPort, 4) == TunnelError::None);
			AnnounceHosting("Vault");
			assert(mm->createCalls == 1);
			assert(F4T::Steam::HostLobby() == 0);
			assert(friends->Has("commonwealth", "host"));
			assert(RecordedLog::Contains("SteamTunnel: lobby create failed result=2"));
			F4T::Steam::RememberInviteCode(FriendCode(kMySteamId));
			std::string opened;
			F4T::Steam::BindInvite({ friends.get(), [&opened](void*, const char* a_connect) { opened = a_connect; }, [](void*, int) {}, [](void*) {}, {}, [](void*, std::uint64_t) { assert(false); }, {}, {} });
			assert(F4T::Steam::OpenFriendInvite().empty());
			assert(opened == std::to_string(FriendCode(kMySteamId)));
			mm->failCreate = false;
			AnnounceHosting("Vault");
			assert(F4T::Steam::HostLobby() != 0);
			StopHost();
			Shutdown();
		}
		{
			const auto fake = std::make_shared<FakeSteam>();
			const auto friends = std::make_shared<FakeFriends>();
			const auto mm = std::make_shared<FakeMatchmaking>();
			mm->deferCreate = true;
			Initialize([fake, friends, mm] {
				auto result = MakeAvailable(fake);
				FillFriends(result, friends);
				FillMatchmaking(result, mm);
				return result;
			});
			assert(StartHost(kServerPort, 4) == TunnelError::None);
			AnnounceHosting("Vault");
			AnnounceHosting("Vault");
			assert(mm->createCalls == 1);
			assert(F4T::Steam::HostLobby() == 0);
			StopHost();
			RecordedLog::Clear();
			mm->CompleteDeferred();
			assert(F4T::Steam::HostLobby() == 0);
			assert(mm->Left(mm->deferredLobby));
			assert(mm->Get(mm->deferredLobby, "co_build").empty());
			assert(RecordedLog::Contains("created after hosting stopped"));
			Shutdown();
		}
		{
			const auto fake = std::make_shared<FakeSteam>();
			const auto friends = std::make_shared<FakeFriends>();
			const auto mm = std::make_shared<FakeMatchmaking>();
			mm->deferCreate = true;
			Initialize([fake, friends, mm] {
				auto result = MakeAvailable(fake);
				FillFriends(result, friends);
				FillMatchmaking(result, mm);
				return result;
			});
			assert(StartHost(kServerPort, 4) == TunnelError::None);
			AnnounceHosting("Vault");
			StopHost();
			assert(StartHost(kServerPort, 4) == TunnelError::None);
			assert(HostRunning());
			mm->CompleteDeferred(0);
			assert(F4T::Steam::HostLobby() == 0);
			assert(mm->Left(mm->DeferredLobby(0)));
			assert(mm->Get(mm->DeferredLobby(0), "co_build").empty());
			assert(mm->createCalls == 1);
			StopHost();
			Shutdown();
		}
		{
			const auto fake = std::make_shared<FakeSteam>();
			const auto friends = std::make_shared<FakeFriends>();
			const auto mm = std::make_shared<FakeMatchmaking>();
			mm->deferCreate = true;
			Initialize([fake, friends, mm] {
				auto result = MakeAvailable(fake);
				FillFriends(result, friends);
				FillMatchmaking(result, mm);
				return result;
			});
			assert(StartHost(kServerPort, 4) == TunnelError::None);
			AnnounceHosting("Vault");
			StopHost();
			assert(StartHost(kServerPort, 4) == TunnelError::None);
			AnnounceHosting("Vault");
			assert(mm->createCalls == 2);
			const auto first = mm->DeferredLobby(0);
			const auto second = mm->DeferredLobby(1);
			mm->CompleteDeferred(1);
			assert(F4T::Steam::HostLobby() == second);
			assert(mm->Get(second, "co_build") == kTestBuild);
			mm->CompleteDeferred(0);
			assert(F4T::Steam::HostLobby() == second);
			assert(mm->Left(first) && !mm->Left(second));
			assert(mm->Get(first, "co_build").empty());
			StopHost();
			assert(mm->Left(second));
			Shutdown();
		}
		{
			const auto fake = std::make_shared<FakeSteam>();
			const auto friends = std::make_shared<FakeFriends>();
			const auto mm = std::make_shared<FakeMatchmaking>();
			friends->people = { { kFriend, "Ada", true, "" } };
			mm->deferCreate = true;
			Initialize([fake, friends, mm] {
				auto result = MakeAvailable(fake);
				FillFriends(result, friends);
				FillMatchmaking(result, mm);
				return result;
			});
			assert(StartHost(kServerPort, 4) == TunnelError::None);
			AnnounceHosting("Vault");
			SteamConnectionEvent incoming{};
			incoming.connection = 901;
			incoming.peerSteamId = kFriend;
			incoming.state = F4T::Transport::TunnelState::Connecting;
			fake->PushEvent(incoming);
			assert(WaitFor([&] { return friends->Has("co_players", "2"); }));
			mm->CompleteDeferred(0);
			assert(mm->Get(mm->DeferredLobby(0), "co_players") == "2");
			StopHost();
			Shutdown();
		}
		{
			const auto fake = std::make_shared<FakeSteam>();
			const auto friends = std::make_shared<FakeFriends>();
			RecordedLog::Clear();
			Initialize([fake, friends] {
				auto result = MakeAvailable(fake);
				FillFriends(result, friends);
				result.matchmakingAvailable = false;
				result.matchmakingMissing = { "SteamAPI_ISteamMatchmaking_CreateLobby" };
				result.missingOptional = { "SteamAPI_ISteamMatchmaking_CreateLobby" };
				return result;
			});
			assert(!Status().matchmaking);
			assert(!F4T::Steam::MatchmakingAvailable());
			assert(F4T::Steam::MissingOptionalExports() == std::vector<std::string>{ "SteamAPI_ISteamMatchmaking_CreateLobby" });
			assert(StartHost(kServerPort, 4) == TunnelError::None);
			AnnounceHosting("Vault");
			assert(F4T::Steam::HostLobby() == 0);
			assert(friends->Has("commonwealth", "host"));
			assert(friends->Has("co_build", kTestBuild));
			assert(RecordedLog::Contains("lobby not created: matchmaking is unavailable"));
			StopHost();
			ResetJoinRecords();
			F4T::Steam::JoinLobby(kLobbyBase + 5);
			assert(g_failures.size() == 1);
			assert(g_failures.front().find("Steam lobby invites are not available") != std::string::npos);
			assert(g_connects.empty());
			Shutdown();
		}
		F4T::Steam::SetLocalBuildTag({});
	}

	void CheckLobbyJoin()
	{
		F4T::Steam::SetLocalBuildTag(kTestBuild);
		const auto fake = std::make_shared<FakeSteam>();
		const auto friends = std::make_shared<FakeFriends>();
		const auto mm = std::make_shared<FakeMatchmaking>();
		Initialize([fake, friends, mm] {
			auto result = MakeAvailable(fake);
			FillFriends(result, friends);
			FillMatchmaking(result, mm);
			return result;
		});
		ResetJoinRecords();
		RecordedLog::Clear();
		const std::uint64_t lobby = kLobbyBase + 77;
		mm->data[lobby] = { { "co_build", kTestBuild }, { "co_host", "Vault" }, { "co_players", "1" }, { "co_max", "4" } };

		F4T::Steam::GameLobbyJoinRequest request{ lobby, kFriend };
		F4T::Steam::RunLobbyJoinCallbackForTest(&request);
		assert(mm->joins.size() == 1 && mm->joins.front() == lobby);
		assert(g_connects.size() == 1 && g_connects.front() == std::to_string(kHostSteamId));
		assert(g_failures.empty());
		assert(mm->Left(lobby));
		assert(fake->connectCalls.load() == 0);
		assert(RecordedLog::Contains("lobby join requested lobby=" + std::to_string(lobby)));
		assert(RecordedLog::Contains("lobby entered lobby=" + std::to_string(lobby)));
		assert(RecordedLog::Contains("lobby connect accepted"));

		g_connects.clear();
		mm->data[lobby]["co_build"] = "0.0.9 / Fallout 4 1.10.163";
		F4T::Steam::RunLobbyJoinCallbackForTest(&request);
		assert(g_connects.empty());
		assert(g_failures.size() == 1);
		assert(g_failures.front() == "Host is running a different Commonwealth Online build (host 0.0.9 / Fallout 4 1.10.163, you 0.1.0 / Fallout 4 1.11.240)");
		assert(fake->connectCalls.load() == 0);
		assert(!GuestRunning());
		assert(RecordedLog::Contains("join refused: Host is running a different Commonwealth Online build"));

		g_failures.clear();
		mm->data[lobby]["co_build"] = std::string(500, 'X') + "\x01";
		F4T::Steam::RunLobbyJoinCallbackForTest(&request);
		assert(g_connects.empty() && g_failures.size() == 1);
		assert(g_failures.front().size() < 200);
		assert(g_failures.front().find('\x01') == std::string::npos);
		assert(g_failures.front().ends_with(", you 0.1.0 / Fallout 4 1.11.240)"));

		g_failures.clear();
		mm->data[lobby].erase("co_build");
		F4T::Steam::RunLobbyJoinCallbackForTest(&request);
		assert(g_connects.empty());
		assert(g_failures.size() == 1 && g_failures.front() == "That Steam lobby is not a Commonwealth Online game.");

		g_failures.clear();
		mm->data[lobby]["co_build"] = kTestBuild;
		mm->failJoin = true;
		mm->joinResponse = 5;
		F4T::Steam::RunLobbyJoinCallbackForTest(&request);
		assert(g_connects.empty());
		assert(g_failures.size() == 1 && g_failures.front().starts_with("Could not join that Steam lobby (Steam response 5)"));
		assert(RecordedLog::Contains("lobby join failed lobby=" + std::to_string(lobby) + " response=5"));
		mm->failJoin = false;

		g_failures.clear();
		mm->owner = 0;
		F4T::Steam::RunLobbyJoinCallbackForTest(&request);
		assert(g_connects.empty() && g_failures.size() == 1);
		mm->owner = kMySteamId;
		g_failures.clear();
		F4T::Steam::RunLobbyJoinCallbackForTest(&request);
		assert(g_connects.empty() && g_failures.empty());
		mm->owner = 12;
		F4T::Steam::RunLobbyJoinCallbackForTest(&request);
		assert(g_connects.size() == 1 && g_connects.front() == "12");
		mm->owner = kHostSteamId;

		const auto joinsBefore = mm->joins.size();
		F4T::Steam::GameLobbyJoinRequest notLobby{ kFriend, kFriend };
		F4T::Steam::RunLobbyJoinCallbackForTest(&notLobby);
		assert(mm->joins.size() == joinsBefore);
		g_connects.clear();
		g_failures.clear();
		F4T::Steam::JoinLobby(kFriend);
		assert(mm->joins.size() == joinsBefore);
		assert(g_failures.size() == 1 && g_failures.front() == "That Steam lobby invite is not valid.");

		g_connects.clear();
		g_failures.clear();
		F4T::Steam::QueueLaunchJoin(F4T::Steam::ParseLaunchArgs("Fallout4.exe +connect_lobby " + std::to_string(lobby)));
		assert(F4T::Steam::FireLaunchJoin());
		assert(g_connects.size() == 1 && g_connects.front() == std::to_string(kHostSteamId));
		assert(!F4T::Steam::FireLaunchJoin());
		assert(g_connects.size() == 1);

		g_connects.clear();
		F4T::Steam::QueueLaunchJoin(F4T::Steam::ParseLaunchArgs("Fallout4.exe +connect 39734273"));
		assert(F4T::Steam::FireLaunchJoin());
		assert(g_connects.size() == 1 && g_connects.front() == "39734273");

		g_connects.clear();
		g_failures.clear();
		F4T::Steam::QueueLaunchJoin(F4T::Steam::ParseLaunchArgs("Fallout4.exe +connect Ada"));
		assert(!F4T::Steam::FireLaunchJoin());
		assert(g_failures.size() == 1 && g_failures.front() == "That Steam invite is not valid.");
		F4T::Steam::QueueLaunchJoin(F4T::Steam::ParseLaunchArgs("Fallout4.exe +connect_lobby " + std::to_string(kHostSteamId)));
		const auto joinsAfter = mm->joins.size();
		assert(!F4T::Steam::FireLaunchJoin());
		assert(mm->joins.size() == joinsAfter);
		assert(g_connects.empty());

		Shutdown();
		F4T::Steam::SetJoinConnectHandler(nullptr);
		F4T::Steam::SetJoinFailureHandler(nullptr);
		F4T::Steam::SetLocalBuildTag({});
	}

	void CheckHostingFriendsList()
	{
		F4T::Steam::SetLocalBuildTag(kTestBuild);
		constexpr std::uint64_t kBob = 76561198000000301ull;
		constexpr std::uint64_t kCy = 76561198000000302ull;
		constexpr std::uint64_t kDee = 76561198000000303ull;
		constexpr std::uint64_t kEve = 76561198000000304ull;
		constexpr std::uint64_t kFay = 76561198000000305ull;
		const auto fake = std::make_shared<FakeSteam>();
		const auto friends = std::make_shared<FakeFriends>();
		friends->people = {
			{ kFriend, "Ada", true, "host", { { "co_build", kTestBuild }, { "co_host", "Ada's camp" }, { "co_players", "2" }, { "co_max", "4" } } },
			{ kBob, "Bob", true, "host", { { "co_build", "0.0.9 / Fallout 4 1.10.163" }, { "co_players", "1" }, { "co_max", "8" } } },
			{ kCy, "Cy", true, "", { { "co_build", kTestBuild } } },
			{ kDee, "Dee", false, "host", { { "co_build", kTestBuild } } },
			{ kEve, "Eve", true, "host", {} },
			{ kFay, std::string{ "F\x01" } + std::string(200, 'y'), true, "host", { { "co_host", std::string(300, 'h') }, { "co_players", "5000" }, { "co_max", "x" } } },
			{ 76561198000000306ull, "Gus", true, "host", { { "co_players", "12345" }, { "co_max", "999" } } },
			{ 76561198000000307ull, "Hal", true, "host", { { "co_players", "9999" }, { "co_max", "1000" } } },
		};
		Initialize([fake, friends] {
			auto result = MakeAvailable(fake);
			FillFriends(result, friends);
			return result;
		});
		RecordedLog::Clear();
		const auto listing = F4T::Steam::ListHostingFriends();
		assert(listing.error.empty());
		assert(!listing.matchmakingAvailable);
		assert(listing.hosts.size() == 6);

		const auto& ada = listing.hosts[0];
		assert(ada.steamId == kFriend && ada.friendCode == FriendCode(kFriend));
		assert(ada.name == "Ada" && ada.hostName == "Ada's camp");
		assert(ada.players == 2 && ada.maxPlayers == 4);
		assert(ada.build == kTestBuild && ada.compatible && ada.reason.empty());

		const auto& bob = listing.hosts[1];
		assert(bob.name == "Bob" && bob.hostName == "Bob");
		assert(bob.players == 1 && bob.maxPlayers == 8);
		assert(!bob.compatible);
		assert(bob.reason == "Host is running a different Commonwealth Online build (host 0.0.9 / Fallout 4 1.10.163, you 0.1.0 / Fallout 4 1.11.240)");

		const auto& eve = listing.hosts[2];
		assert(eve.name == "Eve" && eve.hostName == "Eve");
		assert(eve.players == 0 && eve.maxPlayers == 0 && eve.build.empty() && eve.compatible);

		const auto& fay = listing.hosts[3];
		assert(fay.name.size() == 64 && fay.name.front() == 'F' && fay.name.find('\x01') == std::string::npos);
		assert(fay.hostName.size() == 64);
		assert(fay.players == 1000 && fay.maxPlayers == 0);
		assert(listing.hosts[4].name == "Gus" && listing.hosts[4].players == 0 && listing.hosts[4].maxPlayers == 999);
		assert(listing.hosts[5].name == "Hal" && listing.hosts[5].players == 1000 && listing.hosts[5].maxPlayers == 1000);
		assert(RecordedLog::Contains("hosting friends listed count=6 of 8 friends"));
		assert(fake->callbackRuns.load() == 0);
		Shutdown();

		Initialize([] { return MakeUnavailable("no steam"); });
		const auto unavailable = F4T::Steam::ListHostingFriends();
		assert(unavailable.hosts.empty() && unavailable.error == "no steam");
		Shutdown();
		Initialize([] { return MakeAvailable(std::make_shared<FakeSteam>()); });
		const auto noList = F4T::Steam::ListHostingFriends();
		assert(noList.hosts.empty() && !noList.error.empty());
		Shutdown();

		const auto lateFriends = std::make_shared<FakeFriends>();
		lateFriends->people = { { kHostSteamId, "Late", true, "host", { { "co_build", kTestBuild } } } };
		const auto visibleAt = std::chrono::steady_clock::now() + std::chrono::milliseconds{ 120 };
		Initialize([fake, lateFriends, visibleAt] {
			auto result = MakeAvailable(fake);
			FillFriends(result, lateFriends);
			const auto original = result.friends.richPresence;
			result.friends.richPresence = [original, visibleAt](std::uint64_t a_steamId, const char* a_key) {
				if (std::chrono::steady_clock::now() < visibleAt) return std::string{};
				return original(a_steamId, a_key);
			};
			return result;
		});
		const auto late = F4T::Steam::ListHostingFriends();
		assert(late.hosts.size() == 1 && late.hosts.front().name == "Late");
		assert(F4T::Steam::ListHostingFriends(std::chrono::milliseconds{ 0 }).hosts.size() == 1);
		Shutdown();
		F4T::Steam::SetLocalBuildTag({});
	}

	void CheckHostBuildGate()
	{
		F4T::Steam::SetLocalBuildTag(kTestBuild);
		constexpr std::uint64_t kOld = 76561198000000401ull;
		constexpr std::uint64_t kSame = 76561198000000402ull;
		constexpr std::uint64_t kOther = 76561198000000403ull;
		constexpr std::uint64_t kHostile = 76561198000000404ull;
		constexpr std::uint64_t kLate = 76561198000000405ull;
		const auto fake = std::make_shared<FakeSteam>();
		const auto friends = std::make_shared<FakeFriends>();
		friends->people = {
			{ kOld, "Old", true, "host", {} },
			{ kSame, "Same", true, "host", { { "co_build", kTestBuild } } },
			{ kOther, "Other", true, "host", { { "co_build", "0.0.9 / Fallout 4 1.10.163" } } },
			{ kHostile, "Hostile", true, "host", { { "co_build", std::string(500, 'Z') + "\x01\x02" } } },
		};
		const auto visibleAt = std::chrono::steady_clock::now() + std::chrono::milliseconds{ 120 };
		Initialize([fake, friends, visibleAt] {
			auto result = MakeAvailable(fake);
			FillFriends(result, friends);
			const auto original = result.friends.richPresence;
			result.friends.richPresence = [original, visibleAt](std::uint64_t a_steamId, const char* a_key) {
				if (a_steamId == kLate && a_key != nullptr && std::string_view{ a_key } == "co_build") {
					return std::chrono::steady_clock::now() < visibleAt ? std::string{} : std::string{ "0.0.8 / Fallout 4 1.10.163" };
				}
				return original(a_steamId, a_key);
			};
			return result;
		});
		RecordedLog::Clear();

		assert(!F4T::Steam::CheckHostBuild(kSame));
		assert(RecordedLog::Contains("host build matches"));
		const auto mismatch = F4T::Steam::CheckHostBuild(kOther);
		assert(mismatch && *mismatch == "Host is running a different Commonwealth Online build (host 0.0.9 / Fallout 4 1.10.163, you 0.1.0 / Fallout 4 1.11.240)");
		assert(RecordedLog::Contains("host build mismatch"));
		assert(!F4T::Steam::CheckHostBuild(kOld, std::chrono::milliseconds{ 60 }));
		assert(RecordedLog::Contains("no co_build in rich presence, treating as compatible"));
		assert(!F4T::Steam::CheckHostBuild(kHostSteamId + 99, std::chrono::milliseconds{ 0 }));

		const auto hostile = F4T::Steam::CheckHostBuild(kHostile);
		assert(hostile && hostile->size() < 200);
		assert(hostile->find('\x01') == std::string::npos && hostile->find('\x02') == std::string::npos);
		assert(hostile->starts_with("Host is running a different Commonwealth Online build (host ZZZ"));
		assert(hostile->ends_with(", you 0.1.0 / Fallout 4 1.11.240)"));

		const auto late = F4T::Steam::CheckHostBuild(kLate, std::chrono::milliseconds{ 1000 });
		assert(late && late->find("0.0.8") != std::string::npos);
		assert(F4T::Steam::CheckHostBuild(kLate, std::chrono::milliseconds{ 0 }));
		Shutdown();

		Initialize([] { return MakeUnavailable("no steam"); });
		assert(!F4T::Steam::CheckHostBuild(kSame));
		Shutdown();
		F4T::Steam::SetLocalBuildTag({});
	}

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
				"That host only accepts Steam friends. (not a permitted peer)" },
			{ F4T::Transport::kConnectionEndServerFull, "server full",
				"That host is full. (server full)" },
			{ F4T::Transport::kConnectionEndLocalSocketFailed, "local socket failed",
				"The host could not open a relay port for you. (local socket failed)" },
			{ F4T::Transport::kConnectionEndIdentityUnlinked, "identity link failed",
				"The host could not confirm your Steam identity. (identity link failed)" },
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
				.bind = [](std::uint16_t, std::uint64_t, bool) { return true; },
				.unbind = [](std::uint16_t) {}
			};
		});
		assert(StartHost(kServerPort, 4) == TunnelError::None);
		assert(F4T::Steam::HostIdentityLinked());
		StopHost();
		assert(!F4T::Steam::HostIdentityLinked());

		F4T::Steam::SetIdentityBinderFactory({});
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

	void RunChecks()
	{
		CheckUnavailable();
		CheckAvailable();
		CheckFailedLoadRetries();
		CheckResetAfterShutdown();
		CheckRetryIfUnavailable();
		CheckFriendCodeAndPresence();
		CheckPresenceArrivesDuringWait();
		CheckInviteOverlay();
		CheckLobbyHosting();
		CheckLobbyFailureModes();
		CheckLobbyJoin();
		CheckHostingFriendsList();
		CheckHostBuildGate();
		CheckGuestPresence();
		CheckGuestLinkWait();
		CheckGuestRefusalMessages();
		CheckHostIdentityLinkFlag();
	}
}

int main()
{
	F4T::Steam::SetIdentityBinderFactory({});
	std::atomic<bool> finished{ false };
	std::thread checks([&finished] {
		RunChecks();
		finished.store(true);
	});

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 30 };
	while (!finished.load() && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds{ 10 });
	}
	if (!finished.load()) {
		std::fputs("F4TSteamTunnelServiceTests timed out\n", stderr);
		std::_Exit(1);
	}

	checks.join();
	return 0;
}

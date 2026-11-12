#include "F4TSteamTunnelServiceTestSupport.h"

namespace ServiceTest
{
namespace
{
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


}

	void RunLobbyChecks()
	{
		CheckLobbyHosting();
		CheckLobbyFailureModes();
		CheckLobbyJoin();
		CheckHostingFriendsList();
		CheckHostBuildGate();
	}
}

#include "F4TSteamTunnelServiceTestSupport.h"

namespace ServiceTest
{
namespace
{
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


}

	void RunPresenceChecks()
	{
		CheckFriendCodeAndPresence();
		CheckPresenceArrivesDuringWait();
		CheckInviteOverlay();
	}
}

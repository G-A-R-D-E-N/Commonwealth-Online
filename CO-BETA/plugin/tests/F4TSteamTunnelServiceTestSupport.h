#pragma once

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
	inline std::mutex g_mutex;
	inline std::vector<std::string> g_lines;

	inline void Clear()
	{
		std::lock_guard lock{ g_mutex };
		g_lines.clear();
	}

	inline bool Contains(std::string_view a_text)
	{
		std::lock_guard lock{ g_mutex };
		for (const auto& line : g_lines) {
			if (line.find(a_text) != std::string::npos) return true;
		}
		return false;
	}
}

namespace ServiceTest
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
	inline std::string* g_acceptedConnect = nullptr;
	constexpr std::uint64_t kLobbyBase = (1ull << 56) | (8ull << 52) | (0x40000ull << 32);
	constexpr const char* kTestBuild = "0.1.0 / Fallout 4 1.11.240";
	inline std::vector<std::string> g_connects;
	inline std::vector<std::string> g_failures;

	inline void AcceptConnect(const char* a_connect)
	{
		if (g_acceptedConnect != nullptr && a_connect != nullptr) *g_acceptedConnect = a_connect;
	}

	inline void RecordConnect(const char* a_connect)
	{
		g_connects.emplace_back(a_connect ? a_connect : "<null>");
	}

	inline void RecordFailure(const char* a_message)
	{
		g_failures.emplace_back(a_message ? a_message : "<null>");
	}

	inline void ResetJoinRecords()
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

	inline SteamApi MakeSteamApi(const std::shared_ptr<FakeSteam>& a_fake)
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

	inline F4T::Steam::SteamApiLoadResult MakeUnavailable(std::string a_reason)
	{
		F4T::Steam::SteamApiLoadResult result;
		result.available = false;
		result.reason = std::move(a_reason);
		return result;
	}

	inline F4T::Steam::SteamApiLoadResult MakeAvailable(const std::shared_ptr<FakeSteam>& a_fake)
	{
		F4T::Steam::SteamApiLoadResult result;
		result.available = true;
		result.mySteamId = kMySteamId;
		result.isFriend = [](std::uint64_t a_steamId) { return a_steamId == kFriend; };
		result.api = MakeSteamApi(a_fake);
		assert(result.api.IsComplete());
		return result;
	}

	inline void WaitForCallbacks(const std::shared_ptr<FakeSteam>& a_fake)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 5 };
		while (a_fake->callbackRuns.load() == 0 && std::chrono::steady_clock::now() < deadline) {
			std::this_thread::sleep_for(std::chrono::milliseconds{ 5 });
		}
	}

	inline bool WaitForGuestState(F4T::Transport::TunnelState a_expected, std::chrono::milliseconds a_timeout)
	{
		const auto deadline = std::chrono::steady_clock::now() + a_timeout;
		for (;;) {
			if (GuestState() == a_expected) return true;
			if (std::chrono::steady_clock::now() >= deadline) return false;
			std::this_thread::sleep_for(std::chrono::milliseconds{ 5 });
		}
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

	inline void FillMatchmaking(F4T::Steam::SteamApiLoadResult& a_result, const std::shared_ptr<FakeMatchmaking>& a_mm)
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
	inline bool WaitFor(Predicate a_predicate)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 5 };
		while (!a_predicate()) {
			if (std::chrono::steady_clock::now() >= deadline) return false;
			std::this_thread::sleep_for(std::chrono::milliseconds{ 5 });
		}
		return true;
	}

	inline void FillFriends(F4T::Steam::SteamApiLoadResult& a_result, const std::shared_ptr<FakeFriends>& a_friends)
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


	void RunCoreChecks();
	void RunPresenceChecks();
	void RunLobbyChecks();
	void RunGuestChecks();
}

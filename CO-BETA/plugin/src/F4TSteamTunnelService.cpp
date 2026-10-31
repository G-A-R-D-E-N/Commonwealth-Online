#include "F4TSteamTunnelService.h"

#include "F4TClientCompatibility.h"
#include "F4TSteamHostLinkClient.h"
#include "F4TDebugLogging.h"
#include "F4TSteamId.h"
#include "F4TSteamInvite.h"

#include "F4TWinsockLoopbackUdp.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace F4T::Steam
{
	namespace
	{
		constexpr auto kPumpInterval = std::chrono::milliseconds{ 5 };
		constexpr auto kStatsInterval = std::chrono::seconds{ 5 };
		constexpr std::string_view kTunnelLog = "SteamTunnel";
		constexpr std::string_view kSessionLog = "SteamSession";
		constexpr std::string_view kPresenceKey = "commonwealth";
		constexpr std::string_view kPresenceHost = "host";
		constexpr const char* kBuildKey = "co_build";
		constexpr const char* kHostKey = "co_host";
		constexpr const char* kPlayersKey = "co_players";
		constexpr const char* kMaxKey = "co_max";
		constexpr std::size_t kMaximumNameBytes = 64;
		constexpr std::uint32_t kMaximumLobbyMembers = 250;
		constexpr std::uint32_t kMaximumReportedPlayers = 1000;

		std::string Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t");
			if (first == std::string_view::npos) return {};
			const auto last = a_text.find_last_not_of(" \t");
			return std::string{ a_text.substr(first, last - first + 1) };
		}

		std::string CleanText(std::string_view a_text, std::size_t a_maximum = kMaximumNameBytes)
		{
			std::string clean;
			for (const char character : a_text) {
				const auto byte = static_cast<unsigned char>(character);
				if (byte >= 0x20 && byte != 0x7F) clean.push_back(character);
			}
			if (clean.size() > a_maximum) {
				clean.resize(a_maximum);
				while (!clean.empty() && (static_cast<unsigned char>(clean.back()) & 0xC0) == 0x80) clean.pop_back();
				if (!clean.empty() && static_cast<unsigned char>(clean.back()) >= 0xC0) clean.pop_back();
			}
			return clean;
		}

		std::uint32_t ParseCount(std::string_view a_text)
		{
			std::uint32_t value = 0;
			if (a_text.empty() || a_text.size() > 4) return 0;
			const auto parsed = std::from_chars(a_text.data(), a_text.data() + a_text.size(), value);
			if (parsed.ec != std::errc{} || parsed.ptr != a_text.data() + a_text.size()) return 0;
			return std::min(value, kMaximumReportedPlayers);
		}

		std::string BuildMismatchMessage(std::string_view a_host, std::string_view a_local)
		{
			return "Host is running a different Commonwealth Online build (host " + std::string{ a_host } + ", you " + std::string{ a_local } + ")";
		}

		std::mutex& BuildTagMutex()
		{
			static std::mutex mutex;
			return mutex;
		}

		std::string& BuildTagOverride()
		{
			static std::string tag;
			return tag;
		}

		std::string Lower(std::string a_text)
		{
			std::transform(a_text.begin(), a_text.end(), a_text.begin(), [](unsigned char a_char) {
				return static_cast<char>(std::tolower(a_char));
			});
			return a_text;
		}

		bool SameText(std::string_view a_left, std::string_view a_right)
		{
			return a_left.size() == a_right.size() &&
				std::equal(a_left.begin(), a_left.end(), a_right.begin(), [](char a_leftChar, char a_rightChar) {
					return std::tolower(static_cast<unsigned char>(a_leftChar)) == std::tolower(static_cast<unsigned char>(a_rightChar));
				});
		}

		struct Service
		{
			~Service();

			std::mutex mutex;
			std::optional<SteamApiLoadResult> loaded;
			LoaderFn loader;
			std::unique_ptr<Transport::SteamHostTunnel> host;
			std::unique_ptr<Transport::SteamGuestTunnel> guest;
			std::thread pump;
			bool pumpStop{ false };
			bool presenceAdvertised{ false };
			std::vector<std::uint64_t> lobbyPeerIds;
			std::uint64_t hostLobby{};
			bool lobbyPending{ false };
			std::uint64_t lobbyEpoch{ 0 };
			std::string hostName;
			std::uint32_t hostMaxPlayers{};
			bool guestPresenceAdvertised{ false };
			LobbyPeersSink lobbyPeersSink;
			std::mutex lobbyDeliveryMutex;
			std::atomic<std::uint64_t> lobbyGeneration{ 0 };
			bool hostIdentityLinked{ false };
			bool retried{ false };
			IdentityBinderFactory binderFactory{ &SteamHostLink::MakeDefaultIdentityBinder };
			Transport::TunnelLog admissionLog;
		};

		std::mutex& PresenceMutex()
		{
			static std::mutex mutex;
			return mutex;
		}

		Service& Instance()
		{
			static Service service;
			return service;
		}

		[[nodiscard]] bool TunnelRunning(const Service& a_service)
		{
			return (a_service.host && a_service.host->IsRunning()) ||
				(a_service.guest && a_service.guest->IsRunning());
		}

		bool RefreshLobbyLocked(Service& a_service, std::vector<std::uint64_t>& a_steamIds)
		{
			auto steamIds = a_service.host->PeerSteamIds();
			if (steamIds == a_service.lobbyPeerIds) return false;
			a_service.lobbyPeerIds = std::move(steamIds);
			a_steamIds = a_service.lobbyPeerIds;
			return true;
		}

		std::vector<LobbyPeer> ResolveLobbyPeers(const SteamFriendApi& a_friends, const std::vector<std::uint64_t>& a_steamIds)
		{
			std::vector<LobbyPeer> peers;
			peers.reserve(a_steamIds.size());
			for (const auto steamId : a_steamIds) {
				std::string name = a_friends.friendName ? a_friends.friendName(steamId) : std::string{};
				if (name.empty()) name = std::to_string(FriendCode(steamId));
				peers.push_back({ steamId, std::move(name) });
			}
			return peers;
		}

		std::size_t CurrentPlayers()
		{
			Service& service = Instance();
			std::lock_guard lock{ service.mutex };
			return service.host && service.host->IsRunning() ? service.host->PeerSteamIds().size() + 1 : 1;
		}

		void PublishPlayers(const SteamFriendApi& a_friends, const SteamMatchmakingApi& a_matchmaking, std::uint64_t a_lobby)
		{
			const auto players = CurrentPlayers();
			const auto text = std::to_string(players);
			if (a_friends.setRichPresence) {
				std::lock_guard presence{ PresenceMutex() };
				a_friends.setRichPresence(kPlayersKey, text.c_str());
			}
			if (a_lobby != 0 && a_matchmaking.setLobbyData) a_matchmaking.setLobbyData(a_lobby, kPlayersKey, text.c_str());
			F4T::DebugLogging::Log(kTunnelLog, "lobby players refreshed lobby={} players={}", a_lobby, players);
		}

		void PublishLobbyDetails(const SteamMatchmakingApi& a_matchmaking, std::uint64_t a_lobby, std::string_view a_name, std::uint32_t a_maxPlayers)
		{
			if (!a_matchmaking.setLobbyData) return;
			const auto playerCount = CurrentPlayers();
			const auto build = LocalBuildTag();
			const auto name = CleanText(a_name);
			const auto players = std::to_string(playerCount);
			const auto maximum = std::to_string(a_maxPlayers);
			const bool built = a_matchmaking.setLobbyData(a_lobby, kBuildKey, build.c_str());
			const bool named = a_matchmaking.setLobbyData(a_lobby, kHostKey, name.c_str());
			const bool counted = a_matchmaking.setLobbyData(a_lobby, kPlayersKey, players.c_str());
			const bool capped = a_matchmaking.setLobbyData(a_lobby, kMaxKey, maximum.c_str());
			if (a_matchmaking.setJoinable) a_matchmaking.setJoinable(a_lobby, true);
			F4T::DebugLogging::Log(kTunnelLog, "lobby data lobby={} build=\"{}\" host=\"{}\" players={} max={} stored={}",
				a_lobby, F4T::Steam::SanitizeForLog(build), F4T::Steam::SanitizeForLog(name), playerCount, a_maxPlayers, built && named && counted && capped);
		}

		void OnLobbyCreated(std::uint64_t a_epoch, bool a_ok, int a_result, std::uint64_t a_lobby);

		bool SameStats(const Transport::SteamTunnelStats& a_left, const Transport::SteamTunnelStats& a_right)
		{
			return a_left.datagramsToLocal == a_right.datagramsToLocal &&
				a_left.datagramsToSteam == a_right.datagramsToSteam &&
				a_left.datagramsDroppedInvalid == a_right.datagramsDroppedInvalid &&
				a_left.datagramsDroppedBufferFull == a_right.datagramsDroppedBufferFull &&
				a_left.connectionsAccepted == a_right.connectionsAccepted &&
				a_left.connectionsRejected == a_right.connectionsRejected &&
				a_left.connectionsRefusedCapacity == a_right.connectionsRefusedCapacity &&
				a_left.identityBindFailures == a_right.identityBindFailures &&
				a_left.activeConnections == a_right.activeConnections;
		}

		void LogEvents(std::string_view a_end, std::vector<Transport::TunnelEvent> a_events)
		{
			for (const auto& event : a_events) {
				F4T::DebugLogging::Log(kTunnelLog, "{} {}", a_end, Transport::FormatTunnelEvent(event));
			}
		}

		void LogCounters(
			const Transport::SteamTunnelStats& a_host,
			const Transport::SteamTunnelStats& a_guest,
			bool a_hostRunning,
			bool a_guestRunning)
		{
			F4T::DebugLogging::Log(
				kTunnelLog,
				"counters host running={} peers={} accepted={} rejected={} full={} bindFailed={} toLocal={} toSteam={} invalid={} dropped={}",
				a_hostRunning, a_host.activeConnections, a_host.connectionsAccepted, a_host.connectionsRejected,
				a_host.connectionsRefusedCapacity, a_host.identityBindFailures, a_host.datagramsToLocal,
				a_host.datagramsToSteam, a_host.datagramsDroppedInvalid, a_host.datagramsDroppedBufferFull);
			F4T::DebugLogging::Log(
				kTunnelLog,
				"counters guest running={} links={} toLocal={} toSteam={} invalid={} dropped={}",
				a_guestRunning, a_guest.activeConnections, a_guest.datagramsToLocal, a_guest.datagramsToSteam,
				a_guest.datagramsDroppedInvalid, a_guest.datagramsDroppedBufferFull);
		}

		void PumpLoop()
		{
			Service& service = Instance();
			auto nextCounters = std::chrono::steady_clock::now() + kStatsInterval;
			Transport::SteamTunnelStats lastHost{};
			Transport::SteamTunnelStats lastGuest{};
			for (;;) {
				std::vector<Transport::TunnelEvent> hostEvents;
				std::vector<Transport::TunnelEvent> guestEvents;
				Transport::SteamTunnelStats host{};
				Transport::SteamTunnelStats guest{};
				bool hostRunning = false;
				bool guestRunning = false;
				bool stopping = false;
				LobbyPeersSink sink;
				SteamFriendApi friends;
				SteamMatchmakingApi matchmaking;
				std::vector<std::uint64_t> steamIds;
				std::uint64_t generation = 0;
				std::uint64_t lobby = 0;
				bool peersChanged = false;
				{
					std::lock_guard lock{ service.mutex };
					stopping = service.pumpStop;
					generation = service.lobbyGeneration.load(std::memory_order_acquire);
					if (service.host) {
						hostRunning = service.host->IsRunning();
						if (hostRunning) service.host->Pump();
						hostEvents = service.host->DrainEvents();
						if (hostRunning) host = service.host->Stats();
						if (hostRunning && RefreshLobbyLocked(service, steamIds)) {
							peersChanged = true;
							lobby = service.hostLobby;
							sink = service.lobbyPeersSink;
							if (service.loaded) {
								friends = service.loaded->friends;
								matchmaking = service.loaded->matchmaking;
							}
						}
					}
					if (service.guest) {
						guestRunning = service.guest->IsRunning();
						if (guestRunning) service.guest->Pump();
						guestEvents = service.guest->DrainEvents();
						if (guestRunning) guest = service.guest->Stats();
					}
				}
				if (peersChanged) {
					std::lock_guard delivery{ service.lobbyDeliveryMutex };
					if (generation == service.lobbyGeneration.load(std::memory_order_acquire)) {
						PublishPlayers(friends, matchmaking, lobby);
					}
				}
				if (sink) {
					const auto peers = ResolveLobbyPeers(friends, steamIds);
					std::lock_guard delivery{ service.lobbyDeliveryMutex };
					if (generation == service.lobbyGeneration.load(std::memory_order_acquire)) sink(peers);
				}
				LogEvents("host", std::move(hostEvents));
				LogEvents("guest", std::move(guestEvents));
				if (std::chrono::steady_clock::now() >= nextCounters) {
					nextCounters = std::chrono::steady_clock::now() + kStatsInterval;
					if ((hostRunning && !SameStats(host, lastHost)) || (guestRunning && !SameStats(guest, lastGuest))) {
						LogCounters(host, guest, hostRunning, guestRunning);
						lastHost = host;
						lastGuest = guest;
					}
				}
				if (stopping) break;
				std::this_thread::sleep_for(kPumpInterval);
			}
		}

		void StartPumpLocked(Service& a_service)
		{
			if (a_service.pump.joinable()) return;
			a_service.pumpStop = false;
			a_service.pump = std::thread(PumpLoop);
		}

		void JoinStoppedPump(Service& a_service)
		{
			std::thread pump;
			{
				std::lock_guard lock{ a_service.mutex };
				if (!a_service.pumpStop) return;
				pump = std::move(a_service.pump);
			}
			if (pump.joinable()) pump.join();

			std::lock_guard lock{ a_service.mutex };
			if (!a_service.pump.joinable() && TunnelRunning(a_service)) StartPumpLocked(a_service);
		}

		void LogPendingEvents(Service& a_service)
		{
			std::vector<Transport::TunnelEvent> hostEvents;
			std::vector<Transport::TunnelEvent> guestEvents;
			{
				std::lock_guard lock{ a_service.mutex };
				if (a_service.host) hostEvents = a_service.host->DrainEvents();
				if (a_service.guest) guestEvents = a_service.guest->DrainEvents();
			}
			LogEvents("host", std::move(hostEvents));
			LogEvents("guest", std::move(guestEvents));
		}

		void InstallLoadedLocked(Service& a_service, SteamApiLoadResult a_result)
		{
			if (a_result.available) {
				auto guestApi = a_result.api;
				if (a_result.guestConnectionEvents) guestApi.pollConnectionEvents = std::move(a_result.guestConnectionEvents);
				a_service.host = std::make_unique<Transport::SteamHostTunnel>(a_result.api, MakeWinsockLoopbackUdp());
				a_service.guest = std::make_unique<Transport::SteamGuestTunnel>(std::move(guestApi), MakeWinsockLoopbackUdp());
			}
			a_service.loaded = std::move(a_result);
		}

		void StopTunnels(Service& a_service)
		{
			{
				std::lock_guard lock{ a_service.mutex };
				if (a_service.host) a_service.host->Stop();
				if (a_service.guest) a_service.guest->Stop();
				a_service.pumpStop = true;
			}
			JoinStoppedPump(a_service);
		}

		Service::~Service()
		{
			StopTunnels(*this);
		}
	}

	void SetIdentityBinderFactory(IdentityBinderFactory a_factory)
	{
		Service& service = Instance();
		std::lock_guard lock{ service.mutex };
		service.binderFactory = std::move(a_factory);
	}

	void SetAdmissionLog(Transport::TunnelLog a_log)
	{
		Service& service = Instance();
		std::lock_guard lock{ service.mutex };
		service.admissionLog = std::move(a_log);
	}

	void Initialize(LoaderFn a_loader)
	{
		Service& service = Instance();
		std::lock_guard lock{ service.mutex };
		if (service.loaded && service.loaded->available) return;

		service.loader = a_loader;
		InstallLoadedLocked(service, a_loader ? a_loader() : SteamApiLoadResult{});
	}

	bool RetryIfUnavailable()
	{
		Service& service = Instance();
		std::lock_guard lock{ service.mutex };
		if (!service.loaded || service.loaded->available || service.retried || !service.loader) return false;
		service.retried = true;
		InstallLoadedLocked(service, service.loader());
		return true;
	}

	ServiceStatus Status()
	{
		Service& service = Instance();
		std::lock_guard lock{ service.mutex };
		if (!service.loaded) return { false, "Steam support has not been initialised" };
		return { service.loaded->available, service.loaded->reason, service.loaded->matchmakingAvailable };
	}

	bool MatchmakingAvailable()
	{
		Service& service = Instance();
		std::lock_guard lock{ service.mutex };
		return service.loaded && service.loaded->available && service.loaded->matchmakingAvailable;
	}

	std::vector<std::string> MissingOptionalExports()
	{
		Service& service = Instance();
		std::lock_guard lock{ service.mutex };
		return service.loaded ? service.loaded->missingOptional : std::vector<std::string>{};
	}

	std::string LocalBuildTag()
	{
		{
			std::lock_guard lock{ BuildTagMutex() };
			if (!BuildTagOverride().empty()) return BuildTagOverride();
		}
		std::string tag{ ClientCompatibility::kClientVersion };
		const auto target = ClientCompatibility::ResolveCompileTimeTarget();
		const auto* metadata = target ? ClientCompatibility::Find(*target) : nullptr;
		if (metadata != nullptr && !metadata->falloutRuntimeVersion.empty()) tag += " / Fallout 4 " + metadata->falloutRuntimeVersion;
		return tag;
	}

	void SetLocalBuildTag(std::string a_tag)
	{
		std::lock_guard lock{ BuildTagMutex() };
		BuildTagOverride() = std::move(a_tag);
	}

	std::vector<std::string> InterfaceProbe()
	{
		Service& service = Instance();
		std::lock_guard lock{ service.mutex };
		return service.loaded ? service.loaded->interfaceProbe : std::vector<std::string>{};
	}

	std::string ModuleVersion()
	{
		Service& service = Instance();
		std::lock_guard lock{ service.mutex };
		return service.loaded ? service.loaded->moduleVersion : std::string{};
	}

	std::uint64_t MySteamId()
	{
		Service& service = Instance();
		std::lock_guard lock{ service.mutex };
		return service.loaded ? service.loaded->mySteamId : 0;
	}

	std::optional<std::string> CreateAuthTicketHex()
	{
		std::function<std::vector<std::uint8_t>()> create;
		{
			Service& service = Instance();
			std::lock_guard lock{ service.mutex };
			if (!service.loaded || !service.loaded->createAuthTicket) return std::nullopt;
			create = service.loaded->createAuthTicket;
		}
		const auto ticket = create();
		if (ticket.empty()) return std::nullopt;
		return SteamId::TicketHex(ticket.data(), ticket.size());
	}

	std::uint64_t FriendCode(std::uint64_t a_steamId)
	{
		return a_steamId & 0xFFFFFFFFull;
	}

	HostLookup FindHost(std::string_view a_target)
	{
		SteamFriendApi friends;
		{
			Service& service = Instance();
			std::lock_guard lock{ service.mutex };
			if (!service.loaded) return { 0, {}, "Steam support has not been initialised" };
			if (!service.loaded->available) {
				return { 0, {}, service.loaded->reason.empty() ? "Steam is not available in this game" : service.loaded->reason };
			}
			friends = service.loaded->friends;
		}

		const std::string target = Trim(a_target);
		const bool automatic = target.empty() || SameText(target, "auto");
		const bool digits = !target.empty() && std::all_of(target.begin(), target.end(), [](unsigned char a_char) {
			return std::isdigit(a_char) != 0;
		});
		if (digits) {
			std::uint64_t value = 0;
			const auto parsed = std::from_chars(target.data(), target.data() + target.size(), value);
			if (parsed.ec != std::errc{} || parsed.ptr != target.data() + target.size() || value == 0 || target.front() == '0') {
				return { 0, {}, "Friend Code should be a number from the host's Steam profile" };
			}
			const auto steamId = value < 0x100000000ull ? kSteamId64Base + value : value;
			std::string name = friends.friendName ? friends.friendName(steamId) : std::string{};
			if (name.empty()) name = std::to_string(steamId);
			return { steamId, std::move(name), {} };
		}

		if (!friends.friendCount || !friends.friendByIndex) {
			return { 0, {}, "Steam did not provide the friend list. Enter the host's Friend Code." };
		}

		struct Candidate
		{
			std::uint64_t steamId{};
			std::string name;
			bool hosting{};
		};
		const int count = friends.friendCount();
		const std::string needle = Lower(target);
		auto scan = [&](bool a_request) {
			std::vector<Candidate> found;
			for (int index = 0; index < count; ++index) {
				const auto steamId = friends.friendByIndex(index);
				if (steamId == 0) continue;
				if (friends.playingFallout && !friends.playingFallout(steamId)) continue;
				if (a_request && friends.requestRichPresence) friends.requestRichPresence(steamId);
				std::string name = friends.friendName ? friends.friendName(steamId) : std::string{};
				if (name.empty()) name = std::to_string(steamId);
				const bool hosting = friends.richPresence && friends.richPresence(steamId, kPresenceKey.data()) == kPresenceHost;
				if (automatic) {
					if (hosting) found.push_back({ steamId, std::move(name), true });
					continue;
				}
				if (Lower(name).find(needle) != std::string::npos) found.push_back({ steamId, std::move(name), hosting });
			}
			return found;
		};
		auto matches = scan(true);
		const bool waitingForPresence = static_cast<bool>(friends.requestRichPresence) &&
			((automatic && matches.empty()) || (!automatic && matches.size() > 1 &&
				std::none_of(matches.begin(), matches.end(), [](const Candidate& a_candidate) { return a_candidate.hosting; })));
		if (waitingForPresence) {
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{ 400 };
			do {
				std::this_thread::sleep_for(std::chrono::milliseconds{ 20 });
				matches = scan(false);
				const bool foundHost = std::any_of(matches.begin(), matches.end(), [](const Candidate& a_candidate) { return a_candidate.hosting; });
				if (automatic ? !matches.empty() : foundHost) break;
			} while (std::chrono::steady_clock::now() < deadline);
		}

		if (automatic) {
			if (matches.empty()) return { 0, {}, "No Steam friend is hosting right now. Start the host first, or enter their Friend Code." };
			return { matches.front().steamId, matches.front().name, {} };
		}
		const auto hosting = std::find_if(matches.begin(), matches.end(), [](const Candidate& a_candidate) { return a_candidate.hosting; });
		if (hosting != matches.end()) return { hosting->steamId, hosting->name, {} };
		if (matches.empty()) return { 0, {}, "No Steam friend matching that name is playing Fallout 4." };
		if (matches.size() > 1) return { 0, {}, "More than one Steam friend matches that name. Enter the Friend Code." };
		return { matches.front().steamId, matches.front().name, {} };
	}

	HostingFriends ListHostingFriends(std::chrono::milliseconds a_wait)
	{
		HostingFriends listing;
		SteamFriendApi friends;
		{
			Service& service = Instance();
			std::lock_guard lock{ service.mutex };
			if (!service.loaded) {
				listing.error = "Steam support has not been initialised";
				return listing;
			}
			if (!service.loaded->available) {
				listing.error = service.loaded->reason.empty() ? "Steam is not available in this game" : service.loaded->reason;
				return listing;
			}
			friends = service.loaded->friends;
			listing.matchmakingAvailable = service.loaded->matchmakingAvailable;
		}
		if (!friends.friendCount || !friends.friendByIndex || !friends.richPresence) {
			listing.error = "Steam did not provide the friend list. Enter the host's Friend Code.";
			return listing;
		}

		const std::string local = LocalBuildTag();
		const int count = friends.friendCount();
		auto scan = [&](bool a_request) {
			std::vector<HostingFriend> found;
			for (int index = 0; index < count; ++index) {
				const auto steamId = friends.friendByIndex(index);
				if (steamId == 0) continue;
				if (friends.playingFallout && !friends.playingFallout(steamId)) continue;
				if (a_request && friends.requestRichPresence) friends.requestRichPresence(steamId);
				if (friends.richPresence(steamId, kPresenceKey.data()) != kPresenceHost) continue;
				HostingFriend host;
				host.steamId = steamId;
				host.friendCode = FriendCode(steamId);
				host.name = CleanText(friends.friendName ? friends.friendName(steamId) : std::string{});
				if (host.name.empty()) host.name = std::to_string(host.friendCode);
				host.hostName = CleanText(friends.richPresence(steamId, kHostKey));
				if (host.hostName.empty()) host.hostName = host.name;
				host.players = ParseCount(friends.richPresence(steamId, kPlayersKey));
				host.maxPlayers = ParseCount(friends.richPresence(steamId, kMaxKey));
				host.build = CleanText(friends.richPresence(steamId, kBuildKey));
				if (!host.build.empty() && host.build != local) {
					host.compatible = false;
					host.reason = BuildMismatchMessage(host.build, local);
				}
				found.push_back(std::move(host));
			}
			return found;
		};

		auto hosts = scan(true);
		if (friends.requestRichPresence) {
			const auto start = std::chrono::steady_clock::now();
			const auto deadline = start + a_wait;
			std::size_t previous = hosts.size();
			while (std::chrono::steady_clock::now() < deadline) {
				std::this_thread::sleep_for(std::chrono::milliseconds{ 20 });
				hosts = scan(false);
				const bool settled = !hosts.empty() && hosts.size() == previous &&
					std::chrono::steady_clock::now() - start >= std::chrono::milliseconds{ 80 };
				previous = hosts.size();
				if (settled) break;
			}
		}
		std::sort(hosts.begin(), hosts.end(), [](const HostingFriend& a_left, const HostingFriend& a_right) {
			const auto left = Lower(a_left.name);
			const auto right = Lower(a_right.name);
			return left != right ? left < right : a_left.steamId < a_right.steamId;
		});
		F4T::DebugLogging::Log(kSessionLog, "hosting friends listed count={} of {} friends matchmaking={}", hosts.size(), count, listing.matchmakingAvailable);
		listing.hosts = std::move(hosts);
		return listing;
	}

	std::optional<std::string> CheckHostBuild(std::uint64_t a_hostSteamId, std::chrono::milliseconds a_wait)
	{
		SteamFriendApi friends;
		{
			Service& service = Instance();
			std::lock_guard lock{ service.mutex };
			if (!service.loaded || !service.loaded->available) return std::nullopt;
			friends = service.loaded->friends;
		}
		if (!friends.richPresence) {
			F4T::DebugLogging::Log(kSessionLog, "host build check skipped host={}: Steam did not provide rich presence", a_hostSteamId);
			return std::nullopt;
		}
		if (friends.requestRichPresence) friends.requestRichPresence(a_hostSteamId);
		auto build = CleanText(friends.richPresence(a_hostSteamId, kBuildKey));
		if (build.empty() && friends.requestRichPresence) {
			const auto deadline = std::chrono::steady_clock::now() + a_wait;
			while (build.empty() && std::chrono::steady_clock::now() < deadline) {
				std::this_thread::sleep_for(std::chrono::milliseconds{ 20 });
				build = CleanText(friends.richPresence(a_hostSteamId, kBuildKey));
			}
		}
		if (build.empty()) {
			F4T::DebugLogging::Log(kSessionLog, "host build unknown host={}: no co_build in rich presence, treating as compatible", a_hostSteamId);
			return std::nullopt;
		}
		const auto local = LocalBuildTag();
		if (build == local) {
			F4T::DebugLogging::Log(kSessionLog, "host build matches host={} build=\"{}\"", a_hostSteamId, F4T::Steam::SanitizeForLog(build));
			return std::nullopt;
		}
		auto message = BuildMismatchMessage(F4T::Steam::SanitizeForLog(build, kMaximumNameBytes), local);
		F4T::DebugLogging::Log(kSessionLog, "host build mismatch host={}: {}", a_hostSteamId, message);
		return message;
	}

	void AnnounceHosting(std::string_view a_name)
	{
		SteamFriendApi friends;
		SteamMatchmakingApi matchmaking;
		bool lobbies = false;
		std::uint32_t maxPlayers = 0;
		std::size_t players = 1;
		std::uint64_t existingLobby = 0;
		{
			Service& service = Instance();
			std::lock_guard lock{ service.mutex };
			if (!service.loaded || !service.loaded->available) return;
			friends = service.loaded->friends;
			matchmaking = service.loaded->matchmaking;
			lobbies = service.loaded->matchmakingAvailable && static_cast<bool>(service.loaded->matchmaking.createLobby);
			service.hostName = std::string{ a_name };
			existingLobby = service.hostLobby;
			maxPlayers = service.hostMaxPlayers;
			if (service.host && service.host->IsRunning()) players = service.host->PeerSteamIds().size() + 1;
		}
		if (friends.setRichPresence) {
			{
				std::lock_guard presence{ PresenceMutex() };
				friends.setRichPresence(std::string{ kPresenceKey }.c_str(), std::string{ kPresenceHost }.c_str());
				const std::string status = a_name.empty() ? std::string{ "Hosting Commonwealth Online" } : "Hosting Commonwealth Online: " + std::string{ a_name };
				friends.setRichPresence("status", status.c_str());
				const auto build = LocalBuildTag();
				const auto name = CleanText(a_name);
				const auto playersText = std::to_string(players);
				const auto maximumText = std::to_string(maxPlayers);
				friends.setRichPresence(kBuildKey, build.c_str());
				friends.setRichPresence(kHostKey, name.c_str());
				friends.setRichPresence(kPlayersKey, playersText.c_str());
				if (maxPlayers != 0) friends.setRichPresence(kMaxKey, maximumText.c_str());
				F4T::DebugLogging::Log(kSessionLog, "rich presence set commonwealth=host status=\"{}\" build=\"{}\" players={} max={}",
					F4T::Steam::SanitizeForLog(status, 96), F4T::Steam::SanitizeForLog(build), players, maxPlayers);
			}
			std::lock_guard lock{ Instance().mutex };
			Instance().presenceAdvertised = true;
		}

		if (existingLobby != 0 && matchmaking.setLobbyData) {
			const auto name = CleanText(a_name);
			matchmaking.setLobbyData(existingLobby, kHostKey, name.c_str());
			F4T::DebugLogging::Log(kTunnelLog, "lobby host name refreshed lobby={} host=\"{}\"", existingLobby, F4T::Steam::SanitizeForLog(name));
		}

		Service& service = Instance();
		std::uint64_t epoch = 0;
		{
			std::lock_guard lock{ service.mutex };
			if (!lobbies) {
				F4T::DebugLogging::Log(kTunnelLog, "lobby not created: matchmaking is unavailable, hosting by rich presence only");
				return;
			}
			if (!service.host || !service.host->IsRunning() || service.hostLobby != 0 || service.lobbyPending) return;
			service.lobbyPending = true;
			epoch = ++service.lobbyEpoch;
		}
		const auto capacity = std::clamp<std::uint32_t>(maxPlayers, 2, kMaximumLobbyMembers);
		F4T::DebugLogging::Log(kTunnelLog, "lobby create requested type=friends-only members={}", capacity);
		matchmaking.createLobby(static_cast<int>(capacity), [epoch](bool a_ok, int a_result, std::uint64_t a_lobby) {
			OnLobbyCreated(epoch, a_ok, a_result, a_lobby);
		});
	}

	std::uint64_t HostLobby()
	{
		Service& service = Instance();
		std::lock_guard lock{ service.mutex };
		return service.hostLobby;
	}

	namespace
	{
		void OnLobbyCreated(std::uint64_t a_epoch, bool a_ok, int a_result, std::uint64_t a_lobby)
		{
			Service& service = Instance();
			SteamMatchmakingApi matchmaking;
			std::string name;
			std::uint32_t maxPlayers = 0;
			bool stale = false;
			{
				std::lock_guard lock{ service.mutex };
				stale = a_epoch != service.lobbyEpoch || !service.host || !service.host->IsRunning();
				if (a_epoch == service.lobbyEpoch) service.lobbyPending = false;
				if (service.loaded) matchmaking = service.loaded->matchmaking;
				if (!stale && a_ok) service.hostLobby = a_lobby;
				name = service.hostName;
				maxPlayers = service.hostMaxPlayers;
			}
			if (!a_ok) {
				F4T::DebugLogging::Log(kTunnelLog, "lobby create failed result={} lobby={}", a_result, a_lobby);
				return;
			}
			if (stale) {
				F4T::DebugLogging::Log(kTunnelLog, "lobby {} created after hosting stopped, leaving it", a_lobby);
				if (matchmaking.leaveLobby) matchmaking.leaveLobby(a_lobby);
				return;
			}
			F4T::DebugLogging::Log(kTunnelLog, "lobby created lobby={}", a_lobby);
			std::lock_guard delivery{ service.lobbyDeliveryMutex };
			PublishLobbyDetails(matchmaking, a_lobby, name, maxPlayers);
		}

		void OnLobbyEntered(bool a_ok, std::uint32_t a_response, std::uint64_t a_lobby)
		{
			SteamMatchmakingApi matchmaking;
			std::uint64_t mine = 0;
			{
				Service& service = Instance();
				std::lock_guard lock{ service.mutex };
				if (service.loaded) {
					matchmaking = service.loaded->matchmaking;
					mine = service.loaded->mySteamId;
				}
			}
			if (!a_ok || !matchmaking.getLobbyData) {
				F4T::DebugLogging::Log(kSessionLog, "lobby join failed lobby={} response={}", a_lobby, a_response);
				ReportJoinFailure("Could not join that Steam lobby (Steam response " + std::to_string(a_response) + "). The host may have closed it, or it is full.");
				return;
			}
			const auto build = matchmaking.getLobbyData(a_lobby, kBuildKey);
			const auto hostName = matchmaking.getLobbyData(a_lobby, kHostKey);
			const auto owner = matchmaking.lobbyOwner ? matchmaking.lobbyOwner(a_lobby) : 0;
			if (matchmaking.leaveLobby) matchmaking.leaveLobby(a_lobby);
			F4T::DebugLogging::Log(kSessionLog, "lobby entered lobby={} build=\"{}\" host=\"{}\" owner={}",
				a_lobby, F4T::Steam::SanitizeForLog(build), F4T::Steam::SanitizeForLog(hostName), owner);
			if (build.empty()) {
				ReportJoinFailure("That Steam lobby is not a Commonwealth Online game.");
				return;
			}
			const auto local = LocalBuildTag();
			if (build != local) {
				ReportJoinFailure(BuildMismatchMessage(F4T::Steam::SanitizeForLog(build, kMaximumNameBytes), local));
				return;
			}
			if (owner == 0) {
				ReportJoinFailure("Steam did not report who is hosting that lobby.");
				return;
			}
			if (owner == mine) {
				F4T::DebugLogging::Log(kSessionLog, "lobby {} is owned by this account, join ignored", a_lobby);
				return;
			}
			(void)HandOffConnect(std::to_string(owner), "lobby");
		}
	}

	void JoinLobby(std::uint64_t a_lobby)
	{
		SteamMatchmakingApi matchmaking;
		std::string reason;
		{
			Service& service = Instance();
			std::lock_guard lock{ service.mutex };
			if (!service.loaded) {
				reason = "Steam support has not been initialised";
			} else if (!service.loaded->available) {
				reason = service.loaded->reason.empty() ? "Steam is not available in this game" : service.loaded->reason;
			} else if (!service.loaded->matchmakingAvailable) {
				reason = "Steam lobby invites are not available on this game version. Ask the host for their Friend Code.";
			} else {
				matchmaking = service.loaded->matchmaking;
			}
		}
		if (reason.empty() && !IsLobbySteamId(a_lobby)) reason = "That Steam lobby invite is not valid.";
		if (!reason.empty()) {
			ReportJoinFailure(reason);
			return;
		}
		F4T::DebugLogging::Log(kSessionLog, "lobby join requested lobby={}", a_lobby);
		matchmaking.joinLobby(a_lobby, [](bool a_ok, std::uint32_t a_response, std::uint64_t a_entered) {
			OnLobbyEntered(a_ok, a_response, a_entered);
		});
	}

	void SetGuestPresence(std::string_view a_hostName, std::uint64_t a_hostFriendCode)
	{
		std::function<bool(const char*, const char*)> setPresence;
		{
			Service& service = Instance();
			std::lock_guard lock{ service.mutex };
			if (!service.loaded || !service.loaded->available) return;
			setPresence = service.loaded->friends.setRichPresence;
		}
		if (!setPresence) return;
		const auto name = CleanText(a_hostName);
		const std::string status = name.empty() ? std::string{ "Playing Commonwealth Online" } : "Playing Commonwealth Online - " + name;
		{
			std::lock_guard presence{ PresenceMutex() };
			setPresence("status", status.c_str());
			if (a_hostFriendCode != 0) setPresence("connect", std::to_string(a_hostFriendCode).c_str());
		}
		F4T::DebugLogging::Log(kSessionLog, "rich presence set status=\"{}\"", F4T::Steam::SanitizeForLog(status, 96));
		std::lock_guard lock{ Instance().mutex };
		Instance().guestPresenceAdvertised = true;
	}

	void ClearGuestPresence()
	{
		std::function<void()> clearPresence;
		{
			Service& service = Instance();
			std::lock_guard lock{ service.mutex };
			if (!service.guestPresenceAdvertised) return;
			service.guestPresenceAdvertised = false;
			if (service.presenceAdvertised || !service.loaded) return;
			clearPresence = service.loaded->friends.clearRichPresence;
		}
		if (!clearPresence) return;
		{
			std::lock_guard presence{ PresenceMutex() };
			clearPresence();
		}
		F4T::DebugLogging::Log(kSessionLog, "rich presence cleared for guest");
	}

	void SetHostMemberLimit(std::uint32_t a_maxPlayers)
	{
		Service& service = Instance();
		std::unique_lock lock{ service.mutex };
		if (!service.host || !service.host->IsRunning()) return;
		const auto activeConnections = static_cast<std::size_t>(service.host->Stats().activeConnections);
		const auto requestedConnections = a_maxPlayers > 0 ? static_cast<std::size_t>(a_maxPlayers) - 1 : 0;
		service.host->SetMaximumConnections(Transport::ClampHostMaximumConnections(requestedConnections, activeConnections));
		service.hostMaxPlayers = a_maxPlayers;
		const bool advertised = service.presenceAdvertised;
		const auto lobby = service.hostLobby;
		const auto matchmaking = service.loaded ? service.loaded->matchmaking : SteamMatchmakingApi{};
		const auto friends = service.loaded ? service.loaded->friends : SteamFriendApi{};
		lock.unlock();
		const auto text = std::to_string(a_maxPlayers);
		if (friends.setRichPresence && advertised) {
			std::lock_guard presence{ PresenceMutex() };
			friends.setRichPresence(kMaxKey, text.c_str());
		}
		if (lobby != 0 && matchmaking.setLobbyData) {
			matchmaking.setLobbyData(lobby, kMaxKey, text.c_str());
			if (matchmaking.setMemberLimit) matchmaking.setMemberLimit(lobby, static_cast<int>(std::clamp<std::uint32_t>(a_maxPlayers, 2, kMaximumLobbyMembers)));
		}
	}

	void SetJoinConnectString(std::string_view a_connect)
	{
		std::function<bool(const char*, const char*)> setPresence;
		{
			Service& service = Instance();
			std::lock_guard lock{ service.mutex };
			if (!service.loaded || !service.loaded->available) return;
			setPresence = service.loaded->friends.setRichPresence;
		}
		if (!setPresence) return;
		const std::string connect{ a_connect };
		std::lock_guard presence{ PresenceMutex() };
		setPresence("connect", connect.c_str());
	}

	void SetLobbyPeersSink(LobbyPeersSink a_sink)
	{
		Service& service = Instance();
		std::lock_guard lock{ service.mutex };
		service.lobbyPeersSink = std::move(a_sink);
	}

	Transport::TunnelError StartHost(std::uint16_t a_serverPort, std::uint32_t a_maxPlayers)
	{
		Service& service = Instance();
		IdentityBinderFactory binderFactory;
		std::uint64_t hostSteamId = 0;
		{
			std::lock_guard lock{ service.mutex };
			service.hostIdentityLinked = false;
			if (!service.loaded || !service.loaded->available || !service.host) return Transport::TunnelError::SteamUnavailable;
			if (service.host->IsRunning()) return Transport::TunnelError::InvalidArgument;
			binderFactory = service.binderFactory;
			hostSteamId = service.loaded->mySteamId;
		}
		if (a_maxPlayers < 2 || a_serverPort == 0) return Transport::TunnelError::InvalidArgument;

		auto binder = binderFactory ? binderFactory(a_serverPort, hostSteamId) : Transport::SteamIdentityBinder{};

		std::lock_guard lock{ service.mutex };
		if (!service.loaded || !service.loaded->available || !service.host) return Transport::TunnelError::SteamUnavailable;
		if (service.host->IsRunning()) return Transport::TunnelError::InvalidArgument;
		service.hostIdentityLinked = !binder.IsEmpty();
		service.hostMaxPlayers = a_maxPlayers;
		service.host->SetLog(service.admissionLog);
		if (service.admissionLog) {
			const auto policy = binder.hostPolicy.value_or(SteamAdmission::HostPolicy{});
			service.admissionLog(std::string{ "host policy " } + (binder.hostPolicy ? "from server" : "unavailable, using defaults") +
				(policy.friendsOnly ? ": friends only" : ": friends only off") +
				(policy.serverPasswordSet ? ", join password set" : ", no join password") +
				(binder.IsComplete() ? ", identity link up" : ", no identity link"));
		}
		const auto error = service.host->Start(
			a_serverPort,
			Transport::MakeFriendsOnlyPolicy(service.loaded->isFriend, {}),
			a_maxPlayers - 1,
			std::move(binder));
		if (error != Transport::TunnelError::None) return error;

		StartPumpLocked(service);
		return Transport::TunnelError::None;
	}

	void StopHost()
	{
		ClearInviteCode();
		Service& service = Instance();
		std::function<void()> clearPresence;
		LobbyPeersSink lobbySink;
		bool clearAdvertisement = false;
		std::uint64_t lobby = 0;
		SteamMatchmakingApi matchmaking;
		{
			std::lock_guard delivery{ service.lobbyDeliveryMutex };
			service.lobbyGeneration.fetch_add(1, std::memory_order_acq_rel);
		}
		{
			std::lock_guard lock{ service.mutex };
			const bool hostRunning = service.host && service.host->IsRunning();
			lobby = std::exchange(service.hostLobby, 0);
			service.lobbyPending = false;
			++service.lobbyEpoch;
			service.hostName.clear();
			if (lobby != 0 && service.loaded) matchmaking = service.loaded->matchmaking;
			clearAdvertisement = hostRunning || service.presenceAdvertised;
			if (clearAdvertisement && service.loaded) clearPresence = service.loaded->friends.clearRichPresence;
			service.presenceAdvertised = false;
			service.lobbyPeerIds.clear();
			lobbySink = service.lobbyPeersSink;
			service.hostIdentityLinked = false;
			if (service.host) service.host->Stop();
			if (!TunnelRunning(service)) service.pumpStop = true;
		}
		if (clearPresence) {
			std::lock_guard presence{ PresenceMutex() };
			clearPresence();
		}
		{
			std::lock_guard delivery{ service.lobbyDeliveryMutex };
			service.lobbyGeneration.fetch_add(1, std::memory_order_acq_rel);
			if (lobbySink) lobbySink({});
		}
		if (lobby != 0 && matchmaking.leaveLobby) {
			matchmaking.leaveLobby(lobby);
			F4T::DebugLogging::Log(kTunnelLog, "lobby left lobby={}", lobby);
		}
		if (clearAdvertisement) F4T::DebugLogging::Log(kSessionLog, "rich presence cleared for host");
		JoinStoppedPump(service);
		LogPendingEvents(service);
	}

	bool HostRunning()
	{
		Service& service = Instance();
		std::lock_guard lock{ service.mutex };
		return service.host && service.host->IsRunning();
	}

	bool HostIdentityLinked()
	{
		Service& service = Instance();
		std::lock_guard lock{ service.mutex };
		return service.hostIdentityLinked;
	}

	Transport::GuestConnectResult ConnectGuest(std::uint64_t a_hostSteamId)
	{
		Service& service = Instance();
		Transport::GuestConnectResult result{ Transport::TunnelError::SteamUnavailable, 0 };
		{
			std::lock_guard lock{ service.mutex };
			if (service.guest) {
				service.guest->Stop();
				result = service.guest->Connect(a_hostSteamId);
				if (result.ok()) StartPumpLocked(service);
				else if (!TunnelRunning(service)) service.pumpStop = true;
			}
		}
		JoinStoppedPump(service);
		LogPendingEvents(service);
		if (!result.ok()) {
			F4T::DebugLogging::Log(kTunnelLog, "guest connect refused host={} error={}", a_hostSteamId,
				Transport::DescribeTunnelError(result.error));
		} else {
			F4T::DebugLogging::Log(kTunnelLog, "guest connecting host={} loopbackPort={}", a_hostSteamId, result.loopbackPort);
		}
		return result;
	}

	void StopGuest()
	{
		Service& service = Instance();
		{
			std::lock_guard lock{ service.mutex };
			if (service.guest) service.guest->Stop();
			if (!TunnelRunning(service)) service.pumpStop = true;
		}
		JoinStoppedPump(service);
		LogPendingEvents(service);
	}

	bool GuestRunning()
	{
		Service& service = Instance();
		std::lock_guard lock{ service.mutex };
		return service.guest && service.guest->IsRunning();
	}

	std::string GuestRefusalMessage()
	{
		Service& service = Instance();
		std::lock_guard lock{ service.mutex };
		if (!service.guest || service.guest->IsRunning()) return {};
		return std::string{ SteamAdmission::GuestMessage(service.guest->EndReason()) };
	}

	Transport::TunnelState GuestState()
	{
		Service& service = Instance();
		std::lock_guard lock{ service.mutex };
		return service.guest ? service.guest->State() : Transport::TunnelState::Closed;
	}

	std::string GuestFailureReason()
	{
		Service& service = Instance();
		std::lock_guard lock{ service.mutex };
		return service.guest ? service.guest->FailureReason() : std::string{};
	}

	namespace
	{
		std::string GuestFailureText(const Transport::SteamGuestTunnel& a_guest)
		{
			const auto code = a_guest.EndReason();
			const auto debug = a_guest.FailureReason();
			if (code == 0) {
				return debug.empty() ? std::string{ Transport::DescribeConnectionEnd(code) } : debug;
			}
			std::string text{ Transport::DescribeConnectionEnd(code) };
			if (!debug.empty() && debug != text) {
				text += " (";
				text += debug;
				text += ')';
			}
			return text;
		}
	}

	bool WaitForGuestConnected(std::chrono::milliseconds a_timeout, std::string& a_failure)
	{
		a_failure.clear();
		const auto deadline = std::chrono::steady_clock::now() + a_timeout;
		for (;;) {
			{
				Service& service = Instance();
				std::lock_guard lock{ service.mutex };
				if (!service.guest || !service.guest->IsRunning()) {
					a_failure = service.guest ? GuestFailureText(*service.guest) : std::string{ "The Steam link is not running." };
					return false;
				}
				if (service.guest->State() == Transport::TunnelState::Connected) {
					return true;
				}
			}
			if (std::chrono::steady_clock::now() >= deadline) {
				const auto millis = static_cast<long long>(a_timeout.count());
				const auto waited = millis >= 1000 ?
					std::to_string(millis / 1000) + " seconds" :
					std::to_string(millis) + " milliseconds";
				a_failure = "The Steam link was " + std::string{ Transport::DescribeTunnelState(GuestState()) } +
				           " after " + waited + ".";
				return false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds{ 10 });
		}
	}

	void Shutdown()
	{
		ClearInviteCode();
		Service& service = Instance();
		StopTunnels(service);
		UnbindInvite();

		std::uint64_t lobby = 0;
		SteamMatchmakingApi matchmaking;
		{
			std::lock_guard lock{ service.mutex };
			lobby = std::exchange(service.hostLobby, 0);
			service.lobbyPending = false;
			++service.lobbyEpoch;
			if (lobby != 0 && service.loaded) matchmaking = service.loaded->matchmaking;
		}
		if (lobby != 0 && matchmaking.leaveLobby) matchmaking.leaveLobby(lobby);

		std::lock_guard lock{ service.mutex };
		service.guestPresenceAdvertised = false;
		service.host.reset();
		service.guest.reset();
		service.loaded.reset();
		service.loader = {};
		service.retried = false;
	}
}

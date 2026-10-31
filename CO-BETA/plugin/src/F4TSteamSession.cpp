#include "pch.h"

#include "F4TServerLive.h"
#include "F4TSteamSession.h"

#include "F4TLocalServerLauncher.h"
#include "F4TConnectionPolicy.h"
#include "F4TNetworking.h"
#include "F4TSessionWorldState.h"
#include "F4TSteamInvite.h"
#include "F4TSteamJoinGate.h"
#include "F4TSteamTunnel.h"
#include "F4TSteamTunnelService.h"
#include "F4TUIClientJoin.h"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace F4T::SteamSession
{
	namespace
	{
		constexpr std::uint16_t kLocalServerPort = 7777;
		constexpr std::size_t kMaxFriendHostRows = 50;
		constexpr std::size_t kMaxFriendHostsBytes = 28 * 1024;
		constexpr std::chrono::milliseconds kLaunchJoinPoll{ 250 };
		constexpr std::chrono::seconds kLaunchJoinGiveUp{ 120 };
		constexpr std::string_view kListHostsToken{ "\x01steamListHosts" };

		COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI7* g_ui = nullptr;
		SteamJoinGate::Gate g_gate;
		std::mutex g_hostJoinMutex;
		std::string g_hostJoinName;
		std::string g_hostJoinPassword;
		std::atomic_bool g_listHostsRunning{ false };
		std::atomic_bool g_launchJoinWatching{ false };
		std::atomic_bool g_launchJoinFired{ false };
		std::atomic_bool g_hasLaunchJoin{ false };

		bool EpochCurrent(std::uint64_t a_epoch)
		{
			return g_gate.IsCurrent(a_epoch);
		}

		bool FinishOperation(SteamJoinGate::Operation a_operation, std::uint64_t a_epoch)
		{
			return g_gate.Finish(a_operation, a_epoch);
		}

		void Push(const nlohmann::json& a_event)
		{
			if (!g_ui || !g_ui->PushSteamEvent) {
				return;
			}
			const auto text = a_event.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
			g_ui->PushSteamEvent(text.c_str());
		}

		void PushFailed(const char* a_type, std::string_view a_reason)
		{
			Push(nlohmann::json{ { "type", a_type }, { "reason", a_reason } });
		}

		void PushStatus()
		{
			const auto status = F4T::Steam::Status();
			Push(nlohmann::json{
				{ "type", "steamStatus" },
				{ "available", status.available },
				{ "reason", status.reason },
				{ "matchmaking", status.matchmaking } });
		}

		void OnJoinRefused(const char* a_message)
		{
			PushFailed("steamJoinFailed", a_message && a_message[0] ? a_message : "Could not join that friend.");
		}

		void PushFriendHosts(const F4T::Steam::HostingFriends& a_friends)
		{
			nlohmann::json hosts = nlohmann::json::array();
			for (const auto& host : a_friends.hosts) {
				if (hosts.size() >= kMaxFriendHostRows) {
					break;
				}
				hosts.push_back(nlohmann::json{
					{ "friendCode", std::to_string(host.friendCode) },
					{ "name", host.name },
					{ "host", host.hostName },
					{ "players", host.players },
					{ "maxPlayers", host.maxPlayers },
					{ "build", host.build },
					{ "compatible", host.compatible },
					{ "reason", host.reason } });
				if (nlohmann::json{ { "type", "steamFriendHosts" }, { "hosts", hosts } }.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).size() > kMaxFriendHostsBytes) {
					hosts.erase(hosts.size() - 1);
					break;
				}
			}
			Push(nlohmann::json{ { "type", "steamFriendHosts" }, { "hosts", hosts } });
		}

		void OnListHosts()
		{
			if (g_listHostsRunning.exchange(true)) {
				return;
			}
			try {
				std::thread([]() {
					try {
						F4T::Steam::Initialize();
						if (F4T::Steam::RetryIfUnavailable()) {
							const auto retried = F4T::Steam::Status();
							F4T::DebugLogging::Log("SteamSession", "steam reload available {} reason {}", retried.available, retried.reason);
						}
						PushStatus();
						if (F4T::Steam::Status().available) {
							const auto friends = F4T::Steam::ListHostingFriends();
							if (!friends.error.empty()) {
								F4T::DebugLogging::Log("SteamSession", "hosting friends list error: {}", friends.error);
							}
							PushFriendHosts(friends);
						} else {
							PushFriendHosts(F4T::Steam::HostingFriends{});
						}
					} catch (const std::exception& a_error) {
						F4T::DebugLogging::Log("SteamSession", "hosting friends list failed: {}", a_error.what());
					}
					g_listHostsRunning.store(false);
				}).detach();
			} catch (const std::exception& a_error) {
				g_listHostsRunning.store(false);
				F4T::DebugLogging::Log("SteamSession", "hosting friends list not started: {}", a_error.what());
			}
		}

		void RunAsync(std::string a_failureType, SteamJoinGate::Operation a_operation, std::function<void(std::uint64_t)> a_work)
		{
			const auto epoch = g_gate.Begin(a_operation);
			try {
				std::thread([failureType = std::move(a_failureType), operation = a_operation, epoch, work = std::move(a_work)]() {
					try {
						work(epoch);
					} catch (const std::exception& a_error) {
						if (FinishOperation(operation, epoch)) {
							PushFailed(failureType.c_str(), a_error.what());
						}
					}
				}).detach();
			} catch (const std::exception& a_error) {
				if (FinishOperation(a_operation, epoch)) {
					PushFailed(a_failureType.c_str(), a_error.what());
				}
			}
		}

		bool BeginLocalJoin(
			std::string                                    a_id,
			std::string                                    a_name,
			std::uint16_t                                  a_port,
			std::string                                    a_password,
			COMMONWEALTH_ONLINE_UI_API::JoinCompleteFn     a_onComplete,
			std::uint64_t                                  a_epoch,
			std::string&                                   a_error)
		{
			COMMONWEALTH_ONLINE_UI_API::JoinRequest request{};
			request.id = a_id.c_str();
			request.name = a_name.c_str();
			request.host = "127.0.0.1";
			request.port = a_port;
			char errorBuf[512]{};
			const bool started = F4T::UIClient::BeginJoin(
				&request,
				a_password.empty() ? std::nullopt : std::optional<std::string>{ std::move(a_password) },
				a_onComplete,
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(a_epoch)),
				errorBuf,
				sizeof(errorBuf));
			if (!started) {
				a_error = errorBuf[0] ? errorBuf : "Could not start the join.";
			}
			return started;
		}

		void OnHostJoinComplete(bool a_success, const char* a_error, void* a_epoch)
		{
			const auto epoch = reinterpret_cast<std::uintptr_t>(a_epoch);
			if (!FinishOperation(SteamJoinGate::Operation::HostStart, epoch)) {
				return;
			}
			if (!a_success) {
				F4T::UIClient::DisconnectFromSession();
				PushFailed("steamHostFailed", a_error && a_error[0] ? a_error : "Could not connect to the local server on this PC.");
			}
		}

		void OnGuestJoinComplete(bool a_success, const char* a_error, void* a_epoch)
		{
			const auto epoch = reinterpret_cast<std::uintptr_t>(a_epoch);
			if (!FinishOperation(SteamJoinGate::Operation::Join, epoch)) {
				return;
			}
			const auto refusal = F4T::Steam::GuestRefusalMessage();
			if (!a_success) {
				F4T::Steam::ClearGuestPresence();
				const auto linkState = F4T::Steam::GuestState();
				const auto linkReason = F4T::Steam::GuestFailureReason();
				F4T::DebugLogging::Log(
					"SteamSession",
					"steam join failed link={} reason={} error={}",
					F4T::Transport::DescribeTunnelState(linkState),
					linkReason.empty() ? std::string{ "<none>" } : linkReason,
					a_error && a_error[0] ? std::string{ a_error } : std::string{ "<none>" });
				F4T::UIClient::DisconnectFromSession();
				F4T::DebugLogging::Log("SteamSession", "join failed: {}", refusal.empty() ? "no host refusal reason" : refusal);
				std::string reason = !refusal.empty() ? refusal : a_error && a_error[0] ? std::string{ a_error } : std::string{ "Could not join that friend." };
				if (linkState != F4T::Transport::TunnelState::Connected) {
					reason += " (the Steam link was ";
					reason += F4T::Transport::DescribeTunnelState(linkState);
					reason += " when the join gave up)";
				}
				PushFailed("steamJoinFailed", reason);
			}
		}

		void OnHostStart(const char* a_name, const char* a_password, std::uint32_t a_maxPlayers)
		{
			const std::string name = a_name && a_name[0] ? a_name : "Steam host";
			const std::string password = a_password ? a_password : "";
			RunAsync("steamHostFailed", SteamJoinGate::Operation::HostStart, [name, password, a_maxPlayers](std::uint64_t epoch) {
				const auto serverError = F4T::LocalServer::EnsureRunning(name, a_maxPlayers, kLocalServerPort);
				if (!EpochCurrent(epoch)) {
					return;
				}
				if (!serverError.empty()) {
					if (FinishOperation(SteamJoinGate::Operation::HostStart, epoch)) {
						PushFailed("steamHostFailed", serverError);
					}
					return;
				}
				F4T::Steam::Initialize();
				if (F4T::Steam::RetryIfUnavailable()) {
					const auto retried = F4T::Steam::Status();
					F4T::DebugLogging::Log("SteamSession", "steam reload available {} reason {}", retried.available, retried.reason);
				}
				PushStatus();
				const auto error = F4T::Steam::StartHost(kLocalServerPort, a_maxPlayers);
				if (!EpochCurrent(epoch)) {
					F4T::Steam::StopHost();
					return;
				}
				if (error != F4T::Transport::TunnelError::None) {
					if (FinishOperation(SteamJoinGate::Operation::HostStart, epoch)) {
						const auto status = F4T::Steam::Status();
						PushFailed(
							"steamHostFailed",
							error == F4T::Transport::TunnelError::SteamUnavailable && !status.reason.empty()
								? std::string_view{ status.reason }
								: F4T::Transport::DescribeTunnelError(error));
					}
					return;
				}
				if (!F4T::Steam::HostIdentityLinked()) {
					F4T::DebugLogging::Log(
						"SteamSession",
						"no server identity link for port {}: Steam guests will join unverified",
						kLocalServerPort);
				}
				const auto steamId = F4T::Steam::MySteamId();
				const auto friendCode = F4T::Steam::FriendCode(steamId);
				if (!EpochCurrent(epoch)) {
					F4T::Steam::StopHost();
					return;
				}
				if (friendCode == 0) {
					const bool reported = FinishOperation(SteamJoinGate::Operation::HostStart, epoch);
					F4T::Steam::StopHost();
					if (reported) {
						PushFailed("steamHostFailed", "Steam did not report an id for this account.");
					}
					return;
				}
				F4T::Steam::AnnounceHosting(name);
				F4T::Steam::RememberInviteCode(friendCode);
				if (!EpochCurrent(epoch)) {
					F4T::Steam::StopHost();
					return;
				}
				F4T::DebugLogging::Log("SteamSession", "host friend code {} steamid {}", friendCode, steamId);
				{
					std::lock_guard lock{ g_hostJoinMutex };
					g_hostJoinName = name;
					g_hostJoinPassword = password;
				}
				if (!FinishOperation(SteamJoinGate::Operation::HostStart, epoch)) {
					F4T::Steam::StopHost();
					std::lock_guard lock{ g_hostJoinMutex };
					g_hostJoinPassword.clear();
					return;
				}
				Push(nlohmann::json{ { "type", "steamHostStarted" }, { "friendCode", std::to_string(friendCode) } });
			});
		}

		void OnHostEnter()
		{
			std::string name;
			std::string password;
			{
				std::lock_guard lock{ g_hostJoinMutex };
				name = g_hostJoinName;
				password = g_hostJoinPassword;
			}
			RunAsync("steamHostFailed", SteamJoinGate::Operation::HostStart, [name, password](std::uint64_t epoch) {
				if (!F4T::Steam::HostRunning()) {
					if (FinishOperation(SteamJoinGate::Operation::HostStart, epoch)) {
						PushFailed("steamHostFailed", "Hosting is not running. Start hosting again.");
					}
					return;
				}
				std::string joinError;
				if (!BeginLocalJoin("steam-host", name, kLocalServerPort, password, &OnHostJoinComplete, epoch, joinError)) {
					if (FinishOperation(SteamJoinGate::Operation::HostStart, epoch)) {
						PushFailed("steamHostFailed", joinError.empty() ? "Could not start the local join." : joinError);
					}
				}
			});
		}

		void OnLobbyPeers(const std::vector<F4T::Steam::LobbyPeer>& a_peers)
		{
			nlohmann::json peers = nlohmann::json::array();
			for (const auto& peer : a_peers) {
				peers.push_back(nlohmann::json{ { "steamId", std::to_string(peer.steamId) }, { "name", peer.name } });
			}
			Push(nlohmann::json{ { "type", "steamLobbyPeers" }, { "peers", peers } });
		}

		void OnHostStop()
		{
			F4T::UIClient::DisconnectFromSession();
			Push(nlohmann::json{ { "type", "steamHostStopped" } });
		}

		void OnInvite()
		{
			const auto reason = F4T::Steam::OpenFriendInvite();
			if (reason.empty()) {
				Push(nlohmann::json{ { "type", "steamInviteOpened" } });
				return;
			}
			PushFailed("steamInviteFailed", reason);
		}

		void OnJoin(const char* a_friendCode, const char* a_password)
		{
			if (a_friendCode && kListHostsToken == a_friendCode) {
				OnListHosts();
				return;
			}
			const std::string friendCode = a_friendCode ? a_friendCode : "";
			const std::string password = a_password ? a_password : "";
			RunAsync("steamJoinFailed", SteamJoinGate::Operation::Join, [friendCode, password](std::uint64_t epoch) {
				F4T::Steam::Initialize();
				if (F4T::Steam::RetryIfUnavailable()) {
					const auto retried = F4T::Steam::Status();
					F4T::DebugLogging::Log("SteamSession", "steam reload available {} reason {}", retried.available, retried.reason);
				}
				PushStatus();
				const auto host = F4T::Steam::FindHost(friendCode);
				if (!host.ok()) {
					if (FinishOperation(SteamJoinGate::Operation::Join, epoch)) {
						PushFailed("steamJoinFailed", host.error);
					}
					return;
				}
				if (host.steamId == 0) {
					if (FinishOperation(SteamJoinGate::Operation::Join, epoch)) {
						PushFailed("steamJoinFailed", "Steam did not report the host's Steam ID, so the join has no stable profile key.");
					}
					return;
				}
				if (const auto buildRefusal = F4T::Steam::CheckHostBuild(host.steamId)) {
					if (FinishOperation(SteamJoinGate::Operation::Join, epoch)) {
						PushFailed("steamJoinFailed", *buildRefusal);
					}
					return;
				}
				const auto connected = F4T::Steam::ConnectGuest(host.steamId);
				if (!EpochCurrent(epoch)) {
					F4T::Steam::StopGuest();
					return;
				}
				if (!connected.ok() || connected.loopbackPort == 0) {
					if (FinishOperation(SteamJoinGate::Operation::Join, epoch)) {
						const auto status = F4T::Steam::Status();
						PushFailed(
							"steamJoinFailed",
							connected.error == F4T::Transport::TunnelError::SteamUnavailable && !status.reason.empty()
								? std::string_view{ status.reason }
								: F4T::Transport::DescribeTunnelError(connected.error));
					}
					return;
				}
				const auto linkStarted = std::chrono::steady_clock::now();
				const auto linkWait = std::chrono::milliseconds{
					static_cast<std::int64_t>(F4T::ConnectionPolicy::DefaultSteamLinkWaitSeconds * 1000.0)
				};
				std::string linkFailure;
				if (!F4T::Steam::WaitForGuestConnected(linkWait, linkFailure)) {
					const bool reported = FinishOperation(SteamJoinGate::Operation::Join, epoch);
					F4T::Steam::StopGuest();
					F4T::DebugLogging::Log(
						"SteamSession",
						"steam link failed host={} state={} reason={}",
						host.steamId,
						static_cast<int>(F4T::Steam::GuestState()),
						linkFailure.empty() ? std::string{ "<none>" } : linkFailure);
					if (reported) {
						PushFailed("steamJoinFailed", linkFailure.empty() ? std::string_view{ "The Steam link did not connect." } : std::string_view{ linkFailure });
					}
					return;
				}
				if (!EpochCurrent(epoch)) {
					F4T::Steam::StopGuest();
					return;
				}
				F4T::DebugLogging::Log(
					"SteamSession",
					"steam link connected host={} after {} ms",
					host.steamId,
					std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - linkStarted).count());
				F4T::Steam::SetGuestPresence(host.name, F4T::Steam::FriendCode(host.steamId));
				std::string joinError;
				if (!BeginLocalJoin("steam-" + std::to_string(host.steamId), "Steam friend", connected.loopbackPort, password, &OnGuestJoinComplete, epoch, joinError)) {
					const bool reported = FinishOperation(SteamJoinGate::Operation::Join, epoch);
					F4T::Steam::StopGuest();
					F4T::Steam::ClearGuestPresence();
					if (reported) {
						PushFailed("steamJoinFailed", joinError.empty() ? "The join was cancelled." : joinError);
					}
				}
			});
		}

		void OnRichPresenceJoin(const char* a_connect)
		{
			if (a_connect == nullptr || a_connect[0] == '\0') return;
			if (g_gate.Pending() != SteamJoinGate::Operation::None) {
				F4T::DebugLogging::Log("SteamSession", "join request ignored: a Steam join or host start is already in progress");
				return;
			}
			if (F4T::Networking::IsConnectedToServer()) {
				F4T::DebugLogging::Log("SteamSession", "join request ignored: already connected to a server");
				return;
			}
			const auto mine = std::to_string(F4T::Steam::FriendCode(F4T::Steam::MySteamId()));
			if (F4T::Steam::HostRunning() && mine != "0" && mine == a_connect) return;
			OnJoin(a_connect, "");
		}
	}

	void StopTunnels()
	{
		g_gate.Cancel();
		const bool wasHosting = F4T::Steam::HostRunning();
		{
			std::lock_guard lock{ g_hostJoinMutex };
			g_hostJoinName.clear();
			g_hostJoinPassword.clear();
		}
		F4T::Steam::StopHost();
		F4T::Steam::StopGuest();
		F4T::Steam::ClearGuestPresence();
		if (wasHosting) {
			Push(nlohmann::json{ { "type", "steamHostStopped" } });
		}
	}

	void CancelJoins()
	{
		const auto cancelled = g_gate.Cancel();
		F4T::Steam::StopGuest();
		F4T::Steam::ClearGuestPresence();
		if (SteamJoinGate::Has(cancelled, SteamJoinGate::Operation::HostStart)) {
			F4T::Steam::StopHost();
			PushFailed("steamHostFailed", "Hosting was cancelled.");
		}
		if (SteamJoinGate::Has(cancelled, SteamJoinGate::Operation::Join)) {
			PushFailed("steamJoinFailed", "The join was cancelled.");
		}
	}

	void Attach()
	{
		F4T::Steam::SetAdmissionLog([](std::string_view a_line) { F4T::DebugLogging::Log("SteamAdmission", "{}", a_line); });
		F4T::Steam::SetJoinConnectHandler(&OnRichPresenceJoin);
		F4T::Steam::SetJoinFailureHandler(&OnJoinRefused);
		F4T::Steam::SetLobbyPeersSink(&OnLobbyPeers);
		F4T::ServerLive::Attach();
		F4T::Steam::Initialize();
		const auto launch = F4T::Steam::ParseLaunchArgs(GetCommandLineA());
		g_hasLaunchJoin.store(launch.connect.has_value() || launch.lobbyId.has_value());
		F4T::Steam::QueueLaunchJoin(launch);
		const auto status = F4T::Steam::Status();
		F4T::DebugLogging::Log("SteamSession", "steam p2p available {} reason {}", status.available, status.reason);
		F4T::DebugLogging::Log("SteamSession", "steam module {}", F4T::Steam::ModuleVersion());
		F4T::DebugLogging::Log("SteamSession", "steam identity {}", F4T::Steam::MySteamId() != 0 ? "resolved" : "not resolved");
		for (const auto& line : F4T::Steam::InterfaceProbe()) {
			F4T::DebugLogging::Log("SteamSession", "generic interface probe: {}", line);
		}
		g_ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI7>();
		if (!g_ui || !g_ui->SetSteamCallbacks) {
			return;
		}
		static const COMMONWEALTH_ONLINE_UI_API::SteamCallbacks kCallbacks{
			OnHostStart,
			OnHostStop,
			OnJoin,
			OnInvite,
			OnHostEnter
		};
		g_ui->SetSteamCallbacks(&kCallbacks);
		PushStatus();
	}

	void FireLaunchJoin()
	{
		if (!g_hasLaunchJoin.load() || g_launchJoinWatching.exchange(true)) {
			return;
		}
		try {
			std::thread([]() {
				const auto deadline = std::chrono::steady_clock::now() + kLaunchJoinGiveUp;
				while (!g_launchJoinFired.load() && std::chrono::steady_clock::now() < deadline) {
					std::this_thread::sleep_for(kLaunchJoinPoll);
					const auto* taskInterface = F4SE::GetTaskInterface();
					if (!taskInterface) {
						continue;
					}
					taskInterface->AddTask([]() {
						if (g_launchJoinFired.load() || !F4T::SessionWorldState::IsMainMenuOpen()) {
							return;
						}
						g_launchJoinFired.store(true);
						(void)F4T::Steam::FireLaunchJoin();
					});
				}
				if (!g_launchJoinFired.load()) {
					F4T::DebugLogging::Log("SteamSession", "launch join not fired: the main menu did not open in time");
				}
			}).detach();
		} catch (const std::exception& a_error) {
			g_launchJoinWatching.store(false);
			F4T::DebugLogging::Log("SteamSession", "launch join watcher not started: {}", a_error.what());
		}
	}
}

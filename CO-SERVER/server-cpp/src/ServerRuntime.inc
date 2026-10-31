#include "AdminCommandDispatcher.h"
#include "AdminControlServer.h"
#include "AdminTokenStore.h"
#include "BanStore.h"
#include "DurableStateFileStore.h"
#include "IdentityGate.h"
#include "DirectoryRegistrar.h"
#include "LanDiscoveryService.h"
#include "OpenSslTokenService.h"
#include "PersistenceGate.h"
#include "SaveAttestationAuthority.h"
#include "ServerConfiguration.h"
#include "ServerProtocol.h"
#include "ServerFactionAdminAction.h"
#include "ServerQuestAdminAction.h"
#include "ServerPacketDecode.h"
#include "ServerLiveRelay.h"
#include "ServerLiveSettings.h"
#include "F4TUtf8Path.h"
#include "F4TServerSettingsFile.h"
#include "ServerTextHelpers.h"
#include "SteamAdmin.h"
#include "SteamIdentityRegistry.h"
#include "SteamWebApiVerifier.h"
#include "ServerVoiceHost.h"
#include "ServerVoiceMeter.h"
#include "F4TAdmission.h"
#include "F4TConnectionPolicy.h"
#include "F4TPacketRateLimit.h"
#include "F4TServerWorldState.h"
#include "co_gns_server_bridge.h"
#include "ServerBootstrap.h"
#include "ServerRuntimeSupport.h"
#include "ServerManifestChunks.h"
#include "ServerModpackFiles.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace CommonwealthOnline::Server::RuntimeSupport;

int CommonwealthOnlineServerMain(int argc, char** argv)
{
	using CommonwealthOnline::Server::AdminActionResult;
	using CommonwealthOnline::Server::AdminCommand;
	using CommonwealthOnline::Server::AdminCommandDispatcher;
	using CommonwealthOnline::Server::AdminCommandHooks;
	using CommonwealthOnline::Server::AdminControlServer;
	using CommonwealthOnline::Server::AdminTokenStore;
	using CommonwealthOnline::Server::BanStore;
	using CommonwealthOnline::Server::LanDiscoveryService;
	using CommonwealthOnline::Server::OpenSslTokenService;

	CommonwealthOnline::Server::ServerBootstrap bootstrap;
	if (const auto exitCode = CommonwealthOnline::Server::ServerBootstrap::Prepare(argc, argv, bootstrap)) {
		return *exitCode;
	}
	auto& options = bootstrap.options;
	auto& identityTokens = bootstrap.identityTokens;
	auto& durableStore = bootstrap.durableStore;
	auto& banStore = bootstrap.banStore;
	auto& admin = *bootstrap.admin;
	auto& worldPolicyData = bootstrap.worldPolicyData;
	auto& worldMode = bootstrap.worldMode;
	auto& worldPolicyPath = bootstrap.worldPolicyPath;
	const auto& baseDirectory = bootstrap.baseDirectory;
	auto server = bootstrap.server;
	const auto localPort = co_gns_server_local_port(server);
	std::atomic_size_t activePlayers{ 0 };
	#include "ServerDirectoryRuntime.inc"
	if (admin.LocalPort() != 0) std::cout << "Admin control listening on 127.0.0.1:" << admin.LocalPort() << " (authenticated)" << std::endl;
	if (durableStore) {
		std::cout << "Durable persistence enabled for server instance " << durableStore->ServerInstanceId() << std::endl;
		if (const auto notice = durableStore->RecoveryNotice()) {
			std::cerr << "Durable persistence recovery: " << *notice << '\n';
		}
	}

	%:include "ServerPersistenceSetup.inc"

	nlohmann::json serverWorldState = durableStore ? durableStore->GetServerWorldState() : nlohmann::json::object();
	F4T::ConnectionPolicy::ConnectAttemptLimiter connectAttempts;
	std::unordered_map<std::uint32_t, RuntimeConnectionState> runtimeConnections;
	F4T::Admission::AdmissionRegistry sessionAdmissions{ options.maximumPlayers };
	std::array<std::uint8_t, 64 * 1024> payload{};
	double nextMaintenanceMonotonic = MonotonicSeconds() + 0.5;

	AdmissionContext admission{
		.bans = banStore.get(),
		.attempts = std::addressof(connectAttempts),
		.connections = std::addressof(runtimeConnections),
		.maximumPlayers = options.maximumPlayers
	};
	(void)co_gns_server_set_admission_callback(server, &AdmitConnection, std::addressof(admission));
	(void)co_gns_server_set_address_admission_callback(server, &AdmitAddressConnection, std::addressof(admission));

	auto eraseRuntimeConnection = [&](std::uint32_t a_connectionId) {
		const auto found = runtimeConnections.find(a_connectionId);
		if (found == runtimeConnections.end()) {
			return;
		}
		if (found->second.active) {
			activePlayers.fetch_sub(1, std::memory_order_relaxed);
		}
		runtimeConnections.erase(found);
	};

	auto disconnectTracked = [&](std::uint32_t a_connectionId, const char* a_reason) {
		LogDisconnect(runtimeConnections, a_connectionId, "server", a_reason);
		if (persistence != nullptr) {
			persistence->ConnectionClosed(a_connectionId);
		}
		if (identity != nullptr) {
			identity->ConnectionClosed(a_connectionId);
		}
		co_gns_server_disconnect(server, a_connectionId, 0, a_reason);
		const auto unixNow = UnixTimeSeconds();
		auto frames = protocol.HandleDisconnected(a_connectionId, unixNow);
		SendFrames(server, protocol, frames, identity, persistence, MonotonicSeconds(), unixNow, nullptr);
		eraseRuntimeConnection(a_connectionId);
	};

	// Sends frames and then retires any connection whose session could not be established.
	auto sendFramesTracked = [&](
		std::vector<CommonwealthOnline::Server::OutboundFrame>& a_frames,
		double a_monotonicSeconds,
		double a_unixTimeSeconds) {
		std::vector<std::uint32_t> retire;
		SendFrames(server, protocol, a_frames, identity, persistence, a_monotonicSeconds, a_unixTimeSeconds, &retire);
		for (const auto connectionId : retire) {
			disconnectTracked(connectionId, "Session could not be established");
		}
	};

	const CommonwealthOnline::Server::FactionFrameSender sendFactionFrames = [&](std::vector<CommonwealthOnline::Server::OutboundFrame>& a_frames) { sendFramesTracked(a_frames, MonotonicSeconds(), UnixTimeSeconds()); };
	const CommonwealthOnline::Server::VoiceFrameSender sendVoiceFrames = [&](std::vector<CommonwealthOnline::Server::OutboundFrame>& a_frames) { sendFramesTracked(a_frames, MonotonicSeconds(), UnixTimeSeconds()); };
	const CommonwealthOnline::Server::QuestFrameSender sendQuestFrames = [&](std::vector<CommonwealthOnline::Server::OutboundFrame>& a_frames) { sendFramesTracked(a_frames, MonotonicSeconds(), UnixTimeSeconds()); };
	CommonwealthOnline::Server::ServerLiveSettingsContext liveSettingsContext{
		.options = &options,
		.protocol = &protocol,
		.persistence = persistence,
		.identityGate = identity,
		.activePlayers = &activePlayers,
		.sessionAdmissions = &sessionAdmissions,
		.admission = &admission,
		.discovery = &discovery,
		.directory = &directory,
		.worldPolicyData = &worldPolicyData,
		.worldMode = &worldMode,
		.baseDirectory = &baseDirectory,
		.worldPolicyPath = &worldPolicyPath,
		.localPort = localPort,
		.sendFrames = [&](std::vector<CommonwealthOnline::Server::OutboundFrame> a_frames) {
			sendFramesTracked(a_frames, MonotonicSeconds(), UnixTimeSeconds());
		},
		.broadcastPacket = [&](const nlohmann::json& a_packet) {
			BroadcastWorldPacket(server, runtimeConnections, a_packet);
		}
	};

	const auto applyWorldMode = [&](std::string_view a_mode) -> AdminActionResult { return CommonwealthOnline::Server::ApplyAdminWorldMode(liveSettingsContext, a_mode); };
	const auto applyLiveSettings = [&](const nlohmann::json& a_document) -> AdminActionResult { return CommonwealthOnline::Server::ApplyServerLiveSettings(liveSettingsContext, a_document); };

	AdminCommandHooks adminHooks{
		.shutdown = [&] {
			std::cout << "Authenticated admin shutdown requested" << std::endl;
			g_running = 0;
			return AdminActionResult{ true, "Server shutdown requested.", nullptr };
		},
		.status = [&] {
			const auto active = static_cast<std::size_t>(std::count_if(
				runtimeConnections.begin(), runtimeConnections.end(),
				[](const auto& entry) { return entry.second.active; }));
			const auto stats = protocol.Stats();
			auto status = nlohmann::json{
				{ "is_running", true },
				{ "host", options.bindHost },
				{ "port", localPort },
				{ "admin_port", admin.LocalPort() },
				{ "server_name", options.serverName },
				{ "server_description", options.serverDescription },
				{ "max_players", options.maximumPlayers },
				{ "connected_clients", active },
				{ "pending_connections", runtimeConnections.size() - active },
				{ "protocol_version", 2 },
				{ "require_v2_identity", options.requireV2Identity },
				{ "persistence_enabled", options.enablePersistence },
				{ "world_mode", std::string{ F4T::WorldPolicy::ModeName(protocol.CurrentWorldPolicy().mode) } },
				{ "friendly_fire", options.friendlyFire },
				{ "faction_friendly_fire", options.factionFriendlyFire },
				{ "public_listing", directory
					? CommonwealthOnline::Server::ToJson(directory->Status())
					: CommonwealthOnline::Server::ToJson(CommonwealthOnline::Server::DirectoryStatus{}) }
			};
			status.update(CommonwealthOnline::Server::RuntimeCountersJson(stats));
			return status;
		},
		.clients = [&] {
			nlohmann::json clients = nlohmann::json::array();
			for (const auto& [connectionId, state] : runtimeConnections) {
				(void)connectionId;
				if (!state.active || state.playerId == 0) {
					continue;
				}
				nlohmann::json client{
					{ "player_id", state.playerId },
					{ "address", state.remoteAddress.empty() ? Ipv4Text(state.remoteIpv4) : state.remoteAddress },
					{ "port", state.remotePort },
					{ "connected_at", state.connectedAtUnixSeconds }
				};
				if (state.lastPacketAtUnixSeconds) {
					client["last_packet_at"] = *state.lastPacketAtUnixSeconds;
				}
				if (!state.steamId.empty()) client["steam_id"] = state.steamId;
				clients.push_back(std::move(client));
			}
			return nlohmann::json{
				{ "total_clients", clients.size() },
				{ "clients", std::move(clients) }
			};
		},
		.kick = [&](std::uint32_t a_playerId, std::string_view a_reason) {
			std::optional<std::uint32_t> targetConnectionId;
			std::string targetAddress;
			for (const auto& [connectionId, state] : runtimeConnections) {
				if (state.active && state.playerId == a_playerId) {
					targetConnectionId = connectionId;
					targetAddress = state.remoteAddress.empty() ? Ipv4Text(state.remoteIpv4) : state.remoteAddress;
					break;
				}
			}
			if (!targetConnectionId) {
				return AdminActionResult{ false, "No connected player with id " + std::to_string(a_playerId), nullptr };
			}
			const auto reason = std::string{ a_reason };
			SendSessionEnded(server, *targetConnectionId, "kicked", reason, UnixTimeSeconds());
			disconnectTracked(*targetConnectionId, "Kicked by administrator");
			return AdminActionResult{
				true,
				"Kicked player " + std::to_string(a_playerId) + ".",
				nlohmann::json{ { "playerId", a_playerId }, { "ip", targetAddress }, { "reason", reason } }
			};
		},
		.resetIdentity = identity != nullptr ? std::function<AdminActionResult(std::string_view)>{ [&](std::string_view a_profileId) {
			std::string reason;
			try {
				const bool reset = identity->TryResetResumeCredential(a_profileId, reason);
				return AdminActionResult{
					reset,
					reason,
					reset ? nlohmann::json{ { "profileId", a_profileId } } : nlohmann::json{ nullptr }
				};
			} catch (const std::exception& exception) {
				return AdminActionResult{ false, "Resume credential reset failed: " + std::string{ exception.what() }, nullptr };
			}
		} } : std::function<AdminActionResult(std::string_view)>{},
		.revive = [&](std::uint32_t a_playerId) {
			const auto before = protocol.Stats().downedEventsEmitted;
			auto frames = protocol.AdminRevivePlayer(a_playerId, MonotonicSeconds());
			if (protocol.Stats().downedEventsEmitted == before) {
				return AdminActionResult{ false, "No downed or dead player with id " + std::to_string(a_playerId) + ".", nullptr };
			}
			sendFramesTracked(frames, MonotonicSeconds(), UnixTimeSeconds());
			return AdminActionResult{
				true,
				"Revived player " + std::to_string(a_playerId) + ".",
				nlohmann::json{ { "playerId", a_playerId } }
			};
		},
		.killPlayer = [&](std::uint32_t a_playerId) {
			const auto before = protocol.Stats().downedEventsEmitted;
			auto frames = protocol.AdminKillPlayer(a_playerId);
			if (protocol.Stats().downedEventsEmitted == before) {
				return AdminActionResult{ false, "No tracked player with id " + std::to_string(a_playerId) + ".", nullptr };
			}
			sendFramesTracked(frames, MonotonicSeconds(), UnixTimeSeconds());
			return AdminActionResult{
				true,
				"Killed player " + std::to_string(a_playerId) + ".",
				nlohmann::json{ { "playerId", a_playerId } }
			};
		},
		.voiceMute = [&](std::optional<std::uint32_t> a_playerId, std::string_view a_profileId, bool a_muted, std::optional<double> a_seconds) { return VoiceAdminAction(protocol, a_muted, a_playerId, a_profileId, a_seconds, MonotonicSeconds(), sendVoiceFrames); },
		.setWorldTime = [&](std::string_view a_value) {
			const auto normalized = F4T::ServerWorldState::NormalizeHhmm(a_value);
			if (!normalized) {
				return AdminActionResult{ false, "Invalid time format. Use HHmm (e.g., 1430 for 14:30).", nullptr };
			}
			std::optional<std::string> persistenceWarning;
			if (durableStore) {
				try {
					durableStore->UpdateServerWorldState(nlohmann::json{ { "timeHHmm", *normalized } });
					persistenceWarning = durableStore->TakePersistenceWarning();
				} catch (const std::exception& exception) {
					return AdminActionResult{ false, "Failed to persist server time: " + std::string{ exception.what() }, nullptr };
				}
			}
			serverWorldState["timeHHmm"] = *normalized;
			const auto now = UnixTimeSeconds();
			BroadcastWorldPacket(server, runtimeConnections, nlohmann::json{
				{ "type", "serverWorldState" },
				{ "timeHHmm", *normalized },
				{ "serverTime", now }
			});
			return AdminActionResult{
				true,
				AppendPersistenceWarning("Server time set to " + *normalized + ".", std::move(persistenceWarning)),
				nullptr
			};
		},
		.setWorldWeather = [&](std::string_view a_value) {
			const auto consoleArg = F4T::ServerWorldState::NormalizeWeatherConsoleArg(a_value);
			const auto relayFormId = F4T::ServerWorldState::RelayWeatherFormId(a_value);
			if (!consoleArg || !relayFormId) {
				return AdminActionResult{ false, "Invalid weather ID. Use an 8-digit hex form ID.", nullptr };
			}
			std::optional<std::string> persistenceWarning;
			if (durableStore) {
				try {
					durableStore->UpdateServerWorldState(nlohmann::json{
						{ "weatherConsoleArg", *consoleArg },
						{ "weatherFormId", *relayFormId }
					});
					persistenceWarning = durableStore->TakePersistenceWarning();
				} catch (const std::exception& exception) {
					return AdminActionResult{ false, "Failed to persist server weather: " + std::string{ exception.what() }, nullptr };
				}
			}
			serverWorldState["weatherConsoleArg"] = *consoleArg;
			serverWorldState["weatherFormId"] = *relayFormId;
			const auto now = UnixTimeSeconds();
			BroadcastWorldPacket(server, runtimeConnections, nlohmann::json{
				{ "type", "serverWorldState" },
				{ "weatherFormId", *relayFormId },
				{ "serverTime", now }
			});
			return AdminActionResult{
				true,
				AppendPersistenceWarning("Server weather updated to " + *consoleArg + ".", std::move(persistenceWarning)),
				nullptr
			};
		},
		.worldMode = [&] {
			const auto policy = protocol.CurrentWorldPolicy();
			return nlohmann::json{
				{ "mode", std::string{ F4T::WorldPolicy::ModeName(policy.mode) } },
				{ "doors", std::string{ F4T::WorldPolicy::DoorsName(policy.doors) } },
				{ "suppress_quests", policy.suppressQuests },
				{ "disabled_entries", policy.disable.size() },
				{ "data_version", worldPolicyData.version },
				{ "game", worldPolicyData.game },
				{ "data_path", F4T::Utf8Path::Encode(worldPolicyPath) },
				{ "available_modes", nlohmann::json::array({
					std::string{ F4T::WorldPolicy::ModeName(F4T::WorldPolicy::Mode::CoopStory) },
					std::string{ F4T::WorldPolicy::ModeName(F4T::WorldPolicy::Mode::FreeRoam) } }) }
			};
		},
		.setWorldMode = applyWorldMode,
		.reloadWorldPolicy = [&] {
			const auto loaded = F4T::WorldPolicy::LoadData(worldPolicyPath);
			if (!loaded) {
				return AdminActionResult{
					false,
					"World policy data missing or invalid at " + F4T::Utf8Path::Encode(worldPolicyPath) + ".",
					nullptr
				};
			}
			if (!F4T::WorldPolicy::CanServeMode(*loaded, worldMode)) {
				return AdminActionResult{
					false,
					"World policy data at " + F4T::Utf8Path::Encode(worldPolicyPath) + " is the placeholder sample; "
						"the current world mode " + std::string{ F4T::WorldPolicy::ModeName(worldMode) } +
						" needs generated story or companion entries.",
					nullptr
				};
			}
			worldPolicyData = *loaded;
			auto frames = protocol.SetWorldPolicy(F4T::WorldPolicy::DerivePolicy(worldPolicyData, worldMode, options.doorsShared));
			sendFramesTracked(frames, MonotonicSeconds(), UnixTimeSeconds());
			return AdminActionResult{
				true,
				"World policy data reloaded.",
				nlohmann::json{
					{ "data_version", worldPolicyData.version },
					{ "game", worldPolicyData.game },
					{ "entries", worldPolicyData.EntryCount() },
					{ "disabled_entries", protocol.CurrentWorldPolicy().disable.size() }
				}
			};
		},
		.factions = [&] { return protocol.AdminListFactions(); },
		.disbandFaction = [&](std::string_view a_id) { return CommonwealthOnline::Server::RunFactionAdminAction(protocol, CommonwealthOnline::Server::FactionAdminOp::Disband, a_id, {}, UnixTimeSeconds(), sendFactionFrames); },
		.promoteFaction = [&](std::string_view a_id, std::string_view a_target) { return CommonwealthOnline::Server::RunFactionAdminAction(protocol, CommonwealthOnline::Server::FactionAdminOp::Promote, a_id, a_target, UnixTimeSeconds(), sendFactionFrames); },
		.kickFaction = [&](std::string_view a_id, std::string_view a_target) { return CommonwealthOnline::Server::RunFactionAdminAction(protocol, CommonwealthOnline::Server::FactionAdminOp::Kick, a_id, a_target, UnixTimeSeconds(), sendFactionFrames); },

	};
	CommonwealthOnline::Server::SteamAdminDependencies steamAdminDependencies{
		.bans = banStore.get(),
		.connections = [&] {
			std::vector<CommonwealthOnline::Server::SteamAdminConnection> connections;
			connections.reserve(runtimeConnections.size());
			for (const auto& [connectionId, state] : runtimeConnections) {
				connections.push_back(CommonwealthOnline::Server::SteamAdminConnection{
					.connectionId = connectionId,
					.playerId = state.playerId,
					.remoteIpv4 = state.remoteIpv4,
					.remoteAddress = state.remoteAddress,
					.active = state.active,
					.profileId = state.profileId,
					.steamId = state.steamId
				});
			}
			return connections;
		},
		.endSession = [&](std::uint32_t a_connectionId, std::string_view a_code, std::string_view a_reason) {
			SendSessionEnded(server, a_connectionId, a_code, a_reason, UnixTimeSeconds());
			disconnectTracked(a_connectionId, "Ended by administrator");
		},
		.withPersistenceWarning = [](std::string a_message, std::optional<std::string> a_warning) {
			return AppendPersistenceWarning(std::move(a_message), std::move(a_warning));
		},
		.unixTimeSeconds = [] { return UnixTimeSeconds(); }
	};
	if (durableStore) {
		steamAdminDependencies.saveIdentities = [&](const nlohmann::json& a_identities) {
			durableStore->SaveSteamIdentities(a_identities);
			ReportPersistenceWarning(*durableStore);
		};
	}
	auto steamAdmin = CommonwealthOnline::Server::SteamAdmin::Create(
		durableStore ? durableStore->GetSteamIdentities() : nlohmann::json(nullptr),
		std::move(steamAdminDependencies));
	if (!steamAdmin) {
		co_gns_server_destroy(server);
		return 2;
	}
	if (!durableStore && (identity != nullptr)) {
		std::cout << "Steam identity history is kept in memory only (durable persistence is off)." << std::endl;
	}
	steamAdmin->SweepIfDue(UnixTimeSeconds());
	auto recordSteamIdentity = [&](RuntimeConnectionState& a_state, std::uint32_t a_connectionId, std::string_view a_steamId, CommonwealthOnline::Server::SteamIdSource a_source, double a_now) {
		a_state.steamId = std::string{ a_steamId };
		steamAdmin->RecordIdentity(a_connectionId, a_state.profileId, a_state.nickname, a_steamId, a_source, a_now);
	};
	steamAdmin->InstallHooks(adminHooks);
	CommonwealthOnline::Server::ServerLiveRelay serverLiveRelay;
#include "ServerLiveSettingsLink.inc"
	AdminCommandDispatcher dispatcher(CommonwealthOnline::Server::WithQuestAdminHooks(std::move(adminHooks), protocol, persistence, runtimeConnections, sendQuestFrames, UnixTimeSeconds));

	const auto flushPersistence = [&](bool a_awaitCommit) {
		CommonwealthOnline::Server::RuntimeSupport::FlushPersistence(persistence, durableStore.get(), a_awaitCommit);
	};

	CommonwealthOnline::Server::StartModpackFileCatalog(options);
	while (g_running != 0) {
		LogLoopIteration();
		serverLiveRelay.Drain(applyLiveSettings);
		AdminCommand adminCommand;
		while (admin.TryPop(adminCommand)) {
			// constraint: the pop claimed this ticket, so a timed-out caller gets this result.
			(void)admin.Complete(adminCommand.ticket, dispatcher.Dispatch(adminCommand.request));
		}

		const auto loopMonotonic = MonotonicSeconds();
		%:include "ServerMaintenance.inc"

		co_gns_event event{};
		const auto result = co_gns_server_poll(server, &event, payload.data(), static_cast<std::uint32_t>(payload.size()));
		if (result < 0) {
			std::cerr << "GNS poll failed with result " << result << '\n';
			break;
		}
		if (result == 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			continue;
		}

		switch (event.type) {
		case CO_GNS_EVENT_CONNECTED: {
			const auto monotonicNow = MonotonicSeconds();
			const auto unixNow = UnixTimeSeconds();

			const auto endpoint = ResolveRemoteEndpoint(server, event.connection_id);
			const std::string& remoteAddress = endpoint.address;
			const auto& remoteKey = endpoint.banKey;

			// constraint: released before any rejecting branch, which emits no disconnected event.
			if (admission.reservedPending > 0) {
				--admission.reservedPending;
			}

			// constraint: admission ran pre-accept; a ban that landed in between is rechecked here.
			if (remoteKey) {
				if (const auto ban = banStore->Get(*remoteKey)) {
					LogRefusal("connected", remoteAddress, "banned", ban->reason);
					SendSessionEnded(server, event.connection_id, "banned", ban->reason, unixNow);
					co_gns_server_disconnect(server, event.connection_id, 0, "Banned address");
					break;
				}
			}

			const auto admissionDecision = sessionAdmissions.Begin(
				F4T::Admission::ClaimedIdentity{
					.ip = remoteKey.value_or(std::string{ "unknown" })
				},
				unixNow,
				activePlayers.load(std::memory_order_relaxed));
			if (!admissionDecision.admitted) {
				LogRefusal("admission", remoteAddress, admissionDecision.code, admissionDecision.reason);
				SendSessionEnded(server, event.connection_id, admissionDecision.code, admissionDecision.reason, unixNow);
				co_gns_server_disconnect(server, event.connection_id, 0, admissionDecision.reason.c_str());
				break;
			}

			runtimeConnections[event.connection_id] = RuntimeConnectionState{
				.connectedAtUnixSeconds = unixNow,
				.lastPacketAtUnixSeconds = std::nullopt,
				.remoteIpv4 = endpoint.ipv4,
				.remotePort = endpoint.port,
				.remoteAddress = remoteAddress,
				.playerId = 0,
				.active = false,
				.steamHost = steamHostLink.has_value() && F4T::ConnectionPolicy::IsLoopbackHost(remoteAddress),
				.admissionTicket = admissionDecision.ticketId,
				.rate = F4T::PacketRateLimit::Initialize(monotonicNow),
				.voiceRate = F4T::PacketRateLimit::Initialize(monotonicNow)
			};
			auto frames = protocol.HandleConnected(event.connection_id, unixNow);
			sendFramesTracked(frames, monotonicNow, unixNow);
			break;
		}
		case CO_GNS_EVENT_DISCONNECTED: {
			LogDisconnect(runtimeConnections, event.connection_id, "peer or network", event.debug);
			if (!runtimeConnections.contains(event.connection_id) && admission.reservedPending > 0) {
				--admission.reservedPending;
			}
			if (const auto tracked = runtimeConnections.find(event.connection_id);
				tracked != runtimeConnections.end() && tracked->second.admissionTicket != 0) {
				(void)sessionAdmissions.Cancel(tracked->second.admissionTicket);
			}
			if (persistence != nullptr) {
				persistence->ConnectionClosed(event.connection_id);
			}
			if (identity != nullptr) {
				identity->ConnectionClosed(event.connection_id);
			}
			CommonwealthOnline::Server::ForgetManifestChunkDelivery(server, event.connection_id);
			CommonwealthOnline::Server::ForgetModpackFileDelivery(server, event.connection_id);
			eraseRuntimeConnection(event.connection_id);
			const auto unixNow = UnixTimeSeconds();
			auto frames = protocol.HandleDisconnected(event.connection_id, unixNow);
			sendFramesTracked(frames, MonotonicSeconds(), unixNow);
			break;
		}
		case CO_GNS_EVENT_MESSAGE: {
			const auto stateIt = runtimeConnections.find(event.connection_id);
			if (stateIt == runtimeConnections.end()) {
				co_gns_server_disconnect(server, event.connection_id, 0, "Unknown session");
				break;
			}
			auto& state = stateIt->second;
			const auto monotonicNow = MonotonicSeconds();
			const auto unixNow = UnixTimeSeconds();

			if (event.payload_size > payload.size()) {
				disconnectTracked(event.connection_id, "Inbound payload exceeded host buffer");
				break;
			}
			const auto message = std::span<const std::uint8_t>{ payload.data(), event.payload_size };

			const auto rate = CommonwealthOnline::Server::ObserveInboundDatagram(state.rate, state.voiceRate, monotonicNow, std::as_bytes(message));
			if (rate.verdict != F4T::PacketRateLimit::Verdict::Allowed) {
				if (rate.verdict == F4T::PacketRateLimit::Verdict::Disconnect) {
					const std::string_view reason = CommonwealthOnline::Server::RateLimitReason(rate);
					LogRefusal("rate_limit", ConnectionAddressText(state), "rate_limited", reason);
					SendSessionEnded(server, event.connection_id, "rate_limited", reason, unixNow);
					disconnectTracked(event.connection_id, reason.data());
				}
				break;
			}
			state.lastPacketAtUnixSeconds = unixNow;
			if (state.admissionTicket != 0) {
				const bool admissionAlive = sessionAdmissions.Update(
					state.admissionTicket,
					RawPacketType(message).value_or("inbound"),
					unixNow);
				if (!admissionAlive) {
					SendSessionEnded(server, event.connection_id, "admission_timeout", "Admission timed out before the session became ready.", unixNow);
					disconnectTracked(event.connection_id, "Admission timed out");
					break;
				}
			}

			if (CommonwealthOnline::Server::ServeManifestChunksIfRequested(server, event.connection_id, message, options, unixNow, disconnectTracked, sessionAdmissions, state.admissionTicket) != CommonwealthOnline::Server::ManifestChunkRequest::notRequest) {
				break;
			}
			if (CommonwealthOnline::Server::ServeModpackFilesIfRequested(server, event.connection_id, message, options, unixNow, state.remoteAddress.empty() ? Ipv4Text(state.remoteIpv4) : state.remoteAddress, disconnectTracked, sessionAdmissions, state.admissionTicket) != CommonwealthOnline::Server::ModpackFileRequest::notRequest) {
				break;
			}
			if (identity != nullptr) {
				try {
					const bool capacityAvailable = F4T::ConnectionPolicy::CanActivate(protocol.ActiveCount(), options.maximumPlayers);
					const auto decision = identity->InspectInbound(event.connection_id, message, unixNow, capacityAvailable);
					if (!decision.forward) {
						LogRefusal("hello", ConnectionAddressText(state), decision.code, decision.reason);
						if (state.admissionTicket != 0) {
							(void)sessionAdmissions.Reject(state.admissionTicket, decision.code);
							state.admissionTicket = 0;
						}
						SendSessionEnded(server, event.connection_id, decision.code, decision.reason, unixNow, decision.requiredModpack, decision.modpackFingerprint, decision.modpackChunks, decision.modpackName);
						disconnectTracked(event.connection_id, decision.reason.c_str());
						break;
					}
					if (decision.consumed) {
						if (decision.rejected) {
							std::cerr << "Save attestation rejected for connection " << event.connection_id << ": "
							          << decision.code << ": " << decision.reason << '\n';
						}
						if (decision.response) {
							(void)SendReliableJson(server, event.connection_id, *decision.response);
						}
						break;
					}
					if (!decision.profileId.empty()) {
						state.profileId = decision.profileId;
						protocol.BindSessionProfileVerified(event.connection_id, decision.profileId, decision.isAdmin, decision.steamId);
						state.nickname = decision.nickname;
						if (!decision.steamId.empty()) recordSteamIdentity(state, event.connection_id, decision.steamId, decision.steamIdSource, unixNow);
						if (state.admissionTicket != 0) {
							const bool admissionAlive = sessionAdmissions.Claim(
								state.admissionTicket,
								decision.steamId,
								decision.nickname,
								decision.profileId,
								"hello",
								unixNow);
							if (!admissionAlive) {
								SendSessionEnded(server, event.connection_id, "admission_timeout", "Admission timed out before the session became ready.", unixNow);
								disconnectTracked(event.connection_id, "Admission timed out");
								break;
							}
						}
					}
					if (decision.evictConnectionId && *decision.evictConnectionId != event.connection_id) {
						constexpr std::string_view superseded =
							"This session was replaced by a reconnect using the profile's resume credential.";
						SendSessionEnded(server, *decision.evictConnectionId, "session_superseded", superseded, unixNow);
						disconnectTracked(*decision.evictConnectionId, superseded.data());
					}
				} catch (const std::exception& exception) {
					std::cerr << "Identity validation failed for connection " << event.connection_id << ": " << exception.what() << '\n';
					disconnectTracked(event.connection_id, "Identity validation failed");
					break;
				}
			}


			if (!state.active) {
				const auto type = RawPacketType(message);
				if (type && *type == "hello" &&
					!F4T::ConnectionPolicy::CanActivate(protocol.ActiveCount(), options.maximumPlayers)) {
					constexpr std::string_view reason = "Server is full.";
					LogRefusal("hello", ConnectionAddressText(state), "server_full", reason);
					SendSessionEnded(server, event.connection_id, "server_full", reason, unixNow);
					disconnectTracked(event.connection_id, reason.data());
					break;
				}
			}

			std::optional<nlohmann::json> playerStateCandidate;
			if (persistence != nullptr) {
				playerStateCandidate = persistence->CapturePlayerStateCandidate(event.connection_id, message);
			}
			const auto rejectedBefore = protocol.Stats().packetsRejected;
			auto frames = protocol.HandleMessage(
				event.connection_id,
				message,
				monotonicNow,
				unixNow);
			const auto rejectedAfter = protocol.Stats().packetsRejected;
			if (protocol.TakePlayerActivity(event.connection_id)) {
				state.lastActivityAtUnixSeconds = unixNow;
			}

			if (playerStateCandidate && rejectedAfter == rejectedBefore && persistence != nullptr) {
				try {
					(void)persistence->QueueAcceptedPlayerState(event.connection_id, *playerStateCandidate);
				} catch (const std::exception& exception) {
					std::cerr << "Failed to persist accepted playerState for connection " << event.connection_id << ": " << exception.what() << '\n';
				}
			}

			if (!state.active && FramesContainType(frames, event.connection_id, "sessionReady")) {
				state.active = true;
				activePlayers.fetch_add(1, std::memory_order_relaxed);
				state.lastActivityAtUnixSeconds = unixNow;
				if (state.admissionTicket != 0) {
					(void)sessionAdmissions.Commit(state.admissionTicket, unixNow);
					state.admissionTicket = 0;
				}
				state.playerId = PlayerIdFromFrames(frames, event.connection_id).value_or(0);
				if (persistence == nullptr && !serverWorldState.empty()) {
					SendWorldStateToConnection(server, event.connection_id, serverWorldState, unixNow);
				}
			}
			sendFramesTracked(frames, monotonicNow, unixNow);
			break;
		}
		case CO_GNS_EVENT_OVERSIZE_MESSAGE:
			disconnectTracked(event.connection_id, "Inbound GNS message exceeded 64 KiB");
			break;
		default:
			break;
		}
	}

	steamIdentityLink.SetApplySettingsHandler({});
	steamIdentityLink.SetServerConfigProvider({});
	serverLiveRelay.FailPending("Server is shutting down.");
	if (steamHostLink) {
		steamHostLink->Stop();
	}
	flushPersistence(true);
	if (directory) {
		directory->Stop();
	}
	discovery.Stop();
	admin.Stop();
	co_gns_server_destroy(server);
	return 0;
}

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
	if (options.maximumMessageBytes > F4T::TransportLimits::DefaultMaximumMessageBytes) {
		std::cerr << "max_message_bytes " << options.maximumMessageBytes << " is above the " << F4T::TransportLimits::DefaultMaximumMessageBytes << "-byte limit every client accepts; using that limit.\n";
		options.maximumMessageBytes = F4T::TransportLimits::DefaultMaximumMessageBytes;
	}
	if (co_gns_set_maximum_message_bytes(options.maximumMessageBytes) == 0) {
		std::cerr << "Invalid max_message_bytes; refusing to start.\n";
		return 2;
	}
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

	CommonwealthOnline::Server::DeferredReplyRuntime deferredReplies(durableStore.get());
	std::vector<std::pair<std::uint32_t, std::uint32_t>> sessionReadyDelivered;

	nlohmann::json serverWorldState = durableStore ? durableStore->GetServerWorldState() : nlohmann::json::object();
	protocol.RestoreLockedWeatherFromWorldState(serverWorldState);
	F4T::ConnectionPolicy::ConnectAttemptLimiter connectAttempts;
	std::unordered_map<std::uint32_t, RuntimeConnectionState> runtimeConnections;
	CommonwealthOnline::Server::LiveSettingsDelivery liveSettingsDelivery;
	F4T::Admission::AdmissionRegistry sessionAdmissions{ options.maximumPlayers };
	std::vector<std::uint8_t> payload(512u * 1024u);
	double nextMaintenanceMonotonic = MonotonicSeconds() + 0.5;

	AdmissionContext admission{
		.bans = banStore.get(),
		.attempts = std::addressof(connectAttempts),
		.connections = std::addressof(runtimeConnections),
		.maximumPlayers = options.maximumPlayers,
		.appealLink = options.appealLink,
		.steamLink = std::addressof(steamIdentityLink)
	};
	(void)co_gns_server_set_admission_callback(server, &AdmitConnection, std::addressof(admission));
	(void)co_gns_server_set_address_admission_callback(server, &AdmitAddressConnection, std::addressof(admission));

	auto eraseRuntimeConnection = [&](std::uint32_t a_connectionId) {
		liveSettingsDelivery.Erase(a_connectionId);
		const auto found = runtimeConnections.find(a_connectionId);
		if (found == runtimeConnections.end()) {
			return;
		}
		if (found->second.admissionTicket != 0) (void)sessionAdmissions.Cancel(found->second.admissionTicket);
		if (found->second.active) {
			activePlayers.fetch_sub(1, std::memory_order_relaxed);
			if (directory) directory->NotifyPlayersChanged();
		}
		runtimeConnections.erase(found);
	};
	auto sendWorldStateToNewHost = [&](std::optional<std::uint32_t> a_previousHost, double a_unixTimeSeconds) {
		const auto host = protocol.WorldStateHostConnectionId();
		if (host && host != a_previousHost) SendWorldStateToConnection(server, *host, serverWorldState, a_unixTimeSeconds);
	};
	auto disconnectTracked = [&](std::uint32_t a_connectionId, const char* a_reason) {
		deferredReplies.Erase(a_connectionId);
		LogDisconnect(runtimeConnections, a_connectionId, "server", a_reason);
		protocol.SettleSurvivalNeeds(a_connectionId, MonotonicSeconds());
		CommonwealthOnline::Server::RuntimeSupport::FlushPersistence(persistence, durableStore.get(), false);
		if (persistence != nullptr) {
			persistence->ConnectionClosed(a_connectionId);
		}
		if (identity != nullptr) {
			identity->ConnectionClosed(a_connectionId);
		}
		co_gns_server_disconnect(server, a_connectionId, 0, a_reason);
		const auto unixNow = UnixTimeSeconds();
		const auto previousHost = protocol.WorldStateHostConnectionId();
		auto frames = protocol.HandleDisconnected(a_connectionId, unixNow);
		sendWorldStateToNewHost(previousHost, unixNow);
		SendFrames(server, protocol, frames, identity, persistence, MonotonicSeconds(), unixNow, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
		eraseRuntimeConnection(a_connectionId);
	};
	CommonwealthOnline::Server::ServerEnforcement enforcement{
		banStore.get(),
		[&](std::uint32_t a_connectionId) -> CommonwealthOnline::Server::ViolationSubject {
			CommonwealthOnline::Server::ViolationSubject subject;
			const auto found = runtimeConnections.find(a_connectionId);
			if (found == runtimeConnections.end()) {
				return subject;
			}
			subject.connected = true;
			const auto& state = found->second;
			const auto address = ConnectionAddressText(state);
			subject.address = F4T::ConnectionPolicy::IsLoopbackHost(address) ? std::string{} : address;
			subject.steamId = state.steamId;
			subject.hostSelf = identity != nullptr && identity->SteamSourceOf(a_connectionId) == CommonwealthOnline::Server::SteamIdSource::HostSelf;
			subject.admin = identity != nullptr && identity->IsAdminConnection(a_connectionId);
			for (const auto& [otherId, other] : runtimeConnections) {
				if (identity != nullptr && identity->IsAdminConnection(otherId) && ConnectionAddressText(other) == address) {
					subject.adminOnAddress = true;
				}
			}
			return subject;
		},
		[&](std::uint32_t a_connectionId, std::string_view a_code, std::string_view a_reason) {
			SendSessionEnded(server, a_connectionId, a_code, a_reason, UnixTimeSeconds());
			disconnectTracked(a_connectionId, "Ended by enforcement");
		},
		options.appealLink,
		[&](std::uint32_t a_connectionId, std::string_view a_text) {
			(void)SendReliableJson(server, a_connectionId, nlohmann::json{
				{ "type", "chatMessage" },
				{ "channel", "global" },
				{ "messageId", protocol.AllocateChatMessageId() },
				{ "fromPlayerId", 0xFFFFFFFFU },
				{ "fromName", "Server" },
				{ "text", std::string{ a_text } },
				{ "serverTime", UnixTimeSeconds() }
			});
		}
	};
	auto sendFramesTracked = [&](
		std::vector<CommonwealthOnline::Server::OutboundFrame>& a_frames,
		double a_monotonicSeconds,
		double a_unixTimeSeconds) {
		std::vector<std::uint32_t> retire;
		auto policy = deferredReplies.Policy(a_monotonicSeconds);
		policy.sessionReadyDelivered = std::addressof(sessionReadyDelivered);
		SendFrames(server, protocol, a_frames, identity, persistence, a_monotonicSeconds, a_unixTimeSeconds, &retire, policy);
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
		.steamLink = &steamIdentityLink,
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
			for (const auto& [id, state] : runtimeConnections) {
				if (state.active) liveSettingsDelivery.Queue(id, a_packet);
			}
		}
	};
	const auto applyWorldMode = [&](std::string_view a_mode) -> AdminActionResult { return CommonwealthOnline::Server::ApplyAdminWorldMode(liveSettingsContext, a_mode); };
	const auto applyLiveSettings = [&](const nlohmann::json& a_document) -> AdminActionResult { return CommonwealthOnline::Server::ApplyServerLiveSettings(liveSettingsContext, a_document); };
	std::uint32_t nextAdminOpId = 1;
	auto sendAdminOp = [&](std::uint32_t a_playerId, nlohmann::json a_packet) {
		std::optional<std::uint32_t> targetConnectionId;
		for (const auto& [connectionId, state] : runtimeConnections) {
			if (state.active && state.playerId == a_playerId) {
				targetConnectionId = connectionId;
				break;
			}
		}
		if (!targetConnectionId) {
			return AdminActionResult{ false, "No connected player with id " + std::to_string(a_playerId), nullptr };
		}
		if (!protocol.HasSessionCapability(*targetConnectionId, "admin-items-v1")) {
			return AdminActionResult{ false, "Player " + std::to_string(a_playerId) + " did not advertise admin-items-v1.", nullptr };
		}
		const auto opId = nextAdminOpId++;
		if (nextAdminOpId == 0) {
			nextAdminOpId = 1;
		}
		if (!protocol.TrackAdminOp(*targetConnectionId, opId)) {
			return AdminActionResult{ false, "Too many outstanding admin operations for this player; wait for results.", nullptr };
		}
		a_packet["opId"] = opId;
		const auto sent = SendReliableJson(server, *targetConnectionId, a_packet);
		std::cout << "Admin op to player " << a_playerId << (sent ? " sent: " : " failed: ") << a_packet.dump() << std::endl;
		if (!sent) {
			protocol.UntrackAdminOp(*targetConnectionId, opId);
			return AdminActionResult{ false, "Failed to send admin operation to player " + std::to_string(a_playerId) + ".", nullptr };
		}
		return AdminActionResult{ true, "Admin operation " + std::to_string(opId) + " sent to player " + std::to_string(a_playerId) + ".", nlohmann::json{ { "opId", opId } } };
	};

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
			auto clients = RuntimeClientsJson(runtimeConnections);
			for (auto& client : clients["clients"]) {
				client["name"] = protocol.PlayerCharacterName(client["player_id"].get<std::uint32_t>());
			}
			return clients;
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
			SendSessionEnded(server, *targetConnectionId, "kicked", CommonwealthOnline::Server::WithAppealLink(reason, options.appealLink), UnixTimeSeconds());
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
			bool refused = false;
			auto frames = protocol.AdminKillPlayer(a_playerId, &refused);
			if (refused) {
				return AdminActionResult{ false, "Death could not be saved yet. Try again.", nullptr };
			}
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
			const auto command = F4T::ServerWorldState::ClassifyWeatherCommand(a_value);
			if (command == F4T::ServerWorldState::WeatherCommand::Invalid) {
				return AdminActionResult{ false, "Invalid weather ID. Use an 8-digit hex form ID (not 00000000), or auto to clear the lock.", nullptr };
			}
			if (command == F4T::ServerWorldState::WeatherCommand::Clear) {
				std::optional<std::string> clearWarning;
				if (durableStore) {
					try {
						durableStore->UpdateServerWorldState(nlohmann::json{ { "weatherConsoleArg", nullptr }, { "weatherFormId", nullptr } });
						clearWarning = durableStore->TakePersistenceWarning();
					} catch (const std::exception& exception) {
						return AdminActionResult{ false, "Failed to persist server weather clear: " + std::string{ exception.what() }, nullptr };
					}
				}
				serverWorldState.erase("weatherConsoleArg");
				serverWorldState.erase("weatherFormId");
				protocol.SetLockedWeatherFormId(std::nullopt);
				return AdminActionResult{
					true,
					AppendPersistenceWarning("Server weather lock cleared; host weather is used again.", std::move(clearWarning)),
					nullptr
				};
			}
			const auto consoleArg = F4T::ServerWorldState::NormalizeWeatherConsoleArg(a_value);
			const auto relayFormId = F4T::ServerWorldState::RelayWeatherFormId(a_value);
			if (!consoleArg || !relayFormId) {
				return AdminActionResult{ false, "Invalid weather ID. Use an 8-digit hex form ID (not 00000000), or auto to clear the lock.", nullptr };
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
			protocol.SetLockedWeatherFormId(*relayFormId);
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
				{ "disabled_entries", policy.disable.size() + policy.noInteract.size() },
				{ "data_version", worldPolicyData.version },
				{ "game", worldPolicyData.game },
				{ "data_path", F4T::Utf8Path::Encode(worldPolicyPath) },
				{ "available_modes", nlohmann::json::array({
					std::string{ F4T::WorldPolicy::ModeName(F4T::WorldPolicy::Mode::CoopStory) },
					std::string{ F4T::WorldPolicy::ModeName(F4T::WorldPolicy::Mode::FreeRoam) } }) }
			};
		},
		.setWorldMode = applyWorldMode,
		.mapPublic = [&](std::optional<bool> a_enabled) { return CommonwealthOnline::Server::ApplyAdminMapPublic(protocol, a_enabled); },
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
			auto frames = protocol.SetWorldPolicy(CommonwealthOnline::Server::DeriveServerWorldPolicy(
				worldPolicyData, worldMode, options.doorsShared, F4T::WorldPolicy::ServerDefaultVats(), options.questOverrides));
			sendFramesTracked(frames, MonotonicSeconds(), UnixTimeSeconds());
			return AdminActionResult{
				true,
				"World policy data reloaded.",
				nlohmann::json{
					{ "data_version", worldPolicyData.version },
					{ "game", worldPolicyData.game },
					{ "entries", worldPolicyData.EntryCount() },
					{ "disabled_entries", protocol.CurrentWorldPolicy().disable.size() + protocol.CurrentWorldPolicy().noInteract.size() }
				}
			};
		},
		.raidSettings = [&] { return CommonwealthOnline::Server::RaidSettingsJson(protocol.WorkshopRaidConfig()); },
		.setRaidSettings = [&](const nlohmann::json& a_patch) { return CommonwealthOnline::Server::ApplyAdminRaidSettings(liveSettingsContext, a_patch); },
		.factions = [&] { return protocol.AdminListFactions(); },
		.disbandFaction = [&](std::string_view a_id) { return CommonwealthOnline::Server::RunFactionAdminAction(protocol, CommonwealthOnline::Server::FactionAdminOp::Disband, a_id, {}, UnixTimeSeconds(), sendFactionFrames); },
		.promoteFaction = [&](std::string_view a_id, std::string_view a_target) { return CommonwealthOnline::Server::RunFactionAdminAction(protocol, CommonwealthOnline::Server::FactionAdminOp::Promote, a_id, a_target, UnixTimeSeconds(), sendFactionFrames); },
		.kickFaction = [&](std::string_view a_id, std::string_view a_target) { return CommonwealthOnline::Server::RunFactionAdminAction(protocol, CommonwealthOnline::Server::FactionAdminOp::Kick, a_id, a_target, UnixTimeSeconds(), sendFactionFrames); },
		.reports = [&] { return protocol.AdminListReports(); },
		.spawnObject = [&](std::uint32_t a_playerId, std::string_view a_itemId, std::uint32_t a_count) {
			return sendAdminOp(a_playerId, nlohmann::json{
				{ "type", "adminItemOp" },
				{ "op", "spawn_object" },
				{ "itemId", std::string{ a_itemId } },
				{ "count", a_count },
				{ "serverTime", UnixTimeSeconds() }
			});
		},
		.giveItem = [&](std::uint32_t a_playerId, std::string_view a_itemId, std::uint32_t a_count) {
			return sendAdminOp(a_playerId, nlohmann::json{
				{ "type", "adminItemOp" },
				{ "op", "give_item" },
				{ "itemId", std::string{ a_itemId } },
				{ "count", a_count },
				{ "serverTime", UnixTimeSeconds() }
			});
		},
		.takeItem = [&](std::uint32_t a_playerId, std::string_view a_itemId, std::uint32_t a_count) {
			return sendAdminOp(a_playerId, nlohmann::json{
				{ "type", "adminItemOp" },
				{ "op", "take_item" },
				{ "itemId", std::string{ a_itemId } },
				{ "count", a_count },
				{ "serverTime", UnixTimeSeconds() }
			});
		},
		.requestInventory = [&](std::uint32_t a_playerId) {
			return sendAdminOp(a_playerId, nlohmann::json{ { "type", "adminInventoryRequest" } });
		},
		.getInventory = [&](std::uint32_t a_playerId) {
			const auto report = protocol.LatestInventoryReport(a_playerId);
			if (!report) {
				return AdminActionResult{ false, "No inventory report yet; run request_inventory first.", nullptr };
			}
			return AdminActionResult{
				true,
				"Inventory report for player " + std::to_string(a_playerId) + ".",
				nlohmann::json{
					{ "playerId", a_playerId },
					{ "opId", report->opId },
					{ "ageSeconds", UnixTimeSeconds() - report->receivedAtUnixSeconds },
					{ "truncated", report->truncated },
					{ "items", report->items }
				}
			};
		},
		.getOpResult = [&](std::uint32_t a_playerId) {
			protocol.ExpireAdminOpsFor(a_playerId);
			const auto result = protocol.LatestAdminItemResult(a_playerId);
			if (!result) {
				return AdminActionResult{ false, "No result yet.", nullptr };
			}
			if (result->unknown) {
				return AdminActionResult{
					true,
					"Admin operation " + std::to_string(result->opId) + " outcome unknown (no reply from the player within "
						+ std::to_string(CommonwealthOnline::Server::AdminOpTimeout.count()) + " seconds).",
					nlohmann::json{ { "playerId", a_playerId }, { "opId", result->opId }, { "outcome", "unknown" } }
				};
			}
			return AdminActionResult{
				true,
				"Last admin result for player " + std::to_string(a_playerId) + ".",
				nlohmann::json{
					{ "playerId", a_playerId },
					{ "opId", result->opId },
					{ "ok", result->ok },
					{ "applied", result->applied },
					{ "error", result->error }
				}
			};
		}
	};
	#include "ServerRuntimeAdminHooks.inc"
	adminHooks.refundLost = [&](std::string_view a_profile, std::string_view a_character, std::string_view a_itemKey,
		std::uint32_t a_count, std::string_view a_reference, const nlohmann::json& a_metadata) -> AdminActionResult {
		if (!durableStore) return { false, "A durable store is required.", nullptr };
		const auto profile = std::string{ a_profile };
		const auto character = std::string{ a_character };
		const auto itemKey = std::string{ a_itemKey };
		const auto reference = std::string{ a_reference };
		if (!F4T::DeathIntent::ValidCharacter(character)) return { false, "characterId must be quest- followed by 32 lowercase hex characters.", nullptr };
		if (!F4T::InventoryContract::ParseItemKey(itemKey)) return { false, "itemKey must be plugin:localFormId (8 hex digits).", nullptr };
		if (reference.empty() || reference.size() > 32) return { false, "reference must be 1-32 characters.", nullptr };
		for (const auto ch : reference) {
			if (ch == '-' || ch == '_' || ch == '.') continue;
			if (ch >= '0' && ch <= '9') continue;
			if (ch >= 'a' && ch <= 'z') continue;
			if (ch >= 'A' && ch <= 'Z') continue;
			return { false, "reference may only contain letters, digits, '.', '-' and '_'.", nullptr };
		}
		const auto slot = F4T::CharacterSlots::ParseSlotKey(profile);
		if (!slot) return { false, "profileId must be an account#slot key.", nullptr };
		if (!durableStore->GetProfileSecret(slot->first)) return { false, "The profile is unknown.", nullptr };
		const auto deathId = F4T::ContentManifest::Sha256Hex(profile + ":" + character + ":" + reference).substr(0, 32);
		const auto operationId = F4T::ContentManifest::Sha256Hex(profile + ":" + reference).substr(0, 16);
		try {
			if (!durableStore->QueueDeathBagRefundGrant(profile, character, deathId, operationId, itemKey, a_count, a_metadata)) {
				return { false, "The refund was rejected: the reference is already used with different contents, or the profile inventory cannot accept it.", nullptr };
			}
		} catch (const std::exception& exception) {
			return { false, "Failed to persist the refund: " + std::string{ exception.what() }, nullptr };
		}
		return { true, "Refund queued; it is delivered when the player next resumes.",
			nlohmann::json{ { "profileId", profile }, { "characterId", character }, { "itemKey", itemKey },
				{ "count", a_count }, { "reference", reference }, { "deathId", deathId }, { "operationId", operationId } } };
	};
	adminHooks.deathBagArchive = [&](std::uint32_t a_offset) -> nlohmann::json {
		return durableStore ? durableStore->ListArchivedDeathBags(a_offset) : nlohmann::json{ { "total", 0u }, { "bags", nlohmann::json::array() } };
	};
	adminHooks.deathBagArchiveRecover = [&](std::string_view a_deathId) -> AdminActionResult {
		if (!durableStore) return { false, "A durable store is required.", nullptr };
		const auto deathId = std::string{ a_deathId };
		if (!F4T::DeathIntent::ValidId(deathId)) return { false, "deathId must be 32 lowercase hex characters.", nullptr };
		try {
			if (!durableStore->RecoverArchivedDeathBag(deathId)) {
				return { false, "The archived death bag cannot be recovered now: it is unknown, its owner's slot no longer holds the character, an earlier recovery is still pending, the pending grant limit is reached, or the bag is unverified (placeFailedUnverified or parkedUnverified); resolve an unverified bag with death_bag_archive_resolve first.", nullptr };
			}
		} catch (const std::exception& exception) {
			return { false, "Failed to persist the recovery: " + std::string{ exception.what() }, nullptr };
		}
		return { true, "Archived death bag queued; its items are delivered when the player next resumes. If death_bag_archive_list still lists this bag, the recovery was partial: run it again after the player acks the queued grants. If it is listed as placeFailedUnverified or parkedUnverified, resolve it with death_bag_archive_resolve first.", nlohmann::json{ { "deathId", deathId } } };
	};
	adminHooks.deathBagArchiveResolve = [&](std::string_view a_deathId, bool a_noneReturned) -> AdminActionResult {
		if (!durableStore) return { false, "A durable store is required.", nullptr };
		const auto deathId = std::string{ a_deathId };
		if (!F4T::DeathIntent::ValidId(deathId)) return { false, "deathId must be 32 lowercase hex characters.", nullptr };
		try {
			if (!durableStore->ResolveUnverifiedDeathBag(deathId, a_noneReturned ? F4T::DeathBag::Resolution::Full : F4T::DeathBag::Resolution::None)) {
				return { false, "The death bag is unknown or is not awaiting an unverified-remainder decision.", nullptr };
			}
		} catch (const std::exception& exception) {
			return { false, "Failed to persist the resolution: " + std::string{ exception.what() }, nullptr };
		}
		return { true, a_noneReturned ? "Marked as nothing returned; death_bag_archive_recover can now queue it." : "Marked as fully returned; the archived bag was dropped.", nlohmann::json{ { "deathId", deathId } } };
	};

	CommonwealthOnline::Server::InstallAdminRosterHooks(adminHooks, adminRoster, [&](std::uint32_t a_playerId) -> std::optional<std::string> { for (const auto& [id, state] : runtimeConnections) if (state.active && state.playerId == a_playerId && !state.profileId.empty()) return state.profileId; return std::nullopt; }, [&](std::string_view a_profileId) { auto frames = protocol.AdminStateFramesForProfile(a_profileId, UnixTimeSeconds()); sendFramesTracked(frames, MonotonicSeconds(), UnixTimeSeconds()); });
	CommonwealthOnline::Server::ServerLiveRelay serverLiveRelay;
#include "ServerLiveSettingsLink.inc"
	CommonwealthOnline::Server::Scripting::ScriptHostBinding scriptHost{ protocol, serverWorldState, [&](std::vector<CommonwealthOnline::Server::OutboundFrame>& a_frames) { sendFramesTracked(a_frames, MonotonicSeconds(), UnixTimeSeconds()); } };
	CommonwealthOnline::Server::Scripting::ResourceManager scripts{ baseDirectory / "resources", {}, {}, scriptHost.Installer() };
	CommonwealthOnline::Server::Scripting::BindAdminHooks(scripts, adminHooks);
	AdminCommandDispatcher dispatcher(CommonwealthOnline::Server::WithQuestAdminHooks(std::move(adminHooks), protocol, persistence, runtimeConnections, sendQuestFrames, UnixTimeSeconds));
	const auto flushPersistence = [&](bool a_awaitCommit) { CommonwealthOnline::Server::RuntimeSupport::FlushPersistence(persistence, durableStore.get(), a_awaitCommit); };
	const auto activateSessions = [&](const std::vector<std::pair<std::uint32_t, std::uint32_t>>& a_delivered) {
		for (const auto& [connectionId, playerId] : a_delivered) {
			const auto found = runtimeConnections.find(connectionId);
			if (found == runtimeConnections.end() || found->second.active) continue;
			auto& sessionState = found->second;
			sessionState.active = true;
			sessionState.playerId = playerId;
			sessionState.steamHost = identity != nullptr && identity->SteamSourceOf(connectionId) == CommonwealthOnline::Server::SteamIdSource::HostSelf;
			sessionState.lastActivityAtMonotonicSeconds = MonotonicSeconds();
			activePlayers.fetch_add(1, std::memory_order_relaxed);
			if (directory) directory->NotifyPlayersChanged();
			if (sessionState.admissionTicket != 0) { (void)sessionAdmissions.Commit(sessionState.admissionTicket, UnixTimeSeconds()); sessionState.admissionTicket = 0; }
		}
	};
	const auto deliverToProtocol = [&](std::uint32_t a_connectionId, auto& a_state, std::span<const std::uint8_t> a_message, double a_monotonicSeconds, double a_unixTimeSeconds, std::optional<nlohmann::json>* a_parsed) {
		if (!a_state.active) {
			const auto type = a_parsed != nullptr ? (*a_parsed ? PacketTypeOf(**a_parsed) : std::nullopt) : RawPacketType(a_message);
			if (type && *type == "hello" &&
				!F4T::ConnectionPolicy::CanActivate(protocol.ActiveCount(), options.maximumPlayers)) {
				constexpr std::string_view reason = "Server is full.";
				LogRefusal("hello", ConnectionAddressText(a_state), "server_full", reason);
				SendSessionEnded(server, a_connectionId, "server_full", reason, a_unixTimeSeconds);
				disconnectTracked(a_connectionId, reason.data());
				return;
			}
		}

		const auto playerStateCandidate = CaptureAcceptedPlayerState(persistence, a_connectionId, a_message, a_parsed);
		deferredReplies.BeginMessage(a_connectionId);
		const auto rejectedBefore = protocol.Stats().packetsRejected;
		const auto adminReceivedBefore = protocol.Stats().adminItemResultsReceived;
		const auto adminRejectedBefore = protocol.Stats().adminItemResultsRejected;
		auto frames = protocol.HandleMessage(
			a_connectionId,
			a_message,
			a_monotonicSeconds,
			a_unixTimeSeconds,
			a_parsed);
		const auto rejectedAfter = protocol.Stats().packetsRejected;
		if (protocol.Stats().adminItemResultsReceived != adminReceivedBefore) {
			const auto latest = protocol.LatestAdminItemResult(a_state.playerId);
			if (!latest || protocol.Stats().adminItemResultsRejected != adminRejectedBefore) {
				std::cout << "Admin item result rejected from connection " << a_connectionId << std::endl;
			} else {
				std::cout << "Admin item result player=" << a_state.playerId << " opId=" << latest->opId
					<< " ok=" << (latest->ok ? "true" : "false") << " applied=" << latest->applied
					<< " error=\"" << latest->error << "\"" << std::endl;
			}
		}
		if (protocol.TakePlayerActivity(a_connectionId)) {
			a_state.lastActivityAtMonotonicSeconds = a_monotonicSeconds;
		}
		QueueAcceptedPlayerState(persistence, a_connectionId, playerStateCandidate, rejectedAfter == rejectedBefore);
		if (!a_state.active && persistence == nullptr && !serverWorldState.empty() &&
			FramesContainType(frames, a_connectionId, "sessionReady")) {
			SendWorldStateToConnection(server, a_connectionId, serverWorldState, a_unixTimeSeconds);
		}
		sendFramesTracked(frames, a_monotonicSeconds, a_unixTimeSeconds);
		deferredReplies.EndMessage();
	};
	CommonwealthOnline::Server::StartModpackFileCatalog(options);
	int gnsPollFailures = 0;
	int exitCode = 0;
	while (g_running != 0) {
		LogLoopIteration();
		serverLiveRelay.Drain(applyLiveSettings);
		liveSettingsDelivery.Flush([&](std::uint32_t a_connectionId, const nlohmann::json& a_packet) {
			return SendReliableJson(server, a_connectionId, a_packet);
		});
		AdminCommand adminCommand;
		while (admin.TryPop(adminCommand)) {
			// constraint: the pop claimed this ticket, so a timed-out caller gets this result.
			(void)admin.Complete(adminCommand.ticket, dispatcher.Dispatch(adminCommand.request));
		}
		const auto loopMonotonic = MonotonicSeconds(); CommonwealthOnline::Server::DrainAdminMenuActions(protocol, dispatcher, [&](auto& a_frames) { sendFramesTracked(a_frames, MonotonicSeconds(), UnixTimeSeconds()); });
		%:include "ServerMaintenance.inc"
		enforcement.ReportViolations(protocol.TakeViolations());
		scripts.Tick(loopMonotonic);
		deferredReplies.Flush(server, protocol, identity, persistence, MonotonicSeconds(), UnixTimeSeconds(), disconnectTracked, sessionReadyDelivered);
		activateSessions(sessionReadyDelivered);
		sessionReadyDelivered.clear();
		co_gns_event event{};
		const auto result = CommonwealthOnline::Server::RuntimeSupport::TimedLoopPoll([&] { return co_gns_server_poll(server, &event, payload.data(), static_cast<std::uint32_t>(payload.size())); });
		if (result == -2) {
			// constraint: -2 drops one undeliverable event and advances the queue, so it must not count as a failure.
			CommonwealthOnline::Server::RuntimeSupport::LogLoopIterationEnd();
			continue;
		}
		if (result < 0) {
			++gnsPollFailures;
			std::cerr << "GNS poll failed with result " << result << " (" << gnsPollFailures << " in a row)\n";
			CommonwealthOnline::Server::RuntimeSupport::LogLoopIterationEnd();
			if (CommonwealthOnline::Server::GnsPollFailuresExhausted(gnsPollFailures)) {
				exitCode = CommonwealthOnline::Server::kGnsPollRestartExitCode;
				break;
			}
			std::this_thread::sleep_for(CommonwealthOnline::Server::GnsPollRetryBackoff(gnsPollFailures));
			continue;
		}
		gnsPollFailures = 0;
		if (result == 0) {
			CommonwealthOnline::Server::RuntimeSupport::LogLoopIterationEnd();
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
					SendSessionEnded(server, event.connection_id, "banned", CommonwealthOnline::Server::WithAppealLink(ban->reason, options.appealLink), unixNow);
					co_gns_server_disconnect(server, event.connection_id, 0, "Banned address");
					break;
				}
			}

			const auto admissionDecision = sessionAdmissions.Begin(
				F4T::Admission::ClaimedIdentity{
					.ip = remoteKey.value_or(std::string{ "unknown" })
				},
				unixNow);
			if (!admissionDecision.admitted) {
				LogRefusal("admission", remoteAddress, admissionDecision.code, admissionDecision.reason);
				SendSessionEnded(server, event.connection_id, admissionDecision.code, admissionDecision.reason, unixNow);
				co_gns_server_disconnect(server, event.connection_id, 0, admissionDecision.reason.c_str());
				break;
			}

			liveSettingsDelivery.Erase(event.connection_id);
			runtimeConnections[event.connection_id] = RuntimeConnectionState{
				.connectedAtUnixSeconds = unixNow,
				.lastPacketAtUnixSeconds = std::nullopt,
				.remoteIpv4 = endpoint.ipv4,
				.remotePort = endpoint.port,
				.remoteAddress = remoteAddress,
				.playerId = 0,
				.active = false,
				.admissionTicket = admissionDecision.ticketId,
				.rate = F4T::PacketRateLimit::Initialize(monotonicNow),
				.voiceRate = F4T::PacketRateLimit::Initialize(monotonicNow),
				.wireRate = F4T::PacketRateLimit::Initialize(monotonicNow),
				.joinedAtMonotonicSeconds = monotonicNow
			};
			auto frames = protocol.HandleConnected(event.connection_id, unixNow);
			sendFramesTracked(frames, monotonicNow, unixNow);
			break;
		}
		case CO_GNS_EVENT_DISCONNECTED: {
			LogDisconnect(runtimeConnections, event.connection_id, "peer or network", event.debug, event.reason);
			if (!runtimeConnections.contains(event.connection_id) && admission.reservedPending > 0) {
				--admission.reservedPending;
			}
			protocol.SettleSurvivalNeeds(event.connection_id, MonotonicSeconds());
			CommonwealthOnline::Server::RuntimeSupport::FlushPersistence(persistence, durableStore.get(), false);
			if (persistence != nullptr) {
				persistence->ConnectionClosed(event.connection_id);
			}
			if (identity != nullptr) {
				identity->ConnectionClosed(event.connection_id);
			}
			CommonwealthOnline::Server::ForgetManifestChunkDelivery(server, event.connection_id);
			CommonwealthOnline::Server::ForgetModpackFileDelivery(server, event.connection_id);
			deferredReplies.Erase(event.connection_id);
			eraseRuntimeConnection(event.connection_id);
			const auto unixNow = UnixTimeSeconds();
			const auto previousHost = protocol.WorldStateHostConnectionId();
			auto frames = protocol.HandleDisconnected(event.connection_id, unixNow, event.peer_closed != 0);
			sendWorldStateToNewHost(previousHost, unixNow);
			sendFramesTracked(frames, MonotonicSeconds(), unixNow);
			break;
		}
		case CO_GNS_EVENT_MESSAGE: {
		#include "ServerRuntimeMessage.inc"
		}
		case CO_GNS_EVENT_OVERSIZE_MESSAGE:
			disconnectTracked(event.connection_id, "Inbound GNS message exceeded maximum message size");
			break;
		default:
			break;
		}
		enforcement.ReportViolations(protocol.TakeViolations());
		activateSessions(sessionReadyDelivered);
		sessionReadyDelivered.clear();
		CommonwealthOnline::Server::RuntimeSupport::LogLoopIterationEnd();
	}

	steamIdentityLink.SetApplySettingsHandler({});
	steamIdentityLink.SetServerConfigProvider({});
	serverLiveRelay.FailPending("Server is shutting down.");
	if (steamHostLink) {
		steamHostLink->Stop();
	}
	(void)protocol.FlushWorkshopPersistence(UnixTimeSeconds(), 0.0);
	flushPersistence(true);
	if (durableStore) { (void)durableStore->FlushForShutdown(); }
	deferredReplies.Flush(server, protocol, identity, persistence, MonotonicSeconds(), UnixTimeSeconds(), disconnectTracked, sessionReadyDelivered);
	deferredReplies.AbandonAll(protocol);
	if (directory) {
		directory->Stop();
	}
	discovery.Stop();
	admin.Stop();
	co_gns_server_destroy(server);
	return exitCode;
}

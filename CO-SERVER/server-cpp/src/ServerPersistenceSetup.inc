	std::string saveAttestationKey;
	try {
		const auto saveAttestationKeyPath = baseDirectory / ".save-attestation-key";
		const bool createdKey = !std::filesystem::exists(saveAttestationKeyPath);
		saveAttestationKey = AdminTokenStore::LoadOrCreateHexKey(
			saveAttestationKeyPath,
			[&identityTokens] { return identityTokens.CreateRandomHex(32); });
		if (createdKey) {
			std::cout << "created new save attestation key: " << std::filesystem::absolute(saveAttestationKeyPath).string() << '\n';
		}
	} catch (const std::exception& exception) {
		std::cerr << "Failed to load or create save attestation key: " << exception.what() << '\n';
		co_gns_server_destroy(server);
		return 2;
	}
	const auto profileSecretDeriver = [&identityTokens, &saveAttestationKey](std::string_view a_profileId) {
		return identityTokens.HmacSha256Hex(saveAttestationKey, "profile-secret:" + std::string{ a_profileId })
			.substr(0, F4T::DurableState::ProfileSecretLength);
	};
	std::unique_ptr<CommonwealthOnline::Server::SaveAttestationAuthority> saveAuthority;
	if (durableStore) {
		saveAuthority = std::make_unique<CommonwealthOnline::Server::DurableSaveAttestationAuthority>(*durableStore, profileSecretDeriver);
	} else {
		saveAuthority = std::make_unique<CommonwealthOnline::Server::MemorySaveAttestationAuthority>(profileSecretDeriver);
	}

	CommonwealthOnline::Server::SteamIdentityLink steamIdentityLink;
	CommonwealthOnline::Server::ApplySteamHostPolicy(steamIdentityLink, options.steamFriendsOnly, options.serverPassword.has_value());
	std::optional<AdminControlServer> steamHostLink;
	CommonwealthOnline::Server::SteamHostLinkPublication steamHostLinkPublication;
	if (const auto linkDirectory = F4T::SteamHostLink::Directory()) {
		std::string linkError;
		try {
			F4T::SteamHostLink::RemoveLinkFiles(*linkDirectory, options.port);
			steamHostLink.emplace(
				std::uint16_t{ 0 },
				AdminTokenStore::LoadOrCreate(F4T::SteamHostLink::TokenPath(*linkDirectory, options.port), [&identityTokens] { return identityTokens.CreateToken(); }),
				AdminControlServer::TimeoutInstaller{},
				[&steamIdentityLink](const nlohmann::json& a_request) { return steamIdentityLink.HandleRequest(a_request); });
			if (!steamHostLink->Start(linkError) ||
				!steamHostLinkPublication.Publish(
					F4T::SteamHostLink::PortPath(*linkDirectory, options.port),
					F4T::SteamHostLink::TokenPath(*linkDirectory, options.port),
					steamHostLink->LocalPort(),
					linkError)) {
				steamHostLink.reset();
			}
		} catch (const std::exception& exception) {
			linkError = exception.what();
			steamHostLink.reset();
		}
		if (!steamHostLink) {
			std::cerr << "Steam host identity link unavailable; Steam guests join unverified: " << linkError << '\n';
		}
	}

	std::unique_ptr<CommonwealthOnline::Server::SteamWebApiVerifier> steamWebApi;
	if (options.requireV2Identity) {
		std::string keyError;
		const auto steamKey = CommonwealthOnline::Server::LoadSteamWebApiKey(baseDirectory, keyError);
		if (!steamKey) {
			if (keyError.empty()) {
				std::cout << "Steam ID verification: OFF for direct connections (no Steam Web API key). "
				          << "To turn it on, open a command prompt in this folder (type cmd in the Explorer address bar), then run: CommonwealthOnline.Server.exe --setup-steam-key" << std::endl;
			} else {
				std::cerr << "Steam ID verification: OFF, " << keyError << ". Re-run --setup-steam-key.\n";
			}
		} else {
			auto steamSender = CommonwealthOnline::Server::CreatePlatformDirectorySender();
			const auto check = CommonwealthOnline::Server::CheckSteamWebApiKey(steamKey->value, steamSender);
			if (check.outcome == CommonwealthOnline::Server::SteamKeyCheck::Rejected) {
				std::cerr << "Steam ID verification: OFF, Steam rejected the key from " << steamKey->source << " (" << check.detail
				          << "). Re-run --setup-steam-key.\n";
			} else {
				steamWebApi = std::make_unique<CommonwealthOnline::Server::SteamWebApiVerifier>(steamKey->value, std::move(steamSender));
				std::cout << "Steam ID verification: ON (key from " << steamKey->source
				          << (check.outcome == CommonwealthOnline::Server::SteamKeyCheck::Accepted ? ", accepted by Steam)" : ", not checked: " + check.detail + ")")
				          << std::endl;
			}
		}
	}

	std::optional<CommonwealthOnline::Server::IdentityGate> identityGate;
	if (options.requireV2Identity) {
		identityGate.emplace(
			CommonwealthOnline::Server::Configuration::IdentityRequirements(options),
			identityTokens,
			durableStore ? static_cast<F4T::Identity::ResumeCredentialStore*>(durableStore.get()) : nullptr,
			[&](std::string_view a_profileId) -> std::optional<std::string> {
				if (const auto ban = banStore->GetProfile(a_profileId)) return ban->reason;
				return std::nullopt;
			},
			saveAuthority.get(),
			[&options](std::string_view a_profileId) -> bool {
				for (const auto& admin : options.adminProfiles) {
					if (F4T::SaveProfileLogic::EqualsIgnoreCase(admin, a_profileId)) {
						return true;
					}
				}
				return false;
			},
			CommonwealthOnline::Server::SteamIdentityHooks{
				.resolve = [&server, &steamIdentityLink](std::uint32_t a_connectionId, std::string_view a_claimedSteamId)
					-> std::optional<CommonwealthOnline::Server::VerifiedSteamId> {
					std::uint32_t remoteIpv4 = 0;
					std::uint16_t remotePort = 0;
					if (co_gns_server_remote_ipv4(server, a_connectionId, &remoteIpv4, &remotePort) != 1) return std::nullopt;
					return steamIdentityLink.Resolve(remoteIpv4, remotePort, a_claimedSteamId);
				},
				.remoteIpv4 = MakeRemoteIpv4Lookup(server),
				.banLookup = [&](std::string_view a_steamId) -> std::optional<std::string> {
					if (const auto ban = banStore->GetSteamId(a_steamId)) return ban->reason;
					return std::nullopt;
				},
				.verifyTicket = [&server, &steamWebApi](std::uint32_t a_connectionId, std::string_view a_ticket, std::string_view a_claimedSteamId, std::string_view a_profileId) {
					if (!steamWebApi) return false;
					std::uint32_t remoteIpv4 = 0;
					std::uint16_t remotePort = 0;
					std::uint64_t sourceKey = 0;
					if (co_gns_server_remote_ipv4(server, a_connectionId, &remoteIpv4, &remotePort) == 1) {
						sourceKey = remoteIpv4;
					} else {
						std::array<char, 64> addressBuffer{};
						if (co_gns_server_remote_address(server, a_connectionId, addressBuffer.data(), addressBuffer.size(), &remotePort) == 1) {
							if (const auto prefix = F4T::ConnectionPolicy::Ipv6Prefix64(addressBuffer.data())) {
								sourceKey = F4T::ConnectionPolicy::Ipv6PeerKey(*prefix);
							}
						}
					}
					if (steamWebApi->Submit(a_connectionId, a_ticket, a_claimedSteamId, a_profileId, sourceKey)) return true;
					std::cerr << "Steam ID check queue is full for this address; connection " << a_connectionId << " stays unverified.\n";
					return false;
				},
				.passwordLockout = {},
				.monotonicNow = {}
			});
	}
	auto* identity = identityGate ? std::addressof(*identityGate) : nullptr;

	std::unique_ptr<CommonwealthOnline::Server::PersistenceGate> persistenceGate;
	if (durableStore) {
		persistenceGate = std::make_unique<CommonwealthOnline::Server::PersistenceGate>(*durableStore, options.startingInventory);
	}
	auto* persistence = persistenceGate.get();

	CommonwealthOnline::Server::ProtocolHost protocol{ options.serverName, options.serverDescription, options.adminProfiles, options.friendlyFire, options.factionFriendlyFire };
	protocol.SetChatEnabled(options.enableChat);
	protocol.SetAdminAuditPath((baseDirectory / "admin-audit.log").string());
	protocol.SetAdminAuditClock([] { return CommonwealthOnline::Server::RuntimeSupport::UnixTimeSeconds(); });
	if (durableStore) {
		protocol.SetServerInstanceId(durableStore->ServerInstanceId());
	}
	protocol.SetDownedConfig(F4T::ServerConfiguration::DownedConfig(options));
	protocol.SetFactionLimits(options.factions);
	protocol.SetAdvertisedMaximumPlayers(options.maximumPlayers);
	if (persistence != nullptr && options.startingInventory && !protocol.EnableInventoryTransactions(*persistence)) {
		std::cerr << "Stable inventory authority unavailable; transactions remain closed.\n";
	}
	if (options.enableFactions && persistence == nullptr) {
		std::cerr << "enable_factions is set without durable state; factions will not persist.\n";
	}
	if (options.enableObjectState && !protocol.EnableObjectState()) {
		std::cerr << "Object state sync requested after session activation; it remains off.\n";
	}
	if (options.enableFactions && !protocol.EnableFactions(persistence)) {
		std::cerr << "Player factions unavailable; factions remain closed.\n";
	}
	if (!protocol.EnableSocial(persistence)) {
		std::cerr << "Known-people store requested after session activation; stranger masking starts empty.\n";
	}
	if (!protocol.EnableSettlementAccess(persistence)) {
		std::cerr << "Settlement build lists could not be restored; every build is denied until the stored "
		             "settlementAccess entry is repaired or removed.\n";
	}
	if (!protocol.EnableCharacterSlots(persistence, options.characterSlots, options.characterSlotRetentionDays)) {
		std::cerr << "Character slots unavailable; single-profile mode remains active.\n";
	}
	if (options.enableQuestSync && !protocol.EnableQuestSync(persistence, options.questMode, options.questOverrides)) {
		std::cerr << "Quest sync unavailable; quest reports remain closed.\n";
	}
	if (options.questStoryOrder) protocol.SetQuestStoryOrder(*options.questStoryOrder);
	if (options.enableQuestSync && persistence == nullptr) {
		std::cerr << "Quest state has no durable state store; quest progress resets on restart.\n";
	}

	(void)protocol.ConfigureWorkshop(persistence, baseDirectory / "workshop-zones.json");
	(void)protocol.SetWorldPolicy(F4T::WorldPolicy::DerivePolicy(worldPolicyData, worldMode, options.doorsShared));

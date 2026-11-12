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
			SendSessionEnded(server, a_connectionId, a_code, CommonwealthOnline::Server::WithAppealLink(a_reason, options.appealLink), UnixTimeSeconds());
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
	profileSteamIdLookup = [&steamAdmin](std::string_view a_profileId) { return steamAdmin->ProfileSteamIds(a_profileId); };
	auto recordSteamIdentity = [&](RuntimeConnectionState& a_state, std::uint32_t a_connectionId, std::string_view a_steamId, CommonwealthOnline::Server::SteamIdSource a_source, double a_now) {
		a_state.steamId = std::string{ a_steamId };
		steamAdmin->RecordIdentity(a_connectionId, a_state.profileId, a_state.nickname, a_steamId, a_source, a_now);
	};
	steamAdmin->InstallHooks(adminHooks);
	if (durableStore) CommonwealthOnline::Server::InstallInventoryAuthorityHooks(adminHooks, *durableStore);
	if (durableStore && bootstrap.saveRecoveryApprovals) {
		const auto knownProfile = [&](std::string_view a_profile) {
				const auto slot = F4T::CharacterSlots::ParseSlotKey(a_profile);
				const auto account = slot ? slot->first : std::string{ a_profile };
				if (!durableStore->GetProfileSecret(account)) return false;
				return !slot || slot->second < options.characterSlots;
			};
			const auto disconnectedProfile = [&](std::string_view a_profile) {
				const auto slot = F4T::CharacterSlots::ParseSlotKey(a_profile);
				const auto account = slot ? slot->first : std::string{ a_profile };
				for (const auto& [connection, state] : runtimeConnections) {
					(void)connection;
					if (state.profileId == account || state.profileId == a_profile) return false;
				}
				return true;
			};
			CommonwealthOnline::Server::InstallSaveRecoveryHooks(adminHooks, *bootstrap.saveRecoveryApprovals, knownProfile, disconnectedProfile,
			[&](std::string_view a_profile, std::string_view a_character) -> CommonwealthOnline::Server::RecoveryCharacterMatch {
				using Match = CommonwealthOnline::Server::RecoveryCharacterMatch;
				bool found = false;
				const auto grant = durableStore->GetStartingGrant(a_profile);
				if (grant && grant->contains("recovery") && grant->at("recovery").is_object()) {
					const auto& recovery = grant->at("recovery");
					if (recovery.contains("characterId") && recovery.at("characterId").is_string()) {
						found = true;
						if (!F4T::SaveProfileLogic::CharacterIdsEqual(recovery.at("characterId").get_ref<const std::string&>(), a_character)) return Match::Conflict;
					}
				}
				for (const auto& save : durableStore->GetSaveAttestations(a_profile)) {
					const auto character = F4T::SaveProfileLogic::ExtractCharacterId(save.saveFile);
					if (!character || !F4T::SaveProfileLogic::CharacterIdsEqual(*character, a_character)) return Match::Conflict;
					found = true;
				}
				return found ? Match::Match : Match::Missing;
			});
		CommonwealthOnline::Server::InstallFirstJoinAbandonHook(adminHooks, knownProfile, disconnectedProfile, [&](std::string_view a_account, std::uint32_t a_slot, bool a_withoutSave) {
			const auto frames = protocol.AbandonSlotForAdmin(a_account, a_slot, UnixTimeSeconds(), a_withoutSave);
			if (!frames) return false;
			auto sent = *frames;
			sendFramesTracked(sent, MonotonicSeconds(), UnixTimeSeconds());
			return true;
		});
	}
	adminHooks.startingGrantInspect = [&](const nlohmann::json& a_request) -> AdminActionResult {
		if (!durableStore || !a_request.contains("profileId") || !a_request.at("profileId").is_string()) return { false, "A durable profileId is required.", nullptr };
		const auto profile = a_request.at("profileId").get<std::string>();
		const auto grant = durableStore->GetStartingGrant(profile);
		if (!grant) return { false, "No starting inventory grant exists for that profile.", nullptr };
		return { true, "Starting inventory recovery record.", nlohmann::json{ { "profileId", profile }, { "serverInstanceId", durableStore->ServerInstanceId() }, { "grant", *grant }, { "inventory", durableStore->GetInventory(profile).value_or(nlohmann::json(nullptr)) } } };
	};
	adminHooks.startingGrantResolve = [&](const nlohmann::json& a_request) -> AdminActionResult {
		if (!durableStore) return { false, "Durable persistence is unavailable.", nullptr };
		for (const auto* field : { "profileId", "characterId", "saveFile", "sha256", "note", "confirm" }) {
			if (!a_request.contains(field) || !a_request.at(field).is_string()) return { false, "Recovery requires profileId, characterId, saveFile, sha256, note, confirm and legacy.", nullptr };
		}
		if (!a_request.contains("legacy") || !a_request.at("legacy").is_boolean() || a_request.at("confirm") != "inventory-verified") return { false, "Explicit inventory-verified confirmation and a legacy flag are required.", nullptr };
		const auto profile = a_request.at("profileId").get<std::string>();
		const auto slot = F4T::CharacterSlots::ParseSlotKey(profile);
		const auto account = slot ? slot->first : profile;
		for (const auto& [connection, state] : runtimeConnections) {
			(void)connection;
			if (state.profileId == account || state.profileId == profile) return { false, "Disconnect the profile before resolving its starting inventory.", nullptr };
		}
		try {
			const F4T::SaveProfileLogic::AttestedSave save{ a_request.at("saveFile").get<std::string>(), a_request.at("sha256").get<std::string>(), 0 };
			if (!durableStore->ResolveStartingGrant(profile, a_request.at("characterId").get<std::string>(), save,
				a_request.at("note").get<std::string>(), a_request.at("legacy").get<bool>())) return { false, "Recovery record or save binding did not match; no resolution was recorded.", nullptr };
			return { true, "Manual starting inventory reconciliation recorded.", nlohmann::json{ { "profileId", profile }, { "saveFile", save.saveFile }, { "sha256", save.sha256Hex } } };
		} catch (const std::exception& error) {
			return { false, std::string{ "Starting inventory reconciliation failed: " } + error.what(), nullptr };
		}
	};

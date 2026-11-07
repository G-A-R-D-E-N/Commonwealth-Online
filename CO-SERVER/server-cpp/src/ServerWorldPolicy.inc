		static F4T::WorldPolicy::Policy LegacyWorldPolicyView(F4T::WorldPolicy::Policy a_policy)
		{
			std::set<std::string> listed;
			for (const auto& key : a_policy.disable) {
				listed.insert(F4T::WorldPolicy::FormatFormKey(key));
			}
			for (const auto& key : a_policy.noInteract) {
				if (listed.insert(F4T::WorldPolicy::FormatFormKey(key)).second) {
					a_policy.disable.push_back(key);
				}
			}
			return a_policy;
		}

		void AppendWorldPolicyFrames(std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, const Session& a_session) const
		{
			if (a_session.compatibilityCapabilities.Has(F4T::WorldPolicy::kCapabilityNameV2)) {
				for (const auto& message : F4T::WorldPolicy::EncodeMessages(worldPolicy, true)) {
					a_frames.push_back(ReliableFrame(a_connectionId, message));
				}
			} else if (a_session.compatibilityCapabilities.Has(F4T::WorldPolicy::kCapabilityName)) {
				for (const auto& message : F4T::WorldPolicy::EncodeMessages(worldPolicyV1, false)) {
					a_frames.push_back(ReliableFrame(a_connectionId, message));
				}
			}
		}

		std::vector<OutboundFrame> SetWorldPolicy(F4T::WorldPolicy::Policy a_policy)
		{
			a_policy.version = worldPolicy.version + 1;
			worldPolicy = std::move(a_policy);
			worldPolicyV1 = LegacyWorldPolicyView(worldPolicy);
			std::vector<OutboundFrame> frames;
			for (const auto& [connectionId, recipient] : sessions) {
				if (recipient.active) {
					AppendWorldPolicyFrames(frames, connectionId, recipient);
				}
			}
			return frames;
		}

		std::vector<OutboundFrame> WorldPolicyFrames(std::uint32_t a_connectionId) const
		{
			std::vector<OutboundFrame> frames;
			const auto found = sessions.find(a_connectionId);
			if (found != sessions.end()) {
				AppendWorldPolicyFrames(frames, a_connectionId, found->second);
			}
			return frames;
		}
		Json BuildCapabilities() const
		{
			Json capabilities = BaseWelcomeCapabilities(componentDefinitions.has_value());
			AppendOptionalCapabilities(capabilities);
			return capabilities;
		}

		Json ServerInfoPacket() const
		{
			return Json{
				{ "type", "capabilities" },
				{ "capabilities", BuildCapabilities() },
				{ "serverName", serverName },
				{ "serverDescription", serverDescription },
				{ "friendlyFire", friendlyFire },
				{ "factionFriendlyFire", factionFriendlyFire },
				{ "survivalMode", survivalMode },
				{ "survivalNeeds", SurvivalNeedsActive() },
				{ "maxPlayers", maximumPlayers }
			};
		}

		void AppendOptionalCapabilities(Json& a_capabilities) const
		{
			a_capabilities.push_back("map-presence-v1");
			if (inventoryAuthority != nullptr) a_capabilities.push_back("player-trade-v1");
			if (inventoryAuthority != nullptr) a_capabilities.push_back("inventory-stable-v1");
			if (factionService.Enabled()) a_capabilities.push_back("factions-v1");
			if (chatService.Enabled()) a_capabilities.push_back("chat-v1");
			if (chatService.Enabled() && factionService.Enabled()) a_capabilities.push_back("faction-chat-v1");
			if (objectStateEnabled) a_capabilities.push_back("object-state-v1");
			if (objectStateEnabled) a_capabilities.push_back("object-state-v2");
			a_capabilities.push_back(std::string{ F4T::ParkedSuit::kCapabilityName });
			if (questsEnabled) {
				a_capabilities.push_back(std::string{ F4T::QuestProtocol::kSyncCapability });
				a_capabilities.push_back(std::string{ F4T::QuestProtocol::kReadyCapability });
				a_capabilities.push_back(std::string{ F4T::QuestProtocol::kFireteamCapability });
			}
			if (workshopRaidConfig.mode == F4T::WorkshopRaids::RaidMode::Windowed) a_capabilities.push_back("settlement-raid-windowed-v1");
		}

		bool EnableFactions(PersistenceGate* a_persistence)
		{
			if (hasEverActivatedSession) return false;
			return factionService.Enable(a_persistence);
		}

		bool SetFactionsEnabled(bool a_enabled, PersistenceGate* a_persistence)
		{
			if (a_enabled) {
				if (factionService.Enabled()) return true;
				return factionService.Enable(a_persistence);
			}
			if (!factionService.Enabled()) return true;
			return factionService.Disable();
		}

		std::vector<OutboundFrame> FactionResyncFrames(double a_unixTimeSeconds)
		{
			if (!factionService.Enabled()) return {};
			std::vector<OutboundFrame> frames;
			const auto peers = FactionPeers();
			for (const auto& [connectionId, session] : sessions) {
				if (!session.active) continue;
				AppendFrames(frames, factionService.ExistingFrames(peers, connectionId, session.profileId, a_unixTimeSeconds));
			}
			AppendFrames(frames, factionService.RosterFrames(peers, a_unixTimeSeconds));
			return frames;
		}

		bool FlushFactionState()
		{
			return factionService.SaveNow();
		}

		bool FlushSocialState()
		{
			return socialGraph.SaveNow();
		}

		void SetObjectStateEnabled(bool a_enabled)
		{
			objectStateEnabled = a_enabled;
		}

		void SetIdentity(std::string a_serverName, std::string a_serverDescription)
		{
			serverName = std::move(a_serverName);
			serverDescription = std::move(a_serverDescription);
		}

		void SetAdvertisedMaximumPlayers(std::size_t a_maximumPlayers)
		{
			maximumPlayers = a_maximumPlayers;
		}

		std::size_t maximumPlayers{ 2 };

		void SetFriendlyFire(bool a_friendlyFire, bool a_factionFriendlyFire)
		{
			friendlyFire = a_friendlyFire;
			factionFriendlyFire = a_factionFriendlyFire;
		}

		void SetSurvivalMode(bool a_enabled)
		{
			if (survivalMode != a_enabled) difficultyResetPending = true;
			survivalMode = a_enabled;
		}

		void SetSurvivalNeeds(bool a_enabled)
		{
			survivalNeeds = a_enabled;
		}
		void SetRequireDifficultyReport(bool a_enabled)
		{
			if (requireDifficultyReport != a_enabled) difficultyResetPending = true;
			requireDifficultyReport = a_enabled;
		}

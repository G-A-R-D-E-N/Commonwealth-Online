		std::vector<OutboundFrame> SetWorldPolicy(F4T::WorldPolicy::Policy a_policy)
		{
			worldPolicy = std::move(a_policy);
			std::vector<OutboundFrame> frames;
			const auto messages = F4T::WorldPolicy::EncodeMessages(worldPolicy);
			for (const auto& [connectionId, recipient] : sessions) {
				if (!recipient.active || !recipient.compatibilityCapabilities.Has(F4T::WorldPolicy::kCapabilityName)) {
					continue;
				}
				for (const auto& message : messages) {
					frames.push_back(ReliableFrame(connectionId, message));
				}
			}
			return frames;
		}

		std::vector<OutboundFrame> WorldPolicyFrames(std::uint32_t a_connectionId) const
		{
			std::vector<OutboundFrame> frames;
			const auto found = sessions.find(a_connectionId);
			if (found == sessions.end() ||
				!found->second.compatibilityCapabilities.Has(F4T::WorldPolicy::kCapabilityName)) {
				return frames;
			}
			for (const auto& message : F4T::WorldPolicy::EncodeMessages(worldPolicy)) {
				frames.push_back(ReliableFrame(a_connectionId, message));
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
				{ "maxPlayers", maximumPlayers }
			};
		}

		void AppendOptionalCapabilities(Json& a_capabilities) const
		{
			if (inventoryAuthority != nullptr) a_capabilities.push_back("inventory-stable-v1");
			if (factionService.Enabled()) a_capabilities.push_back("factions-v1");
			if (chatService.Enabled()) a_capabilities.push_back("chat-v1");
			if (chatService.Enabled() && factionService.Enabled()) a_capabilities.push_back("faction-chat-v1");
			if (objectStateEnabled) a_capabilities.push_back("object-state-v1");
			if (questsEnabled) {
				a_capabilities.push_back(std::string{ F4T::QuestProtocol::kSyncCapability });
				a_capabilities.push_back(std::string{ F4T::QuestProtocol::kReadyCapability });
			}
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

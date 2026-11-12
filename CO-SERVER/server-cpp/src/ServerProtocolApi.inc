	namespace AdminProfiles
	{
		bool Matches(const std::vector<std::string>& a_entries, std::string_view a_profileId)
		{
			if (a_profileId.empty()) {
				return false;
			}
			for (const auto& entry : a_entries) {
				if (std::string_view{ entry } == a_profileId) {
					return true;
				}
			}
			return false;
		}
	}

	ProtocolHost::ProtocolHost(
		std::string a_serverName,
		std::string a_serverDescription,
		std::vector<std::string> a_adminProfiles,
		bool a_friendlyFire,
		bool a_factionFriendlyFire,
		std::string a_playerKeySecret) :
		_impl(std::make_unique<Impl>(
			std::move(a_serverName),
			std::move(a_serverDescription),
			std::move(a_adminProfiles),
			a_friendlyFire,
			a_factionFriendlyFire))
	{
		_impl->playerKeySecret = std::move(a_playerKeySecret);
	}

	ProtocolHost::~ProtocolHost() = default;
	ProtocolHost::ProtocolHost(ProtocolHost&&) noexcept = default;
	ProtocolHost& ProtocolHost::operator=(ProtocolHost&&) noexcept = default;

	std::vector<OutboundFrame> ProtocolHost::HandleConnected(std::uint32_t a_connectionId, double a_unixTimeSeconds)
	{
		return _impl->HandleConnected(a_connectionId, a_unixTimeSeconds);
	}

	std::optional<std::vector<OutboundFrame>> ProtocolHost::AbandonSlotForAdmin(std::string_view a_account, std::uint32_t a_slot, double a_unixTimeSeconds, bool a_withoutSave)
	{
		return _impl->AbandonSlotForAdmin(a_account, a_slot, a_unixTimeSeconds, a_withoutSave);
	}

	std::vector<OutboundFrame> ProtocolHost::HandleDisconnected(std::uint32_t a_connectionId, double a_unixTimeSeconds, bool a_cleanGoodbye)
	{
		return _impl->DisconnectSession(a_connectionId, a_unixTimeSeconds, a_cleanGoodbye);
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::DisconnectSession(std::uint32_t a_connectionId, double a_unixTimeSeconds, bool a_cleanGoodbye)
	{
		std::string profileId;
		std::string socialKey;
		bool wasActive = false;
		if (const auto session = sessions.find(a_connectionId); session != sessions.end()) {
			profileId = session->second.profileId;
			socialKey = std::string{ SessionSocialKeyView(session->second) };
			wasActive = session->second.active;
		}
		auto frames = VoiceDisconnect(voiceService, sessions, a_connectionId, [&] { return HandleDisconnected(a_connectionId, a_unixTimeSeconds, a_cleanGoodbye); });
		if (wasActive) {
			for (auto& frame : FactionDisconnectFrames(profileId, a_unixTimeSeconds)) frames.push_back(std::move(frame));
			for (auto& frame : FriendDisconnectFrames(socialKey, a_unixTimeSeconds)) frames.push_back(std::move(frame));
		}
		return frames;
	}

	std::vector<OutboundFrame> ProtocolHost::HandleMessage(
		std::uint32_t a_connectionId,
		std::span<const std::uint8_t> a_message,
		double a_monotonicSeconds,
		double a_unixTimeSeconds,
		std::optional<nlohmann::json>* a_parsed)
	{
		auto frames = _impl->HandleMessage(a_connectionId, a_message, a_monotonicSeconds, a_unixTimeSeconds, a_parsed);
		for (auto& frame : _impl->FactionNameFrames(a_connectionId, a_monotonicSeconds, a_unixTimeSeconds)) frames.push_back(std::move(frame));
		for (auto& frame : _impl->FriendNameFrames(a_connectionId, a_unixTimeSeconds)) frames.push_back(std::move(frame));
		for (auto& frame : _impl->WorkshopEntryFrames(a_connectionId, a_monotonicSeconds)) frames.push_back(std::move(frame));
		for (auto& frame : _impl->SettlementListFrames(a_connectionId)) frames.push_back(std::move(frame));
		for (auto& frame : _impl->CampGrantFrames(a_connectionId)) frames.push_back(std::move(frame));
		return frames;
	}

	void ProtocolHost::QueueViolation(std::uint32_t a_connectionId, Violation a_violation, std::string_view a_detail)
	{
		_impl->violations.push_back(PendingViolation{ a_connectionId, a_violation, std::string{ a_detail } });
	}

	std::vector<PendingViolation> ProtocolHost::TakeViolations()
	{
		return std::exchange(_impl->violations, {});
	}

	std::vector<OutboundFrame> ProtocolHost::PublishComponentSnapshot(const SnapshotState& a_authoritativeState)
	{
		return _impl->PublishComponentSnapshot(a_authoritativeState);
	}

	const SnapshotState& ProtocolHost::DroppedObjectState() const noexcept
	{
		return _impl->dropState;
	}

	bool ProtocolHost::EnableInventoryTransactions(PersistenceGate& a_persistence)
	{
		return _impl->EnableInventoryTransactions(a_persistence);
	}

	bool ProtocolHost::EnableObjectState()
	{
		return _impl->EnableObjectState();
	}

	bool ProtocolHost::EnableFactions(PersistenceGate* a_persistence)
	{
		return _impl->EnableFactions(a_persistence);
	}

	bool ProtocolHost::EnableSocial(PersistenceGate* a_persistence)
	{
		return _impl->EnableSocial(a_persistence);
	}

	bool ProtocolHost::EnableQuestSync(
		PersistenceGate* a_persistence,
		F4T::QuestProtocol::Mode a_mode,
		std::vector<F4T::QuestProtocol::OverrideEntry> a_overrides)
	{
		return _impl->EnableQuestSync(a_persistence, a_mode, std::move(a_overrides));
	}

	bool ProtocolHost::QuestSyncEnabled() const noexcept
	{
		return _impl->QuestSyncEnabled();
	}

	F4T::QuestProtocol::Mode ProtocolHost::QuestMode() const noexcept
	{
		return _impl->QuestMode();
	}

	nlohmann::json ProtocolHost::QuestStatus() const
	{
		return _impl->QuestStatus();
	}

	bool ProtocolHost::IsQuestGated(const F4T::QuestProtocol::FormKey& a_quest, std::uint16_t a_stage) const
	{
		return _impl->IsQuestGated(a_quest, a_stage);
	}

	bool ProtocolHost::IsQuestSolo(std::string_view a_profileId, const F4T::QuestProtocol::FormKey& a_quest) const
	{
		return _impl->IsQuestSolo(a_profileId, a_quest);
	}

	void ProtocolHost::SetQuestStoryOrder(const F4T::QuestStory::Order& a_order)
	{
		_impl->SetQuestStoryOrder(a_order);
	}

	std::vector<OutboundFrame> ProtocolHost::SetQuestMode(F4T::QuestProtocol::Mode a_mode, double a_unixTimeSeconds)
	{
		return _impl->SetQuestMode(a_mode, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::SetQuestGate(
		const F4T::QuestProtocol::FormKey& a_quest,
		std::uint16_t a_stage,
		bool a_release,
		double a_unixTimeSeconds)
	{
		return _impl->SetQuestGate(a_quest, a_stage, a_release, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::SetQuestSolo(
		std::string_view a_profileId,
		const F4T::QuestProtocol::FormKey& a_quest,
		bool a_solo,
		double a_unixTimeSeconds)
	{
		return _impl->SetQuestSolo(a_profileId, a_quest, a_solo, a_unixTimeSeconds);
	}

	bool ProtocolHost::SetFactionsEnabled(bool a_enabled, PersistenceGate* a_persistence)
	{
		return _impl->SetFactionsEnabled(a_enabled, a_persistence);
	}

	std::vector<OutboundFrame> ProtocolHost::FactionResyncFrames(double a_unixTimeSeconds)
	{
		return _impl->FactionResyncFrames(a_unixTimeSeconds);
	}

	bool ProtocolHost::FlushFactionState()
	{
		return _impl->FlushFactionState();
	}

	bool ProtocolHost::FlushSocialState()
	{
		return _impl->FlushSocialState();
	}

	void ProtocolHost::RetryDirtyPersistence(double a_monotonicSeconds)
	{
		_impl->RetryDirtyPersistence(a_monotonicSeconds);
	}

	void ProtocolHost::SetObjectStateEnabled(bool a_enabled)
	{
		_impl->SetObjectStateEnabled(a_enabled);
	}

	void ProtocolHost::SetIdentity(std::string a_serverName, std::string a_serverDescription)
	{
		_impl->SetIdentity(std::move(a_serverName), std::move(a_serverDescription));
	}

	void ProtocolHost::SetFriendlyFire(bool a_friendlyFire, bool a_factionFriendlyFire)
	{
		_impl->SetFriendlyFire(a_friendlyFire, a_factionFriendlyFire);
	}

	void ProtocolHost::SetSurvivalMode(bool a_enabled)
	{
		_impl->SetSurvivalMode(a_enabled);
	}

	void ProtocolHost::SetSurvivalNeeds(bool a_enabled)
	{
		_impl->SetSurvivalNeeds(a_enabled);
	}

	void ProtocolHost::SetRequireDifficultyReport(bool a_enabled)
	{
		_impl->SetRequireDifficultyReport(a_enabled);
	}

	void ProtocolHost::SetRequiredDifficulty(std::int32_t a_difficulty)
	{
		_impl->SetRequiredDifficulty(a_difficulty);
	}

	void ProtocolHost::SetAdvertisedMaximumPlayers(std::size_t a_maximumPlayers)
	{
		_impl->SetAdvertisedMaximumPlayers(a_maximumPlayers);
	}

	nlohmann::json ProtocolHost::ServerInfoPacket() const
	{
		return _impl->ServerInfoPacket();
	}

	void ProtocolHost::SetDeathBagLootByOthers(bool a_allow)
	{
		_impl->allowDeathBagLootByOthers = a_allow;
	}

	void ProtocolHost::SetDownedConfig(F4T::DownedState::Config a_config)
	{
		_impl->SetDownedConfig(a_config);
	}

	void ProtocolHost::SetFactionLimits(F4T::ServerConfiguration::FactionLimits a_limits)
	{
		_impl->SetFactionLimits(a_limits);
	}

	std::vector<OutboundFrame> ProtocolHost::PollDownedState(double a_monotonicSeconds)
	{
		return _impl->PollDownedState(a_monotonicSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::PollVoice(double a_monotonicSeconds)
	{
		return VoicePoll(_impl->voiceService, _impl->sessions, a_monotonicSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::PollFactionInvites(double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		if (!_impl->factionService.Enabled()) return {};
		return _impl->factionService.PollInviteExpiry(_impl->FactionPeers(), a_monotonicSeconds, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::PollDurablePlayerState(double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		return _impl->PollDurablePlayerState(a_monotonicSeconds, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::PollFriendRequests(double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		return _impl->PollFriendRequests(a_monotonicSeconds, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::PollTrades(double a_monotonicSeconds)
	{
		return _impl->PollTrades(a_monotonicSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::PollFireteamInvites(double a_unixTimeSeconds)
	{
		_impl->fireteamService.NotePeers(_impl->FireteamPeers());
		auto frames = _impl->fireteamService.ExpiryFrames(a_unixTimeSeconds);
		AppendFrames(frames, _impl->RetryFireteamSnapshots(a_unixTimeSeconds));
		return frames;
	}

	std::vector<OutboundFrame> ProtocolHost::PollQuestAssists(double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		return _impl->PollQuestAssists(a_monotonicSeconds, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::PollTeamQuestSync(double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		return _impl->PollTeamQuestSync(a_monotonicSeconds, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::PollPresence(double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		return _impl->PollPresence(a_monotonicSeconds, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::PollNpcLootTakes(double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		return _impl->PollNpcLootTakes(a_monotonicSeconds, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::PollSurvivalNeeds(double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		return _impl->PollSurvivalNeeds(a_monotonicSeconds, a_unixTimeSeconds);
	}

	void ProtocolHost::SettleSurvivalNeeds(std::uint32_t a_connectionId, double a_monotonicSeconds)
	{
		_impl->SettleSurvivalNeeds(a_connectionId, a_monotonicSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::PollDifficulty(double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		return _impl->PollDifficulty(a_monotonicSeconds, a_unixTimeSeconds);
	}

	void ProtocolHost::SetMapPublicPlayers(bool a_enabled)
	{
		_impl->mapPublicPlayers = a_enabled;
	}

	bool ProtocolHost::MapPublicPlayers() const
	{
		return _impl->mapPublicPlayers;
	}

	std::vector<OutboundFrame> ProtocolHost::AdminRevivePlayer(std::uint32_t a_playerId, double a_monotonicSeconds)
	{
		return _impl->AdminRevivePlayer(a_playerId, a_monotonicSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::AdminKillPlayer(std::uint32_t a_playerId, bool* a_refusedOut)
	{
		return _impl->AdminKillPlayer(a_playerId, a_refusedOut);
	}

	nlohmann::json ProtocolHost::AdminListFactions() const
	{
		return _impl->AdminListFactions();
	}

	nlohmann::json ProtocolHost::AdminListReports() const
	{
		return _impl->reportService.ReportsJson();
	}

	std::vector<OutboundFrame> ProtocolHost::AdminDisbandFaction(std::string_view a_factionId, double a_unixTimeSeconds, std::string& a_failure)
	{
		return _impl->AdminDisbandFaction(a_factionId, a_unixTimeSeconds, a_failure);
	}

	std::vector<OutboundFrame> ProtocolHost::AdminPromoteFaction(std::string_view a_factionId, std::string_view a_targetProfileId, double a_unixTimeSeconds, std::string& a_failure)
	{
		return _impl->AdminPromoteFaction(a_factionId, a_targetProfileId, a_unixTimeSeconds, a_failure);
	}

	std::vector<OutboundFrame> ProtocolHost::AdminKickFaction(std::string_view a_factionId, std::string_view a_targetProfileId, double a_unixTimeSeconds, std::string& a_failure)
	{
		return _impl->AdminKickFaction(a_factionId, a_targetProfileId, a_unixTimeSeconds, a_failure);
	}

	void ProtocolHost::FrameDeliveryCompleted(const OutboundFrame& a_frame, bool a_sent)
	{
		_impl->FrameDeliveryCompleted(a_frame, a_sent);
	}

	void ProtocolHost::VoiceSendCompleted(bool a_sent, bool a_backpressure, bool a_dropped)
	{
		_impl->voiceService.RecordSend(a_sent, a_backpressure, a_dropped);
	}

	std::vector<OutboundFrame> ProtocolHost::RetryComponentSnapshots()
	{
		return _impl->RetryComponentSnapshots();
	}

	std::vector<OutboundFrame> ProtocolHost::RetryNpcAuthority(double a_unixTimeSeconds)
	{
		return _impl->RetryNpcAuthority(a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::RetryStateBagFrames(double a_monotonicSeconds)
	{
		return RetryStateBagResync(_impl->sessions, _impl->stateBag, a_monotonicSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::RetryPluginLists()
	{
		return _impl->RetryPluginLists();
	}

	std::vector<OutboundFrame> ProtocolHost::RetryHeldCombatRelays()
	{
		return _impl->RetryHeldCombatRelays();
	}

	bool ProtocolHost::HeldCombatRelayBlocks(const OutboundFrame& a_frame) const
	{
		return _impl->HeldCombatRelayBlocks(a_frame);
	}

	bool ProtocolHost::SetComponentDefinitions(F4T::Replication::FrozenComponentDefinitionTable a_definitions)
	{
		return _impl->SetComponentDefinitions(std::move(a_definitions));
	}

	bool ProtocolHost::SetDirectComponentPublicationContract(DirectComponentPublicationContract a_contract)
	{
		return _impl->SetDirectComponentPublicationContract(std::move(a_contract));
	}

	bool ProtocolHost::RegisterWorkshopBuildZone(F4T::Workshop::BuildZonePolicy a_policy)
	{
		return _impl->RegisterWorkshopBuildZone(std::move(a_policy));
	}

	bool ProtocolHost::EnableWorkshopPersistence(PersistenceGate* a_persistence)
	{
		return _impl->EnableWorkshopPersistence(a_persistence);
	}

	std::vector<OutboundFrame> ProtocolHost::FlushWorkshopPersistence(double a_unixSeconds, double a_monotonicSeconds)
	{
		return _impl->FlushWorkshopPersistence(a_unixSeconds, a_monotonicSeconds);
	}

	std::size_t ProtocolHost::ConfigureWorkshop(PersistenceGate* a_persistence, const std::filesystem::path& a_zonesFile)
	{
		return _impl->ConfigureWorkshop(a_persistence, a_zonesFile);
	}

	void ProtocolHost::LoadSurvivalItems(const std::filesystem::path& a_path)
	{
		_impl->LoadSurvivalItems(a_path);
	}

	void ProtocolHost::ConfigureVendorItemValues(PersistenceGate* a_persistence, const std::filesystem::path& a_itemValuesFile, std::uint32_t a_restockHours)
	{
		_impl->ConfigureVendorItemValues(a_persistence, a_itemValuesFile, a_restockHours);
	}

	void ProtocolHost::SetWorkshopAuthority(F4T::Workshop::WorkshopAuthority::CanBuildAt a_canBuildAt)
	{
		_impl->SetWorkshopAuthority(std::move(a_canBuildAt));
	}

	bool ProtocolHost::EnableSettlementAccess(PersistenceGate* a_persistence)
	{
		return _impl->EnableSettlementAccess(a_persistence);
	}

	void ProtocolHost::EnableParkedSuits(PersistenceGate* a_persistence)
	{
		_impl->EnableParkedSuits(a_persistence);
	}

	void ProtocolHost::SetWorkshopRaidConfig(F4T::WorkshopRaids::RaidConfig a_config)
	{
		_impl->SetWorkshopRaidConfig(a_config);
	}

	F4T::WorkshopRaids::RaidConfig ProtocolHost::WorkshopRaidConfig() const
	{
		return _impl->WorkshopRaidConfig();
	}

	bool ProtocolHost::EnableCharacterSlots(PersistenceGate* a_persistence, std::size_t a_slotCount)
	{
		const bool attached = _impl->characterSlots.Attach(
			a_persistence, CharacterSlotService::Config{ a_slotCount });
		if (attached) _impl->ForgetErasedQuestProfiles();
		return attached;
	}

	std::vector<OutboundFrame> ProtocolHost::SetSettlementOwner(F4T::Workshop::BuildZoneId a_buildZoneId, std::string_view a_ownerProfileId, std::string& a_failure, double a_unixTimeSeconds, std::optional<std::uint64_t> a_protectedUntil)
	{
		return _impl->SetSettlementOwner(a_buildZoneId, a_ownerProfileId, a_failure, a_unixTimeSeconds, a_protectedUntil);
	}

	nlohmann::json ProtocolHost::ScriptSettlementOwner(F4T::Workshop::BuildZoneId a_buildZoneId) const
	{
		return _impl->ScriptSettlementOwner(a_buildZoneId);
	}

	std::vector<OutboundFrame> ProtocolHost::PollSettlementCapture(double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		return _impl->PollSettlementCapture(a_monotonicSeconds, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::SetWorldPolicy(F4T::WorldPolicy::Policy a_policy)
	{
		return _impl->SetWorldPolicy(std::move(a_policy));
	}

	nlohmann::json ProtocolHost::ReadStateBag(const F4T::StateBagProtocol::BagId& a_bag) const
	{
		return _impl->ReadStateBag(a_bag);
	}

	std::vector<OutboundFrame> ProtocolHost::SetStateBag(
		const F4T::StateBagProtocol::BagId& a_bag,
		std::string_view a_key,
		const nlohmann::json& a_value,
		std::string& a_failure)
	{
		return _impl->SetStateBag(a_bag, a_key, a_value, a_failure);
	}

	std::vector<OutboundFrame> ProtocolHost::OpenResourceView(
		std::uint32_t a_playerId,
		std::string_view a_resource,
		std::string_view a_view,
		std::string& a_failure)
	{
		return ResourceViewOpenFrames(_impl->sessions, a_playerId, a_resource, a_view, a_failure);
	}

	std::vector<OutboundFrame> ProtocolHost::CloseResourceView(std::uint32_t a_playerId, std::string_view a_resource)
	{
		return ResourceViewCloseFrames(_impl->sessions, a_playerId, a_resource);
	}

	void ProtocolHost::SetResourceMessageHandler(ResourceMessageHandler a_handler)
	{
		_impl->resourceMessageHandler = std::move(a_handler);
	}

	F4T::WorldPolicy::Policy ProtocolHost::CurrentWorldPolicy() const noexcept
	{
		return _impl->worldPolicy;
	}

	void ProtocolHost::BindSessionProfile(std::uint32_t a_connectionId, std::string_view a_profileId)
	{
		_impl->BindSessionProfile(a_connectionId, a_profileId);
	}

	void ProtocolHost::BindSessionProfileVerified(std::uint32_t a_connectionId, std::string_view a_profileId, bool a_verified, std::string_view a_steamId)
	{
		_impl->BindSessionProfileVerified(a_connectionId, a_profileId, a_verified, a_steamId);
	}

	void ProtocolHost::BindSessionSteamId(std::uint32_t a_connectionId, std::string_view a_steamId)
	{
		_impl->BindSessionSteamId(a_connectionId, a_steamId);
	}

	void ProtocolHost::SetChatEnabled(bool a_enabled)
	{
		_impl->SetChatEnabled(a_enabled);
	}

	void ProtocolHost::SetServerInstanceId(std::string a_serverInstanceId)
	{
		_impl->serverInstanceId = std::move(a_serverInstanceId);
	}

	void ProtocolHost::SetTicketNonceSource(std::function<std::string()> a_source)
	{
		_impl->ticketNonceSource = std::move(a_source);
	}

	std::string ProtocolHost::TicketNonce(std::uint32_t a_connectionId) const
	{
		const auto found = _impl->sessions.find(a_connectionId);
		return found == _impl->sessions.end() ? std::string{} : found->second.ticketNonce;
	}

	void ProtocolHost::SetChatMuted(std::string_view a_profileId, bool a_muted)
	{
		_impl->SetChatMuted(a_profileId, a_muted);
	}

	void ProtocolHost::SetChatCommandRouter(ChatCommandRouter a_router)
	{
		_impl->chatService.SetCommandRouter(std::move(a_router));
	}

	bool ProtocolHost::DeferWhileDispatching(std::vector<OutboundFrame>& a_frames)
	{
		if (a_frames.empty() || !_impl->events.Dispatching()) return false;
		for (auto& frame : a_frames) _impl->deferredFrames.push_back(std::move(frame));
		a_frames.clear();
		return true;
	}

	std::vector<OutboundFrame> ProtocolHost::TakeDeferredFrames()
	{
		return std::exchange(_impl->deferredFrames, {});
	}

	nlohmann::json ProtocolHost::ScriptPlayers() const
	{
		return _impl->ScriptPlayers();
	}

	bool ProtocolHost::ChatProfanityFilter(std::uint32_t a_connectionId) const
	{
		return _impl->ChatProfanityFilter(a_connectionId);
	}

	std::uint64_t ProtocolHost::AllocateChatMessageId()
	{
		return _impl->AllocateChatMessageId();
	}

	std::optional<VoiceAdminOutcome> ProtocolHost::SetVoiceMutedByAdmin(
		std::optional<std::uint32_t> a_playerId,
		std::string_view a_profileId,
		bool a_muted,
		std::optional<double> a_seconds,
		double a_monotonicSeconds)
	{
		return VoiceMuteByAdmin(_impl->voiceService, _impl->sessions, a_playerId, a_profileId, a_muted, a_seconds, a_monotonicSeconds);
	}

	bool ProtocolHost::RegisterCompatibilityAdapter(Compatibility::FamilyAdapter a_adapter)
	{
		return _impl->RegisterCompatibilityAdapter(std::move(a_adapter));
	}

	CompatibilityRouteResult ProtocolHost::SetCompatibilityRecipients(
		EntityId a_entityId,
		Family a_family,
		std::span<const std::uint32_t> a_exactRecipients)
	{
		return _impl->SetCompatibilityRecipients(a_entityId, a_family, a_exactRecipients);
	}

	CompatibilityPublishResult ProtocolHost::PublishCompatible(const CompatiblePublication& a_publication)
	{
		return _impl->PublishCompatible(a_publication);
	}

	std::vector<OutboundFrame> ProtocolHost::RemoveCompatibilityEntity(EntityId a_entityId)
	{
		return _impl->RemoveCompatibilityEntity(a_entityId);
	}

	std::size_t ProtocolHost::ConnectedCount() const noexcept
	{
		return _impl->sessions.size();
	}

	std::size_t ProtocolHost::ActiveCount() const noexcept
	{
		return _impl->ActiveCount();
	}

	void ProtocolHost::SetLockedWeatherFormId(std::optional<std::string> a_weatherFormId)
	{
		_impl->lockedWeatherFormId = std::move(a_weatherFormId);
	}

	void ProtocolHost::RestoreLockedWeatherFromWorldState(const nlohmann::json& a_serverWorldState)
	{
		if (const auto lockedWeather = a_serverWorldState.find("weatherFormId"); lockedWeather != a_serverWorldState.end() && lockedWeather->is_string()) {
			SetLockedWeatherFormId(lockedWeather->get<std::string>());
		}
	}

	std::optional<std::uint32_t> ProtocolHost::WorldStateHostConnectionId() const noexcept
	{
		for (const auto& [connectionId, session] : _impl->sessions) {
			if (session.active && session.playerId == _impl->worldStateHostPlayerId) return connectionId;
		}
		return std::nullopt;
	}

	ProtocolStats ProtocolHost::Stats() const noexcept
	{
		return _impl->stats;
	}

	bool ProtocolHost::HasSessionCapability(std::uint32_t a_connectionId, std::string_view a_capability) const
	{
		const auto found = _impl->sessions.find(a_connectionId);
		return found != _impl->sessions.end() && found->second.compatibilityCapabilities.Has(a_capability);
	}

	std::optional<InventoryReportSnapshot> ProtocolHost::LatestInventoryReport(std::uint32_t a_playerId) const
	{
		for (const auto& [connectionId, session] : _impl->sessions) {
			(void)connectionId;
			if (session.playerId == a_playerId) {
				return session.inventoryReport;
			}
		}
		return std::nullopt;
	}

	bool ProtocolHost::TrackAdminOp(std::uint32_t a_connectionId, std::uint32_t a_opId, std::chrono::steady_clock::time_point a_now)
	{
		const auto found = _impl->sessions.find(a_connectionId);
		if (found == _impl->sessions.end()) {
			return false;
		}
		ExpireAdminOps(found->second, a_now);
		auto& outstanding = found->second.outstandingAdminOps;
		if (outstanding.size() >= F4T::AdminItemValidation::MaximumOutstandingAdminOps) {
			return false;
		}
		outstanding.push_back(OutstandingAdminOp{ a_opId, a_now });
		return true;
	}

	void ProtocolHost::UntrackAdminOp(std::uint32_t a_connectionId, std::uint32_t a_opId)
	{
		const auto found = _impl->sessions.find(a_connectionId);
		if (found == _impl->sessions.end()) {
			return;
		}
		auto& outstanding = found->second.outstandingAdminOps;
		outstanding.erase(
			std::remove_if(outstanding.begin(), outstanding.end(), [a_opId](const OutstandingAdminOp& a_op) { return a_op.opId == a_opId; }),
			outstanding.end());
	}

	void ProtocolHost::ExpireAdminOpsFor(std::uint32_t a_playerId, std::chrono::steady_clock::time_point a_now)
	{
		for (auto& [connectionId, session] : _impl->sessions) {
			(void)connectionId;
			if (session.playerId == a_playerId) {
				ExpireAdminOps(session, a_now);
				return;
			}
		}
	}

	std::optional<AdminItemResultSnapshot> ProtocolHost::LatestAdminItemResult(std::uint32_t a_playerId) const
	{
		for (const auto& [connectionId, session] : _impl->sessions) {
			(void)connectionId;
			if (session.playerId == a_playerId) {
				return session.lastAdminItemResult;
			}
		}
		return std::nullopt;
	}

	std::string ProtocolHost::PlayerCharacterName(std::uint32_t a_playerId) const
	{
		for (const auto& [connectionId, session] : _impl->sessions) {
			(void)connectionId;
			if (session.playerId != a_playerId || !session.lastDurablePlayerState) {
				continue;
			}
			std::string clean;
			for (const char ch : session.lastDurablePlayerState->value("characterName", std::string{})) {
				if (static_cast<unsigned char>(ch) >= 32 && ch != 127) {
					clean.push_back(ch);
				}
			}
			if (clean.size() > 64) {
				std::size_t end = 64;
				while (end > 0 && (static_cast<unsigned char>(clean[end]) & 0xC0) == 0x80) {
					--end;
				}
				clean.resize(end);
			}
			return clean;
		}
		return {};
	}

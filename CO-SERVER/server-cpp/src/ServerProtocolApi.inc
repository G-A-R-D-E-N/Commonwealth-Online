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
		bool a_factionFriendlyFire) :
		_impl(std::make_unique<Impl>(
			std::move(a_serverName),
			std::move(a_serverDescription),
			std::move(a_adminProfiles),
			a_friendlyFire,
			a_factionFriendlyFire))
	{}

	ProtocolHost::~ProtocolHost() = default;
	ProtocolHost::ProtocolHost(ProtocolHost&&) noexcept = default;
	ProtocolHost& ProtocolHost::operator=(ProtocolHost&&) noexcept = default;

	std::vector<OutboundFrame> ProtocolHost::HandleConnected(std::uint32_t a_connectionId, double a_unixTimeSeconds)
	{
		return _impl->HandleConnected(a_connectionId, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::HandleDisconnected(std::uint32_t a_connectionId, double a_unixTimeSeconds)
	{
		std::string profileId;
		bool wasActive = false;
		if (const auto session = _impl->sessions.find(a_connectionId); session != _impl->sessions.end()) {
			profileId = session->second.profileId;
			wasActive = session->second.active;
		}
		auto frames = VoiceDisconnect(_impl->voiceService, _impl->sessions, a_connectionId, [&] { return _impl->HandleDisconnected(a_connectionId, a_unixTimeSeconds); });
		if (wasActive) {
			for (auto& frame : _impl->FactionDisconnectFrames(profileId, a_unixTimeSeconds)) frames.push_back(std::move(frame));
			for (auto& frame : _impl->FriendDisconnectFrames(profileId, a_unixTimeSeconds)) frames.push_back(std::move(frame));
		}
		return frames;
	}

	std::vector<OutboundFrame> ProtocolHost::HandleMessage(
		std::uint32_t a_connectionId,
		std::span<const std::uint8_t> a_message,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		auto frames = _impl->HandleMessage(a_connectionId, a_message, a_monotonicSeconds, a_unixTimeSeconds);
		for (auto& frame : _impl->FactionNameFrames(a_connectionId, a_monotonicSeconds, a_unixTimeSeconds)) frames.push_back(std::move(frame));
		for (auto& frame : _impl->WorkshopEntryFrames(a_connectionId, a_monotonicSeconds)) frames.push_back(std::move(frame));
		for (auto& frame : _impl->SettlementListFrames(a_connectionId)) frames.push_back(std::move(frame));
		return frames;
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

	void ProtocolHost::SetAdvertisedMaximumPlayers(std::size_t a_maximumPlayers)
	{
		_impl->SetAdvertisedMaximumPlayers(a_maximumPlayers);
	}

	nlohmann::json ProtocolHost::ServerInfoPacket() const
	{
		return _impl->ServerInfoPacket();
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

	std::vector<OutboundFrame> ProtocolHost::PollFriendRequests(double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		return _impl->PollFriendRequests(a_monotonicSeconds, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::AdminRevivePlayer(std::uint32_t a_playerId, double a_monotonicSeconds)
	{
		return _impl->AdminRevivePlayer(a_playerId, a_monotonicSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::AdminKillPlayer(std::uint32_t a_playerId)
	{
		return _impl->AdminKillPlayer(a_playerId);
	}

	nlohmann::json ProtocolHost::AdminListFactions() const
	{
		return _impl->AdminListFactions();
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

	std::size_t ProtocolHost::ConfigureWorkshop(PersistenceGate* a_persistence, const std::filesystem::path& a_zonesFile)
	{
		return _impl->ConfigureWorkshop(a_persistence, a_zonesFile);
	}

	void ProtocolHost::SetWorkshopAuthority(F4T::Workshop::WorkshopAuthority::CanBuildAt a_canBuildAt)
	{
		_impl->SetWorkshopAuthority(std::move(a_canBuildAt));
	}

	bool ProtocolHost::EnableSettlementAccess(PersistenceGate* a_persistence)
	{
		return _impl->EnableSettlementAccess(a_persistence);
	}

	bool ProtocolHost::EnableCharacterSlots(PersistenceGate* a_persistence, std::size_t a_slotCount, std::uint32_t a_retentionDays)
	{
		return _impl->characterSlots.Attach(
			a_persistence, CharacterSlotService::Config{ a_slotCount, a_retentionDays });
	}

	std::vector<OutboundFrame> ProtocolHost::SetSettlementOwner(F4T::Workshop::BuildZoneId a_buildZoneId, std::string_view a_ownerProfileId, std::string& a_failure)
	{
		return _impl->SetSettlementOwner(a_buildZoneId, a_ownerProfileId, a_failure);
	}

	std::vector<OutboundFrame> ProtocolHost::PollSettlementCapture(double a_monotonicSeconds)
	{
		return _impl->PollSettlementCapture(a_monotonicSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::SetWorldPolicy(F4T::WorldPolicy::Policy a_policy)
	{
		return _impl->SetWorldPolicy(std::move(a_policy));
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

	void ProtocolHost::SetChatMuted(std::string_view a_profileId, bool a_muted)
	{
		_impl->SetChatMuted(a_profileId, a_muted);
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

	ProtocolStats ProtocolHost::Stats() const noexcept
	{
		return _impl->stats;
	}

#pragma once

#include "F4TCompatibilityShim.h"
#include "ServerAfk.h"
#include "F4TQuestProtocol.h"
#include "F4TQuestStoryOrder.h"
#include "F4TStateBagProtocol.h"
#include "F4TServerConfiguration.h"
#include "F4TDownedState.h"
#include "F4TSnapshotDelta.h"
#include "F4TWorkshopRaids.h"
#include "F4TWorkshopTransactions.h"
#include "F4TWorldPolicy.h"
#include "ServerEvents.h"
#include "ServerViolation.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server
{
	constexpr std::size_t kMaximumMessageBytes = 256 * 1024;

	class PersistenceGate;
	struct PendingAdminAction;

	namespace AdminProfiles
	{
		[[nodiscard]] bool Matches(const std::vector<std::string>& a_entries, std::string_view a_profileId);
	}

	enum class Delivery : std::uint32_t
	{
		UnreliableSequenced = 0,
		ReliableOrdered = 1
	};

	struct DeliveryReceipt
	{
		enum class Kind : std::uint8_t
		{
			None = 0,
			PlayerState,
			ResumeAck,
			PluginList,
			Compatibility
		};

		Kind kind{ Kind::None };
		std::uint32_t sourceConnectionId{};
		std::uint64_t revision{};
		F4T::Replication::ComponentKey component{};
	};

	struct OutboundFrame
	{
		std::uint32_t connectionId{};
		Delivery delivery{ Delivery::ReliableOrdered };
		std::vector<std::uint8_t> payload;
		DeliveryReceipt receipt{};
	};

	using ChatCommandRouter = std::function<std::optional<std::vector<OutboundFrame>>(std::uint32_t a_playerId, std::string_view a_text)>;

	using CompatibilityLegacyEgress = std::function<std::optional<OutboundFrame>(
		std::uint32_t,
		std::string_view)>;

	struct CompatibilityRouteResult
	{
		bool accepted{};
		std::vector<OutboundFrame> frames;
	};

	enum class CompatibilityPublishStatus : std::uint8_t
	{
		Rejected = 0,
		Stale,
		Accepted
	};

	struct CompatiblePublication
	{
		F4T::Replication::EntityId entityId{};
		F4T::Replication::Family componentFamily{};
		std::string legacyFamily;
		std::uint64_t revision{};
		F4T::Compatibility::CanonicalValue value;
		std::vector<std::uint32_t> eligibleRecipients;
		CompatibilityLegacyEgress legacyEgress;
	};

	struct CompatibilityPublishResult
	{
		CompatibilityPublishStatus status{ CompatibilityPublishStatus::Rejected };
		std::vector<OutboundFrame> frames;
	};

	struct VoiceAdminOutcome
	{
		std::string profileId;
		std::vector<OutboundFrame> frames;
	};

	struct DirectComponentPublicationPeer
	{
		std::uint32_t peerId{};
		bool componentSnapshot{};
		bool componentSnapshotChannels{};
		std::set<F4T::Replication::Family> supportedFamilies;

		bool operator==(const DirectComponentPublicationPeer&) const = default;
	};

	struct DirectComponentPublicationContract
	{
		std::vector<DirectComponentPublicationPeer> peers;

		bool operator==(const DirectComponentPublicationContract&) const = default;
	};

	using ResourceMessageHandler = std::function<void(std::uint32_t a_playerId, std::string_view a_resource, const nlohmann::json& a_data)>;

	struct ProtocolStats
	{
		std::uint64_t packetsReceived{};
		std::uint64_t packetsRejected{};
		std::uint64_t movementRejected{};
		std::uint64_t movementCorrectionsSent{};
		std::uint64_t transformOversizeRejected{};
		std::uint64_t snapshotsRejected{};
		std::uint64_t npcAuthorityRejected{};
		std::uint64_t npcStateRejected{};
		std::uint64_t worldStatePacketsReceived{};
		std::uint64_t worldStateRejected{};
		std::uint64_t combatHitsReceived{};
		std::uint64_t combatHitsRouted{};
		std::uint64_t combatHitsRejected{};
		std::uint64_t combatDamageClaimsClamped{};
		std::uint64_t npcDamageClaimsClamped{};
		std::uint64_t durableStateRelays{};
		std::uint64_t durableStateSuppressed{};
		std::uint64_t workshopOpsReceived{};
		std::uint64_t workshopOpsCommitted{};
		std::uint64_t workshopOpsRejected{};
		std::uint64_t npcHitsReceived{};
		std::uint64_t npcHitsRouted{};
		std::uint64_t npcHitsRejected{};
		std::uint64_t npcDamageReceived{};
		std::uint64_t npcDamageRouted{};
		std::uint64_t npcDamageRejected{};
		std::uint64_t npcLootReceived{};
		std::uint64_t npcLootRouted{};
		std::uint64_t npcLootRejected{};
		std::uint64_t npcLootTakeReceived{};
		std::uint64_t npcLootTakeRouted{};
		std::uint64_t npcLootTakeRejected{};
		std::uint64_t npcLootResultReceived{};
		std::uint64_t npcLootResultRouted{};
		std::uint64_t npcLootResultRejected{};
		std::uint64_t explosionFormsRejected{};
		std::uint64_t chatReceived{};
		std::uint64_t chatRelayed{};
		std::uint64_t chatRejected{};
		std::uint64_t adminRejected{};
		std::uint64_t inventoryProjections{};
		std::uint64_t parkedSuitPieceAuditMisses{};
		std::uint64_t factionPacketsReceived{};
		std::uint64_t factionPacketsRejected{};
		std::uint64_t objectStateReceived{};
		std::uint64_t objectStateRouted{};
		std::uint64_t objectStateReplayed{};
		std::uint64_t objectStateRejected{};
		std::uint64_t questReportsReceived{};
		std::uint64_t questStagesRelayed{};
		std::uint64_t questStagesHeld{};
		std::uint64_t questReportsRejected{};
		std::uint64_t questScopesReceived{};
		std::uint64_t questScopesRejected{};
		std::uint64_t questScopesApplied{};
		std::uint64_t questReadyRejected{};
		std::uint64_t questStoryRefused{};
		std::uint64_t questSyncReplays{};
		std::uint64_t questTeamResynced{};
		std::uint64_t questAssistsReceived{};
		std::uint64_t questAssistsRejected{};
		std::uint64_t questCreditsMinted{};
		std::uint64_t questCreditAcksReceived{};
		std::uint64_t questCreditAcksRejected{};
		std::uint64_t questCreditAcksApplied{};
		std::uint64_t questPresencesSent{};
		std::uint64_t downedEventsEmitted{};
		std::uint64_t reviveRequestsReceived{};
		std::uint64_t reviveRequestsRejected{};
		std::uint64_t reviveCancelsReceived{};
		std::uint64_t checkpointsReceived{};
		std::uint64_t checkpointsRejected{};
		std::uint64_t respawnRequestsReceived{};
		std::uint64_t respawnRequestsRejected{};
		std::uint64_t pluginListsReceived{};
		std::uint64_t pluginListsRejected{};
		std::uint64_t voiceReceived{};
		std::uint64_t voiceAccepted{};
		std::uint64_t voiceRelayed{};
		std::uint64_t voiceDroppedInactive{};
		std::uint64_t voiceDroppedUnnegotiated{};
		std::uint64_t voiceDroppedNotSendable{};
		std::uint64_t voiceDroppedNoState{};
		std::uint64_t voiceDroppedMalformed{};
		std::uint64_t voiceDroppedNoMovement{};
		std::uint64_t voiceDroppedMuted{};
		std::uint64_t voiceDroppedDuplicate{};
		std::uint64_t voiceDroppedStale{};
		std::uint64_t voiceDroppedSoftLimit{};
		std::uint64_t voiceDroppedEgress{};
		std::uint64_t voiceAutoMutes{};
		std::uint64_t voiceSent{};
		std::uint64_t voiceSendBackpressure{};
		std::uint64_t voiceSendDropped{};
		std::uint64_t voiceSendFailed{};
		std::uint64_t voiceStatesApplied{};
		std::uint64_t voiceStatesRejected{};
		std::uint64_t voiceStatesLimited{};
		std::uint64_t adminItemResultsReceived{};
		std::uint64_t adminItemResultsRejected{};
		std::uint64_t inventoryReportsReceived{};
		std::uint64_t inventoryReportsRejected{};
	};

	[[nodiscard]] nlohmann::json VoiceStatsJson(const ProtocolStats& a_stats);
	[[nodiscard]] nlohmann::json RuntimeCountersJson(const ProtocolStats& a_stats);

	struct InventoryReportSnapshot
	{
		std::uint32_t opId{};
		bool truncated{};
		nlohmann::json items;
		double receivedAtUnixSeconds{};
	};

	struct AdminItemResultSnapshot
	{
		std::uint32_t opId{};
		bool ok{};
		std::uint32_t applied{};
		std::string error;
		bool unknown{};
	};

	inline constexpr std::chrono::seconds AdminOpTimeout{ 60 };

	class ProtocolHost
	{
	public:
		explicit ProtocolHost(
			std::string a_serverName = "Commonwealth Online",
			std::string a_serverDescription = {},
			std::vector<std::string> a_adminProfiles = {},
			bool a_friendlyFire = true,
			bool a_factionFriendlyFire = false,
			std::string a_playerKeySecret = {});
		~ProtocolHost();

		ProtocolHost(const ProtocolHost&) = delete;
		ProtocolHost& operator=(const ProtocolHost&) = delete;
		ProtocolHost(ProtocolHost&&) noexcept;
		ProtocolHost& operator=(ProtocolHost&&) noexcept;

		std::vector<OutboundFrame> HandleConnected(std::uint32_t a_connectionId, double a_unixTimeSeconds);
		std::vector<OutboundFrame> HandleDisconnected(std::uint32_t a_connectionId, double a_unixTimeSeconds, bool a_cleanGoodbye = false);
		std::vector<OutboundFrame> HandleMessage(
			std::uint32_t a_connectionId,
			std::span<const std::uint8_t> a_message,
			double a_monotonicSeconds,
			double a_unixTimeSeconds,
			std::optional<nlohmann::json>* a_parsed = nullptr);
		std::vector<OutboundFrame> PublishComponentSnapshot(
			const F4T::Replication::SnapshotState& a_authoritativeState);
		void FrameDeliveryCompleted(const OutboundFrame& a_frame, bool a_sent);
		void QueueViolation(std::uint32_t a_connectionId, Violation a_violation, std::string_view a_detail);
		[[nodiscard]] std::vector<PendingViolation> TakeViolations();
		void VoiceSendCompleted(bool a_sent, bool a_backpressure, bool a_dropped);
		[[nodiscard]] std::vector<OutboundFrame> RetryComponentSnapshots();
		[[nodiscard]] std::vector<OutboundFrame> RetryNpcAuthority(double a_unixTimeSeconds);
		[[nodiscard]] std::vector<OutboundFrame> RetryStateBagFrames(double a_monotonicSeconds);
		[[nodiscard]] std::vector<OutboundFrame> RetryPluginLists();
		[[nodiscard]] std::vector<OutboundFrame> RetryHeldCombatRelays();
		[[nodiscard]] bool HeldCombatRelayBlocks(const OutboundFrame& a_frame) const;
		[[nodiscard]] bool EnableInventoryTransactions(PersistenceGate& a_persistence);
		[[nodiscard]] bool EnableObjectState();
		void SetDownedConfig(F4T::DownedState::Config a_config);
		void SetDeathBagLootByOthers(bool a_allow);
		void SetFactionLimits(F4T::ServerConfiguration::FactionLimits a_limits);
		[[nodiscard]] std::vector<OutboundFrame> PollDownedState(double a_monotonicSeconds);
		[[nodiscard]] std::vector<OutboundFrame> PollVoice(double a_monotonicSeconds);
		[[nodiscard]] std::vector<OutboundFrame> PollFactionInvites(double a_monotonicSeconds, double a_unixTimeSeconds);
		[[nodiscard]] std::vector<OutboundFrame> PollDurablePlayerState(double a_monotonicSeconds, double a_unixTimeSeconds);
		[[nodiscard]] std::vector<OutboundFrame> PollFriendRequests(double a_monotonicSeconds, double a_unixTimeSeconds);
		[[nodiscard]] std::vector<OutboundFrame> PollTrades(double a_monotonicSeconds);
		[[nodiscard]] std::vector<OutboundFrame> PollFireteamInvites(double a_unixTimeSeconds);
		[[nodiscard]] std::vector<OutboundFrame> PollQuestAssists(double a_monotonicSeconds, double a_unixTimeSeconds);
		[[nodiscard]] std::vector<OutboundFrame> PollTeamQuestSync(double a_monotonicSeconds, double a_unixTimeSeconds);
		[[nodiscard]] std::vector<OutboundFrame> PollPresence(double a_monotonicSeconds, double a_unixTimeSeconds);
		[[nodiscard]] std::vector<OutboundFrame> PollNpcLootTakes(double a_monotonicSeconds, double a_unixTimeSeconds);
		[[nodiscard]] std::vector<OutboundFrame> PollSurvivalNeeds(double a_monotonicSeconds, double a_unixTimeSeconds);
		void SettleSurvivalNeeds(std::uint32_t a_connectionId, double a_monotonicSeconds);
		[[nodiscard]] std::vector<OutboundFrame> PollDifficulty(double a_monotonicSeconds, double a_unixTimeSeconds);
		void SetMapPublicPlayers(bool a_enabled);
		[[nodiscard]] bool MapPublicPlayers() const;
		[[nodiscard]] std::vector<OutboundFrame> AdminRevivePlayer(std::uint32_t a_playerId, double a_monotonicSeconds);
		[[nodiscard]] std::vector<OutboundFrame> AdminKillPlayer(std::uint32_t a_playerId, bool* a_refusedOut = nullptr);
		[[nodiscard]] nlohmann::json AdminListFactions() const;
		[[nodiscard]] nlohmann::json AdminListReports() const;
		[[nodiscard]] std::vector<OutboundFrame> AdminDisbandFaction(
			std::string_view a_factionId,
			double a_unixTimeSeconds,
			std::string& a_failure);
		[[nodiscard]] std::vector<OutboundFrame> AdminPromoteFaction(
			std::string_view a_factionId,
			std::string_view a_targetProfileId,
			double a_unixTimeSeconds,
			std::string& a_failure);
		[[nodiscard]] std::vector<OutboundFrame> AdminKickFaction(
			std::string_view a_factionId,
			std::string_view a_targetProfileId,
			double a_unixTimeSeconds,
			std::string& a_failure);
		[[nodiscard]] bool EnableFactions(PersistenceGate* a_persistence);
		[[nodiscard]] bool EnableSocial(PersistenceGate* a_persistence);
		[[nodiscard]] bool EnableQuestSync(
			PersistenceGate* a_persistence,
			F4T::QuestProtocol::Mode a_mode,
			std::vector<F4T::QuestProtocol::OverrideEntry> a_overrides);
		[[nodiscard]] bool QuestSyncEnabled() const noexcept;
		[[nodiscard]] F4T::QuestProtocol::Mode QuestMode() const noexcept;
		[[nodiscard]] nlohmann::json QuestStatus() const;
		[[nodiscard]] bool IsQuestGated(const F4T::QuestProtocol::FormKey& a_quest, std::uint16_t a_stage) const;
		[[nodiscard]] bool IsQuestSolo(std::string_view a_profileId, const F4T::QuestProtocol::FormKey& a_quest) const;
		// constraint: tests only; production uses Order::Default().
		void SetQuestStoryOrder(const F4T::QuestStory::Order& a_order);
		[[nodiscard]] std::vector<OutboundFrame> SetQuestMode(
			F4T::QuestProtocol::Mode a_mode,
			double a_unixTimeSeconds);
		[[nodiscard]] std::vector<OutboundFrame> SetQuestGate(
			const F4T::QuestProtocol::FormKey& a_quest,
			std::uint16_t a_stage,
			bool a_release,
			double a_unixTimeSeconds);
		[[nodiscard]] std::vector<OutboundFrame> SetQuestSolo(
			std::string_view a_profileId,
			const F4T::QuestProtocol::FormKey& a_quest,
			bool a_solo,
			double a_unixTimeSeconds);
		[[nodiscard]] bool SetFactionsEnabled(bool a_enabled, PersistenceGate* a_persistence);
		[[nodiscard]] std::vector<OutboundFrame> FactionResyncFrames(double a_unixTimeSeconds);
		[[nodiscard]] bool FlushFactionState();
		[[nodiscard]] bool FlushSocialState();
		void RetryDirtyPersistence(double a_monotonicSeconds);
		void SetObjectStateEnabled(bool a_enabled);
		void SetIdentity(std::string a_serverName, std::string a_serverDescription);
		void SetFriendlyFire(bool a_friendlyFire, bool a_factionFriendlyFire);
		void SetSurvivalMode(bool a_enabled);
		void SetSurvivalNeeds(bool a_enabled);
		void SetRequireDifficultyReport(bool a_enabled);
		void SetRequiredDifficulty(std::int32_t a_difficulty);
		void SetAdvertisedMaximumPlayers(std::size_t a_maximumPlayers);
		[[nodiscard]] nlohmann::json ServerInfoPacket() const;
		[[nodiscard]] const F4T::Replication::SnapshotState& DroppedObjectState() const noexcept;
		[[nodiscard]] bool SetComponentDefinitions(
			F4T::Replication::FrozenComponentDefinitionTable a_definitions);
		[[nodiscard]] bool SetDirectComponentPublicationContract(
			DirectComponentPublicationContract a_contract);
		[[nodiscard]] bool RegisterWorkshopBuildZone(F4T::Workshop::BuildZonePolicy a_policy);
		[[nodiscard]] bool EnableWorkshopPersistence(PersistenceGate* a_persistence);
		[[nodiscard]] std::vector<OutboundFrame> FlushWorkshopPersistence(double a_unixSeconds, double a_monotonicSeconds = 0.0);
		std::size_t ConfigureWorkshop(PersistenceGate* a_persistence, const std::filesystem::path& a_zonesFile);
		void LoadSurvivalItems(const std::filesystem::path& a_path);
		void ConfigureVendorItemValues(PersistenceGate* a_persistence, const std::filesystem::path& a_itemValuesFile, std::uint32_t a_restockHours);
		void SetWorkshopAuthority(F4T::Workshop::WorkshopAuthority::CanBuildAt a_canBuildAt);
		[[nodiscard]] bool EnableSettlementAccess(PersistenceGate* a_persistence);
		void EnableParkedSuits(PersistenceGate* a_persistence);
		void SetWorkshopRaidConfig(F4T::WorkshopRaids::RaidConfig a_config);
		[[nodiscard]] F4T::WorkshopRaids::RaidConfig WorkshopRaidConfig() const;
		[[nodiscard]] bool EnableCharacterSlots(PersistenceGate* a_persistence, std::size_t a_slotCount);
		[[nodiscard]] std::vector<OutboundFrame> SetSettlementOwner(
			F4T::Workshop::BuildZoneId a_buildZoneId,
			std::string_view a_ownerProfileId,
			std::string& a_failure,
			double a_unixTimeSeconds = 0.0,
			std::optional<std::uint64_t> a_protectedUntil = std::nullopt);
		[[nodiscard]] std::vector<OutboundFrame> PollSettlementCapture(double a_monotonicSeconds, double a_unixTimeSeconds = 0.0);
		[[nodiscard]] std::vector<OutboundFrame> SetWorldPolicy(F4T::WorldPolicy::Policy a_policy);
		[[nodiscard]] F4T::WorldPolicy::Policy CurrentWorldPolicy() const noexcept;
		[[nodiscard]] nlohmann::json ReadStateBag(const F4T::StateBagProtocol::BagId& a_bag) const;
		[[nodiscard]] std::vector<OutboundFrame> SetStateBag(
			const F4T::StateBagProtocol::BagId& a_bag,
			std::string_view a_key,
			const nlohmann::json& a_value,
			std::string& a_failure);
		[[nodiscard]] std::vector<OutboundFrame> OpenResourceView(
			std::uint32_t a_playerId,
			std::string_view a_resource,
			std::string_view a_view,
			std::string& a_failure);
		[[nodiscard]] std::vector<OutboundFrame> CloseResourceView(std::uint32_t a_playerId, std::string_view a_resource);
		void SetResourceMessageHandler(ResourceMessageHandler a_handler);
		void BindSessionProfile(std::uint32_t a_connectionId, std::string_view a_profileId);
		void BindSessionProfileVerified(std::uint32_t a_connectionId, std::string_view a_profileId, bool a_verified, std::string_view a_steamId);
		void BindSessionSteamId(std::uint32_t a_connectionId, std::string_view a_steamId);
		void SetChatEnabled(bool a_enabled);
		void SetServerInstanceId(std::string a_serverInstanceId);
		void SetChatMuted(std::string_view a_profileId, bool a_muted);
		void SetChatCommandRouter(ChatCommandRouter a_router);
		[[nodiscard]] nlohmann::json ScriptPlayers() const;
		[[nodiscard]] nlohmann::json ScriptSettlementOwner(F4T::Workshop::BuildZoneId a_buildZoneId) const;
		[[nodiscard]] std::uint64_t AllocateChatMessageId();
		[[nodiscard]] bool ChatProfanityFilter(std::uint32_t a_connectionId) const;
		[[nodiscard]] std::optional<VoiceAdminOutcome> SetVoiceMutedByAdmin(
			std::optional<std::uint32_t> a_playerId,
			std::string_view a_profileId,
			bool a_muted,
			std::optional<double> a_seconds,
			double a_monotonicSeconds);
		void SetSessionAdminVerified(std::uint32_t a_connectionId, bool a_verified);
		[[nodiscard]] bool IsLiveAdmin(std::uint32_t a_connectionId) const noexcept;
		[[nodiscard]] std::vector<OutboundFrame> AdminStateFrame(std::uint32_t a_connectionId, double a_unixTimeSeconds) const;
		[[nodiscard]] std::vector<PendingAdminAction> TakePendingAdminActions();
		[[nodiscard]] bool IsAdminActionAuthorized(const PendingAdminAction& a_pending) const;
		[[nodiscard]] std::vector<OutboundFrame> CompleteAdminAction(const PendingAdminAction& a_pending, const nlohmann::json& a_reply);
		void RejectAdminAction(const PendingAdminAction& a_pending, std::string_view a_reason);
		[[nodiscard]] std::vector<OutboundFrame> FailAdminAction(const PendingAdminAction& a_pending, std::string_view a_what);
		void SetAdminProfiles(std::vector<std::string> a_adminProfiles);
		void SetAdminLookup(std::function<bool(std::string_view)> a_lookup);
		[[nodiscard]] std::vector<OutboundFrame> AdminStateFramesForProfile(std::string_view a_profileId, double a_unixTimeSeconds) const;
		[[nodiscard]] std::optional<std::vector<OutboundFrame>> AbandonSlotForAdmin(std::string_view a_account, std::uint32_t a_slot, double a_unixTimeSeconds, bool a_withoutSave = false);
		void SetAdminAuditPath(std::string a_path);
		void SetAdminAuditClock(std::function<double()> a_clock);

		[[nodiscard]] bool RegisterCompatibilityAdapter(F4T::Compatibility::FamilyAdapter a_adapter);
		[[nodiscard]] CompatibilityRouteResult SetCompatibilityRecipients(
			F4T::Replication::EntityId a_entityId,
			F4T::Replication::Family a_family,
			std::span<const std::uint32_t> a_exactRecipients);
		[[nodiscard]] CompatibilityPublishResult PublishCompatible(const CompatiblePublication& a_publication);
		[[nodiscard]] std::vector<OutboundFrame> RemoveCompatibilityEntity(F4T::Replication::EntityId a_entityId);

		[[nodiscard]] std::size_t ConnectedCount() const noexcept;
		[[nodiscard]] std::size_t ActiveCount() const noexcept;
		[[nodiscard]] std::optional<std::uint32_t> WorldStateHostConnectionId() const noexcept;
		void SetLockedWeatherFormId(std::optional<std::string> a_weatherFormId);
		void RestoreLockedWeatherFromWorldState(const nlohmann::json& a_serverWorldState);
		[[nodiscard]] ProtocolStats Stats() const noexcept;
		// constraint: consumes the flag, so call it once per handled packet.
		[[nodiscard]] bool TakePlayerActivity(std::uint32_t a_connectionId);
		[[nodiscard]] Afk::Exemption SessionAfkExemption(std::uint32_t a_connectionId) const noexcept;
		[[nodiscard]] ServerEvents& Events() noexcept;
		[[nodiscard]] bool DeferWhileDispatching(std::vector<OutboundFrame>& a_frames);
		[[nodiscard]] std::vector<OutboundFrame> TakeDeferredFrames();
		[[nodiscard]] bool HasSessionCapability(std::uint32_t a_connectionId, std::string_view a_capability) const;
		[[nodiscard]] std::optional<InventoryReportSnapshot> LatestInventoryReport(std::uint32_t a_playerId) const;
		[[nodiscard]] bool TrackAdminOp(
				std::uint32_t a_connectionId,
				std::uint32_t a_opId,
				std::chrono::steady_clock::time_point a_now = std::chrono::steady_clock::now());
		void UntrackAdminOp(std::uint32_t a_connectionId, std::uint32_t a_opId);
		void ExpireAdminOpsFor(
				std::uint32_t a_playerId,
				std::chrono::steady_clock::time_point a_now = std::chrono::steady_clock::now());
		[[nodiscard]] std::optional<AdminItemResultSnapshot> LatestAdminItemResult(std::uint32_t a_playerId) const;
		[[nodiscard]] std::string PlayerCharacterName(std::uint32_t a_playerId) const;

	private:
		class Impl;
		std::unique_ptr<Impl> _impl;
	};
}

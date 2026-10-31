#pragma once

#include "F4TCompatibilityShim.h"
#include "F4TQuestProtocol.h"
#include "F4TQuestStoryOrder.h"
#include "F4TServerConfiguration.h"
#include "F4TDownedState.h"
#include "F4TSnapshotDelta.h"
#include "F4TWorkshopTransactions.h"
#include "F4TWorldPolicy.h"

#include <nlohmann/json.hpp>

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
	class PersistenceGate;

	namespace AdminProfiles
	{
		[[nodiscard]] bool Matches(const std::vector<std::string>& a_entries, std::string_view a_profileId);
	}

	enum class Delivery : std::uint32_t
	{
		UnreliableSequenced = 0,
		ReliableOrdered = 1
	};

	struct OutboundFrame
	{
		std::uint32_t connectionId{};
		Delivery delivery{ Delivery::ReliableOrdered };
		std::vector<std::uint8_t> payload;
	};

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
		std::uint64_t chatReceived{};
		std::uint64_t chatRelayed{};
		std::uint64_t chatRejected{};
		std::uint64_t adminRejected{};
		std::uint64_t inventoryProjections{};
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
	};

	[[nodiscard]] nlohmann::json VoiceStatsJson(const ProtocolStats& a_stats);
	[[nodiscard]] nlohmann::json RuntimeCountersJson(const ProtocolStats& a_stats);

	class ProtocolHost
	{
	public:
		explicit ProtocolHost(
			std::string a_serverName = "Commonwealth Online",
			std::string a_serverDescription = {},
			std::vector<std::string> a_adminProfiles = {},
			bool a_friendlyFire = true,
			bool a_factionFriendlyFire = false);
		~ProtocolHost();

		ProtocolHost(const ProtocolHost&) = delete;
		ProtocolHost& operator=(const ProtocolHost&) = delete;
		ProtocolHost(ProtocolHost&&) noexcept;
		ProtocolHost& operator=(ProtocolHost&&) noexcept;

		std::vector<OutboundFrame> HandleConnected(std::uint32_t a_connectionId, double a_unixTimeSeconds);
		std::vector<OutboundFrame> HandleDisconnected(std::uint32_t a_connectionId, double a_unixTimeSeconds);
		std::vector<OutboundFrame> HandleMessage(
			std::uint32_t a_connectionId,
			std::span<const std::uint8_t> a_message,
			double a_monotonicSeconds,
			double a_unixTimeSeconds);
		std::vector<OutboundFrame> PublishComponentSnapshot(
			const F4T::Replication::SnapshotState& a_authoritativeState);
		void FrameDeliveryCompleted(const OutboundFrame& a_frame, bool a_sent);
		void VoiceSendCompleted(bool a_sent, bool a_backpressure, bool a_dropped);
		[[nodiscard]] std::vector<OutboundFrame> RetryComponentSnapshots();
		[[nodiscard]] bool EnableInventoryTransactions(PersistenceGate& a_persistence);
		[[nodiscard]] bool EnableObjectState();
		void SetDownedConfig(F4T::DownedState::Config a_config);
		void SetFactionLimits(F4T::ServerConfiguration::FactionLimits a_limits);
		[[nodiscard]] std::vector<OutboundFrame> PollDownedState(double a_monotonicSeconds);
		[[nodiscard]] std::vector<OutboundFrame> PollVoice(double a_monotonicSeconds);
		[[nodiscard]] std::vector<OutboundFrame> PollFactionInvites(double a_monotonicSeconds, double a_unixTimeSeconds);
		[[nodiscard]] std::vector<OutboundFrame> AdminRevivePlayer(std::uint32_t a_playerId, double a_monotonicSeconds);
		[[nodiscard]] std::vector<OutboundFrame> AdminKillPlayer(std::uint32_t a_playerId);
		[[nodiscard]] nlohmann::json AdminListFactions() const;
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
		void SetObjectStateEnabled(bool a_enabled);
		void SetIdentity(std::string a_serverName, std::string a_serverDescription);
		void SetFriendlyFire(bool a_friendlyFire, bool a_factionFriendlyFire);
		void SetAdvertisedMaximumPlayers(std::size_t a_maximumPlayers);
		[[nodiscard]] nlohmann::json ServerInfoPacket() const;
		[[nodiscard]] const F4T::Replication::SnapshotState& DroppedObjectState() const noexcept;
		[[nodiscard]] bool SetComponentDefinitions(
			F4T::Replication::FrozenComponentDefinitionTable a_definitions);
		[[nodiscard]] bool SetDirectComponentPublicationContract(
			DirectComponentPublicationContract a_contract);
		[[nodiscard]] bool RegisterWorkshopBuildZone(F4T::Workshop::BuildZonePolicy a_policy);
		[[nodiscard]] bool EnableWorkshopPersistence(PersistenceGate* a_persistence);
		std::size_t ConfigureWorkshop(PersistenceGate* a_persistence, const std::filesystem::path& a_zonesFile);
		void SetWorkshopAuthority(F4T::Workshop::WorkshopAuthority::CanBuildAt a_canBuildAt);
		[[nodiscard]] bool EnableSettlementAccess(PersistenceGate* a_persistence);
		[[nodiscard]] bool EnableCharacterSlots(PersistenceGate* a_persistence, std::size_t a_slotCount, std::uint32_t a_retentionDays);
		[[nodiscard]] std::vector<OutboundFrame> SetSettlementOwner(
			F4T::Workshop::BuildZoneId a_buildZoneId,
			std::string_view a_ownerProfileId,
			std::string& a_failure);
		[[nodiscard]] std::vector<OutboundFrame> PollSettlementCapture(double a_monotonicSeconds);
		[[nodiscard]] std::vector<OutboundFrame> SetWorldPolicy(F4T::WorldPolicy::Policy a_policy);
		[[nodiscard]] F4T::WorldPolicy::Policy CurrentWorldPolicy() const noexcept;
		void BindSessionProfile(std::uint32_t a_connectionId, std::string_view a_profileId);
		void BindSessionProfileVerified(std::uint32_t a_connectionId, std::string_view a_profileId, bool a_verified, std::string_view a_steamId);
		void BindSessionSteamId(std::uint32_t a_connectionId, std::string_view a_steamId);
		void SetChatEnabled(bool a_enabled);
		void SetServerInstanceId(std::string a_serverInstanceId);
		void SetChatMuted(std::string_view a_profileId, bool a_muted);
		[[nodiscard]] std::optional<VoiceAdminOutcome> SetVoiceMutedByAdmin(
			std::optional<std::uint32_t> a_playerId,
			std::string_view a_profileId,
			bool a_muted,
			std::optional<double> a_seconds,
			double a_monotonicSeconds);
		void SetSessionAdminVerified(std::uint32_t a_connectionId, bool a_verified);
		[[nodiscard]] bool IsLiveAdmin(std::uint32_t a_connectionId) const noexcept;
		[[nodiscard]] std::vector<OutboundFrame> AdminStateFrame(std::uint32_t a_connectionId, double a_unixTimeSeconds) const;
		void SetAdminProfiles(std::vector<std::string> a_adminProfiles);
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
		[[nodiscard]] ProtocolStats Stats() const noexcept;

	private:
		class Impl;
		std::unique_ptr<Impl> _impl;
	};
}

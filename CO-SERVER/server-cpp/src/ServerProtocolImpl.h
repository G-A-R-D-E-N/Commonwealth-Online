#pragma once

#include "ServerProtocol.h"
#include "PersistenceGate.h"
#include "ServerCharacterSlots.h"
#include "ServerChat.h"
#include "ServerCombatChecks.h"
#include "ServerDowned.h"
#include "ServerFactions.h"
#include "ServerFriends.h"
#include "ServerReports.h"
#include "ServerMapPresence.h"
#include "ServerAdminMenu.h"
#include "ServerSettlementAccess.h"
#include "ServerSettlementCapture.h"
#include "ServerSocialGraph.h"
#include "ServerStateBag.h"
#include "ServerNpcHitChecks.h"
#include "ServerNpcLedger.h"
#include "ServerNpcLootChecks.h"
#include "ServerOutboundFrames.h"
#include "ServerPacketRead.h"
#include "ServerActivity.h"
#include "ServerRefusalWindow.h"
#include "ServerResourceBridge.h"
#include "ServerScopeKeys.h"
#include "ServerSessionTypes.h"
#include "ServerFurnitureRelay.h"
#include "ServerShotReplayRelay.h"
#include "ServerTextHelpers.h"
#include "ServerVoiceHost.h"
#include "ServerWelcome.h"

#include "F4TAdminItemValidation.h"
#include "F4TAuthoritativePacketValidation.h"
#include "F4TAuthorityElection.h"
#include "F4TCompatibilityShim.h"
#include "F4TGnsSnapshotEnvelope.h"
#include "F4TIdentityProtocol.h"
#include "F4TInterestScope.h"
#include "F4TNetworkValidation.h"
#include "F4TNpcAuthority.h"
#include "F4TNpcCoopValidation.h"
#include "F4TNpcLootSync.h"
#include "ServerVendorTrade.h"
#include "F4TPacketRateLimit.h"
#include "F4TPayloadCodec.h"
#include "F4TPeerEntitySet.h"
#include "F4TDownedValidation.h"
#include "F4TDeathBagLocalPlan.h"
#include "F4TDropLedger.h"
#include "F4TInventoryWire.h"
#include "F4TFormIdentity.h"
#include "F4TLoadOrder.h"
#include "F4TObjectState.h"
#include "F4TObjectWire.h"
#include "F4TParkedSuit.h"
#include "F4TQuestProtocol.h"
#include "F4TQuestState.h"
#include "F4TCreditLedger.h"
#include "F4TFactionValidation.h"
#include "F4TPlayerStateValidation.h"
#include "F4TProgressionValidation.h"
#include "F4TSessionLifecycle.h"
#include "F4TSurvivalItems.h"
#include "F4TTransformValidation.h"
#include "F4TUtf8Path.h"
#include "F4TSettlementAccessProtocol.h"
#include "F4TSettlementCharge.h"
#include "F4TWorkshopCamps.h"
#include "F4TWorkshopDefenseForms.h"
#include "F4TWorkshopNoBuild.h"
#include "F4TWorkshopDefenses.h"
#include "F4TWorkshopLocks.h"
#include "F4TWorkshopPersistence.h"
#include "F4TCampGrants.h"
#include "F4TWorkshopCosts.h"
#include "F4TWeaponClasses.h"
#include "F4TWorkshopPieces.h"
#include "F4TWorkshopProtocol.h"
#include "F4TWorkshopRaids.h"
#include "F4TWorkshopScale.h"
#include "F4TWorkshopSettlers.h"
#include "F4TWorkshopUpkeep.h"
#include "F4TWorkshopPower.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace CommonwealthOnline::Server
{
	using SessionTypes::Json;

	using F4T::GnsSnapshotEnvelope::DecodeResult;

	using F4T::GnsSnapshotEnvelope::Family;

	using F4T::NpcAuthority::Scope;

	using SessionTypes::ComponentKey;

	using SessionTypes::OutstandingAdminOp;

	using F4T::Replication::EntityId;

	using F4T::Replication::PeerId;

	using F4T::Replication::SnapshotState;

	namespace Compatibility = SessionTypes::Compatibility;

	inline bool SessionHasSocialNames(const SessionTypes::Session& a_session) noexcept
	{
		return a_session.compatibilityCapabilities.Has(F4T::Identity::SocialNamesCapability);
	}

	using CommonwealthOnline::Server::TextHelpers::ObjectStateRejectReason;

	using CommonwealthOnline::Server::CombatChecks::RecordCombatActions;

	using CommonwealthOnline::Server::CombatChecks::CombatWindowAllows;

	using CommonwealthOnline::Server::CombatChecks::CombatActionAllows;

	using CommonwealthOnline::Server::CombatChecks::RecordCombatHit;

	using CommonwealthOnline::Server::CombatChecks::RecordAcceptedHit;

	using CommonwealthOnline::Server::CombatChecks::FindAcceptedHit;

	using CommonwealthOnline::Server::SessionTypes::AcceptedHitRecord;

	using CommonwealthOnline::Server::CombatChecks::CombatGeometryAllows;

	using CommonwealthOnline::Server::NpcHitChecks::NpcDamageRateAllows;

	using CommonwealthOnline::Server::NpcHitChecks::NpcGeometryAllows;

	using CommonwealthOnline::Server::NpcLootChecks::NpcLootTakeRateAllows;

	using CommonwealthOnline::Server::NpcLootChecks::NpcLootTakerAllows;

	constexpr std::uint32_t kProtocolVersion = 2;

	constexpr std::size_t kMaximumTransformBytes = 4 * 1024;

	constexpr std::size_t kMaximumNpcStateBytes = 32 * 1024;

	constexpr std::size_t kMaximumNpcLootBytes = 8 * 1024;

	constexpr std::size_t kMaximumPluginListBytes = 64 * 1024;

	constexpr std::size_t kMaximumInventoryTradeBytes = 8 * 1024;

	constexpr std::size_t kMaximumInventorySnapshotBytes = 128 * 1024;

	constexpr std::size_t kMaximumPlayerStateBytes = 32 * 1024;

	constexpr std::size_t kMaximumHelloBytes = 8 * 1024;

	constexpr std::size_t kMaximumSmallPacketBytes = 2 * 1024;

	constexpr std::size_t kMaximumChatSendBytes = 4 * 1024;

	constexpr std::uint32_t kSnapshotKeyframeInterval = 32;

	using SessionTypes::kMaximumCombatPositionAgeSeconds;
	using SessionTypes::kMaximumCampPlacementAgeSeconds;

	using SessionTypes::kMaximumNpcSnapshotAgeSeconds;

	constexpr double kNpcLootCacheLifetimeSeconds = 30.0 * 60.0;

	constexpr std::size_t kMaximumNpcLootsPerScope = 64;

	constexpr std::size_t kMaximumNpcLootScopes = 8192;

	constexpr double kMaximumObjectReach = 512.0;

	constexpr double kObjectIntentRefillPerSecond = 4.0;

	constexpr double kObjectIntentBurst = 8.0;

	constexpr double kQuestReportRefillPerSecond = 2.0;

	constexpr double kQuestReportBurst = 8.0;

	using SessionTypes::kNpcLootTakeGeometryType;

	using F4T::NpcLootSync::TakeAck;
	using F4T::NpcLootSync::TakeAckState;
	using F4T::NpcLootSync::TakeAckToJson;
	using F4T::NpcLootSync::TakeLedger;
	using F4T::NpcLootSync::TakeLifetimeMilliseconds;
	using F4T::NpcLootSync::TakeOp;
	using F4T::NpcLootSync::TakeState;

	constexpr double kVitalsUpdatesRefillPerSecond = 4.0;

	constexpr double kVitalsUpdatesBurst = 8.0;

	using SessionTypes::StoredMovementState;

	using SessionTypes::CombatActionState;

	using SessionTypes::NpcDamageRecord;

	struct CachedNpcLoot
	{
		Json packet;
		std::uint32_t revision{};
		std::uint32_t epoch{};
		double unixTimeSeconds{};
		std::optional<F4T::InterestScope::StateScope> interest;
	};

	using SessionTypes::WorkshopOperationRecord;

	using SessionTypes::Session;

	struct CompatibilityRouteKey
	{
		PeerId peerId{};
		ComponentKey component;

		[[nodiscard]] friend bool operator<(const CompatibilityRouteKey& a_lhs, const CompatibilityRouteKey& a_rhs) noexcept
		{
			if (a_lhs.peerId != a_rhs.peerId) {
				return a_lhs.peerId < a_rhs.peerId;
			}
			return a_lhs.component < a_rhs.component;
		}
	};

	struct CompatibilityProjectionState
	{
		std::uint64_t latestRevision{};
		std::vector<std::uint8_t> bytes;
		bool hasBytes{};
		bool dirty{};
	};

	std::size_t MaximumBytesForFamily(Family a_family) noexcept;

	std::size_t MaximumBytesForType(std::string_view a_type) noexcept;

	void ExpireAdminOps(Session& a_session, std::chrono::steady_clock::time_point a_now);

	Json SessionStatePacket(
		const Session& a_session,
		F4T::Session::LifecycleState a_state,
		double a_unixTimeSeconds);

	bool IsSpaceChangeMovementType(std::string_view a_movementType) noexcept;

	std::optional<Compatibility::SessionCapabilities> NormalizeCompatibilityCapabilities(const Json& a_packet);

	std::string NormalizeFormId(std::string_view a_value);

	std::size_t FamilyIndex(Family a_family);

	std::optional<Family> FamilyForPacketType(std::string_view a_type);

	std::optional<Json> ParsePacket(std::span<const std::uint8_t> a_payload);

	std::optional<StoredMovementState> NormalizeTransform(Json& a_packet);

	std::optional<Json> NormalizePlayerState(const Json& a_packet);

	std::optional<Json> NormalizeWorldState(const Json& a_packet);

	std::optional<Json> NormalizeNpcState(const Json& a_packet);

	std::optional<Json> NormalizeCombatHit(const Json& a_packet);

	std::optional<Json> NormalizeNpcHit(const Json& a_packet);

	std::optional<Json> NormalizeNpcDamage(const Json& a_packet);

	std::optional<Json> NormalizeNpcLoot(const Json& a_packet);

	std::optional<Json> NormalizeNpcLootTake(const Json& a_packet);

	std::optional<Json> NormalizeNpcLootResult(const Json& a_packet);

	std::optional<Scope> ScopeFromMovement(const std::optional<StoredMovementState>& a_movement);

	std::optional<F4T::InterestScope::StateScope> InterestScopeFromMovement(const std::optional<StoredMovementState>& a_movement);

	std::optional<F4T::InterestScope::StateScope> InterestScopeFromNpc(const Json& a_npc, const Scope& a_scope);

	std::optional<Scope> ScopeFromPacket(const Json& a_packet);

	std::string MovementRejectReason(const F4T::NetworkValidation::MovementValidationResult& a_result);

	Json AuthorityPacket(const F4T::NpcAuthority::Assignment& a_assignment, double a_unixTimeSeconds, const F4T::NpcAuthority::Scope& a_wire);

	Json AuthorityPacket(const F4T::NpcAuthority::Change& a_change, double a_unixTimeSeconds, const F4T::NpcAuthority::Scope& a_wire);

	Json WorldStateHostPacket(std::uint32_t a_playerId, double a_unixTimeSeconds);

	std::vector<OutboundFrame> SendLifecycleState(
		std::uint32_t a_connectionId,
		Session& a_session,
		F4T::Session::LifecycleState a_next,
		double a_unixTimeSeconds);

	std::vector<OutboundFrame> LifecycleFramesForMovement(
		std::uint32_t a_connectionId,
		Session& a_session,
		double a_unixTimeSeconds);

	class ProtocolHost::Impl
	{
	public:
		CharacterSlotService characterSlots;
		std::optional<std::vector<OutboundFrame>> AbandonSlotForAdmin(std::string_view a_account, std::uint32_t a_slot, double a_unixTimeSeconds, bool a_withoutSave = false);
		std::vector<OutboundFrame> DisconnectSession(std::uint32_t a_connectionId, double a_unixTimeSeconds, bool a_cleanGoodbye = false);
		bool EnableSocial(PersistenceGate* a_persistence);
		void ForgetErasedQuestProfiles();
		[[nodiscard]] std::vector<OutboundFrame> RetryNpcAuthority(double a_unixTimeSeconds);
		[[nodiscard]] bool TakePlayerActivity(std::uint32_t a_connectionId);
		[[nodiscard]] Afk::Exemption SessionAfkExemption(std::uint32_t a_connectionId) const noexcept;
		ServerEvents events;
		std::vector<OutboundFrame> deferredFrames;
		StateBagStore stateBag;
		ResourceMessageHandler resourceMessageHandler;
		RuntimeSupport::RefusalWindows movementRefusals;
		[[nodiscard]] Json ReadStateBag(const F4T::StateBagProtocol::BagId& a_bag) const;
		std::vector<OutboundFrame> SetStateBag(
			const F4T::StateBagProtocol::BagId& a_bag,
			std::string_view a_key,
			const Json& a_value,
			std::string& a_failure);
		std::vector<OutboundFrame> ExistingFactionFrames(std::uint32_t a_connectionId, const Session& a_target, double a_unixTimeSeconds);
		std::vector<OutboundFrame> FactionNameFrames(std::uint32_t a_connectionId, double a_monotonicSeconds, double a_unixTimeSeconds);
		std::vector<OutboundFrame> FactionDisconnectFrames(const std::string& a_profileId, double a_unixTimeSeconds);
		std::vector<OutboundFrame> PollNpcLootTakes(double a_monotonicSeconds, double a_unixTimeSeconds);
		std::vector<OutboundFrame> HandleNeedsConsume(std::uint32_t a_connectionId, Session& a_session, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds);
		void LoadSurvivalItems(const std::filesystem::path& a_path);
		std::vector<OutboundFrame> SleepUseWithNeeds(std::uint32_t a_connectionId, Session& a_session, const Json& a_packet, double a_monotonicSeconds);
		std::vector<OutboundFrame> PollSurvivalNeeds(double a_monotonicSeconds, double a_unixTimeSeconds);
		void SettleSurvivalNeeds(std::uint32_t a_connectionId, double a_monotonicSeconds);
		void LogSurvivalWelcome(std::uint32_t a_playerId) const;
		void StartDifficultyTracking(Session& a_session, const Json& a_packet, double a_monotonicSeconds);
		std::vector<OutboundFrame> HandleDifficultyReport(std::uint32_t a_connectionId, Session& a_session, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds);
		std::vector<OutboundFrame> PollDifficulty(double a_monotonicSeconds, double a_unixTimeSeconds);
		void ResetDifficultyTracking(double a_monotonicSeconds);
		[[nodiscard]] static bool DifficultyFieldValid(const Json& a_value) noexcept;
	private:
		[[nodiscard]] bool SurvivalNeedsActive() const;
		void AccrueAwakeNeeds(const std::vector<std::uint32_t>& a_connectionIds, double a_monotonicSeconds);
		[[nodiscard]] bool ApplySleepCredit(const std::string& a_profileId);
		void RecordNeedsDelivery(const OutboundFrame& a_frame, bool a_sent);
		void ObserveNeedsActivity(double a_monotonicSeconds);
		void CloseSleepInterval(std::uint32_t a_connectionId, Session& a_session, double a_monotonicSeconds);
		void RecordSleepDelivery(const OutboundFrame& a_frame, bool a_sent);
		std::vector<OutboundFrame> ResyncSleepRelays();
		std::vector<OutboundFrame> ApplyDifficultyStatus(std::uint32_t a_connectionId, Session& a_session, double a_monotonicSeconds, double a_unixTimeSeconds);
		F4T::SurvivalItems::Catalog survivalItems;
		std::map<std::string, double> awakeRetainedByProfile;
		std::map<std::string, double> sleepCreditByProfile;
		double nextSurvivalPollMonotonic{};
		bool needsObservedActive{};
		double nextDifficultySweepMonotonic{};
		std::unordered_map<std::uint32_t, double> sleepRefusedLogMonotonic;
		std::optional<std::vector<OutboundFrame>> DispatchCharacterSlotPacket(
			const std::string& a_type,
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds);
		std::vector<OutboundFrame> ForgetErasedProfiles(double a_unixTimeSeconds);
		std::optional<std::vector<OutboundFrame>> VetoQuest(std::uint32_t a_connectionId, const Session& a_session, const std::string& a_type, const Json& a_packet, double a_unixTimeSeconds);
		std::optional<std::vector<OutboundFrame>> VetoInventory(const std::string& a_type, std::uint32_t a_connectionId, Session& a_session, const Json& a_packet);
		std::optional<std::vector<OutboundFrame>> VetoWorkshop(std::uint32_t a_connectionId, Session& a_session, const Json& a_packet, const F4T::Workshop::Request& a_request, double a_monotonicSeconds);
		struct PeerSnapshotSender
		{
			SnapshotState baseline;
			SnapshotState lastPublished;
			std::uint32_t baselineSequence{};
			std::uint32_t lastSnapshotSequence{};
			std::uint32_t publishesSinceBaseline{};
			bool hasBaseline{};
			bool pendingRetry{};
		};

		enum class SnapshotSendStatus : std::uint8_t
		{
			Emitted,
			SuppressedUnchanged,
			Failed
		};

		struct SnapshotSendResult
		{
			SnapshotSendStatus status{ SnapshotSendStatus::Failed };
			std::optional<OutboundFrame> frame;
		};

		enum class CompatibilityViewStatus : std::uint8_t
		{
			NoSurface,
			Complete,
			Incomplete
		};

		struct CompatibilityView
		{
			CompatibilityViewStatus status{ CompatibilityViewStatus::NoSurface };
			SnapshotState state;
		};

		struct RouteUpdateResult
		{
			bool accepted{};
			std::vector<OutboundFrame> frames;
			std::set<PeerId> added;
			std::set<PeerId> removed;
		};

		%:include "ServerProtocolImpl.inc"
	};
}

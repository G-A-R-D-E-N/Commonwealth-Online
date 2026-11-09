#pragma once

#include "F4TCompatibilityShim.h"
#include "F4TDifficultyEnforcement.h"
#include "F4TInterestScope.h"
#include "F4TLoadOrder.h"
#include "F4TNetworkValidation.h"
#include "F4TNpcAuthority.h"
#include "F4TPacketRateLimit.h"
#include "F4TPlayerStateValidation.h"
#include "F4TPrivacyValidation.h"
#include "F4TSessionLifecycle.h"
#include "F4TSnapshotDeltaModel.h"
#include "F4TStateBagProtocol.h"
#include "F4TWorkshopRaids.h"
#include "F4TWorkshopTransactions.h"
#include "ServerAfk.h"
#include "ServerChat.h"
#include "ServerDowned.h"
#include "ServerEnforcement.h"
#include "ServerFactions.h"
#include "ServerFriends.h"
#include "ServerFireteam.h"
#include "ServerProtocol.h"
#include "ServerVoice.h"

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server
{
	namespace SessionTypes
	{
		using Json = nlohmann::json;
		using F4T::Replication::ComponentKey;
		namespace Compatibility = F4T::Compatibility;

		inline std::uint32_t EquippedFormId(const Json& a_state, std::string_view a_slot)
		{
			const auto items = a_state.find("equippedItems");
			if (items == a_state.end() || !items->is_array()) {
				return 0;
			}
			for (const auto& item : *items) {
				if (!item.is_object() || item.value("slot", std::string{}) != a_slot) {
					continue;
				}
				return static_cast<std::uint32_t>(std::strtoul(item.value("formId", std::string{}).c_str(), nullptr, 16));
			}
			return 0;
		}

		inline std::optional<F4T::WorkshopRaids::ExplosiveKind> EquippedExplosiveKind(const Json& a_state)
		{
			for (const char* slot : { F4T::PlayerStateValidation::kThrowableSlot, F4T::PlayerStateValidation::kRightHandSlot }) {
				if (const auto kind = F4T::WorkshopRaids::ExplosiveKindForSource(EquippedFormId(a_state, slot))) {
					return kind;
				}
			}
			return std::nullopt;
		}

		inline constexpr double kCombatActionLifetimeSeconds = 2.0;
		inline constexpr std::size_t kMaximumRecentCombatActions = 64;
		inline constexpr double kCombatHitWindowSeconds = 1.0;
		inline constexpr std::uint32_t kMaximumCombatHitsPerWindow = 30;
		inline constexpr std::uint32_t kMaximumUnarmedHitsPerAction = 1;
		inline constexpr std::uint32_t kMaximumMeleeHitsPerAction = 1;
		inline constexpr std::uint32_t kMaximumRangedHitsPerAction = 16;
		inline constexpr double kMaximumCombatPositionAgeSeconds = 5.0;
		inline constexpr double kMaximumCampPlacementAgeSeconds = 3.0;
		inline constexpr double kMaximumUnarmedHitDistance = 256.0;
		inline constexpr double kMaximumMeleeHitDistance = 384.0;
		inline constexpr double kMaximumNpcSnapshotAgeSeconds = 5.0;
		inline constexpr double kNpcRespawnCooldownSeconds = 1800.0;
		inline constexpr double kNpcKillCreditCooldownSeconds = 1800.0;
		inline constexpr double kNpcQuarantineSeconds = 120.0;
		inline constexpr double kNpcMaximumSpeedPerSecond = 3000.0;
		inline constexpr double kNpcTeleportSlack = 1500.0;
		inline constexpr double kNpcContinuityMaxGapSeconds = 5.0;
		inline constexpr std::size_t kNpcLedgerMaximumEntries = 8192;
		inline constexpr double kMaximumNpcDamageSumPerWindow = 1000.0;
		inline constexpr double kMaximumRangedHitDistance = F4T::InterestScope::ExteriorInterestRadius;
		inline constexpr double kNpcLootTakeWindowSeconds = 1.0;
		inline constexpr std::size_t kMaximumNpcLootTakesPerWindow = 10;
		inline constexpr double kNpcDamageWindowSeconds = 1.0;
		inline constexpr std::size_t kMaximumNpcDamagePerWindow = 30;
		inline constexpr double kAssistHitWindowSeconds = 5.0;
		inline constexpr double kNonKillAssistPositionAgeSeconds = 10.0;
		inline constexpr double kNonKillAssistWindowSeconds = 60.0;
		inline constexpr std::size_t kMaximumNonKillAssistsPerQuestWindow = 6;
		inline constexpr std::size_t kMaximumNonKillAssistsPerWindow = 20;
		inline constexpr double kNonKillClaimCooldownSeconds = 120.0;
		inline constexpr std::size_t kMaximumNonKillClaims = 128;
		inline constexpr std::size_t kMaximumNonKillQuestKeys = 32;
		inline constexpr std::size_t kMaximumRecentAcceptedHits = 256;
		inline constexpr std::size_t kMaximumNpcDamagePerTargetWindow = 5;
		inline constexpr double kMaximumNpcLootTakeDistance = 768.0;
		inline constexpr std::uint32_t kNpcLootTakeGeometryType = 4U;
		inline constexpr double kShotReplayRefillPerSecond = 20.0;
		inline constexpr double kShotReplayBurst = 40.0;
		inline constexpr double kExplosionSpawnRefillPerSecond = 10.0;
		inline constexpr double kExplosionSpawnBurst = 20.0;
		inline constexpr double kFurnitureUseRefillPerSecond = 20.0;
		inline constexpr double kFurnitureUseBurst = 40.0;
		inline constexpr double kDurablePlayerStateRelayIntervalSeconds = 0.5;
		inline constexpr double kMaximumWorldGameHourStepPerPacket = 1.0;
		inline constexpr double kMinimumWorldWeatherChangeIntervalSeconds = 3.0;
		inline constexpr double kWorldGameTimeResyncSeconds = 5.0;
		inline constexpr double kWorldGameDaysAdvanceSlack = 2.0;
		inline constexpr double kMaximumWorldGameDaysPerRealSecond = 1.0 / 60.0;
		inline constexpr double kWorldGameDaysResyncIntervalSeconds = 1800.0;
		inline constexpr double kReconnectGraceSeconds = 60.0;
		inline constexpr std::size_t kHeldPlayersPerMaximumPlayer = 4;

		struct HeldPlayer
		{
			std::uint32_t playerId{};
			double expiresAtUnixSeconds{};
			std::string persistenceProfile;
		};

		struct WorldDaysBaseline
		{
			std::optional<double> gameDays;
			double acceptedUnix{};
			std::optional<double> lastResyncUnix;
		};

		struct StoredMovementState
		{
			double x{};
			double y{};
			double z{};
			double angleZ{};
			std::string cellId;
			std::string worldspaceId;
			std::string movementType{ "normal" };
			std::string cellKey;
			std::string worldspaceKey;
		};

		struct CombatActionState
		{
			std::uint32_t sequence{};
			std::uint32_t type{};
			double monotonicSeconds{};
			std::uint32_t acceptedHits{};
		};

		struct NpcDamageRecord
		{
			double monotonicSeconds{};
			std::uint32_t npcId{};
			std::uint32_t targetPlayerId{};
			double damage{};
		};

		struct AcceptedHitRecord
		{
			double monotonicSeconds{};
			std::uint32_t sequence{};
			std::uint32_t attackSequence{};
			std::uint32_t targetId{};
			std::string sourceFormId;
			std::string cellId;
			std::string worldspaceId;
			bool npc{};
			std::set<std::string> creditKeys;
			std::uint32_t authorityEpoch{};
		};

		struct PendingKillAssist
		{
			nlohmann::json packet;
			double deadlineMonotonic{};
		};

		struct NonKillAssistState
		{
			std::deque<double> recent;
			std::map<std::string, std::deque<double>> perQuest;
			std::map<std::string, double> claims;
		};

		struct WorkshopOperationRecord
		{
			F4T::Workshop::Request request;
			F4T::Workshop::Result result;
		};

		struct PendingInventorySnapshot
		{
			std::string operationId;
			std::uint32_t baseRevision{};
			std::uint32_t parts{};
			std::uint32_t nextPart{};
			Json items = Json::array();
		};

		struct ParkedSuitBreachChallenge
		{
			std::string suitId;
			std::uint32_t seed{};
			std::size_t length{};
			double issuedAtMonotonic{};
		};

		struct ParkedSuitFrameObservation
		{
			std::string frameBaseFormId;
			double observedMonotonic{};
		};

		struct ParkedSuitPieceAudit
		{
			std::string profileId;
			std::string suitId;
			std::vector<std::string> pieceKeys;
			bool appearedAfterEntry{};
			double deadlineMonotonic{};
		};

		struct OutstandingAdminOp
		{
			std::uint32_t opId{};
			std::chrono::steady_clock::time_point issuedAt{};
		};

		struct Session
		{
			std::uint32_t playerId{};
			std::uint64_t playerIncarnation{};
			std::string profileId;
			std::string socialKey;
			std::string questProfile;
			// Constraint: set on every hello from the slot service; a no-slot session on a slot server has no social identity.
			bool socialKeyRequired{};
			std::string steamId;
			bool active{};
			std::array<std::uint32_t, 2> inboundSnapshotSequences{};
			std::array<std::uint32_t, 2> outboundSnapshotSequences{};
			std::uint64_t outboundVisibilitySequence{};
			std::optional<StoredMovementState> lastMovement;
			std::optional<StoredMovementState> pausedMovement;
			std::uint64_t movementEpoch{};
			std::uint64_t lastMovementSequence{};
			std::uint64_t acceptedResumeTransitionId{};
			std::uint64_t acknowledgedResumeTransitionId{};
			bool visibilityPaused{};
			bool resumeAwaitingTransform{};
			std::optional<double> lastMovementMonotonic;
			std::optional<F4T::InterestScope::StateScope> objectScanPoint;
			bool playerActivityPending{};
			Afk::Exemption afkExemption;
			std::optional<Json> lastTransformPacket;
			std::optional<Json> lastSleepPacket;
			std::optional<Json> lastSeatPacket;
			std::set<std::uint32_t> sleepResyncPlayers;
			std::optional<double> sleepStartedMonotonic;
			std::optional<double> needsAccruedMonotonic;
			std::optional<std::uint64_t> sentNeedsRevision;
			std::optional<Json> lastDurablePlayerState;
			std::optional<F4T::WorkshopRaids::ExplosiveKind> lastEquippedExplosive;
			double lastEquippedExplosiveMonotonic{};
			std::optional<std::uint64_t> lastDurableDigest;
			std::optional<double> lastDurableRelayMonotonic;
			bool durableRelayPending{};
			std::optional<double> lastWorldGameHour;
			bool pendingWorldTimeSync{};
			std::optional<std::string> lastWorldWeatherFormId;
			double lastWorldTimeAcceptedUnix{};
			double lastWorldWeatherChangeUnix{};
			std::optional<Json> pluginListPacket;
			std::optional<F4T::LoadOrder::PluginList> pluginList;
			std::map<F4T::NpcAuthority::Scope, std::uint32_t> npcAuthorityRetries;
			bool pluginListResendPending{};
			F4T::NetworkValidation::MovementTransitionBudget movementTransitions;
			std::uint32_t lastActionSequenceSeen{};
			std::deque<CombatActionState> combatActions;
			std::uint32_t lastCombatSequence{};
			std::optional<double> combatWindowStartMonotonic;
			std::uint32_t combatHitsInWindow{};
			ViolationWindow teleportRejections;
			ViolationWindow pvpOverCapClaims;
			std::map<F4T::Workshop::OperationId, WorkshopOperationRecord> workshopOperations;
			std::deque<F4T::Workshop::OperationId> workshopOperationOrder;
			std::set<F4T::Workshop::BuildZoneId> workshopZonesInside;
			F4T::PacketRateLimit::TokenBucket workshopSnapshotRequests;
			F4T::PacketRateLimit::TokenBucket settlementAccessRequests;
			F4T::PacketRateLimit::TokenBucket settlementCaptureRequests;
			std::size_t settlementListSentZones{ static_cast<std::size_t>(-1) };
			bool campGrantsSent{};
			std::uint64_t sentPlayerProtectedUntil{};
			F4T::PacketRateLimit::TokenBucket characterSlotRequests;
			F4T::PacketRateLimit::TokenBucket adminRequests;
			bool adminSecretVerified{};
			bool adminRejectLogged{};
			std::uint32_t lastNpcHitSequence{};
			std::uint32_t lastNpcDamageSequence{};
			std::deque<NpcDamageRecord> recentNpcDamage;
			std::deque<double> recentNpcLootTakes;
			std::deque<double> recentVendorOps;
			std::deque<AcceptedHitRecord> recentAcceptedHits;
			std::deque<PendingKillAssist> pendingKillAssists;
			NonKillAssistState nonKillAssists;
			ChatSessionState chat;
			VoiceSessionState voice;
			FactionSessionState faction;
			FriendSessionState friends;
			F4T::PrivacyValidation::Settings privacy;
			F4T::PacketRateLimit::TokenBucket privacySets;
			FireteamSessionState fireteams;
			Compatibility::SessionCapabilities compatibilityCapabilities;
			std::map<ComponentKey, std::uint64_t> compatibilityRevisions;
			std::optional<InventoryReportSnapshot> inventoryReport;
			std::vector<OutstandingAdminOp> outstandingAdminOps;
			std::optional<AdminItemResultSnapshot> lastAdminItemResult;
			F4T::PacketRateLimit::TokenBucket inventoryRequests;
			F4T::PacketRateLimit::TokenBucket deathBagPlacements;
			F4T::PacketRateLimit::TokenBucket needsConsumes;
			std::optional<PendingInventorySnapshot> pendingInventorySnapshot;
			F4T::PacketRateLimit::TokenBucket objectStateRequests;
			F4T::PacketRateLimit::TokenBucket parkedSuitRequests;
			std::optional<ParkedSuitBreachChallenge> parkedSuitChallenge;
			std::optional<ParkedSuitPieceAudit> parkedSuitPieceAudit;
			F4T::PacketRateLimit::TokenBucket questReports;
			F4T::PacketRateLimit::TokenBucket questAssists;
			bool questReady{};
			bool questUpdateNoticeSent{};
			F4T::PacketRateLimit::TokenBucket vitalsUpdates;
			F4T::PacketRateLimit::TokenBucket shotReplays;
			F4T::PacketRateLimit::TokenBucket animEventReplays;
			F4T::PacketRateLimit::TokenBucket explosionSpawns;
			F4T::PacketRateLimit::TokenBucket furnitureUses;
			F4T::PacketRateLimit::TokenBucket resourceMessages{ F4T::StateBagProtocol::kMaximumResourceMessagesPerSecond };
			std::map<std::string, std::string> openResourceViews;
			std::set<std::string> resourceViewResync;
			bool stateBagResync{};
			double stateBagRetryAt{};
			F4T::Session::LifecycleTracker lifecycle;
			DownedSessionState downed;
			F4T::DifficultyEnforcement::SessionState difficulty;
			bool difficultyExempt{};
		};

		inline std::string_view SessionNameView(const Session& a_session)
		{
			if (a_session.lastDurablePlayerState) {
				const auto found = a_session.lastDurablePlayerState->find("characterName");
				if (found != a_session.lastDurablePlayerState->end() && found->is_string() && !found->get_ref<const std::string&>().empty()) {
					return found->get_ref<const std::string&>();
				}
			}
			return "Unknown";
		}

		inline std::string_view SessionSocialKeyView(const Session& a_session)
		{
			if (!a_session.socialKey.empty()) return std::string_view{ a_session.socialKey };
			if (a_session.socialKeyRequired) return {};
			return std::string_view{ a_session.profileId };
		}

		inline std::string_view SessionQuestProfileView(const Session& a_session)
		{
			if (a_session.socialKeyRequired) return a_session.questProfile;
			return a_session.profileId;
		}
	}
}

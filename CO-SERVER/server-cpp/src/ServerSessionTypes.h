#pragma once

#include "F4TCompatibilityShim.h"
#include "F4TInterestScope.h"
#include "F4TNetworkValidation.h"
#include "F4TPacketRateLimit.h"
#include "F4TSessionLifecycle.h"
#include "F4TSnapshotDeltaModel.h"
#include "F4TWorkshopTransactions.h"
#include "ServerChat.h"
#include "ServerDowned.h"
#include "ServerFactions.h"
#include "ServerVoice.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace CommonwealthOnline::Server
{
	namespace SessionTypes
	{
		using Json = nlohmann::json;
		using F4T::Replication::ComponentKey;
		namespace Compatibility = F4T::Compatibility;

		inline constexpr double kCombatActionLifetimeSeconds = 2.0;
		inline constexpr std::size_t kMaximumRecentCombatActions = 64;
		inline constexpr double kCombatHitWindowSeconds = 1.0;
		inline constexpr std::uint32_t kMaximumCombatHitsPerWindow = 30;
		inline constexpr std::uint32_t kMaximumUnarmedHitsPerAction = 1;
		inline constexpr std::uint32_t kMaximumMeleeHitsPerAction = 1;
		inline constexpr std::uint32_t kMaximumRangedHitsPerAction = 16;
		inline constexpr double kMaximumCombatPositionAgeSeconds = 5.0;
		inline constexpr double kMaximumUnarmedHitDistance = 256.0;
		inline constexpr double kMaximumMeleeHitDistance = 384.0;
		inline constexpr double kMaximumNpcSnapshotAgeSeconds = 5.0;
		inline constexpr double kMaximumRangedHitDistance = F4T::InterestScope::ExteriorInterestRadius;
		inline constexpr double kNpcLootTakeWindowSeconds = 1.0;
		inline constexpr std::size_t kMaximumNpcLootTakesPerWindow = 10;
		inline constexpr double kNpcDamageWindowSeconds = 1.0;
		inline constexpr std::size_t kMaximumNpcDamagePerWindow = 30;
		inline constexpr std::size_t kMaximumNpcDamagePerTargetWindow = 5;
		inline constexpr double kMaximumNpcLootTakeDistance = 768.0;
		inline constexpr std::uint32_t kNpcLootTakeGeometryType = 4U;
		inline constexpr double kShotReplayRefillPerSecond = 20.0;
		inline constexpr double kShotReplayBurst = 40.0;

		struct StoredMovementState
		{
			double x{};
			double y{};
			double z{};
			double angleZ{};
			std::string cellId;
			std::string worldspaceId;
			std::string movementType{ "normal" };
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
		};

		struct WorkshopOperationRecord
		{
			F4T::Workshop::Request request;
			F4T::Workshop::Result result;
		};

		struct Session
		{
			std::uint32_t playerId{};
			std::string profileId;
			std::string steamId;
			bool active{};
			std::array<std::uint32_t, 2> inboundSnapshotSequences{};
			std::array<std::uint32_t, 2> outboundSnapshotSequences{};
			std::optional<StoredMovementState> lastMovement;
			std::optional<double> lastMovementMonotonic;
			std::optional<Json> lastTransformPacket;
			std::optional<Json> lastDurablePlayerState;
			std::optional<Json> pluginListPacket;
			F4T::NetworkValidation::MovementTransitionBudget movementTransitions;
			std::uint32_t lastActionSequenceSeen{};
			std::deque<CombatActionState> combatActions;
			std::uint32_t lastCombatSequence{};
			std::optional<double> combatWindowStartMonotonic;
			std::uint32_t combatHitsInWindow{};
			std::map<F4T::Workshop::OperationId, WorkshopOperationRecord> workshopOperations;
			std::deque<F4T::Workshop::OperationId> workshopOperationOrder;
			std::set<F4T::Workshop::BuildZoneId> workshopZonesInside;
			F4T::PacketRateLimit::TokenBucket workshopSnapshotRequests;
			F4T::PacketRateLimit::TokenBucket settlementAccessRequests;
			F4T::PacketRateLimit::TokenBucket settlementCaptureRequests;
			std::size_t settlementListSentZones{ static_cast<std::size_t>(-1) };
			F4T::PacketRateLimit::TokenBucket characterSlotRequests;
			F4T::PacketRateLimit::TokenBucket adminRequests;
			bool adminSecretVerified{};
			bool adminRejectLogged{};
			std::uint32_t lastNpcHitSequence{};
			std::uint32_t lastNpcDamageSequence{};
			std::deque<NpcDamageRecord> recentNpcDamage;
			std::deque<double> recentNpcLootTakes;
			ChatSessionState chat;
			VoiceSessionState voice;
			FactionSessionState faction;
			Compatibility::SessionCapabilities compatibilityCapabilities;
			std::map<ComponentKey, std::uint64_t> compatibilityRevisions;
			F4T::PacketRateLimit::TokenBucket inventoryRequests;
			F4T::PacketRateLimit::TokenBucket objectStateRequests;
			F4T::PacketRateLimit::TokenBucket questReports;
			bool questReady{};
			bool questUpdateNoticeSent{};
			F4T::PacketRateLimit::TokenBucket vitalsUpdates;
			F4T::PacketRateLimit::TokenBucket shotReplays;
			F4T::PacketRateLimit::TokenBucket animEventReplays;
			F4T::Session::LifecycleTracker lifecycle;
			DownedSessionState downed;
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
	}
}

#pragma once

#include "ServerProtocol.h"
#include "PersistenceGate.h"
#include "ServerCharacterSlots.h"
#include "ServerChat.h"
#include "ServerCombatChecks.h"
#include "ServerDowned.h"
#include "ServerFactions.h"
#include "ServerAdminMenu.h"
#include "ServerSettlementAccess.h"
#include "ServerSettlementCapture.h"
#include "ServerNpcHitChecks.h"
#include "ServerNpcLootChecks.h"
#include "ServerOutboundFrames.h"
#include "ServerPacketRead.h"
#include "ServerSessionTypes.h"
#include "ServerShotReplayRelay.h"
#include "ServerTextHelpers.h"
#include "ServerVoiceHost.h"
#include "ServerWelcome.h"

#include "F4TAuthoritativePacketValidation.h"
#include "F4TCompatibilityShim.h"
#include "F4TGnsSnapshotEnvelope.h"
#include "F4TInterestScope.h"
#include "F4TNetworkValidation.h"
#include "F4TNpcAuthority.h"
#include "F4TNpcCoopValidation.h"
#include "F4TPacketRateLimit.h"
#include "F4TPeerEntitySet.h"
#include "F4TDownedValidation.h"
#include "F4TDropLedger.h"
#include "F4TInventoryWire.h"
#include "F4TLoadOrder.h"
#include "F4TObjectState.h"
#include "F4TObjectWire.h"
#include "F4TQuestProtocol.h"
#include "F4TQuestState.h"
#include "F4TFactionValidation.h"
#include "F4TPlayerStateValidation.h"
#include "F4TSessionLifecycle.h"
#include "F4TTransformValidation.h"
#include "F4TUtf8Path.h"
#include "F4TWorkshopProtocol.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string_view>
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

	using F4T::Replication::EntityId;

	using F4T::Replication::PeerId;

	using F4T::Replication::SnapshotState;

	namespace Compatibility = SessionTypes::Compatibility;

	using CommonwealthOnline::Server::TextHelpers::ObjectStateRejectReason;

	using CommonwealthOnline::Server::CombatChecks::RecordCombatActions;

	using CommonwealthOnline::Server::CombatChecks::CombatWindowAllows;

	using CommonwealthOnline::Server::CombatChecks::CombatActionAllows;

	using CommonwealthOnline::Server::CombatChecks::RecordCombatHit;

	using CommonwealthOnline::Server::CombatChecks::CombatGeometryAllows;

	using CommonwealthOnline::Server::NpcHitChecks::NpcDamageRateAllows;

	using CommonwealthOnline::Server::NpcHitChecks::NpcGeometryAllows;

	using CommonwealthOnline::Server::NpcLootChecks::NpcLootTakeRateAllows;

	using CommonwealthOnline::Server::NpcLootChecks::NpcLootTakerAllows;

	constexpr std::uint32_t kProtocolVersion = 2;

	constexpr std::size_t kMaximumMessageBytes = 64 * 1024;

	constexpr std::size_t kMaximumTransformBytes = 4 * 1024;

	constexpr std::size_t kMaximumNpcStateBytes = 32 * 1024;

	constexpr std::size_t kMaximumNpcLootBytes = 8 * 1024;

	constexpr std::size_t kMaximumPluginListBytes = 64 * 1024;

	constexpr std::size_t kMaximumInventoryTradeBytes = 8 * 1024;

	constexpr std::size_t kMaximumPlayerStateBytes = 32 * 1024;

	constexpr std::size_t kMaximumHelloBytes = 8 * 1024;

	constexpr std::size_t kMaximumSmallPacketBytes = 2 * 1024;

	constexpr std::size_t kMaximumChatSendBytes = 4 * 1024;

	constexpr std::uint32_t kSnapshotKeyframeInterval = 32;

	using SessionTypes::kMaximumCombatPositionAgeSeconds;

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

	constexpr double kNpcLootTakeLifetimeSeconds = 3.0;

	constexpr std::size_t kMaximumPendingNpcLootTakes = 256;

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

	struct PendingNpcLootTake
	{
		std::uint32_t takerConnectionId{};
		std::uint32_t takerPlayerId{};
		std::uint32_t authorityPlayerId{};
		std::uint32_t epoch{};
		Scope scope;
		std::uint32_t npcId{};
		std::string itemId;
		std::uint32_t count{};
		double createdMonotonicSeconds{};
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

	std::optional<F4T::InterestScope::StateScope> InterestScopeFromNpc(const Json& a_npc);

	std::optional<Scope> ScopeFromPacket(const Json& a_packet);

	std::string MovementRejectReason(const F4T::NetworkValidation::MovementValidationResult& a_result);

	Json AuthorityPacket(const F4T::NpcAuthority::Assignment& a_assignment, double a_unixTimeSeconds);

	Json AuthorityPacket(const F4T::NpcAuthority::Change& a_change, double a_unixTimeSeconds);

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
	private:
		std::optional<std::vector<OutboundFrame>> DispatchCharacterSlotPacket(
			const std::string& a_type,
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds);
		%:include "ServerProtocolImpl.inc"
	};
}

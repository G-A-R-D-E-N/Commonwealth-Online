#include "pch.h"

#include "F4TNpcSync.h"

#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TDebugLogging.h"
#include "F4TNetworkValidation.h"
#include "F4TNpcActorGuard.h"
#include "F4TNpcCoopHit.h"
#include "F4TRemoteNpcState.h"
#include "F4TRemotePlayerState.h"

#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <string>

#include <nlohmann/json.hpp>

namespace F4T::Networking
{
	using namespace Detail;

	namespace
	{
		bool IsValidNpcHitDamage(float a_damage)
		{
			return std::isfinite(a_damage) &&
			       a_damage >= F4T::NpcCoop::kMinimumNpcHitDamage &&
			       a_damage <= F4T::NpcCoop::kMaximumNpcHitDamage;
		}

		bool IsValidNpcHitEpoch(std::uint64_t a_epoch)
		{
			return a_epoch != 0 && a_epoch <= (std::numeric_limits<std::uint32_t>::max)();
		}

		bool IsValidNpcHitScope(const std::string& a_cellId, const std::string& a_worldspaceId)
		{
			return F4T::NetworkValidation::IsValidHexFormId(a_cellId, false, false) &&
			       F4T::NetworkValidation::IsValidHexFormId(a_worldspaceId, true, true);
		}

		bool IsValidNpcDamage(float a_damage)
		{
			return std::isfinite(a_damage) &&
			       a_damage >= F4T::NpcCoop::kMinimumNpcHitDamage &&
			       a_damage <= F4T::NpcCoop::kMaximumNpcDamage;
		}
	}

	bool SendNpcHitPacket(
		const F4T::NpcCoop::NpcHitIdentity& a_identity,
		std::uint32_t a_sequence,
		std::uint32_t a_attackSequence,
		float a_damage,
		std::uint32_t a_weaponFormId)
	{
		if (!IsConnectedToServer() || !F4T::NpcActorGuard::IsInstalled()) {
			return false;
		}
		if (a_identity.npcId == 0 || a_identity.sourceFormId == 0 ||
			F4T::RemoteNpcState::IsRuntimeFormId(a_identity.sourceFormId) ||
			a_sequence == 0 || a_attackSequence == 0 ||
			!IsValidNpcHitEpoch(a_identity.authorityEpoch) ||
			!IsValidNpcHitDamage(a_damage) ||
			!IsValidNpcHitScope(a_identity.cellId, a_identity.worldspaceId)) {
			LogWarningWithLocalPlayerPrefix(std::format(
				"Could not send invalid npcHit: npcId={}, sequence={}, attackSequence={}, damage={}.",
				a_identity.npcId,
				a_sequence,
				a_attackSequence,
				a_damage));
			return false;
		}

		const auto localPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
		const auto authority = GetNpcAuthorityForScope(a_identity.cellId, a_identity.worldspaceId);
		if (!localPlayerId || !authority || authority->playerId == *localPlayerId ||
			authority->epoch != a_identity.authorityEpoch) {
			return false;
		}

		nlohmann::json packet{
			{ "type", "npcHit" },
			{ "npcId", a_identity.npcId },
			{ "cellId", a_identity.cellId },
			{ "worldspaceId", a_identity.worldspaceId },
			{ "sourceFormId", std::format("{:08X}", a_identity.sourceFormId) },
			{ "sequence", a_sequence },
			{ "attackSequence", a_attackSequence },
			{ "authorityEpoch", a_identity.authorityEpoch },
			{ "damage", a_damage }
		};
		if (a_weaponFormId != 0) {
			packet["weaponFormId"] = std::format("{:08X}", a_weaponFormId);
		}

		if (!SendPacketBytes(packet.dump())) {
			LogThrottledWarning("npc_hit_send_failed", "Could not send Commonwealth Online npcHit packet.");
			return false;
		}
		return true;
	}

	bool SendNpcDamagePacket(
		const F4T::NpcCoop::NpcHitIdentity& a_identity,
		std::uint32_t a_targetPlayerId,
		std::uint32_t a_sequence,
		float a_damage,
		std::uint32_t a_weaponFormId)
	{
		if (!IsConnectedToServer() || !F4T::NpcActorGuard::IsInstalled()) {
			return false;
		}
		if (a_identity.npcId == 0 || a_identity.sourceFormId == 0 ||
			F4T::RemoteNpcState::IsRuntimeFormId(a_identity.sourceFormId) ||
			a_targetPlayerId == 0 || a_sequence == 0 ||
			!IsValidNpcHitEpoch(a_identity.authorityEpoch) ||
			!IsValidNpcDamage(a_damage) ||
			!IsValidNpcHitScope(a_identity.cellId, a_identity.worldspaceId)) {
			LogWarningWithLocalPlayerPrefix(std::format(
				"Could not send invalid npcDamage: npcId={}, sequence={}, damage={}.",
				a_identity.npcId,
				a_sequence,
				a_damage));
			return false;
		}

		const auto localPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
		const auto authority = GetNpcAuthorityForScope(a_identity.cellId, a_identity.worldspaceId);
		if (!localPlayerId || !authority || authority->playerId != *localPlayerId ||
			authority->epoch != a_identity.authorityEpoch || a_targetPlayerId == *localPlayerId) {
			return false;
		}

		nlohmann::json packet{
			{ "type", "npcDamage" },
			{ "targetPlayerId", a_targetPlayerId },
			{ "npcId", a_identity.npcId },
			{ "cellId", a_identity.cellId },
			{ "worldspaceId", a_identity.worldspaceId },
			{ "sourceFormId", std::format("{:08X}", a_identity.sourceFormId) },
			{ "authorityEpoch", a_identity.authorityEpoch },
			{ "sequence", a_sequence },
			{ "damage", a_damage }
		};
		if (a_weaponFormId != 0) {
			packet["weaponFormId"] = std::format("{:08X}", a_weaponFormId);
		}

		if (!SendPacketBytes(packet.dump())) {
			LogThrottledWarning("npc_damage_send_failed", "Could not send Commonwealth Online npcDamage packet.");
			return false;
		}
		return true;
	}
}

namespace F4T::Networking::Detail
{
	void HandleNpcHitPacket(const Json& a_packet)
	{
		const auto senderId = ReadPlayerId(a_packet, "playerId");
		const auto localPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
		const auto npcId = ReadUint32(a_packet, "npcId");
		const auto sequence = ReadUint32(a_packet, "sequence");
		const auto epoch = ReadUint32(a_packet, "authorityEpoch");
		const auto damage = ReadFloat(a_packet, "damage");
		const auto cellId = ReadString(a_packet, "cellId");
		const auto worldspaceId = ReadString(a_packet, "worldspaceId").value_or("");
		const auto sourceText = ReadString(a_packet, "sourceFormId");
		const auto sourceFormId = sourceText ? ParseHexFormId(*sourceText) : std::nullopt;
		if (!senderId || *senderId == 0 || !localPlayerId || *senderId == *localPlayerId ||
			!npcId || *npcId == 0 || !sequence || *sequence == 0 ||
			!epoch || !IsValidNpcHitEpoch(*epoch) || !damage || !IsValidNpcHitDamage(*damage) ||
			!cellId || !IsValidNpcHitScope(*cellId, worldspaceId) ||
			!sourceText || !F4T::NetworkValidation::IsValidHexFormId(*sourceText, false, false) ||
			!sourceFormId || *sourceFormId == 0 || F4T::RemoteNpcState::IsRuntimeFormId(*sourceFormId)) {
			LogThrottledWarning("npc_hit_malformed", "Ignoring malformed npcHit packet.");
			return;
		}

		std::uint32_t weaponFormId = 0;
		if (a_packet.contains("weaponFormId")) {
			const auto weaponText = ReadString(a_packet, "weaponFormId");
			const auto weapon = weaponText ? ParseHexFormId(*weaponText) : std::nullopt;
			if (!weapon || !F4T::NetworkValidation::IsValidHexFormId(*weaponText, false, true)) {
				LogThrottledWarning("npc_hit_weapon", "Ignoring npcHit packet with an invalid weaponFormId.");
				return;
			}
			weaponFormId = *weapon;
		}

		F4T::NpcCoop::NpcHitClaim claim;
		claim.identity.npcId = *npcId;
		claim.identity.sourceFormId = *sourceFormId;
		claim.identity.cellId = *cellId;
		claim.identity.worldspaceId = worldspaceId;
		claim.identity.authorityEpoch = *epoch;
		claim.senderPlayerId = *senderId;
		claim.sequence = *sequence;
		claim.damage = *damage;
		claim.weaponFormId = weaponFormId;
		if (!F4T::NpcSync::Health::EnqueueNpcHit(std::move(claim))) {
			LogThrottledWarning("npc_hit_dropped", "Dropped an npcHit packet because the pending NPC hit queue is full.");
		}
	}

	void HandleNpcDamagePacket(const Json& a_packet)
	{
		const auto authorityId = ReadPlayerId(a_packet, "authorityPlayerId");
		const auto targetPlayerId = ReadPlayerId(a_packet, "targetPlayerId");
		const auto localPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
		const auto npcId = ReadUint32(a_packet, "npcId");
		const auto sequence = ReadUint32(a_packet, "sequence");
		const auto damage = ReadFloat(a_packet, "damage");
		const auto cellId = ReadString(a_packet, "cellId");
		const auto worldspaceId = ReadString(a_packet, "worldspaceId").value_or("");
		const auto sourceText = ReadString(a_packet, "sourceFormId");
		const auto sourceFormId = sourceText ? ParseHexFormId(*sourceText) : std::nullopt;
		if (!authorityId || *authorityId == 0 || !localPlayerId || *authorityId == *localPlayerId ||
			!targetPlayerId || *targetPlayerId != *localPlayerId ||
			!npcId || *npcId == 0 || !sequence || *sequence == 0 ||
			!damage || !IsValidNpcDamage(*damage) ||
			!cellId || !IsValidNpcHitScope(*cellId, worldspaceId) ||
			!sourceText || !F4T::NetworkValidation::IsValidHexFormId(*sourceText, false, false) ||
			!sourceFormId || *sourceFormId == 0 || F4T::RemoteNpcState::IsRuntimeFormId(*sourceFormId)) {
			LogThrottledWarning("npc_damage_malformed", "Ignoring malformed npcDamage packet.");
			return;
		}

		if (a_packet.contains("weaponFormId")) {
			const auto weaponText = ReadString(a_packet, "weaponFormId");
			const auto weapon = weaponText ? ParseHexFormId(*weaponText) : std::nullopt;
			if (!weapon || !F4T::NetworkValidation::IsValidHexFormId(*weaponText, false, true)) {
				LogThrottledWarning("npc_damage_weapon", "Ignoring npcDamage packet with an invalid weaponFormId.");
				return;
			}
		}

		const auto authority = GetNpcAuthorityForScope(*cellId, worldspaceId);
		if (!authority || authority->playerId != *authorityId) {
			LogThrottledWarning("npc_damage_authority", "Ignoring npcDamage from a sender that is not the authority for that scope.");
			return;
		}

		if (!F4T::RemotePlayerState::QueueIncomingNpcDamage(*authorityId, *sequence, *damage, *npcId)) {
			LogThrottledWarning("npc_damage_dropped", "Dropped an npcDamage packet: duplicate, out-of-order or the queue is full.");
		}
	}
}

#pragma once

#include "F4TGnsSnapshotEnvelope.h"

#include <cmath>
#include <cstdint>
#include <deque>
#include <unordered_map>

namespace F4T::PvPCombatContract
{
	constexpr float kMaxAllowedDamage = 10000.0F;
	constexpr std::size_t kMaxPendingHits = 32;

	inline bool IsValidDamage(float a_damage) noexcept
	{
		return std::isfinite(a_damage) && a_damage > 0.0F && a_damage <= kMaxAllowedDamage;
	}

	inline bool ValidateHitRouting(
		std::uint32_t a_senderId,
		std::uint32_t a_targetPlayerId,
		std::uint32_t a_localPlayerId,
		std::uint32_t a_sequence,
		float a_damage) noexcept
	{
		return a_senderId != 0 &&
			a_sequence != 0 &&
			a_targetPlayerId == a_localPlayerId &&
			a_senderId != a_localPlayerId &&
			IsValidDamage(a_damage);
	}

	struct QueuedCombatHit
	{
		std::uint32_t senderId{};
		std::uint32_t sequence{};
		float damage{};
		std::uint32_t npcId{};
	};

	struct InboundHitTracker
	{
		std::unordered_map<std::uint32_t, std::uint32_t> lastSeenSequence;
		std::deque<QueuedCombatHit> pendingHits;

		bool QueueHit(std::uint32_t a_senderId, std::uint32_t a_sequence, float a_damage, std::uint32_t a_npcId = 0)
		{
			if (a_senderId == 0 || a_sequence == 0 || !IsValidDamage(a_damage)) {
				return false;
			}

			const auto it = lastSeenSequence.find(a_senderId);
			if (it != lastSeenSequence.end() &&
				!F4T::GnsSnapshotEnvelope::IsNewer(a_sequence, it->second)) {
				return false;
			}

			lastSeenSequence[a_senderId] = a_sequence;

			pendingHits.push_back(QueuedCombatHit{ a_senderId, a_sequence, a_damage, a_npcId });
			if (pendingHits.size() > kMaxPendingHits) {
				pendingHits.pop_front();
			}

			return true;
		}

		void PurgeSender(std::uint32_t a_senderId)
		{
			lastSeenSequence.erase(a_senderId);
			std::erase_if(pendingHits, [a_senderId](const QueuedCombatHit& hit) {
				return hit.senderId == a_senderId;
			});
		}
	};
}

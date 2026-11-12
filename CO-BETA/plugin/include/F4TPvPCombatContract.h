#pragma once

#include "F4TGnsSnapshotEnvelope.h"

#include <algorithm>
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
		std::uint32_t count{ 1 };
	};

	struct InboundHitTracker
	{
		std::unordered_map<std::uint32_t, std::uint32_t> lastSeenSequence;
		std::deque<QueuedCombatHit> pendingHits;
		std::uint64_t evictions{};

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

			if (pendingHits.size() >= kMaxPendingHits) {
				const auto newest = std::find_if(pendingHits.rbegin(), pendingHits.rend(), [a_senderId, a_npcId](const QueuedCombatHit& a_queued) {
					return a_queued.senderId == a_senderId && a_queued.npcId == a_npcId;
				});
				if (newest != pendingHits.rend()) {
					newest->damage += a_damage;
					++newest->count;
					return true;
				}
				pendingHits.pop_front();
				++evictions;
			}
			pendingHits.push_back(QueuedCombatHit{ a_senderId, a_sequence, a_damage, a_npcId });
			return true;
		}

		void ForgetSenderSequence(std::uint32_t a_senderId)
		{
			lastSeenSequence.erase(a_senderId);
		}
	};
}

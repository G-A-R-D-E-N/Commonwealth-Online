#include "ServerNpcLedger.h"

#include "ServerSessionTypes.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace CommonwealthOnline::Server::NpcLedger
{
	namespace
	{
		using SessionTypes::kNpcContinuityMaxGapSeconds;
		using SessionTypes::kNpcKillCreditCooldownSeconds;
		using SessionTypes::kNpcLedgerMaximumEntries;
		using SessionTypes::kNpcMaximumSpeedPerSecond;
		using SessionTypes::kNpcQuarantineSeconds;
		using SessionTypes::kNpcRespawnCooldownSeconds;
		using SessionTypes::kNpcTeleportSlack;

		template <typename Map>
		void MakeRoom(Map& a_map)
		{
			if (a_map.size() < kNpcLedgerMaximumEntries) return;
			a_map.erase(std::min_element(a_map.begin(), a_map.end(), [](const auto& a_lhs, const auto& a_rhs) {
				return a_lhs.second.touched < a_rhs.second.touched;
			}));
		}
	}

	void Ledger::ObserveSnapshot(const Scope& a_scope, std::uint32_t a_epoch, std::span<const NpcObservation> a_npcs, double a_monotonicSeconds)
	{
		const auto previous = continuity.find(a_scope);
		const auto elapsed = previous == continuity.end() ? 0.0 : a_monotonicSeconds - previous->second.touched;
		const bool continuous = previous != continuity.end() && previous->second.epoch == a_epoch && elapsed >= 0.0 && elapsed <= kNpcContinuityMaxGapSeconds;
		Continuity current{ a_epoch, a_monotonicSeconds, {} };
		for (const auto& npc : a_npcs) {
			const NpcKey key{ a_scope, LedgerSourceKey(a_epoch, npc.npcId, npc.sourceFormId) };
			const bool wasDead = tombstones.contains(key) && tombstones.at(key).deadSince.has_value();
			if (npc.dead) {
				if (!tombstones.contains(key)) MakeRoom(tombstones);
				auto& tomb = tombstones.try_emplace(key, Tombstone{ std::nullopt, 0.0, a_monotonicSeconds }).first->second;
				if (!tomb.deadSince) tomb.deadSince = a_monotonicSeconds;
			} else if (const auto found = tombstones.find(key); found != tombstones.end() && found->second.deadSince) {
				if (a_monotonicSeconds - *found->second.deadSince < kNpcRespawnCooldownSeconds) {
					Quarantine(key, *found->second.deadSince + kNpcRespawnCooldownSeconds, a_monotonicSeconds);
				} else if (a_monotonicSeconds >= found->second.quarantinedUntil) {
					tombstones.erase(found);
				}
			}
			if (const auto found = tombstones.find(key); found != tombstones.end()) found->second.touched = a_monotonicSeconds;
			if (continuous && !npc.dead && !wasDead) {
				const auto last = previous->second.npcs.find(npc.npcId);
				if (last != previous->second.npcs.end()) {
					const auto dx = npc.x - last->second.x;
					const auto dy = npc.y - last->second.y;
					const auto dz = npc.z - last->second.z;
					const auto distance = std::sqrt(dx * dx + dy * dy + dz * dz);
					if (last->second.sourceFormId != npc.sourceFormId || !(distance <= kNpcMaximumSpeedPerSecond * elapsed + kNpcTeleportSlack)) {
						Quarantine(key, a_monotonicSeconds + kNpcQuarantineSeconds, a_monotonicSeconds);
					}
				}
			}
			current.npcs[npc.npcId] = Position{ npc.sourceFormId, npc.x, npc.y, npc.z };
		}
		if (!continuity.contains(a_scope)) MakeRoom(continuity);
		continuity[a_scope] = std::move(current);
	}

	bool Ledger::Quarantined(const Scope& a_scope, const std::string& a_sourceFormId, double a_monotonicSeconds) const
	{
		const auto found = tombstones.find(NpcKey{ a_scope, a_sourceFormId });
		return found != tombstones.end() && a_monotonicSeconds < found->second.quarantinedUntil;
	}

	bool Ledger::KillClaimed(std::string_view a_claimKey, const Scope& a_scope, const std::string& a_sourceFormId, double a_monotonicSeconds) const
	{
		const auto found = kills.find(KillKey{ std::string{ a_claimKey }, NpcKey{ a_scope, a_sourceFormId } });
		return found != kills.end() && a_monotonicSeconds - found->second.touched < kNpcKillCreditCooldownSeconds;
	}

	void Ledger::RecordKill(std::string_view a_claimKey, const Scope& a_scope, const std::string& a_sourceFormId, double a_monotonicSeconds)
	{
		KillKey key{ std::string{ a_claimKey }, NpcKey{ a_scope, a_sourceFormId } };
		if (!kills.contains(key)) MakeRoom(kills);
		kills.insert_or_assign(std::move(key), KillClaim{ a_monotonicSeconds });
	}

	void Ledger::ForgetScope(const Scope& a_scope)
	{
		continuity.erase(a_scope);
	}

	void Ledger::Quarantine(const NpcKey& a_key, double a_untilMonotonicSeconds, double a_monotonicSeconds)
	{
		if (!tombstones.contains(a_key)) MakeRoom(tombstones);
		auto& tomb = tombstones.try_emplace(a_key, Tombstone{ std::nullopt, 0.0, a_monotonicSeconds }).first->second;
		tomb.quarantinedUntil = std::max(tomb.quarantinedUntil, a_untilMonotonicSeconds);
	}
}

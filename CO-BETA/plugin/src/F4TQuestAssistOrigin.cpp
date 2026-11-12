#include "F4TQuestAssistOrigin.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <iterator>
#include <mutex>

namespace
{
	struct SentHit
	{
		std::uint32_t sequence{};
		std::uint32_t attackSequence{};
		std::uint32_t targetFormId{};
		std::uint64_t monotonicMilliseconds{};
	};

	struct QueuedDeath
	{
		std::uint32_t deadFormId{};
		std::uint64_t monotonicMilliseconds{};
	};

	std::mutex g_mutex;
	std::deque<SentHit> g_hits;
	std::deque<QueuedDeath> g_deaths;
	std::atomic<bool> g_enabled{ false };

	bool IsWithinWindow(std::uint64_t a_recordedAt, std::uint64_t a_now)
	{
		return a_now >= a_recordedAt && a_now - a_recordedAt <= F4T::QuestAssistOrigin::kHitWindowMilliseconds;
	}

	std::optional<F4T::QuestAssistOrigin::HitContext> TakeHitLocked(std::uint32_t a_targetFormId, std::uint64_t a_now)
	{
		for (auto hit = g_hits.rbegin(); hit != g_hits.rend(); ++hit) {
			if (hit->targetFormId != a_targetFormId || !IsWithinWindow(hit->monotonicMilliseconds, a_now)) {
				continue;
			}
			F4T::QuestAssistOrigin::HitContext context{ hit->attackSequence, hit->sequence, hit->targetFormId, hit->monotonicMilliseconds };
			g_hits.erase(std::prev(hit.base()));
			return context;
		}
		return std::nullopt;
	}
}

namespace F4T::QuestAssistOrigin
{
	void SetEnabled(bool a_enabled)
	{
		g_enabled.store(a_enabled, std::memory_order_release);
	}

	bool Enabled()
	{
		return g_enabled.load(std::memory_order_acquire);
	}

	void QueueDeath(std::uint32_t a_deadFormId, std::uint64_t a_nowMilliseconds)
	{
		if (a_deadFormId == 0 || !Enabled()) {
			return;
		}
		std::lock_guard lock(g_mutex);
		while (g_deaths.size() >= kMaximumPendingDeaths) {
			g_deaths.pop_front();
		}
		g_deaths.push_back(QueuedDeath{ a_deadFormId, a_nowMilliseconds });
	}

	std::optional<DeathWithHit> TakeDeathWithHit(std::uint64_t a_nowMilliseconds)
	{
		std::lock_guard lock(g_mutex);
		std::erase_if(g_deaths, [a_nowMilliseconds](const QueuedDeath& a_death) {
			return !IsWithinWindow(a_death.monotonicMilliseconds, a_nowMilliseconds);
		});
		for (auto death = g_deaths.begin(); death != g_deaths.end(); ++death) {
			const auto hit = TakeHitLocked(death->deadFormId, a_nowMilliseconds);
			if (!hit) {
				continue;
			}
			DeathWithHit matched{ death->deadFormId, *hit };
			g_deaths.erase(death);
			return matched;
		}
		return std::nullopt;
	}

	void RecordSentHit(
		std::uint32_t a_sequence,
		std::uint32_t a_attackSequence,
		std::uint32_t a_targetFormId,
		std::uint64_t a_nowMilliseconds)
	{
		if (!Enabled() || a_sequence == 0 || a_attackSequence == 0 || a_targetFormId == 0) {
			return;
		}
		std::lock_guard lock(g_mutex);
		std::erase_if(g_hits, [a_nowMilliseconds](const SentHit& a_hit) {
			return !IsWithinWindow(a_hit.monotonicMilliseconds, a_nowMilliseconds);
		});
		while (g_hits.size() >= kMaximumTrackedHits) {
			g_hits.pop_front();
		}
		g_hits.push_back(SentHit{ a_sequence, a_attackSequence, a_targetFormId, a_nowMilliseconds });
	}

	std::optional<Match> MatchActorObjectives(
		std::span<const ObjectiveTarget> a_objectives,
		std::uint32_t a_deadActorFormId,
		const HitContext& a_hit,
		std::uint64_t a_nowMilliseconds)
	{
		if (a_deadActorFormId == 0 || a_hit.attackSequence == 0 || a_hit.sequence == 0 ||
			a_hit.targetFormId != a_deadActorFormId || !IsWithinWindow(a_hit.monotonicMilliseconds, a_nowMilliseconds)) {
			return std::nullopt;
		}

		Match match{};
		match.hit = a_hit;
		for (const auto& objective : a_objectives) {
			if (match.candidates.size() >= kMaximumCandidates) {
				break;
			}
			if (objective.quest.empty()) {
				continue;
			}
			if (std::find(objective.actorFormIds.begin(), objective.actorFormIds.end(), a_deadActorFormId) == objective.actorFormIds.end()) {
				continue;
			}
			const bool duplicate = std::any_of(match.candidates.begin(), match.candidates.end(), [&](const Candidate& a_candidate) {
				return a_candidate.quest == objective.quest && a_candidate.objectiveId == objective.objectiveId;
			});
			if (duplicate) {
				continue;
			}
			match.candidates.push_back(Candidate{ objective.quest, objective.objectiveId });
		}
		if (match.candidates.empty()) {
			return std::nullopt;
		}
		return match;
	}

	bool ObjectiveTargetsContain(
		std::span<const ObjectiveTarget> a_targets,
		std::uint32_t a_objectiveId,
		std::string_view a_targetKey,
		const std::function<std::vector<std::string>(std::uint32_t)>& a_keysOf)
	{
		if (a_targetKey.empty()) {
			return false;
		}
		for (const auto& objective : a_targets) {
			if (objective.objectiveId != a_objectiveId) {
				continue;
			}
			for (const auto actorFormId : objective.actorFormIds) {
				const auto keys = a_keysOf(actorFormId);
				if (std::find(keys.begin(), keys.end(), a_targetKey) != keys.end()) {
					return true;
				}
			}
		}
		return false;
	}

	void Clear()
	{
		std::lock_guard lock(g_mutex);
		g_hits.clear();
		g_deaths.clear();
	}
}

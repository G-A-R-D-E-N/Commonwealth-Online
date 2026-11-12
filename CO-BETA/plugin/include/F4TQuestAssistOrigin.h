#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::QuestAssistOrigin
{
	inline constexpr std::uint64_t kHitWindowMilliseconds = 2000;
	inline constexpr std::size_t kMaximumCandidates = 4;
	inline constexpr std::size_t kMaximumTrackedHits = 16;
	inline constexpr std::size_t kMaximumPendingDeaths = 8;

	struct ObjectiveTarget
	{
		std::string quest;
		std::uint32_t objectiveId{};
		std::vector<std::uint32_t> actorFormIds;
	};

	struct HitContext
	{
		std::uint32_t attackSequence{};
		std::uint32_t sequence{};
		std::uint32_t targetFormId{};
		std::uint64_t monotonicMilliseconds{};
	};

	struct Candidate
	{
		std::string quest;
		std::uint32_t objectiveId{};
	};

	struct Match
	{
		std::vector<Candidate> candidates;
		HitContext hit;
	};

	struct DeathWithHit
	{
		std::uint32_t deadFormId{};
		HitContext hit;
	};

	void SetEnabled(bool a_enabled);

	bool Enabled();

	void QueueDeath(std::uint32_t a_deadFormId, std::uint64_t a_nowMilliseconds);

	[[nodiscard]] std::optional<DeathWithHit> TakeDeathWithHit(std::uint64_t a_nowMilliseconds);

	void RecordSentHit(
		std::uint32_t a_sequence,
		std::uint32_t a_attackSequence,
		std::uint32_t a_targetFormId,
		std::uint64_t a_nowMilliseconds);

	[[nodiscard]] std::optional<Match> MatchActorObjectives(
		std::span<const ObjectiveTarget> a_objectives,
		std::uint32_t a_deadActorFormId,
		const HitContext& a_hit,
		std::uint64_t a_nowMilliseconds);

	[[nodiscard]] bool ObjectiveTargetsContain(
		std::span<const ObjectiveTarget> a_targets,
		std::uint32_t a_objectiveId,
		std::string_view a_targetKey,
		const std::function<std::vector<std::string>(std::uint32_t)>& a_keysOf);

	void Clear();
}

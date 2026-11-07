#pragma once

#include "F4TActionSequence.h"
#include "F4TNpcCoopHit.h"
#include "F4TNpcSync.h"
#include "F4TRemotePlayerState.h"
#include "RE/A/Actor.h"
#include "RE/T/TESHitEvent.h"

#include <chrono>
#include <cstdint>
#include <span>
#include <vector>

namespace F4T::RangedCombatCapture
{
	enum class HitKind : std::uint8_t
	{
		kPlayer,
		kNpc
	};

	struct CombatHit
	{
		std::uint32_t sequence{};
		std::uint32_t targetPlayerId{};
		float damage{};
		std::uint32_t weaponFormId{};
		std::uint32_t attackSequence{};
		std::uint64_t sessionGeneration{};
		HitKind kind{ HitKind::kPlayer };
		F4T::NpcCoop::NpcHitIdentity npc;
		std::uint64_t connectionGeneration{};
		std::uint64_t policyRevision{};
	};

	inline std::uint32_t AllocateNonZeroSequence(std::uint32_t& a_next) noexcept
	{
		return F4T::ActionSequence::AllocateNonZero(a_next);
	}

	void Initialize();
	void Shutdown();

	void QueueNpcHit(const RE::TESHitEvent& a_event, const F4T::NpcCoop::NpcHitIdentity& a_identity);

	void NoteLocalAttackAction(
		std::uint32_t a_sequence,
		std::uint32_t a_type,
		std::chrono::steady_clock::time_point a_capturedAt);

	struct RangedShot
	{
		std::uint64_t generation{};
		std::uint64_t shot{};
		F4T::RemotePlayerState::RemoteActionEvent action;
	};

	bool BeginRangedShot(std::uint64_t a_generation, std::uint64_t a_shot);
	void NoteShotLaunch(std::uint64_t a_generation, std::uint64_t a_shot, std::uint32_t a_handle, std::chrono::steady_clock::time_point a_at);
	void QueueRangedShot(std::uint64_t a_generation, std::uint64_t a_shot, std::chrono::steady_clock::time_point a_at);
	std::vector<RangedShot> DrainRangedShots(std::uint32_t& a_nextActionSequence, std::size_t a_limit, std::uint64_t a_generation);
	void CommitRangedShots(bool a_sent, std::span<const RangedShot> a_shots, std::uint64_t a_generation);

	std::vector<CombatHit> GetAndClearPendingCombatHits();
}

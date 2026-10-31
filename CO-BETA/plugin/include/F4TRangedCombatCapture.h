#pragma once

#include "F4TActionSequence.h"
#include "F4TNpcCoopHit.h"
#include "F4TNpcSync.h"
#include "F4TRemotePlayerState.h"
#include "RE/A/Actor.h"
#include "RE/T/TESHitEvent.h"

#include <chrono>
#include <cstdint>
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

	void NoteLocalProjectileLaunch(
		std::uint32_t a_projectileHandle,
		std::chrono::steady_clock::time_point a_now);

	std::vector<F4T::RemotePlayerState::RemoteActionEvent> CaptureRangedFireEvents(
		RE::Actor& a_player,
		bool& a_lastRangedFireState,
		std::uint32_t& a_nextActionSequence);

	std::vector<CombatHit> GetAndClearPendingCombatHits();
}

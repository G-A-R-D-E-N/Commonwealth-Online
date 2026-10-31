#pragma once

#include "F4TRemotePlayerState.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace F4T::CombatActionContract
{
	using Action = F4T::RemotePlayerState::RemoteActionEvent;
	using TimePoint = std::chrono::steady_clock::time_point;

	constexpr auto kBindingWindow = std::chrono::seconds{ 2 };
	constexpr auto kFutureSlack = std::chrono::milliseconds{ 150 };

	inline std::span<const Action> SelectInitialActions(
		bool a_soloCell,
		std::span<const Action> a_actions) noexcept
	{
		return a_soloCell ? std::span<const Action>{} : a_actions;
	}

	template <class RecordFn>
	void CommitSentActions(
		bool a_sent,
		std::span<const Action> a_actions,
		RecordFn&& a_record)
	{
		if (!a_sent) {
			return;
		}
		for (const auto& action : a_actions) {
			std::forward<RecordFn>(a_record)(action);
		}
	}

	inline bool CanBind(
		std::uint32_t a_actionType,
		TimePoint a_actionAt,
		std::uint32_t a_requiredType,
		TimePoint a_hitAt) noexcept
	{
		return a_actionType == a_requiredType &&
			a_actionAt <= a_hitAt + kFutureSlack &&
			a_hitAt <= a_actionAt + kBindingWindow;
	}

	inline bool ShouldRetryReplay(
		bool a_applied,
		bool a_permanentFailure,
		TimePoint a_actionAt,
		TimePoint a_now) noexcept
	{
		return !a_applied && !a_permanentFailure && a_now - a_actionAt <= kBindingWindow;
	}

	inline constexpr bool IsPermanentReplayFailure(bool a_emptyEventName, bool a_hitGuardInstalled) noexcept
	{
		return a_emptyEventName || !a_hitGuardInstalled;
	}

	struct UniqueAttack
	{
		std::uint32_t sequence{};
		std::size_t candidates{};
	};

	template <class Attacks>
	UniqueAttack FindUniqueAttackSequence(
		std::uint32_t a_requiredType,
		TimePoint a_hitAt,
		const Attacks& a_attacks)
	{
		UniqueAttack match{};
		for (const auto& attack : a_attacks) {
			if (CanBind(attack.type, attack.capturedAt, a_requiredType, a_hitAt)) {
				match.sequence = attack.sequence;
				++match.candidates;
			}
		}
		if (match.candidates != 1) {
			match.sequence = 0;
		}
		return match;
	}

	struct ProjectileLaunch
	{
		std::uint32_t attackSequence{};
		std::uint32_t type{};
		std::uint32_t projectileHandle{};
		TimePoint launchedAt{};
	};

	template <class Launches>
	std::uint32_t FindExactAttackSequence(
		std::uint32_t a_projectileHandle,
		std::uint32_t a_requiredType,
		TimePoint a_hitAt,
		const Launches& a_launches)
	{
		if (a_projectileHandle == 0) {
			return 0;
		}
		std::uint32_t sequence{};
		std::size_t candidates{};
		for (const auto& launch : a_launches) {
			if (launch.projectileHandle == a_projectileHandle &&
				CanBind(launch.type, launch.launchedAt, a_requiredType, a_hitAt)) {
				sequence = launch.attackSequence;
				++candidates;
			}
		}
		return candidates == 1 ? sequence : 0;
	}

	inline bool IsHitDecidable(TimePoint a_hitAt, TimePoint a_now) noexcept
	{
		return a_now > a_hitAt + kFutureSlack;
	}
}

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

	template <class Attacks>
	std::uint32_t FindNearestAttackSequence(
		std::uint32_t a_requiredType,
		TimePoint a_launchAt,
		const Attacks& a_attacks)
	{
		std::uint32_t sequence{};
		std::chrono::steady_clock::duration bestDistance{};
		std::size_t bestCount{};
		for (const auto& attack : a_attacks) {
			if (!CanBind(attack.type, attack.capturedAt, a_requiredType, a_launchAt)) {
				continue;
			}
			const auto distance = std::chrono::abs(attack.capturedAt - a_launchAt);
			if (bestCount == 0 || distance < bestDistance) {
				sequence = attack.sequence;
				bestDistance = distance;
				bestCount = 1;
			} else if (distance == bestDistance) {
				++bestCount;
			}
		}
		return bestCount == 1 ? sequence : 0;
	}

	inline bool IsHitDecidable(TimePoint a_hitAt, TimePoint a_now) noexcept
	{
		return a_now > a_hitAt + kFutureSlack;
	}
}

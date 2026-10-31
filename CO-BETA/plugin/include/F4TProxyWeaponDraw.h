#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace RE
{
	class Actor;
}

namespace F4T::RemotePlayerState
{
	struct RemotePlayerState;
}

namespace F4T::ProxyWeaponDraw
{
	inline constexpr double kStabilitySeconds = 0.150;
	inline constexpr double kMinChangeIntervalSeconds = 0.500;
	inline constexpr double kProxyGenerationGapSeconds = 1.0;
	inline constexpr double kStateIdlePruneSeconds = 30.0;
	inline constexpr std::uint32_t kMaxAttempts = 5;

	struct Parameters
	{
		double stabilitySeconds{ kStabilitySeconds };
		double minChangeIntervalSeconds{ kMinChangeIntervalSeconds };
		std::uint32_t maxAttempts{ kMaxAttempts };
	};

	enum class Decision
	{
		kNone,
		kDraw,
		kSheathe
	};

	struct State
	{
		std::uint32_t proxyFormId{ 0 };
		double lastVisitSeconds{ 0.0 };
		bool hasPendingDrawn{ false };
		bool pendingDrawn{ false };
		double pendingSinceSeconds{ 0.0 };
		double lastChangeSeconds{ 0.0 };
		std::uint32_t attempts{ 0 };
	};

	inline void Reset(State& a_state)
	{
		a_state = State{};
	}

	inline void ClearPendingKeepInterval(State& a_state)
	{
		a_state.hasPendingDrawn = false;
		a_state.attempts = 0;
	}

	inline void BindToProxy(
		State& a_state,
		std::uint32_t a_proxyFormId,
		double a_nowSeconds,
		double a_generationGapSeconds = kProxyGenerationGapSeconds)
	{
		if (a_state.proxyFormId != a_proxyFormId ||
			a_nowSeconds - a_state.lastVisitSeconds > a_generationGapSeconds) {
			Reset(a_state);
		}
		a_state.proxyFormId = a_proxyFormId;
		a_state.lastVisitSeconds = a_nowSeconds;
	}

	template <class Map>
	inline std::size_t PruneStale(
		Map& a_states,
		double a_nowSeconds,
		double a_maxIdleSeconds = kStateIdlePruneSeconds)
	{
		std::size_t erased = 0;
		for (auto it = a_states.begin(); it != a_states.end();) {
			if (a_nowSeconds - it->second.lastVisitSeconds > a_maxIdleSeconds) {
				it = a_states.erase(it);
				++erased;
			} else {
				++it;
			}
		}
		return erased;
	}

	[[nodiscard]] inline std::optional<bool> ResolveStableDesired(
		State& a_state,
		double a_nowSeconds,
		bool a_remoteDrawn,
		const Parameters& a_parameters = {})
	{
		if (!a_state.hasPendingDrawn || a_state.pendingDrawn != a_remoteDrawn) {
			a_state.hasPendingDrawn = true;
			a_state.pendingDrawn = a_remoteDrawn;
			a_state.pendingSinceSeconds = a_nowSeconds;
			a_state.attempts = 0;
			return std::nullopt;
		}

		if (a_nowSeconds - a_state.pendingSinceSeconds < a_parameters.stabilitySeconds) {
			return std::nullopt;
		}

		return a_state.pendingDrawn;
	}

	[[nodiscard]] inline Decision Decide(
		bool a_remoteDrawn,
		bool a_actualDrawn,
		bool a_hasWeapon,
		bool a_graphSettling,
		double a_lastChangeSeconds,
		double a_nowSeconds,
		std::uint32_t a_attempts,
		const Parameters& a_parameters = {})
	{
		if (!a_hasWeapon || a_graphSettling) {
			return Decision::kNone;
		}

		if (a_remoteDrawn == a_actualDrawn) {
			return Decision::kNone;
		}

		if (a_attempts >= a_parameters.maxAttempts) {
			return Decision::kNone;
		}

		if (a_lastChangeSeconds > 0.0 &&
			a_nowSeconds - a_lastChangeSeconds < a_parameters.minChangeIntervalSeconds) {
			return Decision::kNone;
		}

		return a_remoteDrawn ? Decision::kDraw : Decision::kSheathe;
	}

	void ReconcileProxyWeaponDraw(
		std::uint32_t a_remotePlayerId,
		RE::Actor& a_proxy,
		const RemotePlayerState::RemotePlayerState& a_remotePlayer,
		bool a_graphSettling);
}

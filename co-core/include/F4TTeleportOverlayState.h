#pragma once

#include <cstdint>

namespace F4T::TeleportOverlay
{
	inline constexpr double kHardTimeoutSeconds = 20.0;
	inline constexpr double kMinimumShowSeconds = 1.0;
	inline constexpr double kNoLoadingMenuTimeoutSeconds = 4.0;
	inline constexpr double kFaderWatchSeconds = 3.0;
	inline constexpr std::uint8_t kFaderMaxRetries = 2;

	enum class Action : std::uint8_t
	{
		None,
		Show,
		Hide
	};

	struct State
	{
		bool active{ false };
		bool sawLoadingMenu{ false };
		double startedAtSeconds{ 0.0 };
	};

	enum class FaderAction : std::uint8_t
	{
		None,
		RetryHide,
		RetryFadeIn,
		GiveUp
	};

	struct FaderWatch
	{
		bool armed{ false };
		std::uint8_t retries{ 0 };
		double checkAtSeconds{ 0.0 };
	};

	inline Action Finish(State& a_state) noexcept
	{
		a_state = State{};
		return Action::Hide;
	}

	inline Action Begin(State& a_state, double a_nowSeconds) noexcept
	{
		if (a_state.active) {
			return Action::None;
		}
		a_state = State{ true, false, a_nowSeconds };
		return Action::Show;
	}

	inline Action Cancel(State& a_state) noexcept
	{
		if (!a_state.active) {
			return Action::None;
		}
		return Finish(a_state);
	}

	inline Action Tick(State& a_state, double a_nowSeconds, bool a_loadingMenuOpen, bool a_playerLoaded) noexcept
	{
		if (!a_state.active) {
			return Action::None;
		}
		const auto elapsed = a_nowSeconds - a_state.startedAtSeconds;
		if (elapsed >= kHardTimeoutSeconds) {
			return Finish(a_state);
		}
		if (a_loadingMenuOpen) {
			a_state.sawLoadingMenu = true;
			return Action::None;
		}
		if (!a_playerLoaded || elapsed < kMinimumShowSeconds) {
			return Action::None;
		}
		if (a_state.sawLoadingMenu || elapsed >= kNoLoadingMenuTimeoutSeconds) {
			return Finish(a_state);
		}
		return Action::None;
	}

	inline void ArmFaderWatch(FaderWatch& a_watch, double a_nowSeconds) noexcept
	{
		a_watch = FaderWatch{ true, 0, a_nowSeconds + kFaderWatchSeconds };
	}

	inline FaderAction WatchFader(FaderWatch& a_watch, double a_nowSeconds, bool a_faderOpen, bool a_loadingMenuOpen, bool a_pauseMenuOpen) noexcept
	{
		if (!a_watch.armed || a_nowSeconds < a_watch.checkAtSeconds) {
			return FaderAction::None;
		}
		if (!a_faderOpen || a_loadingMenuOpen || a_pauseMenuOpen) {
			a_watch = FaderWatch{};
			return FaderAction::None;
		}
		if (a_watch.retries >= kFaderMaxRetries) {
			a_watch = FaderWatch{};
			return FaderAction::GiveUp;
		}
		const auto action = a_watch.retries == 0 ? FaderAction::RetryHide : FaderAction::RetryFadeIn;
		++a_watch.retries;
		a_watch.checkAtSeconds = a_nowSeconds + kFaderWatchSeconds;
		return action;
	}
}

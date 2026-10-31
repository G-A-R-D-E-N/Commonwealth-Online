#pragma once

#include "PrismaUI_F4_API.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::DownedUi
{
	enum class Phase
	{
		Hidden,
		Downed,
		Dead
	};

	struct Rescuer
	{
		bool        active{ false };
		std::string name;
		double      progress{ 0.0 };
	};

	struct Point
	{
		std::string id;
		std::string label;
		bool        available{ false };
	};

	struct Friend
	{
		std::string id;
		std::string name;
	};

	struct State
	{
		Phase               phase{ Phase::Hidden };
		std::int32_t        remainingMs{ 0 };
		Rescuer             rescuer;
		Rescuer             reviving;
		std::vector<Point>  points;
		std::vector<Friend> friends;
	};

	struct Choice
	{
		std::string point;
		std::string friendId;
	};

	constexpr std::int32_t kMaxRemainingMs = 600000;
	constexpr std::size_t  kMaxNameLength = 32;
	constexpr std::size_t  kMaxLabelLength = 48;
	constexpr std::size_t  kMaxIdLength = 64;
	constexpr std::size_t  kMaxPoints = 8;
	constexpr std::size_t  kMaxFriends = 32;
	constexpr std::size_t  kMaxJsonBytes = 64 * 1024;
	constexpr std::size_t  kMaxChoiceBytes = 1024;

	struct ControllerBindRetryTiming
	{
		using Clock = std::chrono::steady_clock;

		std::size_t  maxAttempts{ 8 };
		Clock::duration retrySpacing{ std::chrono::milliseconds(250) };
		std::size_t  attempts{ 0 };
		Clock::time_point retryAfter{};

		void Reset() noexcept
		{
			attempts = 0;
			retryAfter = Clock::time_point{};
		}

		[[nodiscard]] bool CanAttempt(Clock::time_point a_now) const noexcept
		{
			return attempts < maxAttempts && a_now >= retryAfter;
		}

		void RecordAttempt(Clock::time_point a_now) noexcept
		{
			++attempts;
			retryAfter = a_now + retrySpacing;
		}

		void ScheduleRetry(Clock::time_point a_now) noexcept
		{
			retryAfter = a_now + retrySpacing;
		}
	};

	[[nodiscard]] std::string SanitizeText(std::string_view a_text, std::size_t a_maxLength);
	[[nodiscard]] bool IsEchoableId(std::string_view a_id) noexcept;
	[[nodiscard]] std::optional<Phase> ParsePhase(std::string_view a_phase) noexcept;
	[[nodiscard]] const char* PhaseName(Phase a_phase) noexcept;
	[[nodiscard]] bool IsKnownPoint(std::string_view a_point) noexcept;
	[[nodiscard]] bool IsOverlayVisible(const State& a_state) noexcept;
	[[nodiscard]] bool IsRespawnMenuVisible(const State& a_state) noexcept;

	[[nodiscard]] std::optional<State> ParseState(std::string_view a_json);
	[[nodiscard]] std::string FormatSetStateScript(const State& a_state, std::string_view a_entryPoint);
	[[nodiscard]] std::optional<Choice> ParseChoice(std::string_view a_json);
	[[nodiscard]] bool ShouldPushStateScript(
		std::string_view a_lastScript,
		PrismaView a_lastScriptHandle,
		PrismaView a_currentHandle,
		std::string_view a_nextScript) noexcept;
}

#pragma once

#include <cstdint>
#include <functional>

namespace F4T::QuestReady
{
	inline constexpr std::uint64_t kInitialBackoffMilliseconds = 1'000;
	inline constexpr std::uint64_t kMaximumBackoffMilliseconds = 30'000;
	inline constexpr std::uint64_t kQuietResetMilliseconds = 10'000;
	inline constexpr std::uint32_t kMaximumAttempts = 8;

	// constraint: the report must be sent before questReady and a refused ready retries with backoff.
	class Gate
	{
	public:
		enum class Action : std::uint8_t
		{
			Idle,
			Report,
			Ready
		};

		void Reset() noexcept;
		void NoteRefused(std::uint64_t a_nowMilliseconds) noexcept;
		void NoteSendFailed(std::uint64_t a_nowMilliseconds) noexcept;
		void NoteReportSent(std::uint16_t a_stage) noexcept;
		void NoteReadySent(std::uint64_t a_nowMilliseconds) noexcept;

		[[nodiscard]] Action Next(
			bool a_tutorialComplete,
			std::uint16_t a_tutorialStage,
			std::uint64_t a_nowMilliseconds);

		[[nodiscard]] bool ReportSent() const noexcept;
		[[nodiscard]] bool ReadySent() const noexcept;
		[[nodiscard]] bool Exhausted() const noexcept;

	private:
		void BackOff(std::uint64_t a_nowMilliseconds) noexcept;

		bool _reportSent{};
		std::uint16_t _reportedStage{};
		bool _readySent{};
		std::uint32_t _refusals{};
		std::uint64_t _retryAfterMilliseconds{};
		std::uint64_t _backoffMilliseconds{ kInitialBackoffMilliseconds };
		std::uint64_t _readySentMilliseconds{};
	};

	struct DriveResult
	{
		bool exhausted{};
	};

	[[nodiscard]] DriveResult Drive(
		Gate& a_gate,
		bool a_tutorialComplete,
		std::uint16_t a_tutorialStage,
		std::uint64_t a_nowMilliseconds,
		const std::function<bool()>& a_sendReport,
		const std::function<bool()>& a_sendReady);
}

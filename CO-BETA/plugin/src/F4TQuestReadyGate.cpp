#include "F4TQuestReadyGate.h"

#include <algorithm>

namespace F4T::QuestReady
{
	void Gate::Reset() noexcept
	{
		_reportSent = false;
		_reportedStage = 0;
		_readySent = false;
		_refusals = 0;
		_retryAfterMilliseconds = 0;
		_backoffMilliseconds = kInitialBackoffMilliseconds;
		_readySentMilliseconds = 0;
	}

	void Gate::BackOff(std::uint64_t a_nowMilliseconds) noexcept
	{
		_retryAfterMilliseconds = a_nowMilliseconds + _backoffMilliseconds;
		_backoffMilliseconds = (std::min)(_backoffMilliseconds * 2, kMaximumBackoffMilliseconds);
	}

	void Gate::NoteRefused(std::uint64_t a_nowMilliseconds) noexcept
	{
		if (!_readySent) return;
		_reportSent = false;
		_readySent = false;
		++_refusals;
		BackOff(a_nowMilliseconds);
	}

	void Gate::NoteSendFailed(std::uint64_t a_nowMilliseconds) noexcept
	{
		BackOff(a_nowMilliseconds);
	}

	void Gate::NoteReportSent(std::uint16_t a_stage) noexcept
	{
		_reportSent = true;
		_reportedStage = a_stage;
		_retryAfterMilliseconds = 0;
	}

	void Gate::NoteReadySent(std::uint64_t a_nowMilliseconds) noexcept
	{
		_readySent = true;
		_retryAfterMilliseconds = 0;
		_readySentMilliseconds = a_nowMilliseconds;
	}

	Gate::Action Gate::Next(
		bool a_tutorialComplete,
		std::uint16_t a_tutorialStage,
		std::uint64_t a_nowMilliseconds)
	{
		if (_readySent && _refusals > 0 &&
			a_nowMilliseconds - _readySentMilliseconds >= kQuietResetMilliseconds) {
			_refusals = 0;
			_backoffMilliseconds = kInitialBackoffMilliseconds;
		}
		if (!a_tutorialComplete || _refusals >= kMaximumAttempts) return Action::Idle;
		if (a_nowMilliseconds < _retryAfterMilliseconds) return Action::Idle;
		if (!_reportSent || a_tutorialStage > _reportedStage) return Action::Report;
		if (!_readySent) return Action::Ready;
		return Action::Idle;
	}

	bool Gate::ReportSent() const noexcept
	{
		return _reportSent;
	}

	bool Gate::ReadySent() const noexcept
	{
		return _readySent;
	}

	bool Gate::Exhausted() const noexcept
	{
		return _refusals >= kMaximumAttempts;
	}

	DriveResult Drive(
		Gate& a_gate,
		bool a_tutorialComplete,
		std::uint16_t a_tutorialStage,
		std::uint64_t a_nowMilliseconds,
		const std::function<bool()>& a_sendReport,
		const std::function<bool()>& a_sendReady)
	{
		DriveResult result;
		for (int step = 0; step < 2; ++step) {
			const auto action = a_gate.Next(a_tutorialComplete, a_tutorialStage, a_nowMilliseconds);
			if (action == Gate::Action::Idle) break;
			if (action == Gate::Action::Report) {
				if (!a_sendReport()) {
					a_gate.NoteSendFailed(a_nowMilliseconds);
					break;
				}
				a_gate.NoteReportSent(a_tutorialStage);
				continue;
			}
			if (!a_sendReady()) {
				a_gate.NoteSendFailed(a_nowMilliseconds);
				break;
			}
			a_gate.NoteReadySent(a_nowMilliseconds);
			break;
		}
		result.exhausted = a_gate.Exhausted();
		return result;
	}
}

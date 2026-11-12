#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::ReliableOutbox
{
	using Clock = std::chrono::steady_clock;

	inline constexpr auto kAcceptanceTimeout = std::chrono::milliseconds{ 250 };
	inline constexpr std::size_t kMaximumMessages = 64;
	inline constexpr std::size_t kMaximumBytes = 1024 * 1024;

	enum class Attempt
	{
		Sent,
		Backpressure,
		Failed
	};

	enum class SubmitResult
	{
		Accepted,
		Queued,
		Full,
		Failed
	};

	enum class FlushResult
	{
		Drained,
		Pending,
		TimedOut,
		Failed
	};

	class Outbox
	{
	public:
		template <class SendFn>
		SubmitResult Submit(std::string_view a_payload, Clock::time_point a_now, SendFn&& a_send)
		{
			std::optional<Clock::time_point> deadline;
			if (_queue.empty()) {
				const auto attempt = a_send(a_payload);
				if (attempt == Attempt::Sent) {
					return SubmitResult::Accepted;
				}
				if (attempt == Attempt::Failed) {
					return SubmitResult::Failed;
				}
				deadline = a_now + kAcceptanceTimeout;
			}
			if (_queue.size() >= kMaximumMessages || a_payload.size() > kMaximumBytes - _bytes) {
				return SubmitResult::Full;
			}
			_bytes += a_payload.size();
			_queue.push_back(Entry{ std::string{ a_payload }, deadline });
			return SubmitResult::Queued;
		}

		template <class SendFn>
		FlushResult Flush(Clock::time_point a_now, SendFn&& a_send)
		{
			while (!_queue.empty()) {
				auto& head = _queue.front();
				const auto attempt = a_send(std::string_view{ head.payload });
				if (attempt == Attempt::Sent) {
					_bytes -= head.payload.size();
					_queue.pop_front();
					continue;
				}
				if (attempt == Attempt::Failed) {
					return FlushResult::Failed;
				}
				if (!head.deadline) {
					head.deadline = a_now + kAcceptanceTimeout;
				}
				return a_now >= *head.deadline ? FlushResult::TimedOut : FlushResult::Pending;
			}
			return FlushResult::Drained;
		}

		void Clear() noexcept
		{
			_queue.clear();
			_bytes = 0;
		}

		std::size_t Size() const noexcept { return _queue.size(); }
		std::size_t Bytes() const noexcept { return _bytes; }

	private:
		struct Entry
		{
			std::string payload;
			std::optional<Clock::time_point> deadline;
		};

		std::deque<Entry> _queue;
		std::size_t _bytes{ 0 };
	};
}

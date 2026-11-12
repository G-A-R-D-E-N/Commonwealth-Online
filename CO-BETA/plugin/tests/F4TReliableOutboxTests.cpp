#include "F4TReliableOutbox.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using F4T::ReliableOutbox::Attempt;
	using F4T::ReliableOutbox::FlushResult;
	using F4T::ReliableOutbox::kAcceptanceTimeout;
	using F4T::ReliableOutbox::kMaximumBytes;
	using F4T::ReliableOutbox::kMaximumMessages;
	using F4T::ReliableOutbox::Outbox;
	using F4T::ReliableOutbox::SubmitResult;
	using Clock = std::chrono::steady_clock;
	using std::chrono::milliseconds;

	const auto t0 = Clock::time_point{} + std::chrono::seconds{ 10 };

	struct Transport
	{
		Attempt next{ Attempt::Sent };
		std::vector<std::string> attempted;
		std::vector<std::string> accepted;

		auto Sender()
		{
			return [this](std::string_view a_payload) {
				attempted.emplace_back(a_payload);
				if (next == Attempt::Sent) {
					accepted.emplace_back(a_payload);
				}
				return next;
			};
		}
	};

	void VerifyEmptyOutboxSendsImmediately()
	{
		Outbox outbox;
		Transport transport;
		assert(outbox.Submit("a", t0, transport.Sender()) == SubmitResult::Accepted);
		assert(transport.attempted.size() == 1);
		assert(transport.accepted == std::vector<std::string>{ "a" });
		assert(outbox.Size() == 0);
		assert(outbox.Bytes() == 0);
		assert(outbox.Flush(t0, transport.Sender()) == FlushResult::Drained);
		assert(transport.attempted.size() == 1);
	}

	void VerifyBackpressureQueuesWithoutRetrying()
	{
		Outbox outbox;
		Transport transport;
		transport.next = Attempt::Backpressure;
		assert(outbox.Submit("a", t0, transport.Sender()) == SubmitResult::Queued);
		assert(transport.attempted.size() == 1);
		assert(transport.accepted.empty());
		assert(outbox.Size() == 1);
		assert(outbox.Bytes() == 1);
	}

	void VerifyFifoNeverOvertakes()
	{
		Outbox outbox;
		Transport transport;
		transport.next = Attempt::Backpressure;
		assert(outbox.Submit("a", t0, transport.Sender()) == SubmitResult::Queued);

		transport.next = Attempt::Sent;
		assert(outbox.Submit("b", t0, transport.Sender()) == SubmitResult::Queued);
		assert(transport.attempted == std::vector<std::string>{ "a" });

		assert(outbox.Flush(t0 + milliseconds{ 10 }, transport.Sender()) == FlushResult::Drained);
		assert((transport.accepted == std::vector<std::string>{ "a", "b" }));
		assert(outbox.Size() == 0);
		assert(outbox.Bytes() == 0);
	}

	void VerifyTimeoutUsesFirstAttempt()
	{
		Outbox outbox;
		Transport transport;
		transport.next = Attempt::Backpressure;
		assert(outbox.Submit("a", t0, transport.Sender()) == SubmitResult::Queued);
		assert(outbox.Flush(t0 + milliseconds{ 100 }, transport.Sender()) == FlushResult::Pending);
		assert(outbox.Flush(t0 + kAcceptanceTimeout - milliseconds{ 1 }, transport.Sender()) == FlushResult::Pending);
		assert(outbox.Flush(t0 + kAcceptanceTimeout, transport.Sender()) == FlushResult::TimedOut);
		assert(transport.accepted.empty());
	}

	void VerifyQueuedMessageGetsItsOwnWindow()
	{
		Outbox outbox;
		Transport transport;
		transport.next = Attempt::Backpressure;
		assert(outbox.Submit("a", t0, transport.Sender()) == SubmitResult::Queued);
		assert(outbox.Submit("b", t0, transport.Sender()) == SubmitResult::Queued);

		std::size_t sends = 0;
		const auto firstSendsThenBackpressure = [&](std::string_view a_payload) {
			transport.attempted.emplace_back(a_payload);
			if (sends++ == 0) {
				transport.accepted.emplace_back(a_payload);
				return Attempt::Sent;
			}
			return Attempt::Backpressure;
		};
		const auto secondAttemptAt = t0 + milliseconds{ 240 };
		assert(outbox.Flush(secondAttemptAt, firstSendsThenBackpressure) == FlushResult::Pending);
		assert(transport.accepted == std::vector<std::string>{ "a" });
		assert(outbox.Size() == 1);

		assert(outbox.Flush(t0 + milliseconds{ 300 }, transport.Sender()) == FlushResult::Pending);
		assert(outbox.Flush(secondAttemptAt + kAcceptanceTimeout, transport.Sender()) == FlushResult::TimedOut);
	}

	void VerifyFailuresAreNotQueued()
	{
		Outbox outbox;
		Transport transport;
		transport.next = Attempt::Failed;
		assert(outbox.Submit("a", t0, transport.Sender()) == SubmitResult::Failed);
		assert(outbox.Size() == 0);

		transport.next = Attempt::Backpressure;
		assert(outbox.Submit("b", t0, transport.Sender()) == SubmitResult::Queued);
		transport.next = Attempt::Failed;
		assert(outbox.Flush(t0 + milliseconds{ 5 }, transport.Sender()) == FlushResult::Failed);
		assert(transport.accepted.empty());
	}

	void VerifyBounds()
	{
		Outbox outbox;
		Transport transport;
		transport.next = Attempt::Backpressure;
		for (std::size_t index = 0; index < kMaximumMessages; ++index) {
			assert(outbox.Submit("m", t0, transport.Sender()) == SubmitResult::Queued);
		}
		assert(outbox.Size() == kMaximumMessages);
		assert(outbox.Submit("m", t0, transport.Sender()) == SubmitResult::Full);
		assert(outbox.Size() == kMaximumMessages);

		outbox.Clear();
		assert(outbox.Size() == 0);
		assert(outbox.Bytes() == 0);

		const std::string large(kMaximumBytes / 2, 'x');
		assert(outbox.Submit(large, t0, transport.Sender()) == SubmitResult::Queued);
		assert(outbox.Submit(large, t0, transport.Sender()) == SubmitResult::Queued);
		assert(outbox.Bytes() == kMaximumBytes);
		assert(outbox.Submit("y", t0, transport.Sender()) == SubmitResult::Full);
		assert(outbox.Size() == 2);
	}

	void VerifyActionAcceptedBeforeDependentHit()
	{
		Outbox outbox;
		Transport transport;
		transport.next = Attempt::Backpressure;
		assert(outbox.Submit("playerState:action", t0, transport.Sender()) == SubmitResult::Queued);
		transport.next = Attempt::Sent;
		assert(outbox.Submit("combatHit", t0 + milliseconds{ 16 }, transport.Sender()) == SubmitResult::Queued);
		assert(outbox.Flush(t0 + milliseconds{ 40 }, transport.Sender()) == FlushResult::Drained);
		assert((transport.accepted == std::vector<std::string>{ "playerState:action", "combatHit" }));
	}

	void VerifyDependentHitDiscardedWhenActionNeverAccepted()
	{
		Outbox outbox;
		Transport transport;
		transport.next = Attempt::Backpressure;
		assert(outbox.Submit("playerState:action", t0, transport.Sender()) == SubmitResult::Queued);
		assert(outbox.Submit("combatHit", t0 + milliseconds{ 16 }, transport.Sender()) == SubmitResult::Queued);
		assert(outbox.Flush(t0 + kAcceptanceTimeout, transport.Sender()) == FlushResult::TimedOut);
		outbox.Clear();

		transport.next = Attempt::Sent;
		assert(outbox.Flush(t0 + kAcceptanceTimeout + milliseconds{ 1 }, transport.Sender()) == FlushResult::Drained);
		assert(transport.accepted.empty());
		for (const auto& attempt : transport.attempted) {
			assert(attempt != "combatHit");
		}
	}
}

int main()
{
	VerifyEmptyOutboxSendsImmediately();
	VerifyBackpressureQueuesWithoutRetrying();
	VerifyFifoNeverOvertakes();
	VerifyTimeoutUsesFirstAttempt();
	VerifyQueuedMessageGetsItsOwnWindow();
	VerifyFailuresAreNotQueued();
	VerifyBounds();
	VerifyActionAcceptedBeforeDependentHit();
	VerifyDependentHitDiscardedWhenActionNeverAccepted();
	return 0;
}

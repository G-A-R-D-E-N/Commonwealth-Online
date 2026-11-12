#include "F4TWebApiTicket.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

namespace
{
	using namespace F4T::SteamId;
	using namespace std::chrono_literals;

	WebApiTicket::Response Answer(std::uint32_t a_handle, std::int32_t a_result, std::int32_t a_size, std::uint8_t a_fill = 0xAB)
	{
		WebApiTicket::Response response;
		response.handle = a_handle;
		response.result = a_result;
		response.size = a_size;
		for (std::size_t index = 0; index < kMaximumTicketBytes; ++index) response.ticket[index] = a_fill;
		return response;
	}

	std::vector<std::uint8_t> Fetch(WebApiTicket::Requester& a_requester, const WebApiTicket::Requester::Issue& a_issue, std::chrono::milliseconds a_timeout = WebApiTicket::kDefaultTimeout)
	{
		std::vector<std::uint8_t> ticket;
		if (!a_requester.Begin(a_issue, a_timeout)) return {};
		while (a_requester.Poll(ticket) == WebApiTicket::Requester::PollResult::Pending) std::this_thread::sleep_for(1ms);
		return ticket;
	}

	struct Harness
	{
		std::vector<std::uint32_t> cancelled;
		WebApiTicket::Requester requester{ [this](std::uint32_t a_handle) { cancelled.push_back(a_handle); } };
	};

	void TestTicketArrivesFromAnotherThread()
	{
		Harness harness;
		std::thread pump;
		const auto ticket = Fetch(harness.requester, [&] {
			pump = std::thread([&] {
				std::this_thread::sleep_for(20ms);
				harness.requester.Deliver(Answer(7, WebApiTicket::kResultOk, 300));
			});
			return std::uint32_t{ 7 };
		}, 2s);
		pump.join();
		assert(ticket.size() == 300);
		assert(ticket.front() == 0xAB && ticket.back() == 0xAB);
		assert(harness.cancelled.empty());
	}

	void TestAnswerBeforeIssueReturns()
	{
		Harness harness;
		const auto ticket = Fetch(harness.requester, [&] {
			harness.requester.Deliver(Answer(9, WebApiTicket::kResultOk, 64));
			return std::uint32_t{ 9 };
		}, 2s);
		assert(ticket.size() == 64);
		assert(harness.cancelled.empty());
	}

	void TestMaximumSizeAccepted()
	{
		Harness harness;
		const auto ticket = Fetch(harness.requester, [&] {
			harness.requester.Deliver(Answer(3, WebApiTicket::kResultOk, static_cast<std::int32_t>(kMaximumTicketBytes)));
			return std::uint32_t{ 3 };
		}, 2s);
		assert(ticket.size() == kMaximumTicketBytes);
		assert(IsTicketHex(TicketHex(ticket.data(), ticket.size())));
	}

	void TestBadAnswersAreRefusedAndCancelled()
	{
		const std::int32_t sizes[] = { 0, -1, static_cast<std::int32_t>(kMaximumTicketBytes) + 1, 0x7FFFFFFF };
		for (const auto size : sizes) {
			Harness harness;
			const auto ticket = Fetch(harness.requester, [&] {
				harness.requester.Deliver(Answer(5, WebApiTicket::kResultOk, size));
				return std::uint32_t{ 5 };
			}, 2s);
			assert(ticket.empty());
			assert((harness.cancelled == std::vector<std::uint32_t>{ 5 }));
		}
		Harness failed;
		const auto none = Fetch(failed.requester, [&] {
			failed.requester.Deliver(Answer(6, 2, 100));
			return std::uint32_t{ 6 };
		}, 2s);
		assert(none.empty());
		assert((failed.cancelled == std::vector<std::uint32_t>{ 6 }));
	}

	void TestInvalidHandleIssuesNoCancel()
	{
		Harness harness;
		const auto ticket = Fetch(harness.requester, [] { return WebApiTicket::kInvalidHandle; }, 2s);
		assert(ticket.empty());
		assert(harness.cancelled.empty());
	}

	void TestTimeoutCancelsAndLateAnswerIsIgnored()
	{
		Harness harness;
		const auto started = std::chrono::steady_clock::now();
		const auto ticket = Fetch(harness.requester, [] { return std::uint32_t{ 11 }; }, 50ms);
		assert(ticket.empty());
		assert(std::chrono::steady_clock::now() - started < 2s);
		assert((harness.cancelled == std::vector<std::uint32_t>{ 11 }));
		harness.requester.Deliver(Answer(11, WebApiTicket::kResultOk, 100));
		const auto next = Fetch(harness.requester, [&] {
			harness.requester.Deliver(Answer(12, WebApiTicket::kResultOk, 20, 0xCD));
			return std::uint32_t{ 12 };
		}, 2s);
		assert(next.size() == 20 && next.front() == 0xCD);
	}

	void TestOtherHandlesAreIgnored()
	{
		Harness harness;
		const auto ticket = Fetch(harness.requester, [&] {
			harness.requester.Deliver(Answer(100, WebApiTicket::kResultOk, 10, 0x11));
			return std::uint32_t{ 21 };
		}, 50ms);
		assert(ticket.empty());
		assert((harness.cancelled == std::vector<std::uint32_t>{ 21 }));
		Harness second;
		std::thread pump;
		const auto real = Fetch(second.requester, [&] {
			pump = std::thread([&] {
				std::this_thread::sleep_for(20ms);
				second.requester.Deliver(Answer(99, WebApiTicket::kResultOk, 10, 0x22));
				second.requester.Deliver(Answer(22, WebApiTicket::kResultOk, 30, 0x33));
			});
			return std::uint32_t{ 22 };
		}, 2s);
		pump.join();
		assert(real.size() == 30 && real.front() == 0x33);
	}

	void TestHeldTicketIsCancelledOnNextRequestAndRelease()
	{
		Harness harness;
		const auto first = Fetch(harness.requester, [&] {
			harness.requester.Deliver(Answer(31, WebApiTicket::kResultOk, 8));
			return std::uint32_t{ 31 };
		}, 2s);
		assert(first.size() == 8 && harness.cancelled.empty());
		const auto second = Fetch(harness.requester, [&] {
			harness.requester.Deliver(Answer(32, WebApiTicket::kResultOk, 8));
			return std::uint32_t{ 32 };
		}, 2s);
		assert(second.size() == 8);
		assert((harness.cancelled == std::vector<std::uint32_t>{ 31 }));
		harness.requester.Release();
		harness.requester.Release();
		assert((harness.cancelled == std::vector<std::uint32_t>{ 31, 32 }));
	}

	void TestDestructorCancelsHeldTicket()
	{
		std::vector<std::uint32_t> cancelled;
		{
			WebApiTicket::Requester requester{ [&](std::uint32_t a_handle) { cancelled.push_back(a_handle); } };
			const auto ticket = Fetch(requester, [&] {
				requester.Deliver(Answer(41, WebApiTicket::kResultOk, 8));
				return std::uint32_t{ 41 };
			}, 2s);
			assert(ticket.size() == 8);
		}
		assert((cancelled == std::vector<std::uint32_t>{ 41 }));
	}

	void TestAnswerWhileIdleIsDropped()
	{
		Harness harness;
		harness.requester.Deliver(Answer(51, WebApiTicket::kResultOk, 8));
		const auto ticket = Fetch(harness.requester, [] { return std::uint32_t{ 51 }; }, 50ms);
		assert(ticket.empty());
		assert((harness.cancelled == std::vector<std::uint32_t>{ 51 }));
	}

	void TestBeginNeverBlocksAndPollDeliversTheTicket()
	{
		Harness harness;
		std::vector<std::uint8_t> ticket;
		assert(harness.requester.Begin([&] { return std::uint32_t{ 21 }; }, 2s));
		assert(harness.requester.Poll(ticket) == WebApiTicket::Requester::PollResult::Pending);
		harness.requester.Deliver(Answer(99, WebApiTicket::kResultOk, 10));
		assert(harness.requester.Poll(ticket) == WebApiTicket::Requester::PollResult::Pending);
		harness.requester.Deliver(Answer(21, WebApiTicket::kResultOk, 40));
		assert(harness.requester.Poll(ticket) == WebApiTicket::Requester::PollResult::Ready);
		assert(ticket.size() == 40);
		assert(harness.requester.Poll(ticket) == WebApiTicket::Requester::PollResult::Failed);
		assert(harness.cancelled.empty());
		assert(harness.requester.Begin([&] { return std::uint32_t{ 22 }; }, 2s));
		assert((harness.cancelled == std::vector<std::uint32_t>{ 21 }));
	}

	void TestBeginAcceptsAnAnswerThatArrivesDuringIssue()
	{
		Harness harness;
		std::vector<std::uint8_t> ticket;
		assert(harness.requester.Begin([&] {
			harness.requester.Deliver(Answer(23, WebApiTicket::kResultOk, 8));
			return std::uint32_t{ 23 };
		}, 2s));
		assert(harness.requester.Poll(ticket) == WebApiTicket::Requester::PollResult::Ready);
		assert(ticket.size() == 8);
	}

	void TestPollFailsAndCancelsOnTimeoutOrBadAnswer()
	{
		Harness harness;
		std::vector<std::uint8_t> ticket;
		assert(!harness.requester.Begin([] { return WebApiTicket::kInvalidHandle; }, 2s));
		assert(harness.requester.Poll(ticket) == WebApiTicket::Requester::PollResult::Failed);
		assert(harness.requester.Begin([] { return std::uint32_t{ 24 }; }, 10ms));
		std::this_thread::sleep_for(30ms);
		assert(harness.requester.Poll(ticket) == WebApiTicket::Requester::PollResult::Failed);
		assert((harness.cancelled == std::vector<std::uint32_t>{ 24 }));
		harness.requester.Deliver(Answer(24, WebApiTicket::kResultOk, 8));
		assert(harness.requester.Poll(ticket) == WebApiTicket::Requester::PollResult::Failed);
		assert(harness.requester.Begin([&] {
			harness.requester.Deliver(Answer(25, 2, 8));
			return std::uint32_t{ 25 };
		}, 2s));
		assert(harness.requester.Poll(ticket) == WebApiTicket::Requester::PollResult::Failed);
		assert((harness.cancelled == std::vector<std::uint32_t>{ 24, 25 }));
	}

	void TestBeginCancelsAPendingRequestAndDestructorCancelsIt()
	{
		std::vector<std::uint32_t> cancelled;
		{
			WebApiTicket::Requester requester{ [&](std::uint32_t a_handle) { cancelled.push_back(a_handle); } };
			assert(requester.Begin([] { return std::uint32_t{ 26 }; }, 2s));
			assert(requester.Begin([] { return std::uint32_t{ 27 }; }, 2s));
			assert((cancelled == std::vector<std::uint32_t>{ 26 }));
		}
		assert((cancelled == std::vector<std::uint32_t>{ 26, 27 }));
	}
}

int main()
{
	TestTicketArrivesFromAnotherThread();
	TestAnswerBeforeIssueReturns();
	TestMaximumSizeAccepted();
	TestBadAnswersAreRefusedAndCancelled();
	TestInvalidHandleIssuesNoCancel();
	TestTimeoutCancelsAndLateAnswerIsIgnored();
	TestOtherHandlesAreIgnored();
	TestHeldTicketIsCancelledOnNextRequestAndRelease();
	TestDestructorCancelsHeldTicket();
	TestAnswerWhileIdleIsDropped();
	TestBeginNeverBlocksAndPollDeliversTheTicket();
	TestBeginAcceptsAnAnswerThatArrivesDuringIssue();
	TestPollFailsAndCancelsOnTimeoutOrBadAnswer();
	TestBeginCancelsAPendingRequestAndDestructorCancelsIt();
	return 0;
}

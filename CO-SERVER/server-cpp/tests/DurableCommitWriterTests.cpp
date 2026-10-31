#include "DurableCommitWriter.h"

#include <cassert>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using CommonwealthOnline::Server::DurableCommitWriter;

	std::vector<std::string> g_committed;
	std::mutex g_committedMutex;

	void Record(const std::string& a_contents)
	{
		const std::scoped_lock lock(g_committedMutex);
		g_committed.push_back(a_contents);
	}

	std::vector<std::string> TakeCommitted()
	{
		const std::scoped_lock lock(g_committedMutex);
		auto committed = std::move(g_committed);
		g_committed.clear();
		return committed;
	}

	void CommitsRunOnTheWriterThread()
	{
		DurableCommitWriter writer([](const std::string& a_contents) { Record(a_contents); });
		writer.PostAndWait("first");
		assert(writer.CommitThreadId() != std::this_thread::get_id());
		assert(writer.CommitCount() == 1);
		assert(TakeCommitted() == std::vector<std::string>{ "first" });
	}

	void SlowCommitsAreMeasured()
	{
		DurableCommitWriter writer([](const std::string& a_contents) {
			std::this_thread::sleep_for(std::chrono::milliseconds(150));
			Record(a_contents);
		});
		writer.PostAndWait("slow");
		assert(writer.SlowestCommitSeconds() >= 0.1);
		(void)TakeCommitted();
	}

	void PendingCommitsKeepTheirOrder()
	{
		DurableCommitWriter writer([](const std::string& a_contents) {
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			Record(a_contents);
		});
		writer.PostAndWait("one");
		writer.Post("two");
		writer.Post("three");
		writer.Drain();
		assert(TakeCommitted() == (std::vector<std::string>{ "one", "two", "three" }));
	}

	void DrainWaitsForEveryPostedCommit()
	{
		DurableCommitWriter writer([](const std::string& a_contents) {
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			Record(a_contents);
		});
		for (int index = 0; index < 8; ++index) {
			writer.Post(std::to_string(index));
		}
		writer.Drain();
		assert(TakeCommitted().size() == 8);
	}

	void AFailedCommitReachesTheAwaiter()
	{
		DurableCommitWriter writer([](const std::string&) { throw std::runtime_error{ "disk full" }; });
		bool threw = false;
		try {
			writer.PostAndWait("fails");
		} catch (const std::exception&) {
			threw = true;
		}
		assert(threw);
	}
}

int main()
{
	CommitsRunOnTheWriterThread();
	SlowCommitsAreMeasured();
	PendingCommitsKeepTheirOrder();
	DrainWaitsForEveryPostedCommit();
	AFailedCommitReachesTheAwaiter();
	return 0;
}

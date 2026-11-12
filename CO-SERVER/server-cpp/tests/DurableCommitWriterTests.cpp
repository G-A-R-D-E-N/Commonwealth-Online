#include "DurableCommitWriter.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	void Require(bool a_condition)
	{
		if (a_condition) {
			return;
		}
		std::cerr << "check failed\n";
		std::abort();
	}

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
		Require(writer.CommitThreadId() != std::this_thread::get_id());
		Require(writer.CommitCount() == 1);
		Require(TakeCommitted() == std::vector<std::string>{ "first" });
	}

	void SlowCommitsAreMeasured()
	{
		DurableCommitWriter writer([](const std::string& a_contents) {
			std::this_thread::sleep_for(std::chrono::milliseconds(150));
			Record(a_contents);
		});
		writer.PostAndWait("slow");
		Require(writer.SlowestCommitSeconds() >= 0.1);
		(void)TakeCommitted();
	}

	void AwaitingEachPostCommitsThemInOrder()
	{
		DurableCommitWriter writer([](const std::string& a_contents) {
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			Record(a_contents);
		});
		writer.PostAndWait("one");
		writer.PostAndWait("two");
		writer.PostAndWait("three");
		Require(TakeCommitted() == (std::vector<std::string>{ "one", "two", "three" }));
	}

	void AReplacedPostNeverCommits()
	{
		std::mutex gateMutex;
		std::condition_variable gate;
		bool entered = false;
		bool released = false;
		DurableCommitWriter writer([&](const std::string& a_contents) {
			{
				std::unique_lock lock(gateMutex);
				entered = true;
				gate.notify_all();
				gate.wait(lock, [&] { return released; });
			}
			Record(a_contents);
		});

		writer.Post("first");
		{
			std::unique_lock lock(gateMutex);
			gate.wait(lock, [&] { return entered; });
		}
		writer.Post("second");
		writer.Post("third");
		{
			const std::scoped_lock lock(gateMutex);
			released = true;
		}
		gate.notify_all();
		writer.Drain();

		const auto committed = TakeCommitted();
		Require(!committed.empty());
		Require(committed.front() == "first");
		Require(committed.back() == "third");
		Require(std::find(committed.begin(), committed.end(), "second") == committed.end());
	}

	void CoalescedPostsCommitInPostOrderAndDrainCommitsTheLastPost()
	{
		DurableCommitWriter writer([](const std::string& a_contents) {
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			Record(a_contents);
		});
		for (int index = 0; index < 8; ++index) {
			writer.Post(std::to_string(index));
		}
		writer.Drain();

		const auto committed = TakeCommitted();
		Require(!committed.empty());
		Require(committed.size() <= 8);
		Require(committed.back() == "7");
		for (std::size_t index = 1; index < committed.size(); ++index) {
			Require(std::stoi(committed[index - 1]) < std::stoi(committed[index]));
		}
	}

	void DurabilityIsReportedPerSequence()
	{
		DurableCommitWriter writer([](const std::string& a_contents) { Record(a_contents); });
		const auto first = writer.Post("first");
		const auto second = writer.Post("second");
		Require(first < second);
		writer.Drain();
		Require(writer.PostedSequence() == second);
		Require(writer.DurableSequence() == second);
		Require(writer.FailedThrough() == 0);
		(void)TakeCommitted();
	}

	void AFailedCommitIsReportedForItsSequences()
	{
		DurableCommitWriter writer([](const std::string&) { throw std::runtime_error{ "disk full" }; });
		const auto ticket = writer.Post("fails");
		writer.Drain();
		Require(writer.FailedThrough() >= ticket);
		Require(writer.DurableSequence() < ticket);
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
		Require(threw);
	}
}

int main()
{
	CommitsRunOnTheWriterThread();
	SlowCommitsAreMeasured();
	AwaitingEachPostCommitsThemInOrder();
	AReplacedPostNeverCommits();
	CoalescedPostsCommitInPostOrderAndDrainCommitsTheLastPost();
	DurabilityIsReportedPerSequence();
	AFailedCommitIsReportedForItsSequences();
	AFailedCommitReachesTheAwaiter();
	return 0;
}

#include "DurableCommitWriter.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace CommonwealthOnline::Server
{
	namespace
	{
		constexpr double kSlowCommitSeconds = 0.1;

		void LogSlowCommit(double a_seconds, std::size_t a_bytes, bool a_failed)
		{
			static std::mutex s_logMutex;
			const auto line = "Durable commit: " + std::to_string(static_cast<long long>(a_seconds * 1000.0)) +
				" ms for " + std::to_string(a_bytes) + " bytes, failed=" + std::to_string(a_failed ? 1 : 0) + '\n';
			const std::scoped_lock lock(s_logMutex);
			std::cout << line << std::flush;
		}
	}

	DurableCommitWriter::DurableCommitWriter(Commit a_commit) :
		_commit(std::move(a_commit)),
		_thread([this] { Run(); })
	{}

	DurableCommitWriter::~DurableCommitWriter()
	{
		{
			const std::scoped_lock lock(_mutex);
			_stopping = true;
		}
		_wake.notify_all();
		_thread.join();
	}

	void DurableCommitWriter::Post(std::string a_contents)
	{
		const std::scoped_lock lock(_mutex);
		_pending = std::move(a_contents);
		++_postedSequence;
		_lastWriteFailed = false;
		_wake.notify_one();
	}

	void DurableCommitWriter::PostAndWait(std::string a_contents)
	{
		std::unique_lock lock(_mutex);
		_pending = std::move(a_contents);
		Waiter waiter{ ++_postedSequence };
		_lastWriteFailed = false;
		_waiters.push_back(&waiter);
		_wake.notify_one();
		_settled.wait(lock, [&] { return waiter.settled; });
		if (waiter.error) {
			throw std::runtime_error(*waiter.error);
		}
	}

	void DurableCommitWriter::Drain()
	{
		std::unique_lock lock(_mutex);
		_settled.wait(lock, [&] { return _settledSequence >= _postedSequence; });
	}

	void DurableCommitWriter::ReportFailure(std::string a_message)
	{
		const std::scoped_lock lock(_mutex);
		_failure = std::move(a_message);
	}

	bool DurableCommitWriter::LastWriteFailed() const
	{
		const std::scoped_lock lock(_mutex);
		return _lastWriteFailed;
	}

	std::optional<std::string> DurableCommitWriter::TakeFailure()
	{
		const std::scoped_lock lock(_mutex);
		auto failure = std::move(_failure);
		_failure.reset();
		return failure;
	}

	double DurableCommitWriter::SlowestCommitSeconds() const
	{
		const std::scoped_lock lock(_mutex);
		return _slowestCommitSeconds;
	}

	std::size_t DurableCommitWriter::CommitCount() const
	{
		const std::scoped_lock lock(_mutex);
		return _commitCount;
	}

	std::thread::id DurableCommitWriter::CommitThreadId() const
	{
		const std::scoped_lock lock(_mutex);
		return _commitThread;
	}

	void DurableCommitWriter::Run()
	{
		std::unique_lock lock(_mutex);
		for (;;) {
			_wake.wait(lock, [&] { return _pending.has_value() || _stopping; });
			if (!_pending) {
				return;
			}
			auto contents = std::move(*_pending);
			_pending.reset();
			const auto through = _postedSequence;
			lock.unlock();

			const auto started = std::chrono::steady_clock::now();
			std::optional<std::string> error;
			try {
				_commit(contents);
			} catch (const std::exception& exception) {
				error = exception.what();
			} catch (...) {
				error = "Durable write failed with an unspecified error.";
			}
			const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
			if (elapsed > kSlowCommitSeconds) {
				LogSlowCommit(elapsed, contents.size(), error.has_value());
			}

			lock.lock();
			_commitThread = std::this_thread::get_id();
			++_commitCount;
			_slowestCommitSeconds = (std::max)(_slowestCommitSeconds, elapsed);
			_settledSequence = through;
			_lastWriteFailed = error.has_value() && through == _postedSequence;
			bool claimed = false;
			for (auto waiter = _waiters.begin(); waiter != _waiters.end();) {
				if ((*waiter)->sequence <= through) {
					(*waiter)->error = error;
					(*waiter)->settled = true;
					claimed = true;
					waiter = _waiters.erase(waiter);
				} else {
					++waiter;
				}
			}
			if (error && !claimed) {
				_failure = *error;
			}
			_settled.notify_all();
		}
	}
}

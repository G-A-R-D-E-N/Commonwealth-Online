#include "DurableCommitWriter.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace CommonwealthOnline::Server
{
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

			std::optional<std::string> error;
			try {
				_commit(contents);
			} catch (const std::exception& exception) {
				error = exception.what();
			} catch (...) {
				error = "Durable write failed with an unspecified error.";
			}

			lock.lock();
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

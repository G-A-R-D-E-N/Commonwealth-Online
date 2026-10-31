#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace CommonwealthOnline::Server
{
	class DurableCommitWriter final
	{
	public:
		using Commit = std::function<void(const std::string&)>;

		explicit DurableCommitWriter(Commit a_commit);
		~DurableCommitWriter();
		DurableCommitWriter(const DurableCommitWriter&) = delete;
		DurableCommitWriter& operator=(const DurableCommitWriter&) = delete;

		void Post(std::string a_contents);
		void PostAndWait(std::string a_contents);
		void Drain();
		void ReportFailure(std::string a_message);
		[[nodiscard]] bool LastWriteFailed() const;
		[[nodiscard]] std::optional<std::string> TakeFailure();
		[[nodiscard]] double SlowestCommitSeconds() const;
		[[nodiscard]] std::size_t CommitCount() const;
		[[nodiscard]] std::thread::id CommitThreadId() const;

	private:
		struct Waiter
		{
			std::uint64_t sequence{};
			bool settled{};
			std::optional<std::string> error;
		};

		void Run();

		Commit _commit;
		mutable std::mutex _mutex;
		std::condition_variable _wake;
		std::condition_variable _settled;
		std::optional<std::string> _pending;
		std::uint64_t _postedSequence{};
		std::uint64_t _settledSequence{};
		std::vector<Waiter*> _waiters;
		std::optional<std::string> _failure;
		double _slowestCommitSeconds{};
		std::size_t _commitCount{};
		std::thread::id _commitThread;
		bool _lastWriteFailed{};
		bool _stopping{};
		std::thread _thread;
	};
}

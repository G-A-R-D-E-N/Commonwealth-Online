#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace CommonwealthOnline::Server
{
	inline constexpr std::chrono::milliseconds WorkerStopGrace{ 250 };

	struct WorkerExit
	{
		std::mutex mutex;
		std::condition_variable wake;
		std::condition_variable exited;
		bool stopping{};
		bool finished{};
		std::atomic_bool abandoned{};
	};

	inline void MarkWorkerExited(WorkerExit& a_exit)
	{
		{
			std::lock_guard lock{ a_exit.mutex };
			a_exit.finished = true;
		}
		a_exit.exited.notify_all();
	}

	inline bool StopWorker(std::thread& a_thread, WorkerExit& a_exit)
	{
		if (!a_thread.joinable()) {
			return false;
		}
		bool finished = false;
		{
			std::unique_lock lock{ a_exit.mutex };
			a_exit.stopping = true;
			a_exit.wake.notify_all();
			finished = a_exit.exited.wait_for(lock, WorkerStopGrace, [&a_exit] { return a_exit.finished; });
			if (!finished) {
				a_exit.abandoned.store(true, std::memory_order_release);
			}
		}
		if (finished) {
			a_thread.join();
			return true;
		}
		a_thread.detach();
		return false;
	}
}

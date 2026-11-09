#pragma once

#include "F4TAdminItems.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace F4T::AdminItems
{
	inline bool StillCurrent(std::uint64_t a_entryGeneration, std::uint64_t a_currentGeneration, bool a_connected)
	{
		return a_connected && a_entryGeneration == a_currentGeneration;
	}

	template <typename TRequest>
	class PendingQueue
	{
	public:
		struct DrainResult
		{
			std::vector<TRequest> admitted;
			std::size_t discarded{ 0 };
		};

		QueueResult Enqueue(TRequest a_request, std::uint64_t a_connectionGeneration)
		{
			const std::scoped_lock lock(_mutex);
			if (_entries.size() >= kMaxPendingRequests) {
				return QueueResult::kBusy;
			}
			_entries.push_back(Entry{ std::move(a_request), a_connectionGeneration });
			return QueueResult::kQueued;
		}

		DrainResult Drain(std::uint64_t a_currentConnectionGeneration, bool a_connected)
		{
			std::vector<Entry> entries;
			{
				const std::scoped_lock lock(_mutex);
				entries.swap(_entries);
			}

			DrainResult result;
			for (auto& entry : entries) {
				if (StillCurrent(entry.connectionGeneration, a_currentConnectionGeneration, a_connected)) {
					result.admitted.push_back(std::move(entry.request));
				} else {
					++result.discarded;
				}
			}
			return result;
		}

	private:
		static constexpr std::size_t kMaxPendingRequests = 64;

		struct Entry
		{
			TRequest      request;
			std::uint64_t connectionGeneration{ 0 };
		};

		std::mutex _mutex;
		std::vector<Entry> _entries;
	};
}

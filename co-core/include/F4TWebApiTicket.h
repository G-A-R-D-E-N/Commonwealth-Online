#pragma once

// constraint: layout from Steamworks GetTicketForWebApiResponse_t, callback id 168, pack(4)

#include "F4TSteamId.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace F4T::SteamId::WebApiTicket
{
	inline constexpr int kResponseCallbackId = 168;
	inline constexpr std::int32_t kResultOk = 1;
	inline constexpr std::uint32_t kInvalidHandle = 0;
	inline constexpr std::size_t kMaximumEarlyResponses = 4;
	inline constexpr std::chrono::milliseconds kDefaultTimeout{ 5000 };

#pragma pack(push, 4)
	struct Response
	{
		std::uint32_t handle{};
		std::int32_t result{};
		std::int32_t size{};
		std::uint8_t ticket[kMaximumTicketBytes]{};
	};
#pragma pack(pop)

	static_assert(sizeof(Response) == 12 + kMaximumTicketBytes);
	static_assert(offsetof(Response, size) == 8);
	static_assert(offsetof(Response, ticket) == 12);

	class Requester
	{
	public:
		using Issue = std::function<std::uint32_t()>;
		using Cancel = std::function<void(std::uint32_t)>;

		explicit Requester(Cancel a_cancel) :
			_cancel(std::move(a_cancel))
		{}

		Requester(const Requester&) = delete;
		Requester& operator=(const Requester&) = delete;

		~Requester() { Release(); }

		// constraint: Steam answers on the thread that pumps callbacks; once that thread is seen, Request refuses to wait on it
		[[nodiscard]] std::vector<std::uint8_t> Request(const Issue& a_issue, std::chrono::milliseconds a_timeout = kDefaultTimeout)
		{
			std::lock_guard serial{ _serial };
			{
				std::lock_guard lock{ _mutex };
				if (_pump == std::this_thread::get_id()) return {};
			}
			Release();
			{
				std::lock_guard lock{ _mutex };
				_requestThread = std::this_thread::get_id();
				_awaiting = true;
				_handleKnown = false;
				_arrivals.clear();
			}
			const auto handle = a_issue();
			std::unique_lock lock{ _mutex };
			if (handle == kInvalidHandle) {
				_awaiting = false;
				_arrivals.clear();
				return {};
			}
			_handle = handle;
			_handleKnown = true;
			const auto matches = [this] {
				return std::find_if(_arrivals.begin(), _arrivals.end(), [this](const Response& a_response) { return a_response.handle == _handle; });
			};
			_ready.wait_for(lock, a_timeout, [&] { return matches() != _arrivals.end(); });
			const auto found = matches();
			std::optional<Response> response;
			if (found != _arrivals.end()) response = *found;
			_awaiting = false;
			_handleKnown = false;
			_arrivals.clear();
			if (!response || response->result != kResultOk || response->size <= 0 ||
				static_cast<std::size_t>(response->size) > kMaximumTicketBytes) {
				lock.unlock();
				_cancel(handle);
				return {};
			}
			_held = handle;
			return std::vector<std::uint8_t>(response->ticket, response->ticket + response->size);
		}

		void Deliver(const Response& a_response)
		{
			std::lock_guard lock{ _mutex };
			if (!_awaiting) return;
			if (std::this_thread::get_id() != _requestThread) _pump = std::this_thread::get_id();
			if (_handleKnown) {
				if (a_response.handle != _handle) return;
			} else if (_arrivals.size() >= kMaximumEarlyResponses) {
				return;
			}
			_arrivals.push_back(a_response);
			_ready.notify_all();
		}

		void Release()
		{
			std::uint32_t held = kInvalidHandle;
			{
				std::lock_guard lock{ _mutex };
				held = std::exchange(_held, kInvalidHandle);
			}
			if (held != kInvalidHandle) _cancel(held);
		}

	private:
		Cancel _cancel;
		std::mutex _serial;
		std::mutex _mutex;
		std::condition_variable _ready;
		bool _awaiting{};
		std::thread::id _requestThread;
		std::thread::id _pump;
		bool _handleKnown{};
		std::uint32_t _handle{ kInvalidHandle };
		std::uint32_t _held{ kInvalidHandle };
		std::vector<Response> _arrivals;
	};
}

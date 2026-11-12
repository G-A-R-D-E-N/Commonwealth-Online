#pragma once

// constraint: layout from Steamworks GetTicketForWebApiResponse_t, callback id 168, pack(4)

#include "F4TSteamId.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
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

		~Requester()
		{
			Abandon();
			Release();
		}

		enum class PollResult { Pending, Ready, Failed };

		// constraint: Begin never waits; Poll must be called by the same owner and never blocks
		[[nodiscard]] bool Begin(const Issue& a_issue, std::chrono::milliseconds a_timeout = kDefaultTimeout)
		{
			std::lock_guard serial{ _serial };
			Abandon();
			Release();
			{
				std::lock_guard lock{ _mutex };
				_awaiting = true;
				_handleKnown = false;
				_arrivals.clear();
			}
			const auto handle = a_issue();
			std::lock_guard lock{ _mutex };
			if (handle == kInvalidHandle) {
				_awaiting = false;
				_arrivals.clear();
				return false;
			}
			_handle = handle;
			_handleKnown = true;
			_polling = true;
			_deadline = std::chrono::steady_clock::now() + a_timeout;
			return true;
		}

		[[nodiscard]] PollResult Poll(std::vector<std::uint8_t>& a_ticket)
		{
			std::uint32_t handle = kInvalidHandle;
			{
				std::lock_guard lock{ _mutex };
				if (!_polling) return PollResult::Failed;
				handle = _handle;
				const auto found = std::find_if(_arrivals.begin(), _arrivals.end(), [this](const Response& a_response) { return a_response.handle == _handle; });
				if (found == _arrivals.end() && std::chrono::steady_clock::now() < _deadline) return PollResult::Pending;
				std::optional<Response> response;
				if (found != _arrivals.end()) response = *found;
				_polling = false;
				_awaiting = false;
				_handleKnown = false;
				_arrivals.clear();
				if (response && response->result == kResultOk && response->size > 0 &&
					static_cast<std::size_t>(response->size) <= kMaximumTicketBytes) {
					_held = handle;
					a_ticket.assign(response->ticket, response->ticket + response->size);
					return PollResult::Ready;
				}
			}
			_cancel(handle);
			return PollResult::Failed;
		}

		void Deliver(const Response& a_response)
		{
			std::lock_guard lock{ _mutex };
			if (!_awaiting) return;
			if (_handleKnown) {
				if (a_response.handle != _handle) return;
			} else if (_arrivals.size() >= kMaximumEarlyResponses) {
				return;
			}
			_arrivals.push_back(a_response);
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
		void Abandon()
		{
			std::uint32_t pending = kInvalidHandle;
			{
				std::lock_guard lock{ _mutex };
				if (_polling) {
					pending = _handle;
					_polling = false;
					_awaiting = false;
					_handleKnown = false;
					_arrivals.clear();
				}
			}
			if (pending != kInvalidHandle) _cancel(pending);
		}

		Cancel _cancel;
		std::mutex _serial;
		std::mutex _mutex;
		bool _awaiting{};
		bool _handleKnown{};
		bool _polling{};
		std::chrono::steady_clock::time_point _deadline;
		std::uint32_t _handle{ kInvalidHandle };
		std::uint32_t _held{ kInvalidHandle };
		std::vector<Response> _arrivals;
	};
}

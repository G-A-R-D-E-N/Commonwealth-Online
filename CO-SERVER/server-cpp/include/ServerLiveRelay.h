#pragma once

#include "AdminCommandDispatcher.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace CommonwealthOnline::Server
{
	class ServerLiveRelay
	{
	public:
		using Handler = std::function<AdminActionResult(const nlohmann::json&)>;

		[[nodiscard]] inline AdminActionResult Submit(const nlohmann::json& a_request, std::chrono::milliseconds a_timeout)
		{
			std::promise<AdminActionResult> promise;
			auto result = promise.get_future();
			std::uint64_t generation = 0;
			{
				std::scoped_lock lock(_gate);
				if (_pending) {
					return AdminActionResult{ false, "Another server settings change is already in progress.", nullptr };
				}
				generation = _nextGeneration++;
				_pending.emplace(Pending{ a_request, std::move(promise), generation });
			}
			if (result.wait_for(a_timeout) != std::future_status::ready) {
				std::scoped_lock lock(_gate);
				if (_pending && _pending->generation == generation) {
					_pending.reset();
				}
				return AdminActionResult{ false, "The server did not answer the settings change.", nullptr };
			}
			return result.get();
		}

		void Drain(const Handler& a_handler)
		{
			std::optional<Pending> pending;
			{
				std::scoped_lock lock(_gate);
				pending = std::move(_pending);
				_pending.reset();
			}
			if (!pending) {
				return;
			}
			AdminActionResult outcome;
			try {
				outcome = a_handler(pending->request);
			} catch (const std::exception& exception) {
				outcome = AdminActionResult{ false, exception.what(), nullptr };
			} catch (...) {
				outcome = AdminActionResult{ false, "The server settings change failed.", nullptr };
			}
			pending->result.set_value(std::move(outcome));
		}

		void FailPending(std::string_view a_reason)
		{
			std::scoped_lock lock(_gate);
			if (_pending) {
				_pending->result.set_value(AdminActionResult{ false, std::string{ a_reason }, nullptr });
				_pending.reset();
			}
		}

	private:
		struct Pending
		{
			nlohmann::json request;
			std::promise<AdminActionResult> result;
			std::uint64_t generation{};
		};

		std::mutex _gate;
		std::optional<Pending> _pending;
		std::uint64_t _nextGeneration{ 1 };
	};
}

#include "ServerEvents.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <string_view>
#include <utility>

namespace CommonwealthOnline::Server
{
	namespace
	{
		bool Invoke(const ServerEvents::Handler& a_handler, const ServerEvent& a_event)
		{
			try {
				return a_handler(a_event);
			}
			catch (...) {
				std::cerr << "Server event handler threw; the event is allowed.\n";
			}
			return true;
		}

		constexpr auto kDispatchBudget = std::chrono::milliseconds(50);

		struct DispatchScope
		{
			explicit DispatchScope(std::size_t& a_depth) : depth{ a_depth } { ++depth; }
			~DispatchScope() { --depth; }
			DispatchScope(const DispatchScope&) = delete;
			DispatchScope& operator=(const DispatchScope&) = delete;

			std::size_t& depth;
		};

		void ReportOverrun(
			const char* a_operation,
			ServerEventKind a_kind,
			ServerEvents::Token a_token,
			std::size_t a_position,
			std::size_t a_total,
			std::chrono::steady_clock::time_point a_started)
		{
			const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - a_started);
			std::cerr << "Server event " << EventName(a_kind) << " " << a_operation << " dispatch budget spent after handler "
				<< a_position << " of " << a_total << " (subscription " << a_token << ", " << elapsed.count() << " ms); "
				<< (a_total - a_position) << (std::string_view{ a_operation } == "notify" ? " remaining handler(s) skipped.\n" : " remaining handler(s) still run.\n");
		}
	}

	ServerEvents::Token ServerEvents::Subscribe(ServerEventKind a_kind, Handler a_handler, int a_priority)
	{
		if (!a_handler) return 0;
		const auto token = nextToken++;
		subscriptions.push_back(Subscription{ token, a_kind, a_priority, std::move(a_handler) });
		std::stable_sort(subscriptions.begin(), subscriptions.end(), [](const Subscription& a_left, const Subscription& a_right) {
			return a_left.priority > a_right.priority;
		});
		return token;
	}

	bool ServerEvents::Unsubscribe(Token a_token)
	{
		const auto found = std::find_if(subscriptions.begin(), subscriptions.end(), [a_token](const Subscription& a_subscription) {
			return a_subscription.token == a_token;
		});
		if (found == subscriptions.end()) return false;
		subscriptions.erase(found);
		return true;
	}

	std::vector<ServerEvents::Subscription> ServerEvents::Matching(ServerEventKind a_kind) const
	{
		std::vector<Subscription> matches;
		for (const auto& subscription : subscriptions) {
			if (subscription.kind == a_kind) matches.push_back(subscription);
		}
		return matches;
	}

	bool ServerEvents::Dispatching() const noexcept
	{
		return dispatchDepth > 0;
	}

	bool ServerEvents::Allow(const ServerEvent& a_event)
	{
		const DispatchScope scope{ dispatchDepth };
		const auto started = std::chrono::steady_clock::now();
		const auto handlers = Matching(a_event.kind);
		bool reported = false;
		for (std::size_t index = 0; index < handlers.size(); ++index) {
			if (!Invoke(handlers[index].handler, a_event)) return false;
			if (!reported && index + 1 < handlers.size() && std::chrono::steady_clock::now() - started >= kDispatchBudget) {
				ReportOverrun("allow", a_event.kind, handlers[index].token, index + 1, handlers.size(), started);
				reported = true;
			}
		}
		return true;
	}

	void ServerEvents::Notify(const ServerEvent& a_event)
	{
		const DispatchScope scope{ dispatchDepth };
		const auto started = std::chrono::steady_clock::now();
		const auto handlers = Matching(a_event.kind);
		for (std::size_t index = 0; index < handlers.size(); ++index) {
			Invoke(handlers[index].handler, a_event);
			if (index + 1 < handlers.size() && std::chrono::steady_clock::now() - started >= kDispatchBudget) {
				ReportOverrun("notify", a_event.kind, handlers[index].token, index + 1, handlers.size(), started);
				break;
			}
		}
	}
}

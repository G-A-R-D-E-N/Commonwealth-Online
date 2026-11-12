#include "ServerLiveRelay.h"

#include <cassert>
#include <chrono>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

int main()
{
	using CommonwealthOnline::Server::AdminActionResult;
	using CommonwealthOnline::Server::ServerLiveRelay;
	using Json = nlohmann::json;

	ServerLiveRelay relay;
	AdminActionResult submitted;
	std::thread worker([&] { submitted = relay.Submit(Json{ { "document", { { "enable_chat", false } } } }, std::chrono::seconds(2)); });

	bool handled = false;
	for (int attempt = 0; attempt < 500 && !handled; ++attempt) {
		relay.Drain([&](const Json& a_request) {
			handled = true;
			assert(a_request.at("document").at("enable_chat") == false);
			return AdminActionResult{ true, "Server settings applied.", Json{ { "applied", Json::array({ "enableChat" }) } } };
		});
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	worker.join();
	assert(handled);
	assert(submitted.ok);
	assert(submitted.data.at("applied").at(0) == "enableChat");

	const auto timedOut = relay.Submit(Json{ { "document", Json::object() } }, std::chrono::milliseconds(20));
	assert(!timedOut.ok);
	assert(timedOut.message.find("did not answer") != std::string::npos);

	bool drainedAfterTimeout = false;
	relay.Drain([&](const Json&) {
		drainedAfterTimeout = true;
		return AdminActionResult{ true, {}, nullptr };
	});
	assert(!drainedAfterTimeout);

	AdminActionResult failed;
	std::thread waiter([&] { failed = relay.Submit(Json{ { "document", Json::object() } }, std::chrono::seconds(2)); });
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	relay.FailPending("Server is shutting down.");
	waiter.join();
	assert(!failed.ok);
	assert(failed.message == "Server is shutting down.");

	{
		std::promise<void> release;
		const auto gate = release.get_future().share();
		bool dispatched = false;
		std::thread drainer([&] {
			for (int attempt = 0; attempt < 5000 && !dispatched; ++attempt) {
				relay.Drain([&](const Json&) {
					dispatched = true;
					gate.wait();
					return AdminActionResult{ true, "Server settings applied live.", nullptr };
				});
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		});
		const auto held = relay.Submit(Json{ { "document", { { "enable_chat", false } } } }, std::chrono::seconds(2));
		assert(!held.ok);
		assert(held.data.value("unresolved", false));
		const auto refusedWhileHeld = relay.Submit(Json{ { "document", Json::object() } }, std::chrono::seconds(2));
		assert(!refusedWhileHeld.ok);
		assert(refusedWhileHeld.data.value("unresolved", false));
		release.set_value();
		drainer.join();
		assert(dispatched);

		AdminActionResult retried;
		bool retriedHandled = false;
		std::thread retrier([&] { retried = relay.Submit(Json{ { "document", Json::object() } }, std::chrono::seconds(2)); });
		for (int attempt = 0; attempt < 500 && !retriedHandled; ++attempt) {
			relay.Drain([&](const Json&) {
				retriedHandled = true;
				return AdminActionResult{ true, "Server settings applied live.", nullptr };
			});
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		retrier.join();
		assert(retriedHandled);
		assert(retried.ok);
	}

	return 0;
}

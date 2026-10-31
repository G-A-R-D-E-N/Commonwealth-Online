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

	return 0;
}

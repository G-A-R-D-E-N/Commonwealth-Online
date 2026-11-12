#include "ServerConfiguration.h"

#include <nlohmann/json.hpp>

#include <cassert>

int main()
{
	using namespace CommonwealthOnline::Server::Configuration;
	using Json = nlohmann::json;

	const auto defaults = Parse(Json::object());
	assert(defaults.inboundBudget.maximumPacketsPerWindow == 120);
	assert(defaults.inboundBudget.maximumBytesPerWindow == 256ULL * 1024ULL);
	assert(defaults.inboundBudget.maximumViolationWindows == 3);
	assert(Validate(defaults).empty());

	const auto tuned = Parse(Json{ { "inbound_packets_per_second", 200 }, { "inbound_bytes_per_second", 1048576 }, { "inbound_violation_windows", 5 } });
	assert(tuned.inboundBudget.maximumPacketsPerWindow == 200);
	assert(tuned.inboundBudget.maximumBytesPerWindow == 1048576);
	assert(tuned.inboundBudget.maximumViolationWindows == 5);
	assert(Validate(tuned).empty());
	assert(Parse(Json{ { "inboundBytesPerSecond", 131072 } }).inboundBudget.maximumBytesPerWindow == 131072);

	assert(Validate(Parse(Json{ { "inbound_packets_per_second", 512 } })).empty());
	assert(!Validate(Parse(Json{ { "inbound_packets_per_second", 9 } })).empty());
	assert(!Validate(Parse(Json{ { "inbound_packets_per_second", 513 } })).empty());
	assert(!Validate(Parse(Json{ { "inbound_packets_per_second", -5 } })).empty());
	assert(!Validate(Parse(Json{ { "inbound_bytes_per_second", 65535 } })).empty());
	assert(!Validate(Parse(Json{ { "inbound_bytes_per_second", 67108865 } })).empty());
	assert(!Validate(Parse(Json{ { "inbound_violation_windows", 0 } })).empty());
	assert(!Validate(Parse(Json{ { "inbound_violation_windows", 61 } })).empty());
	return 0;
}

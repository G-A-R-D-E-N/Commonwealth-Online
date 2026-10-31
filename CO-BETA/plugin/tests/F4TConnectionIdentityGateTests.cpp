#include "F4TConnectionIdentityGate.h"

#include <cassert>
#include <optional>
#include <string>

int main()
{
	std::string error;
	int connectCalls = 0;
	std::optional<int> heldIdentity = 7;
	const auto prepareFailure = F4T::ConnectionIdentityGate::PrepareHoldConnect(
		[&] { heldIdentity.reset(); },
		[](std::string& a_error) -> std::optional<int> {
			a_error = "persistence failed";
			return std::nullopt;
		},
		[](int) {},
		[&](std::string&) {
			++connectCalls;
			return true;
		},
		error);
	assert(!prepareFailure);
	assert(connectCalls == 0);
	assert(error == "persistence failed");
	assert(!heldIdentity);

	const auto failedConnect = F4T::ConnectionIdentityGate::PrepareHoldConnect(
		[&] { heldIdentity.reset(); },
		[](std::string&) -> std::optional<int> { return 42; },
		[&](int a_identity) { heldIdentity = a_identity; },
		[&](std::string&) {
			++connectCalls;
			return false;
		},
		error);
	assert(!failedConnect);
	assert(connectCalls == 1);
	assert(!heldIdentity);

	const auto nextAttempt = F4T::ConnectionIdentityGate::PrepareHoldConnect(
		[&] { heldIdentity.reset(); },
		[](std::string&) -> std::optional<int> { return 84; },
		[&](int a_identity) { heldIdentity = a_identity; },
		[&](std::string&) {
			++connectCalls;
			return true;
		},
		error);
	assert(nextAttempt);
	assert(connectCalls == 2);
	assert(heldIdentity == 84);
	return 0;
}

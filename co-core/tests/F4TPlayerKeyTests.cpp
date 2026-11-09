#include "F4TPlayerKey.h"

#include <cassert>
#include <string>

int main()
{
	using F4T::PlayerKey::Derive;
	using F4T::PlayerKey::IsPlayerKey;

	const std::string secret(64, 'a');
	const std::string otherSecret(64, 'b');
	const std::string steamId = "76561198000000001";
	const auto steamKey = Derive(secret, steamId, "co-1");
	assert(steamKey.size() == 64);
	assert(IsPlayerKey(steamKey));
	assert(steamKey.find(steamId) == std::string::npos);
	assert(steamKey == Derive(secret, steamId, "co-2"));
	assert(steamKey == Derive(secret, steamId, ""));
	assert(steamKey != Derive(otherSecret, steamId, ""));
	assert(Derive(secret, "", steamId) != steamKey);
	const auto profileKey = Derive(secret, "", "co-1");
	assert(IsPlayerKey(profileKey));
	assert(profileKey != steamKey);
	assert(profileKey == Derive(secret, "", "co-1"));
	assert(profileKey != Derive(otherSecret, "", "co-1"));
	assert(profileKey != Derive(secret, "", "co-2"));
	assert(Derive(secret, "", "").empty());
	assert(Derive("", steamId, "").empty());
	assert(Derive("", "", "co-1").empty());
	assert(IsPlayerKey(std::string(64, '0')));
	assert(!IsPlayerKey(std::string(63, '0')));
	assert(!IsPlayerKey(std::string(64, 'A')));
	assert(!IsPlayerKey(""));
	return 0;
}

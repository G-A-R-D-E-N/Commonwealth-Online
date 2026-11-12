#include "F4TSleepMarkerFilter.h"

#include <array>
#include <cassert>
#include <thread>

int main()
{
	using namespace F4T::SleepMarker;
	int actor{}, otherActor{}, bed{}, otherBed{};
	Registry registry;
	auto oldToken = std::make_shared<const int>(1);
	registry.emplace(&actor, oldToken);
	auto currentToken = std::make_shared<const int>(2);
	registry.insert_or_assign(&actor, currentToken);
	assert(ExtractRegistration(registry, oldToken).empty());
	assert(registry.size() == 1);
	assert(ExtractRegistration(registry, {}).empty());
	assert(!ExtractRegistration(registry, currentToken).empty());
	assert(registry.empty());
	auto* invalidatedActor = new int{};
	registry.emplace(invalidatedActor, oldToken);
	delete invalidatedActor;
	assert(!ExtractRegistration(registry, oldToken).empty());
	assert(registry.empty());
	thread_local Selection current;
	assert(!current.Reject(&actor, &bed, 2));
	{
		CollectionScope outer(current, { &actor, &bed, 2 });
		assert(!current.Reject(&actor, &bed, 2));
		assert(current.Reject(&actor, &bed, 1));
		assert(!current.Reject(&otherActor, &bed, 1));
		assert(!current.Reject(&actor, &otherBed, 1));
		{
			CollectionScope unrelated(current, {});
			assert(!current.Reject(&actor, &bed, 1));
		}
		assert(current.Reject(&actor, &bed, 1));
		{
			CollectionScope nested(current, { &actor, &bed, 1 });
			assert(current.Reject(&actor, &bed, 2));
			assert(!current.Reject(&actor, &bed, 1));
		}
		assert(!current.Reject(&actor, &bed, 2));
		std::thread independent([&] { assert(!current.Reject(&actor, &bed, 1)); });
		independent.join();
	}
	assert(!current.Reject(&actor, &bed, 1));
	std::array<std::uint8_t, 5> call{ 0xE8, 0xFB, 0x00, 0x00, 0x00 };
	assert(MatchesCall(call, 0x1000, 0x1100));
	assert(!MatchesCall(call, 0x1000, 0x1101));
	call = { 0xE8, 0xFB, 0xFE, 0xFF, 0xFF };
	assert(MatchesCall(call, 0x1000, 0xF00));
	call[0] = 0xE9;
	assert(!MatchesCall(call, 0x1000, 0xF00));
	assert(!MatchesCall(std::span(call).first(4), 0x1000, 0xF00));
}

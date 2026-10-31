#include "F4THitAttribution.h"

#include <cassert>

namespace
{
	struct ObjectRef
	{
		int id{};
	};

	struct Actor : ObjectRef
	{
	};

	ObjectRef g_primary{ 1 };
	ObjectRef g_fallback{ 3 };
	Actor g_actor{ 4 };
}

int main()
{
	using F4T::HitAttribution::PickReference;

	const ObjectRef* const nullRef = nullptr;

	// First non-null reference wins.
	assert(PickReference(&g_primary, &g_fallback) == &g_primary);
	assert(PickReference(nullRef, &g_fallback) == &g_fallback);
	assert(PickReference(&g_primary, nullRef) == &g_primary);
	assert(PickReference(nullRef, nullRef) == nullptr);
	// A derived source (an Actor handle) resolves through its base reference type.
	assert(PickReference<ObjectRef>(&g_actor, &g_fallback) == &g_actor);
	assert(PickReference<ObjectRef>(nullRef, &g_actor) == &g_actor);

	return 0;
}

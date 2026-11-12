#pragma once

namespace RE
{
	class Actor;
}

namespace F4T::ProxyPathPredicates
{
	struct PathState
	{
		bool pathing;
		bool pathValid;
		bool pathComplete;
	};

	PathState ReadPathState(RE::Actor& a_actor);
}

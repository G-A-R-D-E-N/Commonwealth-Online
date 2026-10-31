#pragma once

#include "F4TNpcSync.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace RE
{
	class Actor;
}

namespace F4T::ProxyActorController
{
	void Update();

	std::optional<std::uint32_t> TryGetRemotePlayerIdForProxyFormId(std::uint32_t a_proxyFormId);

	RE::Actor* TryGetProxyActorForRemotePlayerId(std::uint32_t a_remotePlayerId);

	bool IsPlayerProxyActor(const RE::Actor& a_actor);

	struct ActiveProxyActor
	{
		std::uint32_t remotePlayerId{};
		RE::Actor* actor{};
	};

	std::vector<ActiveProxyActor> GetActiveProxyActors();

	void UpdateSafetyTest();
}

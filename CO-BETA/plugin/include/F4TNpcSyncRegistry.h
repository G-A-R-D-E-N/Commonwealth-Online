#pragma once

#include "F4TNpcSync.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::NpcSync
{
	struct PuppetIdentity
	{
		std::uint32_t npcId{};
		std::uint32_t sourceFormId{};
		std::uint32_t baseFormId{};
		std::string cellId;
		std::string worldspaceId;
		std::uint32_t authorityPlayerId{};
		std::uint32_t authorityEpoch{};
	};

	struct HostNpcIdentity
	{
		std::uint32_t npcId{};
		std::uint32_t sourceFormId{};
	};

	std::optional<PuppetIdentity> TryGetPuppetByActorFormId(std::uint32_t a_actorFormId);
	std::optional<std::uint32_t> TryGetPuppetActorFormIdByNpcId(std::uint32_t a_npcId, std::string_view a_cellId = {}, std::string_view a_worldspaceId = {}, std::uint32_t a_authorityPlayerId = 0, std::uint32_t a_authorityEpoch = 0);
	std::optional<HostNpcIdentity> TryGetHostNpcByActorFormId(std::uint32_t a_actorFormId);
	std::optional<std::uint32_t> TryGetHostActorFormIdByNpcId(std::uint32_t a_npcId);
	bool IsPuppetFormId(std::uint32_t a_formId);
	bool IsSuppressedFormId(std::uint32_t a_formId);
}

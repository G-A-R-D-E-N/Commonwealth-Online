#pragma once

#include "F4TPowerArmorSync.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace RE
{
	class PlayerCharacter;
}

namespace F4T::ParkedSuitMirror
{
	struct MirrorRequest
	{
		std::uint32_t suitId{};
		bool mine{};
		std::uint32_t frameBaseFormId{};
		std::optional<std::uint32_t> cellId;
		std::optional<std::uint32_t> worldspaceId;
		float x{};
		float y{};
		float z{};
		float angleZ{};
		F4T::PowerArmorSync::PowerArmorState powerArmor;
	};

	void Reconcile(RE::PlayerCharacter& a_player, const std::vector<MirrorRequest>& a_requests);
	[[nodiscard]] std::optional<std::uint32_t> SuitForFrame(std::uint32_t a_refFormId);
	void Graduate(std::uint32_t a_suitId);
	bool DestroyAll();
}

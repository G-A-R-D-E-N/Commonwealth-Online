#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace F4T::FurnitureSync
{
	inline constexpr std::string_view kCapabilityName = "furniture-sync-v1";

	enum class Action
	{
		Enter,
		Exit
	};

	struct RelayedUse
	{
		std::uint32_t playerId{};
		Action action{ Action::Enter };
	};

	[[nodiscard]] std::optional<Action> ParseAction(std::string_view a_action) noexcept;
	[[nodiscard]] std::string_view ActionName(Action a_action) noexcept;
	[[nodiscard]] std::optional<Action> ParseClientUse(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<RelayedUse> ParseRelayedUse(const nlohmann::json& a_packet);
}

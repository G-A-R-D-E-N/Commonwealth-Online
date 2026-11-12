#include "F4TFurnitureSync.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::FurnitureSync
{
	namespace
	{
		constexpr std::string_view kEnterName = "enter";
		constexpr std::string_view kExitName = "exit";
	}

	std::optional<Action> ParseAction(std::string_view a_action) noexcept
	{
		if (a_action == kEnterName) {
			return Action::Enter;
		}
		if (a_action == kExitName) {
			return Action::Exit;
		}
		return std::nullopt;
	}

	std::string_view ActionName(Action a_action) noexcept
	{
		return a_action == Action::Enter ? kEnterName : kExitName;
	}

	std::optional<Action> ParseClientUse(const nlohmann::json& a_packet)
	{
		const auto found = a_packet.find("action");
		if (found == a_packet.end() || !found->is_string()) {
			return std::nullopt;
		}
		return ParseAction(found->get_ref<const std::string&>());
	}

	std::optional<RelayedUse> ParseRelayedUse(const nlohmann::json& a_packet)
	{
		const auto player = a_packet.find("playerId");
		if (player == a_packet.end() || !player->is_number_integer()) {
			return std::nullopt;
		}
		const auto value = player->get<std::int64_t>();
		if (value < 1 || value > static_cast<std::int64_t>((std::numeric_limits<std::uint32_t>::max)())) {
			return std::nullopt;
		}
		const auto action = ParseClientUse(a_packet);
		if (!action) {
			return std::nullopt;
		}
		return RelayedUse{ static_cast<std::uint32_t>(value), *action };
	}
}

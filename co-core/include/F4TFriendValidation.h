#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::FriendValidation
{
	[[nodiscard]] std::optional<nlohmann::json> NormalizeFriendRequest(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeFriendRequestResponse(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeFriendRemove(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeFriendListRequest(const nlohmann::json& a_packet);
}

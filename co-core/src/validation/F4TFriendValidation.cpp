#include "F4TFriendValidation.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace F4T::FriendValidation
{
	namespace
	{
		using Json = nlohmann::json;
		constexpr std::size_t kMaximumIdentifierLength = 64;

		std::optional<std::string> ReadIdentifier(const Json& a_packet, std::string_view a_name)
		{
			const auto found = a_packet.find(a_name);
			if (found == a_packet.end() || !found->is_string()) {
				return std::nullopt;
			}
			auto value = found->get<std::string>();
			if (value.empty() || value.size() > kMaximumIdentifierLength) {
				return std::nullopt;
			}
			return value;
		}

		std::optional<std::uint32_t> ReadTargetPlayerId(const Json& a_packet)
		{
			const auto found = a_packet.find("targetPlayerId");
			if (found == a_packet.end() || !found->is_number_integer()) {
				return std::nullopt;
			}
			std::uint64_t value = 0;
			if (found->is_number_unsigned()) {
				value = found->get<std::uint64_t>();
			} else {
				const auto signedValue = found->get<std::int64_t>();
				if (signedValue < 0) {
					return std::nullopt;
				}
				value = static_cast<std::uint64_t>(signedValue);
			}
			if (value == 0 || value > (std::numeric_limits<std::uint32_t>::max)()) {
				return std::nullopt;
			}
			return static_cast<std::uint32_t>(value);
		}
	}

	std::optional<nlohmann::json> NormalizeFriendRequest(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto targetPlayerId = ReadTargetPlayerId(a_packet);
		if (!targetPlayerId) {
			return std::nullopt;
		}
		return Json{ { "type", "friendRequest" }, { "targetPlayerId", *targetPlayerId } };
	}

	std::optional<nlohmann::json> NormalizeFriendRequestResponse(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto requestId = ReadIdentifier(a_packet, "requestId");
		const auto accept = a_packet.find("accept");
		if (!requestId || accept == a_packet.end() || !accept->is_boolean()) {
			return std::nullopt;
		}
		return Json{ { "type", "friendRequestResponse" }, { "requestId", *requestId }, { "accept", accept->get<bool>() } };
	}

	std::optional<nlohmann::json> NormalizeFriendRemove(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto friendId = ReadIdentifier(a_packet, "friendId");
		if (!friendId) {
			return std::nullopt;
		}
		return Json{ { "type", "friendRemove" }, { "friendId", *friendId } };
	}

	std::optional<nlohmann::json> NormalizeFriendListRequest(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		return Json{ { "type", "friendListRequest" } };
	}
}

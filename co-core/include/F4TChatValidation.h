#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::ChatValidation
{
	enum class ChatSendError : std::uint8_t
	{
		Malformed,
		InvalidText,
		TooLong,
		ChannelUnavailable
	};

	struct ChatSendResult
	{
		std::optional<ChatSendError> error;
		std::string channel;
		std::string text;
	};

	[[nodiscard]] ChatSendResult NormalizeChatSend(const nlohmann::json& a_packet);
	[[nodiscard]] std::string SanitizeChatDisplayName(std::string_view a_name);
}

#pragma once

#include "ServerProtocol.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server::PacketDecode
{
	std::optional<nlohmann::json> DecodeJson(std::span<const std::uint8_t> a_payload);
	std::optional<std::string> RawPacketType(std::span<const std::uint8_t> a_payload);
	std::optional<std::uint32_t> PlayerIdFromFrames(
		std::span<const OutboundFrame> a_frames,
		std::uint32_t a_connectionId);
	std::vector<std::uint8_t> JsonBytes(const nlohmann::json& a_packet);
	bool FramesContainType(
		std::span<const OutboundFrame> a_frames,
		std::uint32_t a_connectionId,
		std::string_view a_type);
}

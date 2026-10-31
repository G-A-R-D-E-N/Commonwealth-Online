#include "ServerPacketDecode.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CommonwealthOnline::Server::PacketDecode
{
	std::optional<nlohmann::json> DecodeJson(std::span<const std::uint8_t> a_payload)
	{
		if (a_payload.empty()) {
			return std::nullopt;
		}
		try {
			const auto text = std::string{
				reinterpret_cast<const char*>(a_payload.data()),
				a_payload.size()
			};
			auto packet = nlohmann::json::parse(text);
			return packet.is_object() ? std::optional{ std::move(packet) } : std::nullopt;
		} catch (...) {
			return std::nullopt;
		}
	}

	std::optional<std::string> RawPacketType(std::span<const std::uint8_t> a_payload)
	{
		const auto packet = DecodeJson(a_payload);
		if (!packet) {
			return std::nullopt;
		}
		const auto found = packet->find("type");
		return found != packet->end() && found->is_string()
			? std::optional{ found->get<std::string>() }
			: std::nullopt;
	}

	std::optional<std::uint32_t> PlayerIdFromFrames(
		std::span<const CommonwealthOnline::Server::OutboundFrame> a_frames,
		std::uint32_t a_connectionId)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId != a_connectionId || frame.payload.empty()) {
				continue;
			}
			const auto packet = DecodeJson(frame.payload);
			if (!packet || packet->value("type", std::string{}) != "sessionReady") {
				continue;
			}
			const auto found = packet->find("playerId");
			if (found == packet->end() || found->is_boolean()) {
				continue;
			}
			try {
				if (found->is_number_unsigned()) {
					const auto value = found->get<std::uint64_t>();
					if (value >= 1 && value <= (std::numeric_limits<std::uint32_t>::max)()) {
						return static_cast<std::uint32_t>(value);
					}
				}
				if (found->is_number_integer()) {
					const auto value = found->get<std::int64_t>();
					if (value >= 1 && static_cast<std::uint64_t>(value) <= (std::numeric_limits<std::uint32_t>::max)()) {
						return static_cast<std::uint32_t>(value);
					}
				}
			} catch (...) {
			}
		}
		return std::nullopt;
	}

	std::vector<std::uint8_t> JsonBytes(const nlohmann::json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	bool FramesContainType(
		std::span<const CommonwealthOnline::Server::OutboundFrame> a_frames,
		std::uint32_t a_connectionId,
		std::string_view a_type)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId != a_connectionId || frame.payload.empty()) {
				continue;
			}
			const auto type = RawPacketType(frame.payload);
			if (type && *type == a_type) {
				return true;
			}
		}
		return false;
	}
}

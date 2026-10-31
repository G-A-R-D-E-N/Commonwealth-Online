#pragma once

#include "ServerProtocol.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

namespace CommonwealthOnline::Server
{
	inline std::vector<std::uint8_t> JsonBytes(const nlohmann::json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	inline OutboundFrame ReliableFrame(std::uint32_t a_connectionId, const nlohmann::json& a_packet)
	{
		return { a_connectionId, Delivery::ReliableOrdered, JsonBytes(a_packet) };
	}

	inline void AppendFrames(std::vector<OutboundFrame>& a_destination, std::vector<OutboundFrame> a_source)
	{
		a_destination.insert(
			a_destination.end(),
			std::make_move_iterator(a_source.begin()),
			std::make_move_iterator(a_source.end()));
	}
}

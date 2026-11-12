#pragma once

#include "ServerProtocol.h"

namespace CommonwealthOnline::Server
{
	inline bool RefreshReadySurvivalPolicy(OutboundFrame& a_frame, const ProtocolHost& a_host)
	{
		auto packet = nlohmann::json::parse(a_frame.payload.begin(), a_frame.payload.end(), nullptr, false);
		if (!packet.is_object()) return false;
		const auto type = packet.find("type");
		if (type == packet.end() || !type->is_string() || *type != "sessionReady") return false;
		const auto info = a_host.ServerInfoPacket();
		packet["survivalMode"] = info.at("survivalMode");
		packet["survivalNeeds"] = info.at("survivalNeeds");
		const auto text = packet.dump();
		a_frame.payload.assign(text.begin(), text.end());
		return true;
	}
}

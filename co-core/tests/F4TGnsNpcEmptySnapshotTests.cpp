#include "F4TGnsProtocolAdapter.h"
#include <nlohmann/json.hpp>
#include <cassert>

int main()
{
	using Json = nlohmann::json;
	using namespace F4T::GnsProtocolAdapter;
	Session session;
	const auto welcome = session.AdaptInbound(Json{{"type", "welcome"}, {"playerId", 7}, {"protocolVersion", 2}}.dump());
	assert(!welcome.protocolViolation && welcome.controlMessages.size() == 1);
	const auto ready = session.AdaptInbound(Json{{"type", "sessionReady"}, {"playerId", 7}, {"protocolVersion", 2},
		{"serverProtocolVersion", 2}, {"worldStateHostPlayerId", 7}}.dump());
	assert(!ready.protocolViolation && session.IsReady());
	const auto assign = [&](unsigned int owner, unsigned int epoch) {
		session.AdaptInbound(Json{{"type", "npcAuthority"}, {"authorityPlayerId", owner}, {"authorityEpoch", epoch},
			{"authorityCellId", "0000003C"}, {"authorityWorldspaceId", "0000003D"}}.dump());
	};
	assign(7, 3);
	const Json empty{{"type", "npcState"}, {"npcs", Json::array()}, {"authorityEpoch", 99},
		{"authorityCellId", "0000003C"}, {"authorityWorldspaceId", "0000003D"}};
	const auto frames = session.AdaptOutbound(empty.dump());
	assert(frames.size() == 1 && frames[0].delivery == Delivery::UnreliableSequenced);
	assert(Json::parse(frames[0].payload).at("authorityEpoch") == 3);
	assign(12, 4);
	assert(session.AdaptOutbound(empty.dump()).empty());
}

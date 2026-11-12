#include "F4TGnsProtocolAdapter.h"

#include <nlohmann/json.hpp>

#include <cassert>

void TestSurvivalPolicyInbound()
{
	using Json = nlohmann::json;
	F4T::GnsProtocolAdapter::Session session;
	assert(!session.GetSurvivalMode().has_value());
	const auto welcome = session.AdaptInbound(Json{ { "type", "welcome" }, { "playerId", 7 },
		{ "protocolVersion", 2 }, { "survivalMode", true } }.dump());
	assert(!welcome.protocolViolation && session.GetSurvivalMode() == true);
	const auto ready = session.AdaptInbound(Json{ { "type", "sessionReady" }, { "playerId", 7 },
		{ "protocolVersion", 2 }, { "serverProtocolVersion", 2 } }.dump());
	assert(!ready.protocolViolation && session.IsReady());
	assert(!session.GetSurvivalMode());
	assert(!Json::parse(ready.legacyMessages.at(0)).contains("survivalMode"));
	assert(!session.AdaptInbound(Json{ { "type", "capabilities" }, { "survivalMode", false } }.dump()).protocolViolation);
	assert(session.GetSurvivalMode() == false);
	assert(session.AdaptInbound(Json{ { "type", "capabilities" }, { "survivalMode", "false" } }.dump()).protocolViolation);
	session.Reset();
	assert(!session.GetSurvivalMode().has_value());
	assert(session.AdaptInbound(Json{ { "type", "welcome" }, { "playerId", 7 },
		{ "protocolVersion", 2 }, { "survivalMode", nullptr } }.dump()).protocolViolation);
	session.Reset();
	const auto legacy = session.AdaptInbound(Json{ { "type", "welcome" }, { "playerId", 7 },
		{ "protocolVersion", 2 } }.dump());
	assert(!legacy.protocolViolation && !session.GetSurvivalMode().has_value());
	session.Reset();
	assert(!session.AdaptInbound(Json{ { "type", "welcome" }, { "playerId", 7 },
		{ "protocolVersion", 2 }, { "survivalMode", false } }.dump()).protocolViolation);
	const auto updatedReady = session.AdaptInbound(Json{ { "type", "sessionReady" }, { "playerId", 7 },
		{ "protocolVersion", 2 }, { "serverProtocolVersion", 2 }, { "survivalMode", true } }.dump());
	assert(!updatedReady.protocolViolation && session.GetSurvivalMode() == true);
	assert(Json::parse(updatedReady.legacyMessages.at(0)).at("survivalMode") == true);
	session.Reset();
	assert(!session.AdaptInbound(Json{ { "type", "welcome" }, { "playerId", 7 }, { "protocolVersion", 2 } }.dump()).protocolViolation);
	assert(session.AdaptInbound(Json{ { "type", "sessionReady" }, { "playerId", 7 }, { "protocolVersion", 2 },
		{ "serverProtocolVersion", 2 }, { "survivalMode", "yes" } }.dump()).protocolViolation);
	session.Reset();
	assert(!session.AdaptInbound(Json{ { "type", "welcome" }, { "playerId", 7 },
		{ "protocolVersion", 2 }, { "survivalNeeds", true } }.dump()).protocolViolation);
	const auto needsReady = session.AdaptInbound(Json{ { "type", "sessionReady" }, { "playerId", 7 },
		{ "protocolVersion", 2 }, { "serverProtocolVersion", 2 }, { "survivalNeeds", false } }.dump());
	assert(!needsReady.protocolViolation && Json::parse(needsReady.legacyMessages.at(0)).at("survivalNeeds") == false);
	session.Reset();
	assert(!session.AdaptInbound(Json{ { "type", "welcome" }, { "playerId", 7 },
		{ "protocolVersion", 2 }, { "survivalNeeds", true } }.dump()).protocolViolation);
	const auto dropped = session.AdaptInbound(Json{ { "type", "sessionReady" }, { "playerId", 7 },
		{ "protocolVersion", 2 }, { "serverProtocolVersion", 2 } }.dump());
	assert(!dropped.protocolViolation && !Json::parse(dropped.legacyMessages.at(0)).contains("survivalNeeds"));
	session.Reset();
	assert(!session.AdaptInbound(Json{ { "type", "welcome" }, { "playerId", 7 }, { "protocolVersion", 2 }, { "serverTime", 1000.0 } }.dump()).protocolViolation);
	const auto timed = session.AdaptInbound(Json{ { "type", "sessionReady" }, { "playerId", 7 },
		{ "protocolVersion", 2 }, { "serverProtocolVersion", 2 }, { "serverTime", 1042.5 } }.dump());
	assert(!timed.protocolViolation && Json::parse(timed.legacyMessages.at(0)).at("serverTime") == 1042.5);
	session.Reset();
	assert(!session.AdaptInbound(Json{ { "type", "welcome" }, { "playerId", 7 }, { "protocolVersion", 2 }, { "serverTime", 1000.0 } }.dump()).protocolViolation);
	const auto untimed = session.AdaptInbound(Json{ { "type", "sessionReady" }, { "playerId", 7 },
		{ "protocolVersion", 2 }, { "serverProtocolVersion", 2 } }.dump());
	assert(!untimed.protocolViolation && Json::parse(untimed.legacyMessages.at(0)).at("serverTime") == 1000.0);
	session.Reset();
	assert(!session.AdaptInbound(Json{ { "type", "welcome" }, { "playerId", 7 }, { "protocolVersion", 2 }, { "serverTime", 1000.0 } }.dump()).protocolViolation);
	const auto badTime = session.AdaptInbound(Json{ { "type", "sessionReady" }, { "playerId", 7 },
		{ "protocolVersion", 2 }, { "serverProtocolVersion", 2 }, { "serverTime", "soon" } }.dump());
	assert(!badTime.protocolViolation && Json::parse(badTime.legacyMessages.at(0)).at("serverTime") == 1000.0);
}

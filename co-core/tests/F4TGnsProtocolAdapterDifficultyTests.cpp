#include "F4TDifficultyReport.h"
#include "F4TGnsProtocolAdapter.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <string>

namespace
{
	using Json = nlohmann::json;
	namespace DR = F4T::DifficultyReport;
	using Session = F4T::GnsProtocolAdapter::Session;

	const std::string kWelcome = Json{ { "type", "welcome" }, { "playerId", 7 }, { "protocolVersion", 2 } }.dump();

	std::string SessionReady(const Json& a_extra)
	{
		Json packet{ { "type", "sessionReady" }, { "playerId", 7 }, { "protocolVersion", 2 }, { "serverProtocolVersion", 2 } };
		for (const auto& [key, value] : a_extra.items()) {
			packet[key] = value;
		}
		return packet.dump();
	}

	void ResetGlobals()
	{
		DR::SetLocalDifficulty(-1);
		DR::SetServerAcceptsReports(false);
	}
}

int main()
{
	ResetGlobals();

	{
		Session session;
		const auto welcome = session.AdaptInbound(kWelcome);
		assert(!welcome.protocolViolation && welcome.controlMessages.size() == 1);
		assert(!Json::parse(welcome.controlMessages.at(0).payload).contains("difficulty"));
	}

	DR::SetLocalDifficulty(2);
	{
		Session session;
		const auto welcome = session.AdaptInbound(kWelcome);
		assert(!welcome.protocolViolation && welcome.controlMessages.size() == 1);
		const auto hello = Json::parse(welcome.controlMessages.at(0).payload);
		assert(hello.at("difficulty") == 2);
		assert(hello.at("type") == "hello");
	}

	DR::SetLocalDifficulty(7);
	{
		Session session;
		const auto welcome = session.AdaptInbound(kWelcome);
		assert(!welcome.protocolViolation && welcome.controlMessages.size() == 1);
		assert(!Json::parse(welcome.controlMessages.at(0).payload).contains("difficulty"));
	}
	DR::SetLocalDifficulty(2);

	DR::SetServerAcceptsReports(true);
	{
		Session session;
		assert(!session.AdaptInbound(kWelcome).protocolViolation);
		assert(!DR::ServerAcceptsReports());
		const auto ready = session.AdaptInbound(SessionReady(Json{ { "difficultyReportSupported", true } }));
		assert(!ready.protocolViolation && session.IsReady());
		assert(DR::ServerAcceptsReports());
	}

	DR::SetServerAcceptsReports(true);
	{
		Session session;
		assert(!session.AdaptInbound(kWelcome).protocolViolation);
		const auto ready = session.AdaptInbound(SessionReady(Json::object()));
		assert(!ready.protocolViolation && session.IsReady());
		assert(!DR::ServerAcceptsReports());
	}

	DR::SetServerAcceptsReports(true);
	{
		Session session;
		assert(!session.AdaptInbound(kWelcome).protocolViolation);
		const auto ready = session.AdaptInbound(SessionReady(Json{ { "difficultyReportSupported", "yes" } }));
		assert(!ready.protocolViolation && session.IsReady());
		assert(!DR::ServerAcceptsReports());
	}

	DR::SetServerAcceptsReports(true);
	{
		Session session;
		session.Reset();
		assert(!DR::ServerAcceptsReports());
	}

	ResetGlobals();
	return 0;
}

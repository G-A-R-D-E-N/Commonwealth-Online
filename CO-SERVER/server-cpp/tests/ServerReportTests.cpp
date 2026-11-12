#include "ServerFactionTestSupport.h"
#include "ServerReports.h"
#include "ServerSessionTypes.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace CommonwealthOnline::Server
{
	struct ReportServiceTestAccess
	{
		static bool Submit(ReportService& a_service, const std::string& a_key, double a_time) { return a_service.SubmitRateAllows(a_key, a_time); }
		static bool Contains(const ReportService& a_service, const std::string& a_key) { return a_service.submitWindows.contains(a_key); }
	};
}

using namespace FactionTestSupport;

namespace
{
	std::uint32_t Join(ProtocolHost& a_host, std::uint32_t a_connectionId, std::string_view a_profileId, bool a_admin, double a_time)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_time);
		const auto playerId = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		a_host.BindSessionProfile(a_connectionId, a_profileId);
		a_host.SetSessionAdminVerified(a_connectionId, a_admin);
		const auto ready = Send(a_host, a_connectionId, Json{ { "type", "hello" }, { "protocolVersion", 2 } }, a_time + 0.1, a_time + 0.2);
		assert(HasPacket(ready, a_connectionId, "sessionReady"));
		return playerId;
	}

	std::vector<OutboundFrame> SendReport(ProtocolHost& a_host, std::uint32_t a_connectionId, const Json& a_packet, double a_monotonicSeconds)
	{
		return Send(a_host, a_connectionId, a_packet, a_monotonicSeconds, a_monotonicSeconds + 99.0);
	}

	void AssertRefused(ProtocolHost& a_host, std::uint32_t a_connectionId, const Json& a_packet, double a_monotonicSeconds, std::string_view a_reason)
	{
		const auto result = RequirePacket(SendReport(a_host, a_connectionId, a_packet, a_monotonicSeconds), a_connectionId, "reportResult");
		assert(result.at("ok") == false);
		assert(result.at("error") == std::string{ a_reason });
	}

	Json PlayerReport(std::uint32_t a_targetPlayerId, std::string_view a_note)
	{
		return Json{ { "type", "reportSubmit" }, { "kind", "player" }, { "targetPlayerId", a_targetPlayerId }, { "reason", "cheating" }, { "note", std::string{ a_note } } };
	}

	void TestSelfTargetRefused()
	{
		ProtocolHost host{ "Reports", "", { "co-admin" } };
		const auto self = Join(host, 10, "pa", false, 1.0);
		AssertRefused(host, 10, PlayerReport(self, "hi"), 2.0, "selfTarget");
	}

	void TestUnknownTargetRefused()
	{
		ProtocolHost host{ "Reports", "", { "co-admin" } };
		Join(host, 10, "pa", false, 1.0);
		AssertRefused(host, 10, PlayerReport(999, "hi"), 2.0, "targetUnavailable");
	}

	void TestUnsafeTextRefused()
	{
		ProtocolHost host{ "Reports", "", { "co-admin" } };
		Join(host, 10, "pa", false, 1.0);
		const auto target = Join(host, 20, "pb", false, 1.1);
		AssertRefused(host, 10, PlayerReport(target, "bad\xE2\x80\x8B text"), 2.0, "unsafeText");
	}

	void TestTooLongNoteRefused()
	{
		ProtocolHost host{ "Reports", "", { "co-admin" } };
		Join(host, 10, "pa", false, 1.0);
		const auto target = Join(host, 20, "pb", false, 1.1);
		AssertRefused(host, 10, PlayerReport(target, std::string(201, 'a')), 2.0, "noteTooLong");
	}

	void TestSupportNeedsNote()
	{
		ProtocolHost host{ "Reports", "", { "co-admin" } };
		Join(host, 10, "pa", false, 1.0);
		AssertRefused(host, 10, Json{ { "type", "reportSubmit" }, { "kind", "support" }, { "note", "" } }, 2.0, "noteRequired");
	}

	void TestRateLimitThirdAllowedFourthRefused()
	{
		ProtocolHost host{ "Reports", "", { "co-admin" } };
		Join(host, 10, "pa", false, 1.0);
		const auto target = Join(host, 20, "pb", false, 1.1);
		const auto packet = PlayerReport(target, "");
		for (const double at : { 2.0, 3.0, 4.0 }) {
			const auto result = RequirePacket(SendReport(host, 10, packet, at), 10, "reportResult");
			assert(result.at("ok") == true);
		}
		AssertRefused(host, 10, packet, 5.0, "rateLimited");
	}

	void TestRateLimitExpiryKeepsActiveProfiles()
	{
		CommonwealthOnline::Server::ProtocolStats stats{};
		CommonwealthOnline::Server::ReportService service{ stats };
		using Access = CommonwealthOnline::Server::ReportServiceTestAccess;
		assert(Access::Submit(service, "expired", 1.0));
		for (int i = 0; i < 3; ++i) assert(Access::Submit(service, "active", 30.0));
		assert(Access::Submit(service, "probe", 61.0));
		assert(Access::Contains(service, "expired"));
		assert(Access::Submit(service, "probe", 61.01));
		assert(!Access::Contains(service, "expired"));
		assert(Access::Contains(service, "active"));
		assert(!Access::Submit(service, "active", 90.0));
		assert(Access::Submit(service, "active", 90.01));
	}

	void TestAdminNotifiedReporterOk()
	{
		ProtocolHost host{ "Reports", "", { "co-admin" } };
		const auto reporter = Join(host, 10, "pa", false, 1.0);
		const auto target = Join(host, 20, "pb", false, 1.1);
		Join(host, 30, "co-admin", true, 1.2);
		const auto frames = SendReport(host, 10, PlayerReport(target, "speed hack"), 2.0);
		const auto result = RequirePacket(frames, 10, "reportResult");
		assert(result.at("ok") == true);
		assert(result.at("reportId").is_string());
		const auto received = RequirePacket(frames, 30, "reportReceived");
		assert(received.at("reporterPlayerId") == reporter);
		assert(received.at("targetPlayerId") == target);
		assert(received.at("reason") == "cheating");
		assert(received.at("note") == "speed hack");
		for (const auto& frame : frames) {
			assert(frame.connectionId != 20);
		}
	}

	using Sessions = CommonwealthOnline::Server::ReportService::Sessions;

	void AddSession(Sessions& a_sessions, std::uint32_t a_playerId, std::string_view a_profileId)
	{
		auto& session = a_sessions[a_playerId];
		session.playerId = a_playerId;
		session.profileId = std::string{ a_profileId };
		session.active = true;
	}

	Json ServiceReport(CommonwealthOnline::Server::ReportService& a_service, Sessions& a_sessions, std::uint32_t a_reporterId, std::uint32_t a_targetId)
	{
		const auto frames = a_service.Submit(
			a_reporterId,
			a_sessions.at(a_reporterId),
			a_sessions,
			PlayerReport(a_targetId, "speed hack"),
			2.0 + a_reporterId,
			100.0 + a_reporterId,
			[](const CommonwealthOnline::Server::Session& a_session) { return a_session.profileId == "co-admin"; });
		return RequirePacket(frames, a_reporterId, "reportResult");
	}

	void TestInternalRecordsDoNotEvictPlayerReports()
	{
		CommonwealthOnline::Server::ProtocolStats stats{};
		CommonwealthOnline::Server::ReportService service{ stats };
		Sessions sessions;
		AddSession(sessions, 10, "pa");
		AddSession(sessions, 20, "pb");
		AddSession(sessions, 30, "co-admin");
		const auto result = ServiceReport(service, sessions, 10, 20);
		assert(result.at("ok") == true);
		const auto reportId = result.at("reportId").get<std::string>();
		for (int i = 0; i < 1000; ++i) {
			service.Record(i % 2 == 0 ? "npcLoot" : "survivalNeeds", "internal refusal " + std::to_string(i), 200.0 + i);
		}
		const auto listed = service.ReportsJson();
		assert(listed.at("reports").size() == 1);
		assert(listed.at("reports").at(0).at("reportId") == reportId);
		assert(listed.at("reports").at(0).at("type") == "reportReceived");
		assert(listed.at("total") == 1);
	}

	void TestListingIsBoundedToLatestReports()
	{
		CommonwealthOnline::Server::ProtocolStats stats{};
		CommonwealthOnline::Server::ReportService service{ stats };
		Sessions sessions;
		AddSession(sessions, 20, "pb");
		AddSession(sessions, 30, "co-admin");
		std::string lastId;
		for (std::uint32_t reporter = 1000; reporter < 1040; ++reporter) {
			AddSession(sessions, reporter, "pr" + std::to_string(reporter));
			const auto result = ServiceReport(service, sessions, reporter, 20);
			assert(result.at("ok") == true);
			lastId = result.at("reportId").get<std::string>();
		}
		const auto listed = service.ReportsJson();
		assert(listed.at("total") == 40);
		assert(listed.at("reports").size() == 30);
		assert(listed.at("reports").back().at("reportId") == lastId);
	}
}

int main()
{
	TestSelfTargetRefused();
	TestUnknownTargetRefused();
	TestUnsafeTextRefused();
	TestTooLongNoteRefused();
	TestSupportNeedsNote();
	TestRateLimitThirdAllowedFourthRefused();
	TestRateLimitExpiryKeepsActiveProfiles();
	TestAdminNotifiedReporterOk();
	TestInternalRecordsDoNotEvictPlayerReports();
	TestListingIsBoundedToLatestReports();
	return 0;
}

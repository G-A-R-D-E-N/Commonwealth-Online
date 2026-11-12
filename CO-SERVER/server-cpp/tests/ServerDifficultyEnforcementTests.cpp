#include "F4TGnsSnapshotEnvelope.h"
#include "ServerProtocol.h"
#include "ServerViolation.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace CommonwealthOnline::Server;

namespace
{
	using Json = nlohmann::json;

	constexpr std::uint32_t kServerSenderPlayerId{ 0xFFFFFFFFU };

	std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	Json DecodePacket(const OutboundFrame& a_frame)
	{
		F4T::GnsSnapshotEnvelope::Header header{};
		std::span<const std::uint8_t> payload{ a_frame.payload };
		if (F4T::GnsSnapshotEnvelope::Decode(a_frame.payload, header) == F4T::GnsSnapshotEnvelope::DecodeResult::Valid) {
			payload = payload.subspan(F4T::GnsSnapshotEnvelope::HeaderSize);
		}
		return Json::parse(std::string{ reinterpret_cast<const char*>(payload.data()), payload.size() });
	}

	std::vector<Json> Frames(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_type)
	{
		std::vector<Json> matches;
		for (const auto& frame : a_frames) {
			if (frame.connectionId != a_connectionId || frame.payload.empty()) continue;
			auto packet = DecodePacket(frame);
			if (packet.value("type", std::string{}) == a_type) matches.push_back(std::move(packet));
		}
		return matches;
	}

	Json Hello()
	{
		return Json{ { "type", "hello" }, { "protocolVersion", 2 } };
	}

	Json HelloWithDifficulty(const Json& a_difficulty)
	{
		auto packet = Hello();
		packet["difficulty"] = a_difficulty;
		return packet;
	}

	Json Report(const Json& a_difficulty)
	{
		return Json{ { "type", "difficultyReport" }, { "difficulty", a_difficulty } };
	}

	std::vector<OutboundFrame> ConnectAt(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_monotonicSeconds, double a_unixTimeSeconds, const Json& a_hello)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		assert(welcome.size() == 1);
		return a_host.HandleMessage(a_connectionId, EncodeJson(a_hello), a_monotonicSeconds, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> Connect(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_seconds, const Json& a_hello)
	{
		return ConnectAt(a_host, a_connectionId, a_seconds, a_seconds, a_hello);
	}

	std::vector<OutboundFrame> Send(ProtocolHost& a_host, std::uint32_t a_connectionId, const Json& a_packet, double a_seconds)
	{
		return a_host.HandleMessage(a_connectionId, EncodeJson(a_packet), a_seconds, a_seconds);
	}

	std::vector<OutboundFrame> Poll(ProtocolHost& a_host, double a_seconds)
	{
		return a_host.PollDifficulty(a_seconds, a_seconds);
	}

	constexpr std::string_view kMismatchWarningText{ "Set your difficulty to Very Hard within 30 seconds or you will be disconnected." };
	constexpr std::string_view kUnreportedWarningText{ "Your client did not report its difficulty; update Commonwealth Online." };

	std::size_t CountViolations(const std::vector<PendingViolation>& a_violations, std::uint32_t a_connectionId, Violation a_kind = Violation::DifficultyMismatch)
	{
		std::size_t count = 0;
		for (const auto& violation : a_violations) {
			if (violation.connectionId == a_connectionId && violation.violation == a_kind) ++count;
		}
		return count;
	}

	void ConnectAdmin(ProtocolHost& a_host, std::uint32_t a_connectionId, std::string_view a_profileId, double a_seconds, const Json& a_hello)
	{
		(void)a_host.HandleConnected(a_connectionId, a_seconds);
		a_host.BindSessionProfile(a_connectionId, a_profileId);
		a_host.SetSessionAdminVerified(a_connectionId, true);
		(void)a_host.HandleMessage(a_connectionId, EncodeJson(a_hello), a_seconds, a_seconds);
	}

	void ReadyReportsSupportAndAcceptsVeryHard()
	{
		ProtocolHost host{ "Difficulty Ready" };
		const auto ready = Connect(host, 10, 100.0, HelloWithDifficulty(4));
		const auto sessionReady = Frames(ready, 10, "sessionReady");
		assert(sessionReady.size() == 1);
		assert(sessionReady.front().at("difficultyReportSupported") == true);

		assert(Frames(Send(host, 10, Report(4), 200.0), 10, "chatMessage").empty());
		assert(Poll(host, 400.0).empty());
		assert(host.TakeViolations().empty());
	}

	void InvalidDifficultyIsRejected()
	{
		ProtocolHost host{ "Difficulty Invalid" };
		const auto rejected = Connect(host, 11, 100.0, HelloWithDifficulty("hard"));
		assert(Frames(rejected, 11, "sessionReady").empty());

		ProtocolHost reportHost{ "Difficulty Invalid Report" };
		Connect(reportHost, 12, 100.0, Hello());
		assert(Frames(Send(reportHost, 12, Report(9), 170.0), 12, "chatMessage").empty());
		assert(reportHost.TakeViolations().empty());
	}

	void WrongDifficultyWarnsThenKicksAfterThirtySeconds()
	{
		ProtocolHost host{ "Difficulty Mismatch" };
		Connect(host, 20, 100.0, Hello());

		const auto first = Send(host, 20, Report(2), 170.0);
		const auto warnings = Frames(first, 20, "chatMessage");
		assert(warnings.size() == 1);
		assert(warnings.front().at("fromPlayerId") == kServerSenderPlayerId);
		assert(warnings.front().at("fromName") == "Server");
		assert(warnings.front().at("channel") == "global");
		assert(warnings.front().at("text").get<std::string>() == kMismatchWarningText);
		assert(CountViolations(host.TakeViolations(), 20) == 0);

		assert(Frames(Send(host, 20, Report(2), 171.0), 20, "chatMessage").empty());
		assert(CountViolations(host.TakeViolations(), 20) == 0);

		assert(Poll(host, 199.9).empty());
		assert(CountViolations(host.TakeViolations(), 20) == 0);

		assert(Poll(host, 210.0).empty());
		assert(CountViolations(host.TakeViolations(), 20) == 1);
	}

	void CustomRequiredDifficultyIsEnforced()
	{
		ProtocolHost host{ "Difficulty Custom" };
		host.SetRequiredDifficulty(2);
		Connect(host, 65, 100.0, Hello());

		assert(Frames(Send(host, 65, Report(2), 150.0), 65, "chatMessage").empty());
		assert(Poll(host, 199.9).empty());
		assert(CountViolations(host.TakeViolations(), 65) == 0);

		const auto warnings = Frames(Send(host, 65, Report(4), 200.0), 65, "chatMessage");
		assert(warnings.size() == 1);
		assert(warnings.front().at("text").get<std::string>().find("Normal") != std::string::npos);
		assert(Poll(host, 210.0).empty());
		assert(CountViolations(host.TakeViolations(), 65) == 0);

		assert(Poll(host, 230.0).empty());
		assert(CountViolations(host.TakeViolations(), 65) == 1);
	}

	void ReportWarningIsNotKickedByImmediateSweep()
	{
		ProtocolHost host{ "Difficulty Warn Then Sweep" };
		Connect(host, 25, 100.0, Hello());

		assert(Frames(Send(host, 25, Report(2), 170.0), 25, "chatMessage").size() == 1);
		assert(Poll(host, 170.1).empty());
		assert(CountViolations(host.TakeViolations(), 25) == 0);
	}

	void RegressionAfterFixIsWarnedAgainBeforeKick()
	{
		ProtocolHost host{ "Difficulty Regression" };
		Connect(host, 90, 100.0, Hello());

		assert(Frames(Send(host, 90, Report(2), 170.0), 90, "chatMessage").size() == 1);
		assert(Frames(Send(host, 90, Report(4), 171.0), 90, "chatMessage").empty());
		assert(Frames(Send(host, 90, Report(2), 172.0), 90, "chatMessage").size() == 1);
		assert(Poll(host, 201.0).empty());
		assert(CountViolations(host.TakeViolations(), 90) == 0);
		assert(Poll(host, 211.0).empty());
		assert(CountViolations(host.TakeViolations(), 90) == 1);
	}

	void GracePeriodDefersEnforcement()
	{
		ProtocolHost host{ "Difficulty Grace" };
		Connect(host, 30, 100.0, Hello());

		assert(Frames(Send(host, 30, Report(2), 150.0), 30, "chatMessage").empty());
		assert(Poll(host, 159.0).empty());
		assert(host.TakeViolations().empty());

		const auto afterGrace = Poll(host, 170.0);
		assert(Frames(afterGrace, 30, "chatMessage").size() == 1);
		assert(host.TakeViolations().empty());
	}

	void SilentClientWarnsThenKicksAfterThirtySeconds()
	{
		ProtocolHost host{ "Difficulty Silent" };
		Connect(host, 40, 100.0, Hello());

		assert(Poll(host, 150.0).empty());
		assert(Poll(host, 340.0).empty());
		const auto warnings = Frames(Poll(host, 350.0), 40, "chatMessage");
		assert(warnings.size() == 1);
		assert(warnings.front().at("text").get<std::string>() == kUnreportedWarningText);
		assert(Poll(host, 360.0).empty());
		assert(Frames(Poll(host, 370.0), 40, "chatMessage").empty());
		assert(CountViolations(host.TakeViolations(), 40, Violation::DifficultyUnreported) == 0);
		assert(Poll(host, 380.0).empty());
		assert(CountViolations(host.TakeViolations(), 40, Violation::DifficultyUnreported) == 1);
		assert(Poll(host, 390.0).empty());
		assert(CountViolations(host.TakeViolations(), 40, Violation::DifficultyUnreported) == 0);
	}

	void ReportedClientThatStopsReportingIsSilent()
	{
		ProtocolHost host{ "Difficulty Stopped Reporting" };
		Connect(host, 110, 10.0, HelloWithDifficulty(4));

		assert(Poll(host, 100.0).empty());
		assert(host.TakeViolations().empty());

		const auto warnings = Frames(Poll(host, 251.0), 110, "chatMessage");
		assert(warnings.size() == 1);
		assert(warnings.front().at("text").get<std::string>() == kUnreportedWarningText);
		assert(CountViolations(host.TakeViolations(), 110, Violation::DifficultyUnreported) == 0);

		assert(Poll(host, 281.0).empty());
		assert(CountViolations(host.TakeViolations(), 110, Violation::DifficultyUnreported) == 1);
	}

	void FreshReportKeepsReportedClientOk()
	{
		ProtocolHost host{ "Difficulty Fresh Report" };
		Connect(host, 111, 10.0, HelloWithDifficulty(4));

		assert(Frames(Send(host, 111, Report(4), 250.0), 111, "chatMessage").empty());
		assert(Poll(host, 251.0).empty());
		assert(host.TakeViolations().empty());
	}

	void AdminIsExemptFromDifficultyCheck()
	{
		ProtocolHost host{ "Difficulty Admin", "", { "admin-profile-01" } };
		ConnectAdmin(host, 95, "admin-profile-01", 100.0, Hello());
		Connect(host, 96, 100.0, Hello());

		assert(Frames(Send(host, 95, Report(2), 170.0), 95, "chatMessage").empty());
		assert(Frames(Send(host, 96, Report(2), 170.0), 96, "chatMessage").size() == 1);
		assert(Poll(host, 199.9).empty());
		assert(Poll(host, 210.0).empty());
		const auto kicked = host.TakeViolations();
		assert(CountViolations(kicked, 95) == 0);
		assert(CountViolations(kicked, 96) == 1);

		host.SetAdminProfiles({});
		assert(Poll(host, 500.0).empty());
		assert(Poll(host, 510.0).empty());
		assert(CountViolations(host.TakeViolations(), 95) == 0);
		assert(Frames(Poll(host, 561.0), 95, "chatMessage").size() == 1);
		assert(Poll(host, 580.0).empty());
		assert(CountViolations(host.TakeViolations(), 95) == 0);
		assert(Poll(host, 592.0).empty());
		assert(CountViolations(host.TakeViolations(), 95) == 1);

		ProtocolHost silentHost{ "Difficulty Silent Admin", "", { "admin-profile-02" } };
		ConnectAdmin(silentHost, 97, "admin-profile-02", 100.0, Hello());
		assert(Poll(silentHost, 340.0).empty());
		assert(Frames(Poll(silentHost, 350.0), 97, "chatMessage").empty());
		assert(Poll(silentHost, 380.0).empty());
		assert(CountViolations(silentHost.TakeViolations(), 97, Violation::DifficultyUnreported) == 0);
	}

	void ValuesFiveAndSixAreMismatch()
	{
		for (const std::int64_t value : { std::int64_t{ 5 }, std::int64_t{ 6 } }) {
			ProtocolHost host{ "Difficulty Survival Values" };
			Connect(host, 70, 100.0, Hello());
			assert(Frames(Send(host, 70, Report(value), 170.0), 70, "chatMessage").size() == 1);
			assert(Poll(host, 199.0).empty());
			assert(Poll(host, 210.0).empty());
			assert(CountViolations(host.TakeViolations(), 70) == 1);
		}
	}

	void PolicyFlipResetsTracking()
	{
		ProtocolHost silentHost{ "Difficulty Reset Silent" };
		Connect(silentHost, 80, 100.0, Hello());
		silentHost.SetSurvivalMode(false);
		silentHost.SetSurvivalMode(true);
		assert(Poll(silentHost, 400.0).empty());
		assert(CountViolations(silentHost.TakeViolations(), 80) == 0);
		assert(Frames(Poll(silentHost, 641.0), 80, "chatMessage").size() == 1);

		ProtocolHost warnedHost{ "Difficulty Reset Warned" };
		Connect(warnedHost, 81, 100.0, Hello());
		assert(Frames(Send(warnedHost, 81, Report(2), 170.0), 81, "chatMessage").size() == 1);
		warnedHost.SetRequireDifficultyReport(false);
		warnedHost.SetRequireDifficultyReport(true);
		assert(Frames(Send(warnedHost, 81, Report(2), 200.0), 81, "chatMessage").empty());
		assert(CountViolations(warnedHost.TakeViolations(), 81) == 0);
	}

	void DisabledWhenSurvivalModeOff()
	{
		ProtocolHost host{ "Difficulty Survival Off" };
		host.SetSurvivalMode(false);
		Connect(host, 50, 100.0, Hello());

		assert(Poll(host, 300.0).empty());
		assert(Frames(Send(host, 50, Report(2), 400.0), 50, "chatMessage").empty());
		assert(host.TakeViolations().empty());
	}

	void DisabledWhenRequireReportOff()
	{
		ProtocolHost host{ "Difficulty Require Off" };
		host.SetRequireDifficultyReport(false);
		Connect(host, 60, 100.0, Hello());

		assert(Poll(host, 300.0).empty());
		assert(Frames(Send(host, 60, Report(2), 400.0), 60, "chatMessage").empty());
		assert(Poll(host, 500.0).empty());
		assert(host.TakeViolations().empty());
	}

	void WallClockJumpDoesNotWarnOrKick()
	{
		ProtocolHost host{ "Difficulty Wall Clock Jump" };
		ConnectAt(host, 100, 1.0, 100.0, Hello());
		ConnectAt(host, 101, 1.0, 100.0, Hello());

		assert(Frames(host.HandleMessage(100, EncodeJson(Report(2)), 2.0, 1.0e9), 100, "chatMessage").empty());
		assert(Frames(host.PollDifficulty(12.0, 1.0e9 + 5.0), 100, "chatMessage").empty());
		assert(Frames(host.PollDifficulty(25.0, 2.0e9), 101, "chatMessage").empty());
		assert(host.TakeViolations().empty());
	}
}

int main()
{
	ReadyReportsSupportAndAcceptsVeryHard();
	InvalidDifficultyIsRejected();
	WrongDifficultyWarnsThenKicksAfterThirtySeconds();
	ReportWarningIsNotKickedByImmediateSweep();
	RegressionAfterFixIsWarnedAgainBeforeKick();
	GracePeriodDefersEnforcement();
	SilentClientWarnsThenKicksAfterThirtySeconds();
	ReportedClientThatStopsReportingIsSilent();
	FreshReportKeepsReportedClientOk();
	AdminIsExemptFromDifficultyCheck();
	ValuesFiveAndSixAreMismatch();
	PolicyFlipResetsTracking();
	DisabledWhenSurvivalModeOff();
	DisabledWhenRequireReportOff();
	WallClockJumpDoesNotWarnOrKick();
	CustomRequiredDifficultyIsEnforced();
	return 0;
}

#include "ServerReports.h"

#include "ClaimedText.h"
#include "F4TChatValidation.h"
#include "ServerLogSanitizer.h"
#include "ServerPacketRead.h"
#include "ServerSessionTypes.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <string>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using Json = nlohmann::json;
		using Sessions = ReportService::Sessions;

		constexpr std::size_t kMaximumNoteBytes = 200;
		constexpr std::size_t kMaximumReportsPerWindow = 3;
		constexpr double kReportWindowSeconds = 60.0;
		constexpr std::size_t kMaximumStoredReports = 200;
		constexpr std::size_t kMaximumListedReports = 30;

		Sessions::iterator ActiveSessionForPlayer(Sessions& a_sessions, std::uint32_t a_playerId)
		{
			return std::find_if(a_sessions.begin(), a_sessions.end(), [a_playerId](const auto& a_entry) {
				return a_entry.second.active && a_entry.second.playerId == a_playerId;
			});
		}
	}

	std::vector<OutboundFrame> ReportService::Failure(std::uint32_t a_connectionId, std::string_view a_reason, double a_unixTimeSeconds)
	{
		stats.packetsRejected++;
		return { ReliableFrame(a_connectionId, Json{ { "type", "reportResult" }, { "ok", false }, { "error", std::string{ a_reason } }, { "serverTime", a_unixTimeSeconds } }) };
	}

	bool ReportService::SubmitRateAllows(const std::string& a_key, double a_monotonicSeconds)
	{
		std::erase_if(submitWindows, [a_monotonicSeconds](const auto& a_entry) {
			return a_monotonicSeconds - a_entry.second.back() > kReportWindowSeconds;
		});
		auto& window = submitWindows[a_key];
		std::erase_if(window, [a_monotonicSeconds](double a_sent) { return a_monotonicSeconds - a_sent > kReportWindowSeconds; });
		if (window.size() >= kMaximumReportsPerWindow) return false;
		window.push_back(a_monotonicSeconds);
		return true;
	}

	std::vector<OutboundFrame> ReportService::Submit(
		std::uint32_t a_connectionId,
		Session& a_self,
		Sessions& a_sessions,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds,
		const AdminCheck& a_isLiveAdmin)
	{
		const auto kind = ReadString(a_packet, "kind").value_or(std::string{});
		const bool playerKind = kind == "player";
		if (!playerKind && kind != "support") return Failure(a_connectionId, "invalidKind", a_unixTimeSeconds);

		auto target = a_sessions.end();
		std::string reason;
		if (playerKind) {
			const auto targetPlayerId = ReadUInt32(a_packet, "targetPlayerId").value_or(0);
			if (targetPlayerId == 0) return Failure(a_connectionId, "invalidTarget", a_unixTimeSeconds);
			if (targetPlayerId == a_self.playerId) return Failure(a_connectionId, "selfTarget", a_unixTimeSeconds);
			reason = ReadString(a_packet, "reason").value_or(std::string{});
			const bool validReason = reason == "harassment" || reason == "cheating" || reason == "griefing" || reason == "other";
			if (!validReason) return Failure(a_connectionId, "invalidReason", a_unixTimeSeconds);
			target = ActiveSessionForPlayer(a_sessions, targetPlayerId);
			if (target == a_sessions.end()) return Failure(a_connectionId, "targetUnavailable", a_unixTimeSeconds);
		}

		const auto raw = ReadString(a_packet, "note").value_or(std::string{});
		std::string_view trimmed{ raw };
		trimmed.remove_prefix(std::min(trimmed.find_first_not_of(" \t\r\n"), trimmed.size()));
		trimmed = trimmed.substr(0, trimmed.find_last_not_of(" \t\r\n") + 1);
		if (trimmed.empty() && !playerKind) return Failure(a_connectionId, "noteRequired", a_unixTimeSeconds);

		std::string note;
		if (!trimmed.empty()) {
			if (trimmed.size() > kMaximumNoteBytes) return Failure(a_connectionId, "noteTooLong", a_unixTimeSeconds);
			if (!IsSafeClaimedText(trimmed)) return Failure(a_connectionId, "unsafeText", a_unixTimeSeconds);
			const auto normalized = F4T::ChatValidation::NormalizeChatSend(Json{ { "channel", "global" }, { "text", std::string{ trimmed } } });
			if (normalized.error) {
				return Failure(a_connectionId, normalized.error == F4T::ChatValidation::ChatSendError::TooLong ? "noteTooLong" : "unsafeText", a_unixTimeSeconds);
			}
			note = normalized.text;
		}

		const auto rateKey = a_self.profileId.empty() ? std::to_string(a_self.playerId) : a_self.profileId;
		if (!SubmitRateAllows(rateKey, a_monotonicSeconds)) return Failure(a_connectionId, "rateLimited", a_unixTimeSeconds);

		const auto reportId = "rp-" + std::to_string(nextReportNumber++);
		const auto reporterName = F4T::ChatValidation::SanitizeChatDisplayName(SessionNameView(a_self));
		Json report{
			{ "type", "reportReceived" },
			{ "reportId", reportId },
			{ "kind", kind },
			{ "reporterPlayerId", a_self.playerId },
			{ "reporterName", reporterName },
			{ "note", note },
			{ "serverTime", a_unixTimeSeconds }
		};
		std::string targetName;
		if (playerKind) {
			targetName = F4T::ChatValidation::SanitizeChatDisplayName(SessionNameView(target->second));
			report["targetPlayerId"] = target->second.playerId;
			report["targetName"] = targetName;
			report["reason"] = reason;
		}

		std::cout << "[report] id=" << reportId << " kind=" << kind
		          << " reporter=" << a_self.playerId << " (" << RuntimeSupport::SanitizeLogField(reporterName) << ")";
		if (playerKind) {
			std::cout << " target=" << target->second.playerId << " (" << RuntimeSupport::SanitizeLogField(targetName) << ") reason=" << reason;
		}
		std::cout << " note=\"" << RuntimeSupport::SanitizeLogField(note) << "\"" << std::endl;

		std::vector<OutboundFrame> frames{ ReliableFrame(a_connectionId, Json{ { "type", "reportResult" }, { "ok", true }, { "reportId", reportId }, { "serverTime", a_unixTimeSeconds } }) };
		// Constraint: reporter identity goes to admins only, and the reported player's connection and profile never receive a report frame.
		for (const auto& [connectionId, session] : a_sessions) {
			if (!session.active || !a_isLiveAdmin(session)) continue;
			if (playerKind && (connectionId == target->first || session.profileId == target->second.profileId)) continue;
			frames.push_back(ReliableFrame(connectionId, report));
		}

		reports.push_back(std::move(report));
		while (reports.size() > kMaximumStoredReports) reports.pop_front();
		return frames;
	}

	void ReportService::Record(std::string_view a_kind, std::string_view a_summary, double a_unixTimeSeconds)
	{
		const auto reportId = "rp-" + std::to_string(nextReportNumber++);
		const auto kind = std::string{ a_kind };
		const auto note = std::string{ a_summary };
		std::cout << "[report] id=" << reportId << " kind=" << RuntimeSupport::SanitizeLogField(kind)
		          << " note=\"" << RuntimeSupport::SanitizeLogField(note) << "\"" << std::endl;
	}

	Json ReportService::ReportsJson() const
	{
		const auto listed = std::min(reports.size(), kMaximumListedReports);
		Json entries = Json::array();
		for (auto it = reports.end() - static_cast<std::ptrdiff_t>(listed); it != reports.end(); ++it) entries.push_back(*it);
		return Json{ { "reports", std::move(entries) }, { "total", reports.size() } };
	}
}

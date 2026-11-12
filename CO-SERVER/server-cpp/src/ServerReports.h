#pragma once

#include "ServerOutboundFrames.h"
#include "ServerProtocol.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CommonwealthOnline::Server
{
	namespace SessionTypes
	{
		struct Session;
	}

	using Session = SessionTypes::Session;

	class ReportService
	{
	public:
		using Json = nlohmann::json;
		using Sessions = std::unordered_map<std::uint32_t, Session>;
		using AdminCheck = std::function<bool(const Session&)>;

		explicit ReportService(ProtocolStats& a_stats) :
			stats(a_stats)
		{}

		std::vector<OutboundFrame> Submit(
			std::uint32_t a_connectionId,
			Session& a_self,
			Sessions& a_sessions,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds,
			const AdminCheck& a_isLiveAdmin);
		void Record(std::string_view a_kind, std::string_view a_summary, double a_unixTimeSeconds);
		[[nodiscard]] Json ReportsJson() const;

	private:
		friend struct ReportServiceTestAccess;
		std::vector<OutboundFrame> Failure(std::uint32_t a_connectionId, std::string_view a_reason, double a_unixTimeSeconds);
		[[nodiscard]] bool SubmitRateAllows(const std::string& a_key, double a_monotonicSeconds);

		ProtocolStats& stats;
		std::deque<Json> reports;
		std::unordered_map<std::string, std::vector<double>> submitWindows;
		std::uint64_t nextReportNumber{ 1 };
	};
}

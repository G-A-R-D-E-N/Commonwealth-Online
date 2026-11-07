#include "ServerProtocolImpl.h"

#include <cstdint>
#include <format>
#include <iostream>
#include <string>
#include <string_view>

namespace CommonwealthOnline::Server
{
	namespace
	{
		constexpr std::uint32_t kServerSenderPlayerId{ 0xFFFFFFFFU };
		constexpr std::string_view kDifficultyWarning{ "Set your difficulty to Very Hard within 30 seconds or you will be disconnected." };
		constexpr std::string_view kDifficultyViolationDetail{ "Difficulty must be set to Very Hard on this server." };
		constexpr std::string_view kUnreportedWarning{ "Your client did not report its difficulty; update Commonwealth Online." };
		constexpr std::string_view kUnreportedViolationDetail{ "No difficulty report was received from this client." };

		std::string ReportedText(const F4T::DifficultyEnforcement::SessionState& a_state)
		{
			return a_state.reportedDifficulty ? std::to_string(*a_state.reportedDifficulty) : std::string{ "none" };
		}
	}

	bool ProtocolHost::Impl::DifficultyFieldValid(const Json& a_value) noexcept
	{
		return a_value.is_number_integer() && F4T::DifficultyEnforcement::IsValidDifficulty(a_value.get<std::int64_t>());
	}

	void ProtocolHost::Impl::StartDifficultyTracking(Session& a_session, const Json& a_packet, double a_monotonicSeconds)
	{
		a_session.difficulty = F4T::DifficultyEnforcement::SessionState{};
		a_session.difficultyExempt = false;
		a_session.difficulty.joinedAt = a_monotonicSeconds;
		a_session.difficulty.reportedAt = a_monotonicSeconds;
		if (const auto found = a_packet.find("difficulty"); found != a_packet.end() && DifficultyFieldValid(*found)) {
			a_session.difficulty.reportedDifficulty = static_cast<std::int32_t>(found->get<std::int64_t>());
		}
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::HandleDifficultyReport(std::uint32_t a_connectionId, Session& a_session, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		const auto found = a_packet.find("difficulty");
		if (found == a_packet.end() || !DifficultyFieldValid(*found)) {
			Reject();
			return {};
		}
		a_session.difficulty.reportedDifficulty = static_cast<std::int32_t>(found->get<std::int64_t>());
		a_session.difficulty.reportedAt = a_monotonicSeconds;
		return ApplyDifficultyStatus(a_connectionId, a_session, a_monotonicSeconds, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::PollDifficulty(double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		if (!survivalMode || !requireDifficultyReport) return {};
		if (a_monotonicSeconds < nextDifficultySweepMonotonic) return {};
		nextDifficultySweepMonotonic = a_monotonicSeconds + F4T::DifficultyEnforcement::kSweepIntervalSeconds;
		std::vector<OutboundFrame> frames;
		for (auto& [connectionId, session] : sessions) {
			AppendFrames(frames, ApplyDifficultyStatus(connectionId, session, a_monotonicSeconds, a_unixTimeSeconds));
		}
		return frames;
	}

	void ProtocolHost::Impl::ResetDifficultyTracking(double a_monotonicSeconds)
	{
		for (auto& [connectionId, session] : sessions) {
			F4T::DifficultyEnforcement::ResetTracking(session.difficulty, a_monotonicSeconds);
		}
		difficultyResetPending = false;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::ApplyDifficultyStatus(std::uint32_t a_connectionId, Session& a_session, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		if (!survivalMode || !requireDifficultyReport) return {};
		if (difficultyResetPending) ResetDifficultyTracking(a_monotonicSeconds);
		if (!a_session.active) return {};
		if (IsLiveAdminSession(a_session)) {
			a_session.difficultyExempt = true;
			return {};
		}
		if (a_session.difficultyExempt) {
			a_session.difficultyExempt = false;
			F4T::DifficultyEnforcement::ResetTracking(a_session.difficulty, a_monotonicSeconds);
		}
		const F4T::DifficultyEnforcement::Settings settings{ survivalMode, requireDifficultyReport };
		const auto status = F4T::DifficultyEnforcement::Evaluate(a_session.difficulty, settings, a_monotonicSeconds);
		const auto action = F4T::DifficultyEnforcement::Step(a_session.difficulty, status, a_monotonicSeconds);
		if (action == F4T::DifficultyEnforcement::Action::None) return {};
		const bool warn = action == F4T::DifficultyEnforcement::Action::Warn;
		const bool unreported = status == F4T::DifficultyEnforcement::Status::Silent;
		std::cout << std::format("[survival] difficulty violation player={} reported={} action={}", a_session.playerId,
			ReportedText(a_session.difficulty), warn ? "warn" : "kick") << std::endl;
		if (warn) {
			return { ReliableFrame(a_connectionId, Json{
				{ "type", "chatMessage" }, { "channel", "global" }, { "messageId", AllocateChatMessageId() },
				{ "fromPlayerId", kServerSenderPlayerId }, { "fromName", "Server" },
				{ "text", std::string{ unreported ? kUnreportedWarning : kDifficultyWarning } }, { "serverTime", a_unixTimeSeconds } }) };
		}
		violations.push_back(PendingViolation{ a_connectionId,
			unreported ? Violation::DifficultyUnreported : Violation::DifficultyMismatch,
			std::string{ unreported ? kUnreportedViolationDetail : kDifficultyViolationDetail } });
		return {};
	}
}

#include "ServerEnforcement.h"

#include "BanStore.h"
#include "ServerLogSanitizer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>

namespace CommonwealthOnline::Server
{
	namespace
	{
		constexpr double kStrikeDecaySeconds = 3600.0;
		constexpr std::size_t kMaximumTrackedSubjects = 4096;
		constexpr std::uint32_t kAddressBanSeconds = 86400;
		constexpr std::size_t kViolationCount = 8;
		constexpr double kViolationWindowSeconds = 30.0;
		constexpr std::uint32_t kViolationWindowHits = 5;
		constexpr std::string_view kInboundBudgetWarning = "Warning: you are sending more data than the server allows. Repeating this disconnects you.";

		constexpr std::array<std::array<Consequence, 3>, kViolationCount> kLadder{ {
			{ { { Action::Kick, 0 }, { Action::TempBan, 600 }, { Action::TempBan, 3600 } } },
			{ { { Action::Kick, 0 }, { Action::TempBan, 3600 }, { Action::PermaBan, 0 } } },
			{ { { Action::Kick, 0 }, { Action::TempBan, 3600 }, { Action::PermaBan, 0 } } },
			{ { { Action::Warn, 0 }, { Action::Kick, 0 }, { Action::TempBan, 3600 } } },
			{ { { Action::Warn, 0 }, { Action::Kick, 0 }, { Action::TempBan, 600 } } },
			{ { { Action::Warn, 0 }, { Action::Kick, 0 }, { Action::TempBan, 3600 } } },
			{ { { Action::Kick, 0 }, { Action::Kick, 0 }, { Action::Kick, 0 } } },
			{ { { Action::Kick, 0 }, { Action::Kick, 0 }, { Action::Kick, 0 } } },
		} };

		constexpr std::array<std::string_view, kViolationCount> kViolationCodes{
			"rate_limited",
			"malformed_payload",
			"steam_identity_mismatch",
			"teleport_distance",
			"inbound_budget",
			"pvp_damage",
			"difficulty_mismatch",
			"difficulty_unreported",
		};

		constexpr std::array<std::string_view, kViolationCount> kViolationLabels{
			"Sent messages too fast.",
			"Sent a malformed message.",
			"Your Steam identity did not match your account.",
			"Moved farther than the server allows.",
			"Sent more data than the server allows for several seconds. Reconnect; report it if it repeats.",
			"Dealt damage that the server does not allow.",
			"Your difficulty must be set to Very Hard on this server.",
			"Your client did not report its difficulty; update Commonwealth Online.",
		};

		std::string BanDescription(std::string_view a_label, double a_expiresAt, double a_nowSeconds)
		{
			if (a_expiresAt == 0.0) {
				return std::string{ a_label } + " Permanent ban.";
			}
			const auto minutes = (std::max)(std::llround((a_expiresAt - a_nowSeconds) / 60.0), 1LL);
			return std::string{ a_label } + " Temporary ban: " + std::to_string(minutes) + " minutes.";
		}
	}

	Consequence ConsequenceFor(Violation a_violation, std::uint32_t a_strikes) noexcept
	{
		const auto rung = (std::clamp)(a_strikes, 1U, 3U) - 1U;
		return kLadder[static_cast<std::size_t>(a_violation)][rung];
	}

	std::string WithAppealLink(std::string_view a_text, std::string_view a_appealLink, std::size_t a_maximumBytes)
	{
		if (a_appealLink.empty()) {
			return std::string{ a_text };
		}
		auto text = std::string{ a_text } + " Appeal: " + std::string{ a_appealLink };
		return text.size() <= a_maximumBytes ? text : std::string{ a_text };
	}

	bool ViolationWindow::Observe(double a_nowSeconds) noexcept
	{
		if (_count == 0 || a_nowSeconds - _startedSeconds >= kViolationWindowSeconds) {
			_startedSeconds = a_nowSeconds;
			_count = 0;
		}
		_count += 1;
		if (_count >= kViolationWindowHits) {
			_count = 0;
			return true;
		}
		return false;
	}

	std::uint32_t StrikeLedger::Record(std::string_view a_subject, Violation a_violation, double a_nowSeconds)
	{
		const auto decayed = [a_nowSeconds](Strike a_strike) {
			const auto steps = std::floor((a_nowSeconds - a_strike.lastSeenSeconds) / kStrikeDecaySeconds);
			if (steps >= static_cast<double>(a_strike.count)) {
				return Strike{ 0, a_nowSeconds };
			}
			if (steps > 0.0) {
				a_strike.count -= static_cast<std::uint32_t>(steps);
				a_strike.lastSeenSeconds += steps * kStrikeDecaySeconds;
			}
			return a_strike;
		};
		const std::pair<std::string, Violation> key{ std::string{ a_subject }, a_violation };
		if (_strikes.size() >= kMaximumTrackedSubjects && _strikes.find(key) == _strikes.end()) {
			std::erase_if(_strikes, [&](const auto& entry) { return decayed(entry.second).count == 0; });
			if (_strikes.size() >= kMaximumTrackedSubjects) {
				_strikes.erase(std::min_element(_strikes.begin(), _strikes.end(), [](const auto& left, const auto& right) {
					return left.second.lastSeenSeconds < right.second.lastSeenSeconds;
				}));
			}
		}
		auto& strike = _strikes[key];
		strike = decayed(strike);
		strike.count += 1;
		return strike.count;
	}

	ServerEnforcement::ServerEnforcement(BanStore* a_bans, SubjectOf a_subjectOf, EndSession a_endSession, std::string a_appealLink, Notify a_notify) :
		_bans(a_bans),
		_subjectOf(std::move(a_subjectOf)),
		_endSession(std::move(a_endSession)),
		_appealLink(std::move(a_appealLink)),
		_notify(std::move(a_notify))
	{
	}

	void ServerEnforcement::ReportViolation(std::uint32_t a_connectionId, Violation a_violation, std::string_view a_detail)
	{
		const auto subject = _subjectOf(a_connectionId);
		if (!subject.connected) {
			return;
		}
		const auto now = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
		const auto subjectKey = !subject.steamId.empty() ? subject.steamId : !subject.address.empty() ? subject.address : "connection:" + std::to_string(a_connectionId);
		const auto strikes = _strikes.Record(subjectKey, a_violation, now);
		auto consequence = ConsequenceFor(a_violation, strikes);
		const auto index = static_cast<std::size_t>(a_violation);
		const bool kickOnly = subject.steamId.empty() && (a_violation == Violation::TeleportDistance || a_violation == Violation::PvpDamage);
		const bool bannable = _bans != nullptr && !subject.hostSelf && !subject.admin && !subject.adminOnAddress && !kickOnly && (!subject.steamId.empty() || !subject.address.empty());
		if ((consequence.action == Action::TempBan || consequence.action == Action::PermaBan) && !bannable) {
			consequence.action = Action::Kick;
		}
		if (consequence.action == Action::PermaBan && subject.steamId.empty()) {
			consequence = Consequence{ Action::TempBan, kAddressBanSeconds };
		}
		const auto code = std::string{ kViolationCodes[index] };
		const auto label = std::string{ kViolationLabels[index] };
		std::cerr << "Enforcement: connection " << a_connectionId << ' ' << code << " strike " << strikes << ": " << RuntimeSupport::SanitizeLogField(a_detail) << '\n';
		if (consequence.action == Action::Warn) {
			if (a_violation == Violation::InboundBudget && _notify) {
				_notify(a_connectionId, kInboundBudgetWarning);
			}
			return;
		}
		if (consequence.action == Action::Kick) {
			_endSession(a_connectionId, code, WithAppealLink("Kicked: " + label, _appealLink));
			return;
		}
		const auto permanent = consequence.action == Action::PermaBan;
		const auto reason = label + (permanent ? " Permanent ban." : " Temporary ban: " + std::to_string(consequence.seconds / 60) + " minutes.");
		const auto expiresAt = permanent ? 0.0 : now + static_cast<double>(consequence.seconds);
		auto effectiveExpiresAt = expiresAt;
		try {
			if (!subject.steamId.empty()) {
				effectiveExpiresAt = _bans->BanSteamId(subject.steamId, reason, now, expiresAt).expiresAt;
			} else {
				effectiveExpiresAt = _bans->Ban(subject.address, reason, now, expiresAt).expiresAt;
			}
		} catch (const std::exception& exception) {
			std::cerr << "Enforcement ban failed for connection " << a_connectionId << ": " << RuntimeSupport::SanitizeLogField(exception.what()) << '\n';
			_endSession(a_connectionId, code, WithAppealLink("Kicked: " + label, _appealLink));
			return;
		}
		_endSession(a_connectionId, "banned", WithAppealLink("Banned: " + BanDescription(label, effectiveExpiresAt, now), _appealLink));
	}

	void ServerEnforcement::ReportViolations(const std::vector<PendingViolation>& a_violations)
	{
		for (const auto& violation : a_violations) {
			ReportViolation(violation.connectionId, violation.violation, violation.detail);
		}
	}
}

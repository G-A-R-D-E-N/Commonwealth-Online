#include "IdentityGate.h"

#include "F4TSteamId.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace CommonwealthOnline::Server
{
	namespace
	{
		IdentityIngressDecision LockedOutDecision(double a_remainingSeconds)
		{
			const auto minutes = static_cast<long long>(std::ceil(a_remainingSeconds / 60.0));
			return IdentityIngressDecision{
				.forward = false,
				.code = "password_locked",
				.reason = "Too many wrong join passwords. Try again in " + std::to_string(minutes) + (minutes == 1 ? " minute." : " minutes."),
				.profileId = {},
				.evictConnectionId = std::nullopt,
				.requiredModpack = {}
			};
		}
	}

	std::optional<IdentityIngressDecision> IdentityGate::CheckSteamPassword(
		std::uint32_t a_connectionId,
		const VerifiedSteamId& a_steam)
	{
		if (a_steam.passwordRequired && !_requirements.serverPasswordSha256) {
			std::cerr << "Steam admission: connection " << a_connectionId << " refused: admitted as a non-friend but this server has no join password.\n";
			return IdentityIngressDecision{ .forward = false, .code = "steam_friends_only", .reason = "This host only lets their Steam friends join.", .profileId = {}, .evictConnectionId = std::nullopt, .requiredModpack = {} };
		}
		const auto steamId = F4T::SteamId::ParseIndividual(a_steam.steamId).value_or(0);
		const auto now = MonotonicNow();
		const auto status = _passwordLockout.Check(steamId, now);
		(void)_passwordLockout.Sweep(now);
		if (status.expired) {
			std::cerr << "Steam admission: connection " << a_connectionId << ": the wrong-password lockout for this Steam ID has expired.\n";
		}
		if (!status.locked) return std::nullopt;
		if (status.blockedAttempts == 1 || status.blockedAttempts % 10 == 0) {
			std::cerr << "Steam admission: connection " << a_connectionId << " refused: locked out after " << status.failures
			          << " wrong passwords, " << static_cast<long long>(std::ceil(status.remainingSeconds)) << " s left, "
			          << status.blockedAttempts << " attempts refused during this lockout.\n";
		}
		return LockedOutDecision(status.remainingSeconds);
	}

	void IdentityGate::RecordSteamPasswordOutcome(std::uint32_t a_connectionId, const VerifiedSteamId& a_steam, std::string_view a_code)
	{
		const auto steamId = F4T::SteamId::ParseIndividual(a_steam.steamId).value_or(0);
		if (a_code.empty()) {
			_passwordLockout.RecordSuccess(steamId);
			return;
		}
		if (a_code != "password_invalid") return;
		const auto status = _passwordLockout.RecordFailure(steamId, MonotonicNow());
		const auto threshold = _passwordLockout.Options().threshold;
		if (status.tableFull) {
			std::cerr << "Steam admission: connection " << a_connectionId << ": wrong password not tracked, the lockout table is full of active locks.\n";
			return;
		}
		if (status.lockedNow) {
			std::cerr << "Steam admission: connection " << a_connectionId << ": wrong password " << status.failures << "/" << threshold
			          << ", this Steam ID is locked out for " << static_cast<long long>(std::ceil(status.remainingSeconds)) << " s.\n";
		} else {
			std::cerr << "Steam admission: connection " << a_connectionId << ": wrong password " << status.failures << "/" << threshold << ".\n";
		}
	}

	std::optional<IdentityIngressDecision> IdentityGate::CheckDirectJoinPassword(
		std::uint32_t a_connectionId,
		std::uint32_t a_remoteIpv4)
	{
		const auto now = MonotonicNow();
		const auto status = _directPasswordLockout.Check(a_remoteIpv4, now);
		(void)_directPasswordLockout.Sweep(now);
		if (status.expired) {
			std::cerr << "Join password: connection " << a_connectionId << ": the wrong-password lockout for this address has expired.\n";
		}
		if (!status.locked) return std::nullopt;
		if (status.blockedAttempts == 1 || status.blockedAttempts % 10 == 0) {
			std::cerr << "Join password: connection " << a_connectionId << " refused: locked out after " << status.failures
			          << " wrong passwords, " << static_cast<long long>(std::ceil(status.remainingSeconds)) << " s left, "
			          << status.blockedAttempts << " attempts refused during this lockout.\n";
		}
		return LockedOutDecision(status.remainingSeconds);
	}

	void IdentityGate::RecordDirectJoinPasswordOutcome(std::uint32_t a_connectionId, std::uint32_t a_remoteIpv4, std::string_view a_code)
	{
		if (a_code.empty()) {
			_directPasswordLockout.RecordSuccess(a_remoteIpv4);
			return;
		}
		if (a_code != "password_invalid") return;
		const auto status = _directPasswordLockout.RecordFailure(a_remoteIpv4, MonotonicNow());
		const auto threshold = _directPasswordLockout.Options().threshold;
		if (status.tableFull) {
			std::cerr << "Join password: connection " << a_connectionId << ": wrong password not tracked, the lockout table is full of active locks.\n";
			return;
		}
		if (status.lockedNow) {
			std::cerr << "Join password: connection " << a_connectionId << ": wrong password " << status.failures << "/" << threshold
			          << ", this address is locked out for " << static_cast<long long>(std::ceil(status.remainingSeconds)) << " s.\n";
		} else {
			std::cerr << "Join password: connection " << a_connectionId << ": wrong password " << status.failures << "/" << threshold << ".\n";
		}
	}
}

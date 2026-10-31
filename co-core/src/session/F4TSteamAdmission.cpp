#include "F4TSteamAdmission.h"

#include "F4TSteamId.h"

#include <cmath>

namespace F4T::SteamAdmission
{
	namespace
	{
		TunnelDecision Refuse(Outcome a_outcome, int a_closeReason) noexcept
		{
			return TunnelDecision{ a_outcome, false, a_closeReason };
		}

		LockoutOptions Sanitize(LockoutOptions a_options) noexcept
		{
			if (a_options.threshold == 0) a_options.threshold = 1;
			if (!std::isfinite(a_options.cooldownSeconds) || a_options.cooldownSeconds <= 0.0) a_options.cooldownSeconds = kDefaultLockoutSeconds;
			if (a_options.maximumTracked == 0) a_options.maximumTracked = 1;
			return a_options;
		}
	}

	TunnelDecision DecideTunnel(const HostPolicy& a_policy, const TunnelRequest& a_request) noexcept
	{
		if (!SteamId::IsIndividual(a_request.peerSteamId)) return Refuse(Outcome::InvalidIdentity, kEndNotPermitted);

		bool passwordRequired = false;
		if (a_request.friendCheck != FriendCheck::Friend) {
			if (a_policy.friendsOnly) {
				return a_request.friendCheck == FriendCheck::Unavailable
					? Refuse(Outcome::FriendCheckUnavailable, kEndFriendCheckUnavailable)
					: Refuse(Outcome::NotFriend, kEndFriendsOnly);
			}
			if (!a_request.identityLinked || !a_policy.serverPasswordSet) return Refuse(Outcome::PasswordNotEnforceable, kEndFriendsOnly);
			passwordRequired = true;
		}

		if (a_request.relayed >= a_request.maximumRelayed) return Refuse(Outcome::ServerFull, kEndServerFull);
		return TunnelDecision{ Outcome::Admitted, passwordRequired, 0 };
	}

	std::string_view OutcomeName(Outcome a_outcome) noexcept
	{
		switch (a_outcome) {
		case Outcome::Admitted: return "admitted";
		case Outcome::InvalidIdentity: return "invalid_identity";
		case Outcome::NotFriend: return "not_friend";
		case Outcome::FriendCheckUnavailable: return "friend_check_unavailable";
		case Outcome::PasswordNotEnforceable: return "password_not_enforceable";
		case Outcome::ServerFull: return "server_full";
		}
		return "unknown";
	}

	std::string_view FriendCheckName(FriendCheck a_check) noexcept
	{
		switch (a_check) {
		case FriendCheck::Friend: return "friend";
		case FriendCheck::NotFriend: return "not_friend";
		case FriendCheck::Unavailable: return "unavailable";
		}
		return "unknown";
	}

	std::string DescribeTunnelDecision(
		std::uint32_t a_connection,
		const HostPolicy& a_policy,
		const TunnelRequest& a_request,
		const TunnelDecision& a_decision)
	{
		std::string line = "admission connection=" + std::to_string(a_connection);
		line += SteamId::IsIndividual(a_request.peerSteamId) ? " steamid=valid" : " steamid=invalid";
		line += a_policy.friendsOnly ? " friends_only=on" : " friends_only=off";
		line += " friend=" + std::string{ FriendCheckName(a_request.friendCheck) };
		line += a_policy.serverPasswordSet ? " server_password=set" : " server_password=none";
		line += a_request.identityLinked ? " identity_link=yes" : " identity_link=no";
		line += " relayed=" + std::to_string(a_request.relayed) + "/" + std::to_string(a_request.maximumRelayed);
		line += " outcome=" + std::string{ a_decision.Admitted() ? "admitted" : "refused" };
		if (!a_decision.Admitted()) line += " reason=" + std::string{ OutcomeName(a_decision.outcome) } + " close=" + std::to_string(a_decision.closeReason);
		if (a_decision.passwordRequired) line += " password=required_by_server";
		return line;
	}

	std::string_view GuestMessage(int a_closeReason) noexcept
	{
		switch (a_closeReason) {
		case kEndServerFull:
		case kLegacyEndServerFull: return "The host's game is full.";
		case kEndFriendsOnly: return "This host only lets their Steam friends join.";
		case kEndFriendCheckUnavailable: return "The host's Steam could not check whether you are friends. Try again in a moment.";
		case kEndNotPermitted:
		case kLegacyEndNotPermitted: return "The host refused the connection.";
		case kEndUndeliverable: return "That host refused you, but it runs an older Commonwealth Online build that cannot say why. Check that you are their Steam friend and that their game is not full, or ask them to update.";
		case kEndLocalSocketFailed:
		case kEndIdentityUnlinked:
		case kLegacyEndLocalSocketFailed:
		case kLegacyEndIdentityUnlinked: return "The host could not set up your connection. Ask the host to restart hosting.";
		default: return {};
		}
	}

	PasswordLockout::PasswordLockout(LockoutOptions a_options) :
		options_{ Sanitize(a_options) }
	{
	}

	bool PasswordLockout::IsStale(const Entry& a_entry, double a_now) const noexcept
	{
		return a_entry.lockedUntil > 0.0 ? a_now >= a_entry.lockedUntil : a_now - a_entry.lastFailure > options_.cooldownSeconds;
	}

	LockoutStatus PasswordLockout::Check(std::uint64_t a_steamId, double a_now)
	{
		const auto found = entries_.find(a_steamId);
		if (a_steamId == 0 || found == entries_.end()) return {};
		auto& entry = found->second;
		if (entry.lockedUntil > 0.0) {
			if (a_now < entry.lockedUntil) {
				++entry.blocked;
				return LockoutStatus{ true, entry.failures, entry.lockedUntil - a_now, false, false, entry.blocked };
			}
			entries_.erase(found);
			return LockoutStatus{ .expired = true };
		}
		if (IsStale(entry, a_now)) {
			entries_.erase(found);
			return {};
		}
		return LockoutStatus{ .failures = entry.failures };
	}

	LockoutStatus PasswordLockout::RecordFailure(std::uint64_t a_steamId, double a_now)
	{
		if (a_steamId == 0) return {};
		auto found = entries_.find(a_steamId);
		if (found == entries_.end()) {
			if (entries_.size() >= options_.maximumTracked && !MakeRoom(a_now)) {
				return LockoutStatus{ .tableFull = true };
			}
			found = entries_.emplace(a_steamId, Entry{}).first;
		}
		auto& entry = found->second;
		if (entry.lockedUntil > 0.0 && a_now < entry.lockedUntil) {
			return LockoutStatus{ true, entry.failures, entry.lockedUntil - a_now, false, false, entry.blocked };
		}
		if (IsStale(entry, a_now)) entry = Entry{};
		++entry.failures;
		entry.lastFailure = a_now;
		if (entry.failures < options_.threshold) return LockoutStatus{ .failures = entry.failures };
		entry.lockedUntil = a_now + options_.cooldownSeconds;
		entry.blocked = 0;
		return LockoutStatus{ true, entry.failures, options_.cooldownSeconds, true, false, 0 };
	}

	void PasswordLockout::RecordSuccess(std::uint64_t a_steamId)
	{
		entries_.erase(a_steamId);
	}

	std::size_t PasswordLockout::Sweep(double a_now)
	{
		return std::erase_if(entries_, [&](const auto& a_item) { return IsStale(a_item.second, a_now); });
	}

	bool PasswordLockout::MakeRoom(double a_now)
	{
		(void)Sweep(a_now);
		if (entries_.size() < options_.maximumTracked) return true;
		auto victim = entries_.end();
		for (auto it = entries_.begin(); it != entries_.end(); ++it) {
			if (it->second.lockedUntil > 0.0) continue;
			if (victim == entries_.end() || it->second.lastFailure < victim->second.lastFailure) victim = it;
		}
		if (victim == entries_.end()) return false;
		entries_.erase(victim);
		return true;
	}
}

#include "F4TAdmission.h"

#include "F4TConnectionPolicy.h"

#include <algorithm>
#include <utility>

namespace F4T::Admission
{
	AdmissionRegistry::AdmissionRegistry(std::size_t a_maximumPlayers, double a_timeoutSeconds, double a_lifetimeSeconds) :
		_maximumPlayers(a_maximumPlayers),
		_timeoutSeconds(a_timeoutSeconds > 0.0 ? a_timeoutSeconds : DefaultAdmissionTimeoutSeconds),
		_lifetimeSeconds(a_lifetimeSeconds > 0.0 ? a_lifetimeSeconds : DefaultAdmissionLifetimeSeconds)
	{}

	Decision AdmissionRegistry::Begin(
		ClaimedIdentity a_identity,
		double a_nowUnixSeconds,
		std::size_t a_activePlayers)
	{
		(void)a_activePlayers;
		if (_maximumPlayers == 0) {
			return Decision{ false, 0, "server_full", "Server is full." };
		}
		if (a_identity.ip.empty()) {
			return Decision{ false, 0, "admission_invalid", "Admission requires a claimed address." };
		}
		if (_pending.size() >= F4T::ConnectionPolicy::MaximumPendingConnections(_maximumPlayers)) {
			return Decision{ false, 0, "admission_busy", "Too many pending connections." };
		}

		std::uint64_t ticketId = _nextTicket++;
		if (ticketId == 0) {
			ticketId = _nextTicket++;
		}
		PendingAdmission pending{
			.ticketId = ticketId,
			.identity = std::move(a_identity),
			.openedAt = a_nowUnixSeconds,
			.deadline = a_nowUnixSeconds + _timeoutSeconds
		};
		_pending.insert_or_assign(ticketId, std::move(pending));
		return Decision{ true, ticketId, {}, {} };
	}

	bool AdmissionRegistry::Update(
		std::uint64_t a_ticketId,
		std::string_view a_message,
		double a_nowUnixSeconds)
	{
		const auto found = _pending.find(a_ticketId);
		if (found == _pending.end()) {
			return false;
		}
		if (Expired(found->second, a_nowUnixSeconds)) {
			_pending.erase(found);
			++_timedOut;
			return false;
		}
		found->second.lastMessage.assign(a_message);
		return true;
	}

	bool AdmissionRegistry::Claim(
		std::uint64_t a_ticketId,
		std::string_view a_steamId,
		std::string_view a_nickname,
		std::string_view a_profileId,
		std::string_view a_message,
		double a_nowUnixSeconds)
	{
		const auto found = _pending.find(a_ticketId);
		if (found == _pending.end() || Expired(found->second, a_nowUnixSeconds)) {
			EraseExpired(a_ticketId, a_nowUnixSeconds);
			return false;
		}
		if (!a_steamId.empty()) {
			found->second.identity.steamId.assign(a_steamId);
		}
		if (!a_nickname.empty()) {
			found->second.identity.nickname.assign(a_nickname);
		}
		if (!a_profileId.empty()) {
			found->second.identity.profileId.assign(a_profileId);
		}
		found->second.lastMessage.assign(a_message);
		found->second.deadline = (std::min)(a_nowUnixSeconds + _timeoutSeconds, found->second.openedAt + _lifetimeSeconds);
		return true;
	}

	bool AdmissionRegistry::Commit(std::uint64_t a_ticketId, double a_nowUnixSeconds)
	{
		const auto found = _pending.find(a_ticketId);
		if (found == _pending.end()) {
			return false;
		}
		if (Expired(found->second, a_nowUnixSeconds)) {
			_pending.erase(found);
			++_timedOut;
			return false;
		}
		_pending.erase(found);
		++_admitted;
		return true;
	}

	bool AdmissionRegistry::Reject(std::uint64_t a_ticketId, std::string_view a_reason)
	{
		const auto found = _pending.find(a_ticketId);
		if (found == _pending.end()) {
			return false;
		}
		_pending.erase(found);
		++_rejected;
		_lastRejectReason.assign(a_reason);
		return true;
	}

	bool AdmissionRegistry::Cancel(std::uint64_t a_ticketId)
	{
		return _pending.erase(a_ticketId) != 0;
	}

	std::optional<PendingAdmission> AdmissionRegistry::Find(
		std::uint64_t a_ticketId,
		double a_nowUnixSeconds) const
	{
		const auto found = _pending.find(a_ticketId);
		if (found == _pending.end() || Expired(found->second, a_nowUnixSeconds)) {
			return std::nullopt;
		}
		return found->second;
	}

	bool AdmissionRegistry::IsExpired(std::uint64_t a_ticketId, double a_nowUnixSeconds) const noexcept
	{
		const auto found = _pending.find(a_ticketId);
		return found == _pending.end() || Expired(found->second, a_nowUnixSeconds);
	}

	std::vector<Expiry> AdmissionRegistry::Sweep(double a_nowUnixSeconds)
	{
		std::vector<Expiry> expired;
		for (auto it = _pending.begin(); it != _pending.end();) {
			if (!Expired(it->second, a_nowUnixSeconds)) {
				++it;
				continue;
			}
			expired.push_back(Expiry{ it->first, std::move(it->second.identity) });
			it = _pending.erase(it);
			++_timedOut;
		}
		return expired;
	}

	std::size_t AdmissionRegistry::PendingCount() const noexcept
	{
		return _pending.size();
	}

	std::size_t AdmissionRegistry::AdmittedCount() const noexcept
	{
		return _admitted;
	}

	std::size_t AdmissionRegistry::RejectedCount() const noexcept
	{
		return _rejected;
	}

	std::size_t AdmissionRegistry::TimedOutCount() const noexcept
	{
		return _timedOut;
	}

	double AdmissionRegistry::TimeoutSeconds() const noexcept
	{
		return _timeoutSeconds;
	}

	std::string_view AdmissionRegistry::LastRejectReason() const noexcept
	{
		return _lastRejectReason;
	}

	bool AdmissionRegistry::Expired(const PendingAdmission& a_pending, double a_nowUnixSeconds) const noexcept
	{
		return a_nowUnixSeconds >= a_pending.deadline;
	}

	void AdmissionRegistry::EraseExpired(std::uint64_t a_ticketId, double a_nowUnixSeconds)
	{
		const auto found = _pending.find(a_ticketId);
		if (found != _pending.end() && Expired(found->second, a_nowUnixSeconds)) {
			_pending.erase(found);
			++_timedOut;
		}
	}
}

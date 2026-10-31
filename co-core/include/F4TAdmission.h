#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace F4T::Admission
{
	inline constexpr double DefaultAdmissionTimeoutSeconds = 30.0;
	inline constexpr double DefaultAdmissionLifetimeSeconds = 60.0;

	struct ClaimedIdentity
	{
		std::string steamId;
		std::string nickname;
		std::string ip;
		std::string profileId;
	};

	struct PendingAdmission
	{
		std::uint64_t ticketId{};
		ClaimedIdentity identity;
		double openedAt{};
		double deadline{};
		std::string lastMessage;
	};

	struct Decision
	{
		bool admitted{};
		std::uint64_t ticketId{};
		std::string code;
		std::string reason;
	};

	struct Expiry
	{
		std::uint64_t ticketId{};
		ClaimedIdentity identity;
	};

	class AdmissionRegistry
	{
	public:
		explicit AdmissionRegistry(
			std::size_t a_maximumPlayers = 16,
			double a_timeoutSeconds = DefaultAdmissionTimeoutSeconds,
			double a_lifetimeSeconds = DefaultAdmissionLifetimeSeconds);

		[[nodiscard]] Decision Begin(
			ClaimedIdentity a_identity,
			double a_nowUnixSeconds,
			std::size_t a_activePlayers);
		[[nodiscard]] bool Update(
			std::uint64_t a_ticketId,
			std::string_view a_message,
			double a_nowUnixSeconds);
		[[nodiscard]] bool Claim(
			std::uint64_t a_ticketId,
			std::string_view a_steamId,
			std::string_view a_nickname,
			std::string_view a_profileId,
			std::string_view a_message,
			double a_nowUnixSeconds);
		[[nodiscard]] bool Commit(std::uint64_t a_ticketId, double a_nowUnixSeconds);
		[[nodiscard]] bool Reject(std::uint64_t a_ticketId, std::string_view a_reason);
		[[nodiscard]] bool Cancel(std::uint64_t a_ticketId);
		[[nodiscard]] std::optional<PendingAdmission> Find(std::uint64_t a_ticketId, double a_nowUnixSeconds) const;
		[[nodiscard]] bool IsExpired(std::uint64_t a_ticketId, double a_nowUnixSeconds) const noexcept;
		[[nodiscard]] std::vector<Expiry> Sweep(double a_nowUnixSeconds);

		void SetMaximumPlayers(std::size_t a_maximumPlayers) noexcept { _maximumPlayers = a_maximumPlayers; }
		[[nodiscard]] std::size_t PendingCount() const noexcept;
		[[nodiscard]] std::size_t AdmittedCount() const noexcept;
		[[nodiscard]] std::size_t RejectedCount() const noexcept;
		[[nodiscard]] std::size_t TimedOutCount() const noexcept;
		[[nodiscard]] double TimeoutSeconds() const noexcept;
		[[nodiscard]] std::string_view LastRejectReason() const noexcept;

	private:
		[[nodiscard]] bool Expired(const PendingAdmission& a_pending, double a_nowUnixSeconds) const noexcept;
		void EraseExpired(std::uint64_t a_ticketId, double a_nowUnixSeconds);

		std::size_t _maximumPlayers{};
		double _timeoutSeconds{};
		double _lifetimeSeconds{};
		std::uint64_t _nextTicket{ 1 };
		std::unordered_map<std::uint64_t, PendingAdmission> _pending;
		std::size_t _admitted{};
		std::size_t _rejected{};
		std::size_t _timedOut{};
		std::string _lastRejectReason;
	};
}

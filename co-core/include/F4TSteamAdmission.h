#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace F4T::SteamAdmission
{
	// GNS delivers an application close reason only in 1000-2999; anything else arrives as 1999.
	inline constexpr int kEndNotPermitted = 1101;
	inline constexpr int kEndServerFull = 1102;
	inline constexpr int kEndFriendsOnly = 1103;
	inline constexpr int kEndFriendCheckUnavailable = 2101;
	inline constexpr int kEndLocalSocketFailed = 2102;
	inline constexpr int kEndIdentityUnlinked = 2103;

	inline constexpr int kLegacyEndNotPermitted = 4001;
	inline constexpr int kLegacyEndServerFull = 4002;
	inline constexpr int kLegacyEndLocalSocketFailed = 4003;
	inline constexpr int kLegacyEndIdentityUnlinked = 4004;

	inline constexpr std::uint32_t kDefaultLockoutThreshold = 5;
	inline constexpr double kDefaultLockoutSeconds = 300.0;
	inline constexpr std::size_t kDefaultMaximumTrackedSteamIds = 1024;

	struct HostPolicy
	{
		bool friendsOnly{ true };
		bool serverPasswordSet{ false };
	};

	enum class FriendCheck : std::uint8_t
	{
		Friend,
		NotFriend,
		Unavailable
	};

	enum class Outcome : std::uint8_t
	{
		Admitted,
		InvalidIdentity,
		NotFriend,
		FriendCheckUnavailable,
		PasswordNotEnforceable,
		ServerFull
	};

	struct TunnelRequest
	{
		std::uint64_t peerSteamId{};
		FriendCheck friendCheck{ FriendCheck::Unavailable };
		std::size_t relayed{};
		std::size_t maximumRelayed{};
		bool identityLinked{};
	};

	struct TunnelDecision
	{
		Outcome outcome{ Outcome::InvalidIdentity };
		bool passwordRequired{};
		int closeReason{ kEndNotPermitted };

		[[nodiscard]] bool Admitted() const noexcept { return outcome == Outcome::Admitted; }
	};

	[[nodiscard]] TunnelDecision DecideTunnel(const HostPolicy& a_policy, const TunnelRequest& a_request) noexcept;
	[[nodiscard]] std::string_view OutcomeName(Outcome a_outcome) noexcept;
	[[nodiscard]] std::string_view FriendCheckName(FriendCheck a_check) noexcept;
	[[nodiscard]] std::string DescribeTunnelDecision(
		std::uint32_t a_connection,
		const HostPolicy& a_policy,
		const TunnelRequest& a_request,
		const TunnelDecision& a_decision);
	[[nodiscard]] std::string_view GuestMessage(int a_closeReason) noexcept;

	struct LockoutOptions
	{
		std::uint32_t threshold{ kDefaultLockoutThreshold };
		double cooldownSeconds{ kDefaultLockoutSeconds };
		std::size_t maximumTracked{ kDefaultMaximumTrackedSteamIds };
	};

	struct LockoutStatus
	{
		bool locked{};
		std::uint32_t failures{};
		double remainingSeconds{};
		bool lockedNow{};
		bool expired{};
		std::uint32_t blockedAttempts{};
		bool tableFull{};
	};

	class PasswordLockout
	{
	public:
		PasswordLockout() = default;
		explicit PasswordLockout(LockoutOptions a_options);

		[[nodiscard]] LockoutStatus Check(std::uint64_t a_steamId, double a_now);
		[[nodiscard]] LockoutStatus RecordFailure(std::uint64_t a_steamId, double a_now);
		void RecordSuccess(std::uint64_t a_steamId);
		std::size_t Sweep(double a_now);

		[[nodiscard]] std::size_t TrackedCount() const noexcept { return entries_.size(); }
		[[nodiscard]] const LockoutOptions& Options() const noexcept { return options_; }

	private:
		struct Entry
		{
			std::uint32_t failures{};
			double lastFailure{};
			double lockedUntil{};
			std::uint32_t blocked{};
		};

		[[nodiscard]] bool IsStale(const Entry& a_entry, double a_now) const noexcept;
		[[nodiscard]] bool MakeRoom(double a_now);

		LockoutOptions options_{};
		std::unordered_map<std::uint64_t, Entry> entries_;
	};
}

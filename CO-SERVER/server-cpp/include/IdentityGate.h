#pragma once

#include "F4TIdentityProtocol.h"
#include "ServerProtocol.h"
#include "SteamIdentityLink.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CommonwealthOnline::Server
{
	class SaveAttestationAuthority;

	struct IdentityIngressDecision
	{
		bool forward{ true };
		std::string code;
		std::string reason;
		std::string profileId;
		// Set when the hello resumed a live connection the caller must end.
		std::optional<std::uint32_t> evictConnectionId;
		nlohmann::json requiredModpack;
		bool consumed{};
		bool rejected{};
		std::optional<nlohmann::json> response;
		// Set only from a verified source; a client claim alone never fills it.
		std::string steamId;
		SteamIdSource steamIdSource{ SteamIdSource::None };
		std::string claimedSteamId;
		bool steamTicketQueued{};
		bool isAdmin{};
		std::string nickname;
		std::string modpackFingerprint;
		nlohmann::json modpackChunks;
		std::string modpackName;
	};

	using ProfileBanLookup = std::function<std::optional<std::string>(std::string_view)>;
	using ProfileAdminLookup = std::function<bool(std::string_view)>;

	struct SteamIdentityHooks
	{
		std::function<std::optional<VerifiedSteamId>(std::uint32_t, std::string_view)> resolve;
		std::function<std::optional<std::uint32_t>(std::uint32_t)> remoteIpv4;
		std::function<std::optional<std::string>(std::string_view)> banLookup;
		std::function<bool(std::uint32_t, std::string_view, std::string_view, std::string_view)> verifyTicket;
		F4T::SteamAdmission::LockoutOptions passwordLockout{};
		std::function<double()> monotonicNow;
	};

	class IdentityGate
	{
	public:
		IdentityGate(
			F4T::Identity::Requirements a_requirements,
			F4T::Identity::TokenService& a_tokens,
			F4T::Identity::ResumeCredentialStore* a_resumeStore = nullptr,
			ProfileBanLookup a_profileBanLookup = {},
			SaveAttestationAuthority* a_saveAuthority = nullptr,
			ProfileAdminLookup a_profileAdminLookup = {},
			SteamIdentityHooks a_steamIdentity = {});

		[[nodiscard]] IdentityIngressDecision InspectInbound(
			std::uint32_t a_connectionId,
			std::span<const std::uint8_t> a_payload,
			double a_nowUnixSeconds,
			bool a_capacityAvailable = true);
		[[nodiscard]] bool EnrichSessionReady(OutboundFrame& a_frame);
		bool CommitReady(std::uint32_t a_connectionId);
		void ConnectionClosed(std::uint32_t a_connectionId);
		[[nodiscard]] bool IsValidated(std::uint32_t a_connectionId) const;
		[[nodiscard]] bool IsAdminConnection(std::uint32_t a_connectionId) const;
		[[nodiscard]] bool TryResetResumeCredential(std::string_view a_profileId, std::string& a_reason);
		void SetServerPasswordSha256(std::optional<std::string> a_passwordSha256);
		void SetResumeLifetimeSeconds(double a_seconds);
		[[nodiscard]] std::size_t RejectedAttestationCount() const;
		[[nodiscard]] std::size_t RateLimitedAttestationCount() const;

	private:
		struct AttestationWindow
		{
			double windowStart{};
			std::uint32_t count{};
		};

		[[nodiscard]] IdentityIngressDecision InspectHello(
			std::uint32_t a_connectionId,
			const nlohmann::json& a_packet,
			double a_nowUnixSeconds,
			bool a_capacityAvailable);
		[[nodiscard]] std::optional<IdentityIngressDecision> CheckSteamPassword(
			std::uint32_t a_connectionId,
			const VerifiedSteamId& a_steam);
		void RecordSteamPasswordOutcome(std::uint32_t a_connectionId, const VerifiedSteamId& a_steam, std::string_view a_code);
		[[nodiscard]] std::optional<IdentityIngressDecision> CheckDirectJoinPassword(
			std::uint32_t a_connectionId,
			std::uint32_t a_remoteIpv4);
		void RecordDirectJoinPasswordOutcome(std::uint32_t a_connectionId, std::uint32_t a_remoteIpv4, std::string_view a_code);
		[[nodiscard]] double MonotonicNow() const;
		[[nodiscard]] static std::string ConnectionKey(std::uint32_t a_connectionId);
		[[nodiscard]] static std::optional<std::uint32_t> ConnectionIdFromKey(std::string_view a_connectionKey);
		[[nodiscard]] IdentityIngressDecision InspectSaveAttestation(
			std::uint32_t a_connectionId,
			const nlohmann::json& a_packet,
			double a_nowUnixSeconds);
		[[nodiscard]] IdentityIngressDecision RejectAttestation(
			std::string a_code,
			std::string a_reason);
		[[nodiscard]] bool SecretMatches(
			std::string_view a_profileId,
			const std::optional<std::string>& a_presentedSecret) const;

		F4T::Identity::Requirements _requirements;
		F4T::Identity::SessionIdentityManager _identities;
		ProfileBanLookup _profileBanLookup;
		SaveAttestationAuthority* _saveAuthority{};
		ProfileAdminLookup _profileAdminLookup;
		SteamIdentityHooks _steamIdentity;
		F4T::SteamAdmission::PasswordLockout _passwordLockout;
		F4T::SteamAdmission::PasswordLockout _directPasswordLockout;
		std::optional<VerifiedSteamId> _helloSteam;
		std::unordered_map<std::uint32_t, std::string> _profileByConnection;
		std::unordered_map<std::uint32_t, std::string> _secretByConnection;
		std::unordered_map<std::uint32_t, AttestationWindow> _attestationWindows;
		std::unordered_set<std::uint32_t> _adminConnections;
		std::unordered_set<std::string> _claimReadmitted;
		std::unordered_set<std::uint32_t> _secretDelivered;
		std::size_t _rejectedAttestations{};
		std::size_t _rateLimitedAttestations{};
	};
}

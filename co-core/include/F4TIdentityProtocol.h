#pragma once

#include "F4TSaveProfileLogic.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace F4T::Identity
{
	inline constexpr std::uint32_t ProtocolVersion = 2;
	inline constexpr std::string_view SaveAttestationCapability = "save-attestation-v1";
	inline constexpr double DefaultResumeLifetimeSeconds = 720.0 * 60.0 * 60.0;
	inline constexpr std::size_t MaximumResumeProfiles = 4096;
	inline constexpr std::size_t kMaximumAdmissionManifestBytes = 48 * 1024;
	inline constexpr std::string_view kModpackPageMarker = " Modpack page: ";
	inline constexpr std::size_t kMaximumModpackUrlBytes = 256;

	using TokenHash = std::array<std::uint8_t, 32>;

	struct Requirements
	{
		std::optional<std::string> clientVersion;
		std::optional<std::string> falloutRuntimeVersion;
		std::optional<std::string> contentManifestFingerprint;
		std::vector<std::string> capabilities;
		double resumeLifetimeSeconds{ DefaultResumeLifetimeSeconds };
		std::optional<std::string> serverPasswordSha256;
		std::optional<std::string> modpackFingerprint;
		std::optional<std::string> modpackManifestJson;
		std::optional<std::string> modpackName;
		std::optional<std::string> modpackUrl;
	};

	struct HelloIdentity
	{
		std::string profileId;
		std::string clientVersion;
		std::string falloutRuntimeVersion;
		std::string contentManifestFingerprint;
		std::vector<std::string> capabilities;
		std::optional<std::string> resumeToken;
		std::optional<F4T::SaveProfileLogic::AttestedSave> saveAttestation;
	};

	struct ValidationResult
	{
		bool accepted{};
		std::string code;
		std::string reason;
		std::optional<HelloIdentity> identity;
		std::optional<std::string> evictConnectionKey;
		nlohmann::json requiredModpack;
		std::string modpackFingerprint;
		nlohmann::json modpackChunks;
		std::string modpackName;

		[[nodiscard]] static ValidationResult Ok(HelloIdentity a_identity);
		[[nodiscard]] static ValidationResult Reject(std::string a_code, std::string a_reason);
	};

	struct PendingIdentity
	{
		std::string connectionKey;
		std::string profileId;
		std::string clientVersion;
		std::string falloutRuntimeVersion;
		std::string contentManifestFingerprint;
		std::vector<std::string> capabilities;
		std::string resumeToken;
		double resumeExpiresAt{};
	};

	struct PersistedResumeCredential
	{
		TokenHash tokenHash{};
		double expiresAt{};
		std::optional<TokenHash> previousTokenHash;
		bool confirmed{ true };
	};

	class ResumeCredentialStore
	{
	public:
		virtual ~ResumeCredentialStore() = default;
		[[nodiscard]] virtual std::optional<PersistedResumeCredential> GetResumeCredential(std::string_view a_profileId) = 0;
		virtual void SaveResumeCredential(
			std::string_view a_profileId,
			const TokenHash& a_tokenHash,
			const std::optional<TokenHash>& a_previousTokenHash,
			bool a_confirmed,
			double a_expiresAt) = 0;
		[[nodiscard]] virtual bool DeleteResumeCredential(std::string_view a_profileId) = 0;
	};

	class TokenService
	{
	public:
		virtual ~TokenService() = default;
		[[nodiscard]] virtual std::string CreateToken() = 0;
		[[nodiscard]] virtual TokenHash HashToken(std::string_view a_token) = 0;
	};

	[[nodiscard]] bool FixedTimeEquals(const TokenHash& a_left, const TokenHash& a_right) noexcept;
	[[nodiscard]] bool IsValidProfileId(std::string_view a_profileId) noexcept;
	[[nodiscard]] std::string_view ModpackUrlHost(std::string_view a_url) noexcept;
	[[nodiscard]] bool IsAllowedModpackPageUrl(std::string_view a_url) noexcept;
	[[nodiscard]] std::optional<std::string> NormalizeModpackUrl(const std::optional<std::string>& a_value);
	[[nodiscard]] std::string ExtractModpackPageUrl(std::string& a_reason);
	[[nodiscard]] ValidationResult NormalizeHello(const nlohmann::json& a_hello, const Requirements& a_requirements);
	[[nodiscard]] std::optional<ValidationResult> CheckServerPassword(const nlohmann::json& a_hello, const Requirements& a_requirements);

	class SessionIdentityManager
	{
	public:
		SessionIdentityManager(
			Requirements a_requirements,
			TokenService& a_tokens,
			ResumeCredentialStore* a_resumeStore = nullptr);

		[[nodiscard]] ValidationResult ValidateAndReserve(
			std::string a_connectionKey,
			const nlohmann::json& a_hello,
			double a_nowUnixSeconds,
			bool a_capacityAvailable = true);
		[[nodiscard]] bool IsValidated(std::string_view a_connectionKey) const;
		[[nodiscard]] std::optional<PendingIdentity> PendingForReady(std::string_view a_connectionKey) const;
		bool CommitReady(std::string_view a_connectionKey);
		bool ConfirmReady(std::string_view a_connectionKey);
		void ConnectionClosed(std::string_view a_connectionKey);
		[[nodiscard]] bool TryResetResumeCredential(std::string_view a_profileId, std::string& a_reason);
		[[nodiscard]] std::optional<PersistedResumeCredential> ResumeForProfile(std::string_view a_profileId) const;
		[[nodiscard]] bool AwaitingConfirmation(std::string_view a_profileId, double a_nowUnixSeconds) const;

	private:
		struct ResumeRecord
		{
			TokenHash tokenHash{};
			double expiresAt{};
			std::optional<TokenHash> previousTokenHash;
			bool confirmed{ true };
			std::optional<std::string> activeConnectionKey;
		};

		[[nodiscard]] static bool RecordAccepts(const ResumeRecord& a_record, const TokenHash& a_candidate);

		Requirements _requirements;
		TokenService& _tokens;
		ResumeCredentialStore* _resumeStore{};
		mutable std::mutex _gate;
		std::unordered_map<std::string, ResumeRecord> _resumeByProfile;
		std::unordered_map<std::string, PendingIdentity> _pendingByConnection;
		std::unordered_map<std::string, std::string> _reservedProfileToConnection;
		std::unordered_map<std::string, std::string> _activeProfileByConnection;
	};
}

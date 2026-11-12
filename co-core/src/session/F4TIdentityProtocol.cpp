#include "F4TIdentityProtocol.h"
#include "F4TContentManifest.h"
#include "F4TModpackChunks.h"
#include "F4TStateBagProtocol.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace F4T::Identity
{
	namespace
	{
		using Json = nlohmann::json;

		std::string Trim(std::string_view a_value)
		{
			std::size_t begin = 0;
			std::size_t end = a_value.size();
			while (begin < end && std::isspace(static_cast<unsigned char>(a_value[begin])) != 0) {
				++begin;
			}
			while (end > begin && std::isspace(static_cast<unsigned char>(a_value[end - 1])) != 0) {
				--end;
			}
			return std::string{ a_value.substr(begin, end - begin) };
		}

		std::optional<std::uint32_t> ReadUInt32(const Json& a_value)
		{
			if (a_value.is_boolean()) {
				return std::nullopt;
			}
			try {
				if (a_value.is_number_unsigned()) {
					const auto value = a_value.get<std::uint64_t>();
					if (value <= UINT32_MAX) {
						return static_cast<std::uint32_t>(value);
					}
				}
				if (a_value.is_number_integer()) {
					const auto value = a_value.get<std::int64_t>();
					if (value >= 0 && static_cast<std::uint64_t>(value) <= UINT32_MAX) {
						return static_cast<std::uint32_t>(value);
					}
				}
			} catch (...) {
			}
			return std::nullopt;
		}

		std::optional<std::vector<std::uint32_t>> ParseDottedVersion(std::string_view a_value)
		{
			std::vector<std::uint32_t> parts;
			std::size_t begin = 0;
			while (true) {
				const auto dot = a_value.find('.', begin);
				const auto end = dot == std::string_view::npos ? a_value.size() : dot;
				std::uint32_t value = 0;
				const auto result = std::from_chars(a_value.data() + begin, a_value.data() + end, value);
				if (result.ec != std::errc{} || result.ptr != a_value.data() + end) {
					return std::nullopt;
				}
				parts.push_back(value);
				if (dot == std::string_view::npos) {
					return parts;
				}
				begin = dot + 1;
			}
		}

		int CompareDottedVersions(const std::vector<std::uint32_t>& a_left, const std::vector<std::uint32_t>& a_right)
		{
			const auto count = std::max(a_left.size(), a_right.size());
			for (std::size_t index = 0; index < count; ++index) {
				const std::uint32_t left = index < a_left.size() ? a_left[index] : 0;
				const std::uint32_t right = index < a_right.size() ? a_right[index] : 0;
				if (left != right) {
					return left < right ? -1 : 1;
				}
			}
			return 0;
		}

		const Json* FindValue(const Json& a_object, std::string_view a_name)
		{
			const auto found = a_object.find(a_name);
			return found == a_object.end() ? nullptr : std::addressof(*found);
		}

		const Json* FindAlias(const Json& a_object, std::string_view a_primary, std::string_view a_alias)
		{
			if (const auto* primary = FindValue(a_object, a_primary); primary != nullptr && !primary->is_null()) {
				return primary;
			}
			return FindValue(a_object, a_alias);
		}

		std::optional<std::string> CleanBoundedString(const Json* a_node, std::size_t a_minimum, std::size_t a_maximum)
		{
			if (a_node == nullptr || !a_node->is_string()) {
				return std::nullopt;
			}
			const auto value = Trim(a_node->get_ref<const std::string&>());
			if (value.size() < a_minimum || value.size() > a_maximum) {
				return std::nullopt;
			}
			for (const unsigned char ch : value) {
				if (ch < 0x20 || ch == 0x7f) {
					return std::nullopt;
				}
			}
			return value;
		}

		bool IsAsciiLetterOrDigit(char a_value) noexcept
		{
			return (a_value >= 'a' && a_value <= 'z') ||
			       (a_value >= 'A' && a_value <= 'Z') ||
			       (a_value >= '0' && a_value <= '9');
		}

		std::optional<std::string> CleanProfileId(const Json* a_node)
		{
			const auto value = CleanBoundedString(a_node, 8, 128);
			if (!value) {
				return std::nullopt;
			}
			for (const char ch : *value) {
				if (!IsAsciiLetterOrDigit(ch) && ch != '-' && ch != '_' && ch != '.' && ch != ':' && ch != '@') {
					return std::nullopt;
				}
			}
			return value;
		}

		std::optional<std::string> CleanProfileId(std::string_view a_value)
		{
			Json node = std::string{ a_value };
			return CleanProfileId(std::addressof(node));
		}

		std::optional<std::string> CleanFingerprint(const Json* a_node)
		{
			auto value = CleanBoundedString(a_node, 64, 64);
			if (!value) {
				return std::nullopt;
			}
			for (auto& ch : *value) {
				const auto byte = static_cast<unsigned char>(ch);
				if (!((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') || (byte >= 'A' && byte <= 'F'))) {
					return std::nullopt;
				}
				if (ch >= 'A' && ch <= 'F') {
					ch = static_cast<char>(ch - 'A' + 'a');
				}
			}
			return value;
		}

		std::optional<std::vector<std::string>> CleanCapabilities(const Json* a_node)
		{
			// Capabilities are optional claims; absent/empty means none (shape rules below apply only when present).
			if (a_node == nullptr) {
				return std::vector<std::string>{};
			}
			if (!a_node->is_array() || a_node->size() > 64) {
				return std::nullopt;
			}
			std::vector<std::string> result;
			result.reserve(a_node->size());
			std::unordered_set<std::string> unique;
			for (const auto& item : *a_node) {
				auto value = CleanBoundedString(std::addressof(item), 1, 64);
				if (!value || !unique.insert(*value).second) {
					return std::nullopt;
				}
				result.push_back(std::move(*value));
			}
			return result;
		}

		std::optional<std::string> NormalizeOptionalRequirement(const std::optional<std::string>& a_value)
		{
			if (!a_value) {
				return std::nullopt;
			}
			const auto normalized = Trim(*a_value);
			return normalized.empty() ? std::nullopt : std::optional{ normalized };
		}

		std::optional<std::string> NormalizeFingerprintRequirement(const std::optional<std::string>& a_value)
		{
			auto normalized = NormalizeOptionalRequirement(a_value);
			if (!normalized) {
				return std::nullopt;
			}
			for (auto& ch : *normalized) {
				if (ch >= 'A' && ch <= 'F') {
					ch = static_cast<char>(ch - 'A' + 'a');
				}
			}
			return normalized;
		}

		Requirements NormalizeRequirements(Requirements a_requirements)
		{
			a_requirements.clientVersion = NormalizeOptionalRequirement(a_requirements.clientVersion);
			a_requirements.falloutRuntimeVersion = NormalizeOptionalRequirement(a_requirements.falloutRuntimeVersion);
			a_requirements.contentManifestFingerprint = NormalizeFingerprintRequirement(a_requirements.contentManifestFingerprint);
			a_requirements.modpackFingerprint = NormalizeFingerprintRequirement(a_requirements.modpackFingerprint);
			a_requirements.modpackViewlessFingerprint = NormalizeFingerprintRequirement(a_requirements.modpackViewlessFingerprint);
			a_requirements.modpackUrl = NormalizeModpackUrl(a_requirements.modpackUrl);
			return a_requirements;
		}

		bool ContainsCapability(const std::vector<std::string>& a_capabilities, std::string_view a_required)
		{
			return std::find(a_capabilities.begin(), a_capabilities.end(), a_required) != a_capabilities.end();
		}

		bool FixedTimeStringEquals(std::string_view a_left, std::string_view a_right) noexcept
		{
			if (a_left.size() != a_right.size()) {
				return false;
			}
			volatile std::uint8_t difference = 0;
			for (std::size_t index = 0; index < a_left.size(); ++index) {
				difference = static_cast<std::uint8_t>(difference | static_cast<std::uint8_t>(a_left[index] ^ a_right[index]));
			}
			return difference == 0;
		}
	}

	ValidationResult ValidationResult::Ok(HelloIdentity a_identity)
	{
		return ValidationResult{ true, {}, {}, std::move(a_identity), std::nullopt, {} };
	}

	ValidationResult ValidationResult::Reject(std::string a_code, std::string a_reason)
	{
		return ValidationResult{ false, std::move(a_code), std::move(a_reason), std::nullopt, std::nullopt, {} };
	}

	bool FixedTimeEquals(const TokenHash& a_left, const TokenHash& a_right) noexcept
	{
		volatile std::uint8_t difference = 0;
		for (std::size_t index = 0; index < a_left.size(); ++index) {
			difference = static_cast<std::uint8_t>(difference | static_cast<std::uint8_t>(a_left[index] ^ a_right[index]));
		}
		return difference == 0;
	}

	bool IsValidProfileId(std::string_view a_profileId) noexcept
	{
		return CleanProfileId(a_profileId).has_value();
	}

	std::optional<ValidationResult> CheckServerPassword(const nlohmann::json& a_hello, const Requirements& a_requirements)
	{
		if (!a_requirements.serverPasswordSha256) {
			return std::nullopt;
		}
		const auto* password = FindValue(a_hello, "serverPassword");
		if (password == nullptr || password->is_null()) {
			return ValidationResult::Reject("password_required", "This server requires a password.");
		}
		if (!password->is_string() || password->get_ref<const std::string&>().size() > 128 ||
			!FixedTimeStringEquals(F4T::ContentManifest::Sha256Hex(password->get_ref<const std::string&>()), *a_requirements.serverPasswordSha256)) {
			return ValidationResult::Reject("password_invalid", "serverPassword is invalid.");
		}
		return std::nullopt;
	}

	std::string_view ModpackUrlHost(std::string_view a_url) noexcept
	{
		const auto schemeEnd = a_url.find("://");
		if (schemeEnd == std::string_view::npos) {
			return {};
		}
		const auto afterScheme = a_url.substr(schemeEnd + 3);
		const auto authority = afterScheme.substr(0, afterScheme.find_first_of("/?#"));
		if (authority.find('@') != std::string_view::npos) {
			return {};
		}
		const auto host = authority.substr(0, authority.find(':'));
		for (const unsigned char character : host) {
			if (character <= 0x20 || character >= 0x7f || character == '\\') {
				return {};
			}
		}
		return host;
	}

	bool IsAllowedModpackPageUrl(std::string_view a_url) noexcept
	{
		if (a_url.empty() || a_url.size() > kMaximumModpackUrlBytes) {
			return false;
		}
		for (const unsigned char character : a_url) {
			if (character <= 0x20 || character >= 0x7f) {
				return false;
			}
		}
		const auto schemeEnd = a_url.find("://");
		if (schemeEnd == std::string_view::npos) {
			return false;
		}
		const auto scheme = a_url.substr(0, schemeEnd);
		const auto host = ModpackUrlHost(a_url);
		if (host.empty()) {
			return false;
		}
		if (scheme == "https") {
			return true;
		}
		return scheme == "http" && (host == "127.0.0.1" || host == "localhost");
	}

	std::optional<std::string> NormalizeModpackUrl(const std::optional<std::string>& a_value)
	{
		if (!a_value) {
			return std::nullopt;
		}
		const auto trimmed = Trim(*a_value);
		if (!IsAllowedModpackPageUrl(trimmed)) {
			return std::nullopt;
		}
		return trimmed;
	}

	std::string ExtractModpackPageUrl(std::string& a_reason)
	{
		const auto marker = a_reason.find(kModpackPageMarker);
		if (marker == std::string::npos) {
			return {};
		}
		const auto start = marker + kModpackPageMarker.size();
		const auto end = a_reason.find_first_of(" \n\r\t", start);
		const auto stop = end == std::string::npos ? a_reason.size() : end;
		const std::string_view candidate{ a_reason.data() + start, stop - start };
		std::string url;
		if (IsAllowedModpackPageUrl(candidate)) {
			url = std::string{ candidate };
		}
		a_reason.erase(marker, stop - marker);
		return url;
	}

	bool ReceivesFullModpack(const nlohmann::json& a_hello, bool a_hasViewless)
	{
		if (!a_hasViewless) {
			return true;
		}
		const auto capabilities = a_hello.find("capabilities");
		return capabilities != a_hello.end() && capabilities->is_array() &&
			std::any_of(capabilities->begin(), capabilities->end(), [](const Json& a_capability) {
				return a_capability.is_string() && a_capability.get_ref<const std::string&>() == F4T::StateBagProtocol::kCapability;
			});
	}

	ValidationResult NormalizeHello(const nlohmann::json& a_hello, const Requirements& a_requirements)
	{
		if (!a_hello.is_object()) {
			return ValidationResult::Reject("identity_required", "hello requires identity fields.");
		}
		const auto protocolNode = FindValue(a_hello, "protocolVersion");
		const auto protocolVersion = protocolNode == nullptr ? std::nullopt : ReadUInt32(*protocolNode);
		if (!protocolVersion || *protocolVersion == 0 || *protocolVersion > 65535 || *protocolVersion != ProtocolVersion) {
			return ValidationResult::Reject("protocol_mismatch", "Server requires protocol 2.");
		}

		const auto clientVersion = CleanBoundedString(FindAlias(a_hello, "clientVersion", "pluginVersion"), 1, 64);
		if (!clientVersion) {
			return ValidationResult::Reject("identity_required", "hello requires clientVersion.");
		}
		const auto runtimeVersion = CleanBoundedString(FindValue(a_hello, "falloutRuntimeVersion"), 1, 64);
		if (!runtimeVersion) {
			return ValidationResult::Reject("identity_required", "hello requires falloutRuntimeVersion.");
		}
		const auto profileId = CleanProfileId(FindAlias(a_hello, "profileId", "characterId"));
		if (!profileId) {
			return ValidationResult::Reject("identity_required", "hello requires a stable profileId.");
		}
		const auto fingerprint = CleanFingerprint(FindAlias(a_hello, "contentManifestFingerprint", "contentFingerprint"));
		if (!fingerprint) {
			return ValidationResult::Reject("identity_required", "hello requires a 64-character SHA-256 contentManifestFingerprint.");
		}
		// Capabilities are optional claims; only a present, malformed array rejects.
		const auto capabilities = CleanCapabilities(FindValue(a_hello, "capabilities"));
		if (!capabilities) {
			return ValidationResult::Reject("identity_required", "hello capabilities, when present, must be an array of at most 64 unique bounded strings.");
		}

		std::optional<F4T::SaveProfileLogic::AttestedSave> saveAttestation;
		if (const auto* attestation = FindValue(a_hello, "saveAttestation"); attestation != nullptr && !attestation->is_null()) {
			if (!attestation->is_object()) {
				return ValidationResult::Reject("save_attestation_invalid", "saveAttestation, when present, must be an object.");
			}
			const auto saveFile = CleanBoundedString(FindValue(*attestation, "saveFile"), 1, 256);
			const auto sha256 = CleanFingerprint(FindValue(*attestation, "sha256"));
			if (!saveFile || !sha256) {
				return ValidationResult::Reject("save_attestation_invalid", "saveAttestation requires a save name and a 64-character SHA-256 hash.");
			}
			saveAttestation = F4T::SaveProfileLogic::AttestedSave{ *saveFile, *sha256, 0 };
		}

		const auto requirements = NormalizeRequirements(a_requirements);
		if (requirements.clientVersion && *clientVersion != *requirements.clientVersion) {
			const auto parsedClient = ParseDottedVersion(*clientVersion);
			const auto parsedRequired = ParseDottedVersion(*requirements.clientVersion);
			const auto order = parsedClient && parsedRequired ? CompareDottedVersions(*parsedClient, *parsedRequired) : 0;
			if (order < 0) {
				return ValidationResult::Reject("client_version_mismatch", "You are running an older version of Commonwealth Online (" + *clientVersion + "). Please update to " + *requirements.clientVersion + " to join.");
			}
			if (order > 0) {
				return ValidationResult::Reject("client_version_mismatch", "This server runs Commonwealth Online " + *requirements.clientVersion + ", but you are running a newer version (" + *clientVersion + "). Use version " + *requirements.clientVersion + " to join.");
			}
			return ValidationResult::Reject("client_version_mismatch", "Server requires Commonwealth Online version " + *requirements.clientVersion + " (you have " + *clientVersion + ").");
		}
		if (requirements.falloutRuntimeVersion && *runtimeVersion != *requirements.falloutRuntimeVersion) {
			return ValidationResult::Reject("runtime_mismatch", "Server requires Fallout runtime " + *requirements.falloutRuntimeVersion + ".");
		}
		if (requirements.contentManifestFingerprint && *fingerprint != *requirements.contentManifestFingerprint) {
			return ValidationResult::Reject("content_mismatch", "Client content manifest does not match this server.");
		}
		for (const auto& required : requirements.capabilities) {
			if (!ContainsCapability(*capabilities, required)) {
				return ValidationResult::Reject("capability_mismatch", "Client is missing required capability '" + required + "'.");
			}
		}
		if (const auto passwordRejection = CheckServerPassword(a_hello, requirements)) {
			return *passwordRejection;
		}
		const bool fullManifest = ReceivesFullModpack(a_hello, requirements.modpackViewlessFingerprint.has_value() || requirements.modpackViewsOnly);
		if (requirements.modpackFingerprint && (fullManifest || !requirements.modpackViewsOnly)) {
			const auto& expectedFingerprint = fullManifest ? requirements.modpackFingerprint : requirements.modpackViewlessFingerprint;
			const auto& expectedManifestJson = fullManifest ? requirements.modpackManifestJson : requirements.modpackViewlessManifestJson;
			const auto* modpack = FindValue(a_hello, "modpackFingerprint");
			if (modpack == nullptr || !modpack->is_string() || modpack->get_ref<const std::string&>().size() > 128 ||
				modpack->get_ref<const std::string&>() != *expectedFingerprint) {
				auto rejection = ValidationResult::Reject("modpack_mismatch", "Your modpack does not match this server. The server manifest is in requiredModpack.");
				rejection.modpackFingerprint = *expectedFingerprint;
				rejection.modpackName = requirements.modpackName.value_or("");
				if (expectedManifestJson) {
					const auto& manifestText = *expectedManifestJson;
					if (manifestText.size() <= kMaximumAdmissionManifestBytes) {
						auto manifest = Json::parse(manifestText, nullptr, false);
						if (!manifest.is_discarded()) {
							rejection.requiredModpack = std::move(manifest);
						}
					} else if (ContainsCapability(*capabilities, F4T::ModpackChunks::kCapabilityName)) {
						if (const auto description = F4T::ModpackChunks::Describe(manifestText)) {
							rejection.modpackChunks = Json{
								{ "totalBytes", description->totalBytes },
								{ "chunkCount", description->chunkCount },
								{ "sha256", description->sha256 }
							};
						}
					} else {
						rejection.reason = "This server's modpack manifest is larger than one admission frame and this client cannot receive it in chunks. Update Commonwealth Online to join.";
					}
				}
				if (requirements.modpackUrl) {
					rejection.reason += kModpackPageMarker;
					rejection.reason += *requirements.modpackUrl;
				}
				return rejection;
			}
		}

		return ValidationResult::Ok(HelloIdentity{
			.profileId = *profileId,
			.clientVersion = *clientVersion,
			.falloutRuntimeVersion = *runtimeVersion,
			.contentManifestFingerprint = *fingerprint,
			.capabilities = *capabilities,
			.resumeToken = CleanBoundedString(FindValue(a_hello, "resumeToken"), 1, 256),
			.saveAttestation = std::move(saveAttestation)
		});
	}

	SessionIdentityManager::SessionIdentityManager(
		Requirements a_requirements,
		TokenService& a_tokens,
		ResumeCredentialStore* a_resumeStore) :
		_requirements(NormalizeRequirements(std::move(a_requirements))),
		_tokens(a_tokens),
		_resumeStore(a_resumeStore)
	{}

	bool SessionIdentityManager::TryResetResumeCredential(std::string_view a_profileId, std::string& a_reason)
	{
		const auto normalized = CleanProfileId(a_profileId);
		if (!normalized) {
			a_reason = "profileId is invalid.";
			return false;
		}

		std::scoped_lock lock(_gate);
		if (_reservedProfileToConnection.contains(*normalized)) {
			a_reason = "The profile is currently connected or completing a handshake.";
			return false;
		}
		for (const auto& [connectionKey, profileId] : _activeProfileByConnection) {
			(void)connectionKey;
			if (profileId == *normalized) {
				a_reason = "The profile is currently connected or completing a handshake.";
				return false;
			}
		}

		bool removed = _resumeByProfile.erase(*normalized) > 0;
		removed = _readmissionByProfile.erase(*normalized) > 0 || removed;
		if (_resumeStore != nullptr) {
			removed = _resumeStore->DeleteResumeCredential(*normalized) || removed;
		}
		if (!removed) {
			a_reason = "No resume credential exists for that profile.";
			return false;
		}
		a_reason = "Resume credential reset. The next validated hello for this profile will receive a new token.";
		return true;
	}

	std::optional<PersistedResumeCredential> SessionIdentityManager::ResumeForProfile(std::string_view a_profileId) const
	{
		const std::string profileId{ a_profileId };
		std::scoped_lock lock(_gate);
		const auto found = _resumeByProfile.find(profileId);
		if (found == _resumeByProfile.end()) {
			return std::nullopt;
		}
		return PersistedResumeCredential{
			found->second.tokenHash,
			found->second.expiresAt,
			found->second.previousTokenHash,
			found->second.confirmed
		};
	}

	bool SessionIdentityManager::AwaitingConfirmation(std::string_view a_profileId, double a_nowUnixSeconds) const
	{
		const std::string profileId{ a_profileId };
		std::scoped_lock lock(_gate);
		if (const auto found = _resumeByProfile.find(profileId); found != _resumeByProfile.end()) {
			return !found->second.confirmed && found->second.expiresAt > a_nowUnixSeconds;
		}
		if (const auto readmission = _readmissionByProfile.find(profileId); readmission != _readmissionByProfile.end()) {
			return readmission->second.expiresAt > a_nowUnixSeconds;
		}
		if (_resumeStore == nullptr) {
			return false;
		}
		const auto persisted = _resumeStore->GetResumeCredential(profileId);
		return persisted && !persisted->confirmed && persisted->expiresAt > a_nowUnixSeconds;
	}
}

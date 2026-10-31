#include "F4TIdentityProtocol.h"

#include <algorithm>
#include <utility>

namespace F4T::Identity
{
	bool SessionIdentityManager::RecordAccepts(const ResumeRecord& a_record, const TokenHash& a_candidate)
	{
		return FixedTimeEquals(a_record.tokenHash, a_candidate) ||
			(a_record.previousTokenHash && FixedTimeEquals(*a_record.previousTokenHash, a_candidate));
	}

	ValidationResult SessionIdentityManager::ValidateAndReserve(
		std::string a_connectionKey,
		const nlohmann::json& a_hello,
		double a_nowUnixSeconds,
		bool a_capacityAvailable)
	{
		auto validation = NormalizeHello(a_hello, _requirements);
		if (!validation.accepted || !validation.identity) {
			return validation;
		}
		const auto identity = *validation.identity;

		std::scoped_lock lock(_gate);
		if (_pendingByConnection.contains(a_connectionKey) || _activeProfileByConnection.contains(a_connectionKey)) {
			return ValidationResult::Reject("duplicate_hello", "This connection already completed identity validation.");
		}
		if (const auto reserved = _reservedProfileToConnection.find(identity.profileId);
			reserved != _reservedProfileToConnection.end() && reserved->second != a_connectionKey) {
			return ValidationResult::Reject("duplicate_identity", "That profile is already connecting.");
		}

		auto resumeIt = _resumeByProfile.find(identity.profileId);
		std::optional<ResumeRecord> loadedResume;
		if (resumeIt == _resumeByProfile.end()) {
			if (_resumeStore != nullptr) {
				if (const auto persisted = _resumeStore->GetResumeCredential(identity.profileId)) {
					loadedResume = ResumeRecord{
						.tokenHash = persisted->tokenHash,
						.expiresAt = persisted->expiresAt,
						.previousTokenHash = persisted->previousTokenHash,
						.confirmed = persisted->confirmed,
						.activeConnectionKey = std::nullopt
					};
				}
			}
			if (!loadedResume && _resumeByProfile.size() >= MaximumResumeProfiles) {
				return ValidationResult::Reject(
					"identity_capacity",
					"Server has reached its resume identity limit. An administrator must reset unused identities before new profiles can join.");
			}
		}

		std::optional<std::string> evictConnectionKey;
		std::optional<TokenHash> authenticatedTokenHash;
		const auto* resumeRecord = resumeIt != _resumeByProfile.end() ? &resumeIt->second :
			(loadedResume ? &*loadedResume : nullptr);
		if (resumeRecord) {
			const auto& resume = *resumeRecord;
			if (resume.activeConnectionKey && *resume.activeConnectionKey != a_connectionKey) {
				if (!identity.resumeToken) {
					return ValidationResult::Reject("duplicate_identity", "That profile is already connected.");
				}
				const auto presented = _tokens.HashToken(*identity.resumeToken);
				if (!RecordAccepts(resume, presented)) {
					return ValidationResult::Reject("invalid_resume", "The resume token is invalid for this profile.");
				}
				authenticatedTokenHash = presented;
				evictConnectionKey = *resume.activeConnectionKey;
			}
			if (resume.expiresAt <= a_nowUnixSeconds) {
				return ValidationResult::Reject(
					"resume_expired",
					"The resume credential for this profile has expired and must be reset by the server administrator.");
			}
			if (identity.resumeToken) {
				if (!authenticatedTokenHash) {
					const auto presented = _tokens.HashToken(*identity.resumeToken);
					if (!RecordAccepts(resume, presented)) {
						return ValidationResult::Reject("invalid_resume", "The resume token is invalid for this profile.");
					}
					authenticatedTokenHash = presented;
				}
			} else if (resume.confirmed || resume.previousTokenHash) {
				return ValidationResult::Reject("resume_required", "This profile requires its server-issued resume token.");
			}
		} else if (identity.resumeToken) {
			return ValidationResult::Reject("invalid_resume", "The server does not recognize this resume credential.");
		}

		if (!a_capacityAvailable && !evictConnectionKey) {
			return ValidationResult::Reject("server_full", "Server is full.");
		}

		auto eviction = _resumeByProfile.end();
		if (resumeIt == _resumeByProfile.end() && _resumeByProfile.size() >= MaximumResumeProfiles) {
			eviction = std::find_if(_resumeByProfile.begin(), _resumeByProfile.end(), [this](const auto& entry) {
				const auto& record = entry.second;
				return record.confirmed && !record.previousTokenHash && !record.activeConnectionKey &&
					!_reservedProfileToConnection.contains(entry.first);
			});
			if (eviction == _resumeByProfile.end()) {
				return ValidationResult::Reject("identity_capacity", "All cached resume identities are in use or awaiting confirmation. Try again shortly.");
			}
		}

		const auto newToken = _tokens.CreateToken();
		const auto expiresAt = a_nowUnixSeconds + _requirements.resumeLifetimeSeconds;
		const std::optional<TokenHash> previousTokenHash = authenticatedTokenHash;
		const auto newTokenHash = _tokens.HashToken(newToken);
		if (_resumeStore != nullptr) {
			try {
				_resumeStore->SaveResumeCredential(identity.profileId, newTokenHash, previousTokenHash, false, expiresAt);
			} catch (...) {
				return ValidationResult::Reject(
					"resume_storage_failed",
					"The server could not store a resume credential for this profile. Try again shortly.");
			}
		}

		if (resumeIt != _resumeByProfile.end()) {
			resumeIt->second.tokenHash = newTokenHash;
			resumeIt->second.previousTokenHash = previousTokenHash;
			resumeIt->second.expiresAt = expiresAt;
			resumeIt->second.confirmed = false;
		} else {
			if (eviction != _resumeByProfile.end()) {
				_resumeByProfile.erase(eviction);
			}
			_resumeByProfile.emplace(identity.profileId, ResumeRecord{
				.tokenHash = newTokenHash,
				.expiresAt = expiresAt,
				.previousTokenHash = previousTokenHash,
				.confirmed = false,
				.activeConnectionKey = std::nullopt
			});
		}

		_pendingByConnection[a_connectionKey] = PendingIdentity{
			.connectionKey = a_connectionKey,
			.profileId = identity.profileId,
			.clientVersion = identity.clientVersion,
			.falloutRuntimeVersion = identity.falloutRuntimeVersion,
			.contentManifestFingerprint = identity.contentManifestFingerprint,
			.capabilities = identity.capabilities,
			.resumeToken = newToken,
			.resumeExpiresAt = expiresAt
		};
		_reservedProfileToConnection[identity.profileId] = a_connectionKey;
		if (evictConnectionKey) {
			_activeProfileByConnection.erase(*evictConnectionKey);
			_pendingByConnection.erase(*evictConnectionKey);
			resumeIt->second.activeConnectionKey.reset();
			validation.evictConnectionKey = std::move(evictConnectionKey);
		}
		return validation;
	}

	bool SessionIdentityManager::IsValidated(std::string_view a_connectionKey) const
	{
		const std::string key{ a_connectionKey };
		std::scoped_lock lock(_gate);
		return _pendingByConnection.contains(key) || _activeProfileByConnection.contains(key);
	}

	std::optional<PendingIdentity> SessionIdentityManager::PendingForReady(std::string_view a_connectionKey) const
	{
		const std::string key{ a_connectionKey };
		std::scoped_lock lock(_gate);
		const auto found = _pendingByConnection.find(key);
		return found == _pendingByConnection.end() ? std::nullopt : std::optional{ found->second };
	}

	bool SessionIdentityManager::CommitReady(std::string_view a_connectionKey)
	{
		const std::string key{ a_connectionKey };
		std::scoped_lock lock(_gate);
		const auto found = _pendingByConnection.find(key);
		if (found == _pendingByConnection.end()) {
			return _activeProfileByConnection.contains(key);
		}
		const auto& pending = found->second;
		const auto resume = _resumeByProfile.find(pending.profileId);
		if (resume == _resumeByProfile.end()) {
			return false;
		}

		_reservedProfileToConnection.erase(pending.profileId);
		resume->second.activeConnectionKey = key;
		_activeProfileByConnection[key] = pending.profileId;
		return true;
	}

	bool SessionIdentityManager::ConfirmReady(std::string_view a_connectionKey)
	{
		const std::string key{ a_connectionKey };
		std::scoped_lock lock(_gate);
		const auto found = _pendingByConnection.find(key);
		if (found == _pendingByConnection.end()) {
			return _activeProfileByConnection.contains(key);
		}
		const auto active = _activeProfileByConnection.find(key);
		if (active == _activeProfileByConnection.end() || active->second != found->second.profileId) {
			return false;
		}

		const auto pending = found->second;
		const auto tokenHash = _tokens.HashToken(pending.resumeToken);
		if (_resumeStore != nullptr) {
			try {
				_resumeStore->SaveResumeCredential(pending.profileId, tokenHash, std::nullopt, true, pending.resumeExpiresAt);
			} catch (...) {
				return false;
			}
		}

		_pendingByConnection.erase(found);
		_resumeByProfile[pending.profileId] = ResumeRecord{
			.tokenHash = tokenHash,
			.expiresAt = pending.resumeExpiresAt,
			.previousTokenHash = std::nullopt,
			.confirmed = true,
			.activeConnectionKey = key
		};
		return true;
	}

	void SessionIdentityManager::ConnectionClosed(std::string_view a_connectionKey)
	{
		const std::string key{ a_connectionKey };
		std::scoped_lock lock(_gate);
		if (const auto pending = _pendingByConnection.find(key); pending != _pendingByConnection.end()) {
			_reservedProfileToConnection.erase(pending->second.profileId);
			_pendingByConnection.erase(pending);
		}
		const auto active = _activeProfileByConnection.find(key);
		if (active == _activeProfileByConnection.end()) {
			return;
		}
		const auto profileId = active->second;
		_activeProfileByConnection.erase(active);
		const auto resume = _resumeByProfile.find(profileId);
		if (resume != _resumeByProfile.end() && resume->second.activeConnectionKey && *resume->second.activeConnectionKey == key) {
			resume->second.activeConnectionKey.reset();
		}
	}
}

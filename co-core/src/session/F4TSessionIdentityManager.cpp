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
		}

		std::optional<std::string> evictConnectionKey;
		std::optional<TokenHash> authenticatedTokenHash;
		const auto* resumeRecord = resumeIt != _resumeByProfile.end() ? &resumeIt->second :
			(loadedResume ? &*loadedResume : nullptr);
		const auto presentedHash = identity.resumeToken ? std::optional{ _tokens.HashToken(*identity.resumeToken) } : std::nullopt;
		const auto readmission = _readmissionByProfile.find(identity.profileId);
		const bool evictedTokenProven = !resumeRecord && presentedHash && readmission != _readmissionByProfile.end() &&
			readmission->second.evictedTokenHash && readmission->second.expiresAt > a_nowUnixSeconds &&
			FixedTimeEquals(*readmission->second.evictedTokenHash, *presentedHash);
		const bool joinedBefore = resumeRecord &&
			(resumeRecord->confirmed || resumeRecord->previousTokenHash || (presentedHash && RecordAccepts(*resumeRecord, *presentedHash)));
		if (_requirements.requireKnownProfile && !joinedBefore) {
			return ValidationResult::Reject(
				"unknown_profile",
				"This server only accepts profiles that have joined before.");
		}
		if (resumeRecord) {
			const auto& resume = *resumeRecord;
			if (resume.activeConnectionKey && *resume.activeConnectionKey != a_connectionKey) {
				if (!identity.resumeToken) {
					return ValidationResult::Reject("duplicate_identity", "That profile is already connected.");
				}
				if (!RecordAccepts(resume, *presentedHash)) {
					return ValidationResult::Reject("invalid_resume", "The resume token is invalid for this profile.");
				}
				authenticatedTokenHash = *presentedHash;
				evictConnectionKey = *resume.activeConnectionKey;
			}
			if (resume.expiresAt <= a_nowUnixSeconds) {
				return ValidationResult::Reject(
					"resume_expired",
					"The resume credential for this profile has expired and must be reset by the server administrator.");
			}
			if (identity.resumeToken) {
				if (!authenticatedTokenHash) {
					if (!RecordAccepts(resume, *presentedHash)) {
						return ValidationResult::Reject("invalid_resume", "The resume token is invalid for this profile.");
					}
					authenticatedTokenHash = *presentedHash;
				}
			} else if (resume.confirmed || resume.previousTokenHash) {
				return ValidationResult::Reject("resume_required", "This profile requires its server-issued resume token.");
			}
		} else if (evictedTokenProven) {
			authenticatedTokenHash = presentedHash;
		} else if (identity.resumeToken) {
			return ValidationResult::Reject("invalid_resume", "The server does not recognize this resume credential.");
		}

		if (!a_capacityAvailable && !evictConnectionKey) {
			return ValidationResult::Reject("server_full", "Server is full.");
		}

		auto eviction = _resumeByProfile.end();
		if (resumeIt == _resumeByProfile.end() && _resumeByProfile.size() >= MaximumResumeProfiles) {
			const bool durableProfile = loadedResume.has_value();
			eviction = std::find_if(_resumeByProfile.begin(), _resumeByProfile.end(), [this, a_nowUnixSeconds, durableProfile](const auto& entry) {
				const auto& record = entry.second;
				if (record.activeConnectionKey || _reservedProfileToConnection.contains(entry.first)) {
					return false;
				}
				// Constraint: a new profile never displaces a confirmed record, whose durable row would stay and grow the store.
				if (record.confirmed) {
					return durableProfile && !record.previousTokenHash;
				}
				return !record.previousTokenHash &&
					a_nowUnixSeconds - record.issuedAt >= StaleUnconfirmedSeconds;
			});
			if (eviction == _resumeByProfile.end()) {
				return ValidationResult::Reject(
					"identity_capacity",
					durableProfile || evictedTokenProven ?
						"All cached resume identities are in use or awaiting confirmation. Try again shortly." :
						"Server has reached its resume identity limit. An administrator must reset unused identities before new profiles can join.");
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

		_readmissionByProfile.erase(identity.profileId);
		if (resumeIt != _resumeByProfile.end()) {
			resumeIt->second.tokenHash = newTokenHash;
			resumeIt->second.previousTokenHash = previousTokenHash;
			resumeIt->second.expiresAt = expiresAt;
			resumeIt->second.confirmed = false;
			resumeIt->second.issuedAt = a_nowUnixSeconds;
		} else {
			if (eviction != _resumeByProfile.end()) {
				const std::string evictedProfileId = eviction->first;
				const bool evictedUnconfirmed = !eviction->second.confirmed;
				const auto evictedTokenHash = eviction->second.tokenHash;
				const auto evictedExpiresAt = eviction->second.expiresAt;
				_resumeByProfile.erase(eviction);
				if (evictedUnconfirmed) {
					RememberReadmission(evictedProfileId, evictedExpiresAt, evictedTokenHash);
				}
				if (evictedUnconfirmed && _resumeStore != nullptr) {
					try {
						(void)_resumeStore->DeleteUnconfirmed(evictedProfileId);
					} catch (...) {
					}
				}
			}
			_resumeByProfile.emplace(identity.profileId, ResumeRecord{
				.tokenHash = newTokenHash,
				.expiresAt = expiresAt,
				.previousTokenHash = previousTokenHash,
				.confirmed = false,
				.activeConnectionKey = std::nullopt,
				.issuedAt = a_nowUnixSeconds
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
		_readmissionByProfile.erase(pending.profileId);
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
		_readmissionByProfile.erase(pending.profileId);
		_resumeByProfile[pending.profileId] = ResumeRecord{
			.tokenHash = tokenHash,
			.expiresAt = pending.resumeExpiresAt,
			.previousTokenHash = std::nullopt,
			.confirmed = true,
			.activeConnectionKey = key
		};
		return true;
	}

	void SessionIdentityManager::RememberReadmission(std::string_view a_profileId, double a_expiresAt, std::optional<TokenHash> a_evictedTokenHash)
	{
		const std::string profileId{ a_profileId };
		if (_readmissionByProfile.size() >= MaximumReadmissionProfiles && !_readmissionByProfile.contains(profileId)) {
			const auto rank = [](const Readmission& a_entry) {
				return std::pair{ a_entry.evictedTokenHash.has_value(), a_entry.expiresAt };
			};
			auto oldest = _readmissionByProfile.begin();
			for (auto candidate = _readmissionByProfile.begin(); candidate != _readmissionByProfile.end(); ++candidate) {
				if (rank(candidate->second) < rank(oldest->second)) {
					oldest = candidate;
				}
			}
			_readmissionByProfile.erase(oldest);
		}
		_readmissionByProfile[profileId] = Readmission{ a_expiresAt, std::move(a_evictedTokenHash) };
	}

	void SessionIdentityManager::ConnectionClosed(std::string_view a_connectionKey)
	{
		const std::string key{ a_connectionKey };
		std::scoped_lock lock(_gate);
		std::optional<std::string> closedProfileId;
		if (const auto pending = _pendingByConnection.find(key); pending != _pendingByConnection.end()) {
			if (const auto reserved = _reservedProfileToConnection.find(pending->second.profileId);
				reserved != _reservedProfileToConnection.end() && reserved->second == key) {
				_reservedProfileToConnection.erase(reserved);
			}
			closedProfileId = pending->second.profileId;
			_pendingByConnection.erase(pending);
		}
		if (const auto active = _activeProfileByConnection.find(key); active != _activeProfileByConnection.end()) {
			closedProfileId = active->second;
			_activeProfileByConnection.erase(active);
		}
		if (!closedProfileId) {
			return;
		}
		const auto resume = _resumeByProfile.find(*closedProfileId);
		if (resume == _resumeByProfile.end()) {
			return;
		}
		if (resume->second.activeConnectionKey && *resume->second.activeConnectionKey == key) {
			resume->second.activeConnectionKey.reset();
			return;
		}
		if (resume->second.confirmed || resume->second.previousTokenHash || resume->second.activeConnectionKey) {
			return;
		}
		const auto readmissionExpiresAt = resume->second.expiresAt;
		_resumeByProfile.erase(resume);
		RememberReadmission(*closedProfileId, readmissionExpiresAt);
		if (_resumeStore != nullptr) {
			try {
				(void)_resumeStore->DeleteUnconfirmed(*closedProfileId);
			} catch (...) {
			}
		}
	}
}

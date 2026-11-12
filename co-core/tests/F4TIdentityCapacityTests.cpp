#include "F4TIdentityProtocol.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace
{
	using Json = nlohmann::json;
	using F4T::Identity::PersistedResumeCredential;
	using F4T::Identity::TokenHash;

	class CountingTokenService final : public F4T::Identity::TokenService
	{
	public:
		std::string CreateToken() override
		{
			return "capacity-token-" + std::to_string(++created);
		}

		TokenHash HashToken(std::string_view a_token) override
		{
			TokenHash hash{};
			std::uint32_t state = 2166136261u;
			for (const unsigned char ch : a_token) {
				state ^= ch;
				state *= 16777619u;
			}
			for (std::size_t index = 0; index < hash.size(); ++index) {
				state ^= static_cast<std::uint32_t>(index + 1);
				state *= 16777619u;
				hash[index] = static_cast<std::uint8_t>((state >> ((index % 4) * 8)) & 0xffu);
			}
			return hash;
		}

		std::uint32_t created{};
	};

	class CountingResumeStore final : public F4T::Identity::ResumeCredentialStore
	{
	public:
		std::optional<PersistedResumeCredential> GetResumeCredential(std::string_view a_profileId) override
		{
			const auto found = records.find(std::string{ a_profileId });
			return found == records.end() ? std::nullopt : std::optional{ found->second };
		}

		void SaveResumeCredential(
			std::string_view a_profileId,
			const TokenHash& a_tokenHash,
			const std::optional<TokenHash>& a_previousTokenHash,
			bool a_confirmed,
			double a_expiresAt) override
		{
			if (failSave) {
				throw std::runtime_error("injected save failure");
			}
			records[std::string{ a_profileId }] = PersistedResumeCredential{
				a_tokenHash,
				a_expiresAt,
				a_previousTokenHash,
				a_confirmed
			};
			++saves;
		}

		bool DeleteResumeCredential(std::string_view a_profileId) override
		{
			++deletes;
			return records.erase(std::string{ a_profileId }) > 0;
		}

		std::unordered_map<std::string, PersistedResumeCredential> records;
		std::uint32_t saves{};
		std::uint32_t deletes{};
		bool failSave{};
	};

	std::size_t ResidentCount(const F4T::Identity::SessionIdentityManager& a_manager, const CountingResumeStore& a_store)
	{
		std::size_t count{};
		for (const auto& [profile, record] : a_store.records) {
			count += a_manager.ResumeForProfile(profile).has_value();
		}
		return count;
	}

	Json Hello(std::string_view a_profileId, std::optional<std::string_view> a_resumeToken = std::nullopt)
	{
		Json hello{
			{ "type", "hello" },
			{ "protocolVersion", 2 },
			{ "clientVersion", "capacity-test" },
			{ "falloutRuntimeVersion", "1.11.240" },
			{ "profileId", a_profileId },
			{ "contentManifestFingerprint", std::string(64, 'a') },
			{ "capabilities", Json::array({ "interest-v1" }) }
		};
		if (a_resumeToken) {
			hello["resumeToken"] = *a_resumeToken;
		}
		return hello;
	}
}

int main()
{
	using F4T::Identity::MaximumResumeProfiles;
	using F4T::Identity::Requirements;
	using F4T::Identity::SessionIdentityManager;

	Requirements requirements;
	requirements.resumeLifetimeSeconds = 1000.0;

	CountingTokenService newTokens;
	CountingResumeStore newStore;
	SessionIdentityManager newManager(requirements, newTokens, &newStore);
	const auto fullNew = newManager.ValidateAndReserve("new-1", Hello("capacity-new"), 1000.0, false);
	assert(!fullNew.accepted);
	assert(fullNew.code == "server_full");
	assert(newTokens.created == 0);
	assert(newStore.saves == 0);
	assert(newStore.records.empty());
	assert(!newManager.IsValidated("new-1"));

	CountingTokenService knownTokens;
	CountingResumeStore knownStore;
	knownStore.records["capacity-known"] = PersistedResumeCredential{
		knownTokens.HashToken("known-token"),
		5000.0,
		std::nullopt,
		true
	};
	SessionIdentityManager knownManager(requirements, knownTokens, &knownStore);
	const auto fullKnown = knownManager.ValidateAndReserve(
		"known-1",
		Hello("capacity-known", "known-token"),
		1000.0,
		false);
	assert(!fullKnown.accepted);
	assert(fullKnown.code == "server_full");
	assert(knownTokens.created == 0);
	assert(knownStore.saves == 0);
	assert(knownStore.records.at("capacity-known").confirmed);

	const auto badKnown = knownManager.ValidateAndReserve(
		"known-bad",
		Hello("capacity-known", "wrong-token"),
		1000.0,
		false);
	assert(!badKnown.accepted);
	assert(badKnown.code == "invalid_resume");
	assert(knownTokens.created == 0);
	assert(knownStore.saves == 0);

	CountingTokenService liveTokens;
	CountingResumeStore liveStore;
	SessionIdentityManager liveManager(requirements, liveTokens, &liveStore);
	const auto first = liveManager.ValidateAndReserve("live-1", Hello("capacity-live"), 2000.0, true);
	assert(first.accepted);
	const auto firstPending = liveManager.PendingForReady("live-1");
	assert(firstPending.has_value());
	assert(liveManager.CommitReady("live-1"));
	assert(liveManager.ConfirmReady("live-1"));
	const auto createsBeforeHandoff = liveTokens.created;
	const auto savesBeforeHandoff = liveStore.saves;

	const auto handoff = liveManager.ValidateAndReserve(
		"live-2",
		Hello("capacity-live", firstPending->resumeToken),
		2001.0,
		false);
	assert(handoff.accepted);
	assert(handoff.evictConnectionKey.has_value());
	assert(*handoff.evictConnectionKey == "live-1");
	assert(liveTokens.created == createsBeforeHandoff + 1);
	assert(liveStore.saves == savesBeforeHandoff + 1);
	assert(!liveManager.IsValidated("live-1"));
	assert(liveManager.IsValidated("live-2"));

	CountingTokenService memoryTokens;
	SessionIdentityManager memoryManager(requirements, memoryTokens, nullptr);
	for (std::size_t index = 0; index < MaximumResumeProfiles; ++index) {
		const auto profile = "memory-profile-" + std::to_string(index);
		const auto connection = "memory-connection-" + std::to_string(index);
		const auto accepted = memoryManager.ValidateAndReserve(connection, Hello(profile), 3000.0, true);
		assert(accepted.accepted);
		assert(memoryManager.ResumeForProfile(profile).has_value());
		memoryManager.ConnectionClosed(connection);
		assert(!memoryManager.ResumeForProfile(profile).has_value());
	}
	assert(memoryTokens.created == MaximumResumeProfiles);
	const auto overflow = memoryManager.ValidateAndReserve(
		"memory-overflow",
		Hello("memory-profile-overflow"),
		3000.0,
		true);
	assert(overflow.accepted);
	assert(memoryTokens.created == MaximumResumeProfiles + 1);
	memoryManager.ConnectionClosed("memory-overflow");

	const auto existingAfterRelease = memoryManager.ValidateAndReserve(
		"memory-existing",
		Hello("memory-profile-0"),
		3001.0,
		true);
	assert(existingAfterRelease.accepted);
	assert(memoryTokens.created == MaximumResumeProfiles + 2);
	memoryManager.ConnectionClosed("memory-existing");

	CountingTokenService releaseTokens;
	CountingResumeStore releaseStore;
	SessionIdentityManager releaseManager(requirements, releaseTokens, &releaseStore);
	for (std::size_t index = 0; index < MaximumResumeProfiles; ++index) {
		const auto accepted = releaseManager.ValidateAndReserve(
			"release-connection-" + std::to_string(index),
			Hello("release-profile-" + std::to_string(index)),
			3500.0,
			true);
		assert(accepted.accepted);
	}
	assert(releaseStore.records.size() == MaximumResumeProfiles);
	for (std::size_t index = 0; index < MaximumResumeProfiles; ++index) {
		releaseManager.ConnectionClosed("release-connection-" + std::to_string(index));
	}
	assert(releaseStore.records.empty());
	assert(releaseStore.deletes == MaximumResumeProfiles);
	assert(releaseManager.ValidateAndReserve(
		"release-after", Hello("release-profile-after"), 3501.0, true).accepted);
	releaseManager.ConnectionClosed("release-after");

	CountingTokenService committedTokens;
	CountingResumeStore committedStore;
	SessionIdentityManager committedManager(requirements, committedTokens, &committedStore);
	const auto committed = committedManager.ValidateAndReserve("committed-1", Hello("committed-profile"), 3600.0, true);
	assert(committed.accepted);
	const auto committedPending = committedManager.PendingForReady("committed-1");
	assert(committedPending.has_value());
	assert(committedManager.CommitReady("committed-1"));
	committedManager.ConnectionClosed("committed-1");
	assert(committedManager.ResumeForProfile("committed-profile").has_value());
	assert(committedStore.records.size() == 1);
	assert(!committedStore.records.at("committed-profile").confirmed);
	assert(committedStore.deletes == 0);
	const auto resumed = committedManager.ValidateAndReserve(
		"committed-2",
		Hello("committed-profile", committedPending->resumeToken),
		3601.0,
		true);
	assert(resumed.accepted);
	const auto resumedPending = committedManager.PendingForReady("committed-2");
	assert(resumedPending.has_value());
	committedManager.ConnectionClosed("committed-2");
	assert(committedStore.deletes == 0);
	SessionIdentityManager restartedManager(requirements, committedTokens, &committedStore);
	assert(restartedManager.ValidateAndReserve(
		"committed-3",
		Hello("committed-profile", resumedPending->resumeToken),
		3602.0,
		true).accepted);

	CountingTokenService keptTokens;
	CountingResumeStore keptStore;
	SessionIdentityManager keptManager(requirements, keptTokens, &keptStore);
	for (std::size_t index = 0; index < MaximumResumeProfiles; ++index) {
		const auto connection = "kept-connection-" + std::to_string(index);
		assert(keptManager.ValidateAndReserve(connection, Hello("kept-profile-" + std::to_string(index)), 3700.0, true).accepted);
		assert(keptManager.CommitReady(connection));
		keptManager.ConnectionClosed(connection);
	}
	const auto keptTooFresh = keptManager.ValidateAndReserve("kept-new", Hello("kept-new-profile"), 3700.0 + 1.0, true);
	assert(!keptTooFresh.accepted && keptTooFresh.code == "identity_capacity");
	assert(keptManager.ValidateAndReserve(
		"kept-new", Hello("kept-new-profile"), 3700.0 + F4T::Identity::StaleUnconfirmedSeconds, true).accepted);
	assert(keptStore.records.size() == MaximumResumeProfiles);
	assert(keptStore.deletes == 1);

	CountingTokenService durableTokens;
	CountingResumeStore durableStore;
	SessionIdentityManager durableManager(requirements, durableTokens, &durableStore);
	for (std::size_t index = 0; index < MaximumResumeProfiles; ++index) {
		const auto profile = "durable-cache-profile-" + std::to_string(index);
		const auto connection = "durable-cache-connection-" + std::to_string(index);
		const auto accepted = durableManager.ValidateAndReserve(connection, Hello(profile), 4000.0, true);
		assert(accepted.accepted);
		assert(durableManager.CommitReady(connection));
		assert(durableManager.ConfirmReady(connection));
		durableManager.ConnectionClosed(connection);
	}
	assert(durableStore.records.size() == MaximumResumeProfiles);
	durableStore.records["durable-only-profile"] = PersistedResumeCredential{
		durableTokens.HashToken("durable-only-token"),
		5000.0,
		std::nullopt,
		true
	};

	const auto rejectedCreates = durableTokens.created;
	const auto rejectedSaves = durableStore.saves;
	const auto badDurable = durableManager.ValidateAndReserve(
		"durable-bad", Hello("durable-only-profile", "wrong-token"), 4001.0);
	assert(!badDurable.accepted && badDurable.code == "invalid_resume");
	const auto missingDurable = durableManager.ValidateAndReserve(
		"durable-missing", Hello("durable-only-profile"), 4001.0);
	assert(!missingDurable.accepted && missingDurable.code == "resume_required");
	const auto expiredDurable = durableManager.ValidateAndReserve(
		"durable-expired", Hello("durable-only-profile", "durable-only-token"), 5000.0);
	assert(!expiredDurable.accepted && expiredDurable.code == "resume_expired");
	const auto fullDurable = durableManager.ValidateAndReserve(
		"durable-full", Hello("durable-only-profile", "durable-only-token"), 4001.0, false);
	assert(!fullDurable.accepted && fullDurable.code == "server_full");
	assert(durableTokens.created == rejectedCreates && durableStore.saves == rejectedSaves);
	assert(!durableManager.ResumeForProfile("durable-only-profile"));
	assert(ResidentCount(durableManager, durableStore) == MaximumResumeProfiles);
	durableStore.failSave = true;
	const auto failedDurable = durableManager.ValidateAndReserve(
		"durable-failed", Hello("durable-only-profile", "durable-only-token"), 4001.0);
	assert(!failedDurable.accepted && failedDurable.code == "resume_storage_failed");
	assert(!durableManager.ResumeForProfile("durable-only-profile"));
	assert(ResidentCount(durableManager, durableStore) == MaximumResumeProfiles);
	assert(durableStore.records.at("durable-only-profile").confirmed);
	durableStore.failSave = false;

	const auto durableOnly = durableManager.ValidateAndReserve(
		"durable-only-connection",
		Hello("durable-only-profile", "durable-only-token"),
		4001.0,
		true);
	assert(durableOnly.accepted);
	assert(ResidentCount(durableManager, durableStore) == MaximumResumeProfiles);
	durableManager.ConnectionClosed("durable-only-connection");
	durableStore.records["durable-only-profile-b"] = PersistedResumeCredential{
		durableTokens.HashToken("durable-only-token-b"), 5000.0, std::nullopt, true
	};
	const auto durableB = durableManager.ValidateAndReserve(
		"durable-only-connection-b", Hello("durable-only-profile-b", "durable-only-token-b"), 4002.0);
	assert(durableB.accepted);
	durableManager.ConnectionClosed("durable-only-connection-b");
	const auto residentCount = ResidentCount(durableManager, durableStore);
	std::cout << "Resident profiles after durable A/B reconnects: " << residentCount << '\n';
	if (residentCount > MaximumResumeProfiles) {
		return 1;
	}
	assert(durableStore.records.size() == MaximumResumeProfiles + 2);
	assert(durableManager.ResumeForProfile("durable-only-profile")->previousTokenHash ==
		durableTokens.HashToken("durable-only-token"));
	const auto retryA = durableManager.ValidateAndReserve(
		"durable-retry-a", Hello("durable-only-profile", "durable-only-token"), 4003.0);
	assert(retryA.accepted);
	assert(durableManager.CommitReady("durable-retry-a"));
	assert(durableManager.ConfirmReady("durable-retry-a"));
	durableManager.ConnectionClosed("durable-retry-a");
	const auto retiredA = durableManager.ValidateAndReserve(
		"durable-retired-a", Hello("durable-only-profile", "durable-only-token"), 4004.0);
	assert(!retiredA.accepted && retiredA.code == "invalid_resume");
	for (std::size_t index = 0; index < MaximumResumeProfiles; ++index) {
		const auto profile = "durable-cache-profile-" + std::to_string(index);
		if (!durableManager.ResumeForProfile(profile)) {
			const auto token = "capacity-token-" + std::to_string(index + 1);
			const auto recovered = durableManager.ValidateAndReserve("evicted-retry", Hello(profile, token), 4004.0);
			assert(recovered.accepted);
			durableManager.ConnectionClosed("evicted-retry");
			assert(ResidentCount(durableManager, durableStore) == MaximumResumeProfiles);
			break;
		}
	}

	CountingTokenService protectedTokens;
	CountingResumeStore protectedStore;
	SessionIdentityManager protectedManager(requirements, protectedTokens, &protectedStore);
	for (std::size_t index = 0; index < MaximumResumeProfiles; ++index) {
		const auto profile = "protected-" + std::to_string(index);
		const auto connection = "protected-connection-" + std::to_string(index);
		assert(protectedManager.ValidateAndReserve(connection, Hello(profile), 4000.0).accepted);
		if (index != 2) {
			assert(protectedManager.CommitReady(connection));
		}
		if (index <= 1 || index > 3) {
			assert(protectedManager.ConfirmReady(connection));
		}
		if (index == 1) {
			protectedManager.ConnectionClosed(connection);
		}
	}
	std::string resetReason;
	assert(!protectedManager.TryResetResumeCredential("protected-0", resetReason));
	assert(!protectedManager.TryResetResumeCredential("protected-2", resetReason));
	assert(!protectedManager.TryResetResumeCredential("protected-3", resetReason));
	for (const auto* profile : { "protected-durable-a", "protected-durable-b" }) {
		protectedStore.records[profile] = PersistedResumeCredential{
			protectedTokens.HashToken("protected-token"), 5000.0, std::nullopt, true
		};
	}
	assert(protectedManager.ValidateAndReserve(
		"protected-a", Hello("protected-durable-a", "protected-token"), 4001.0).accepted);
	assert(!protectedManager.ResumeForProfile("protected-1"));
	for (std::size_t index = 0; index < MaximumResumeProfiles; ++index) {
		if (index != 1) {
			assert(protectedManager.ResumeForProfile("protected-" + std::to_string(index)));
		}
	}
	protectedManager.ConnectionClosed("protected-a");
	const auto protectedCreates = protectedTokens.created;
	const auto protectedSaves = protectedStore.saves;
	const auto noVictim = protectedManager.ValidateAndReserve(
		"protected-b", Hello("protected-durable-b", "protected-token"), 4002.0);
	assert(!noVictim.accepted && noVictim.code == "identity_capacity");
	assert(protectedTokens.created == protectedCreates && protectedStore.saves == protectedSaves);
	const auto protectedHandoff = protectedManager.ValidateAndReserve(
		"protected-handoff", Hello("protected-0", "capacity-token-1"), 4002.0, false);
	assert(protectedHandoff.accepted && protectedHandoff.evictConnectionKey == "protected-connection-0");
	assert(!protectedManager.IsValidated("protected-connection-0"));
	protectedManager.ConnectionClosed("protected-connection-0");
	assert(!protectedManager.TryResetResumeCredential("protected-0", resetReason));
	const auto pendingHandoff = protectedManager.ValidateAndReserve(
		"protected-b", Hello("protected-durable-b", "protected-token"), 4003.0);
	assert(!pendingHandoff.accepted && pendingHandoff.code == "identity_capacity");
	assert(protectedManager.CommitReady("protected-handoff"));
	assert(protectedManager.ConfirmReady("protected-handoff"));
	protectedManager.ConnectionClosed("protected-handoff");
	assert(protectedManager.ValidateAndReserve(
		"protected-b", Hello("protected-durable-b", "protected-token"), 4004.0).accepted);
	assert(!protectedManager.ResumeForProfile("protected-0"));
	assert(ResidentCount(protectedManager, protectedStore) == MaximumResumeProfiles);
	assert(protectedManager.TryResetResumeCredential("protected-1", resetReason));
	assert(!protectedStore.records.contains("protected-1"));
	assert(!protectedManager.TryResetResumeCredential("protected-durable-b", resetReason));
	CountingTokenService knownOnlyTokens;
	CountingResumeStore knownOnlyStore;
	knownOnlyStore.records["known-unconfirmed"] = PersistedResumeCredential{
		knownOnlyTokens.HashToken("unconfirmed-token"), 5000.0, std::nullopt, false
	};
	knownOnlyStore.records["known-confirmed"] = PersistedResumeCredential{
		knownOnlyTokens.HashToken("confirmed-token"), 5000.0, std::nullopt, true
	};
	knownOnlyStore.records["known-rotating"] = PersistedResumeCredential{
		knownOnlyTokens.HashToken("rotating-token"), 5000.0, knownOnlyTokens.HashToken("previous-token"), false
	};
	Requirements knownOnlyRequirements = requirements;
	knownOnlyRequirements.requireKnownProfile = true;
	SessionIdentityManager knownOnlyManager(knownOnlyRequirements, knownOnlyTokens, &knownOnlyStore);
	const auto knownOnlyCreates = knownOnlyTokens.created;
	const auto knownOnlySaves = knownOnlyStore.saves;
	const auto unconfirmedRefused = knownOnlyManager.ValidateAndReserve(
		"known-unconfirmed-conn", Hello("known-unconfirmed"), 4100.0);
	assert(!unconfirmedRefused.accepted && unconfirmedRefused.code == "unknown_profile");
	assert(!unconfirmedRefused.identity.has_value());
	assert(!knownOnlyManager.PendingForReady("known-unconfirmed-conn").has_value());
	assert(knownOnlyTokens.created == knownOnlyCreates && knownOnlyStore.saves == knownOnlySaves);
	assert(knownOnlyStore.records.contains("known-unconfirmed"));
	assert(knownOnlyManager.ValidateAndReserve(
		"known-confirmed-conn", Hello("known-confirmed", "confirmed-token"), 4100.0).accepted);
	assert(knownOnlyManager.ValidateAndReserve(
		"known-rotating-conn", Hello("known-rotating", "rotating-token"), 4100.0).accepted);

	CountingTokenService rejectTokens;
	CountingResumeStore rejectStore;
	SessionIdentityManager rejectManager(requirements, rejectTokens, &rejectStore);
	for (std::size_t index = 0; index < MaximumResumeProfiles; ++index) {
		const auto connection = "reject-fill-" + std::to_string(index);
		assert(rejectManager.ValidateAndReserve(connection, Hello("reject-fill-profile-" + std::to_string(index)), 4200.0).accepted);
	}
	rejectStore.records["reject-durable"] = PersistedResumeCredential{
		rejectTokens.HashToken("reject-durable-token"), 5000.0, std::nullopt, true
	};
	const auto rejectCreates = rejectTokens.created;
	const auto rejectSaves = rejectStore.saves;
	const auto identityFull = rejectManager.ValidateAndReserve("reject-capacity", Hello("reject-new-profile"), 4201.0);
	const auto serverFull = rejectManager.ValidateAndReserve("reject-server-full", Hello("reject-new-profile"), 4201.0, false);
	const auto invalidResume = rejectManager.ValidateAndReserve("reject-invalid", Hello("reject-durable", "wrong-token"), 4201.0);
	assert(!identityFull.accepted && identityFull.code == "identity_capacity");
	assert(!serverFull.accepted && serverFull.code == "server_full");
	assert(!invalidResume.accepted && invalidResume.code == "invalid_resume");
	for (const auto* result : { &identityFull, &serverFull, &invalidResume }) {
		assert(!result->identity.has_value() && !result->evictConnectionKey.has_value());
	}
	for (const auto* connection : { "reject-capacity", "reject-server-full", "reject-invalid" }) {
		assert(!rejectManager.IsValidated(connection));
		assert(!rejectManager.PendingForReady(connection).has_value());
	}
	assert(rejectTokens.created == rejectCreates && rejectStore.saves == rejectSaves);
	assert(!rejectManager.ResumeForProfile("reject-new-profile").has_value());
	assert(!rejectManager.ResumeForProfile("reject-durable").has_value());
	assert(rejectStore.records.size() == MaximumResumeProfiles + 1);
	assert(!rejectStore.records.contains("reject-new-profile"));

	CountingTokenService readmitTokens;
	CountingResumeStore readmitStore;
	SessionIdentityManager readmitManager(requirements, readmitTokens, &readmitStore);
	std::unordered_map<std::string, std::string> readmitIssued;
	for (std::size_t index = 0; index < MaximumResumeProfiles; ++index) {
		const auto profile = "readmit-profile-" + std::to_string(index);
		const auto connection = "readmit-connection-" + std::to_string(index);
		assert(readmitManager.ValidateAndReserve(connection, Hello(profile), 6000.0, true).accepted);
		readmitIssued[profile] = readmitManager.PendingForReady(connection)->resumeToken;
		assert(readmitManager.CommitReady(connection));
		readmitManager.ConnectionClosed(connection);
	}
	assert(readmitStore.records.size() == MaximumResumeProfiles);
	assert(readmitManager.ValidateAndReserve("readmit-newcomer-conn", Hello("readmit-newcomer"), 6060.0, true).accepted);
	std::string victimProfile;
	for (const auto& entry : readmitIssued) {
		if (!readmitManager.ResumeForProfile(entry.first)) {
			victimProfile = entry.first;
		}
	}
	assert(!victimProfile.empty());
	assert(!readmitStore.records.contains(victimProfile));
	assert(readmitStore.records.size() == MaximumResumeProfiles);
	const auto victimToken = readmitIssued.at(victimProfile);

	const auto forgedVictim = readmitManager.ValidateAndReserve(
		"readmit-forged-conn", Hello(victimProfile, "forged-token"), 6061.0, true);
	assert(!forgedVictim.accepted && forgedVictim.code == "unknown_resume");
	assert(!readmitManager.IsValidated("readmit-forged-conn"));

	const auto rotatedVictim = readmitManager.ValidateAndReserve(
		"readmit-victim-conn", Hello(victimProfile, victimToken), 6061.0, true);
	assert(rotatedVictim.accepted);
	assert(readmitStore.records.at(victimProfile).previousTokenHash == readmitTokens.HashToken(victimToken));
	assert(!readmitStore.records.at(victimProfile).confirmed);
	assert(readmitManager.CommitReady("readmit-victim-conn"));
	assert(readmitManager.ConfirmReady("readmit-victim-conn"));
	readmitManager.ConnectionClosed("readmit-victim-conn");
	const auto retiredVictim = readmitManager.ValidateAndReserve(
		"readmit-retired-conn", Hello(victimProfile, victimToken), 6062.0, true);
	assert(!retiredVictim.accepted && retiredVictim.code == "invalid_resume");

	std::string secondVictim;
	for (const auto& entry : readmitIssued) {
		if (entry.first != victimProfile && !readmitManager.ResumeForProfile(entry.first)) {
			secondVictim = entry.first;
		}
	}
	assert(!secondVictim.empty());
	std::string readmitReason;
	assert(readmitManager.TryResetResumeCredential(secondVictim, readmitReason));
	const auto resetVictim = readmitManager.ValidateAndReserve(
		"readmit-reset-conn", Hello(secondVictim, readmitIssued.at(secondVictim)), 6062.0, true);
	assert(!resetVictim.accepted && resetVictim.code == "unknown_resume");

	assert(readmitManager.ValidateAndReserve("readmit-newcomer-2-conn", Hello("readmit-newcomer-2"), 6120.0, true).accepted);
	std::string thirdVictim;
	for (const auto& entry : readmitIssued) {
		if (entry.first != victimProfile && entry.first != secondVictim && !readmitManager.ResumeForProfile(entry.first)) {
			thirdVictim = entry.first;
		}
	}
	assert(!thirdVictim.empty());
	SessionIdentityManager readmitRestarted(requirements, readmitTokens, &readmitStore);
	const auto restartedVictim = readmitRestarted.ValidateAndReserve(
		"readmit-restart-conn", Hello(thirdVictim, readmitIssued.at(thirdVictim)), 6121.0, true);
	assert(!restartedVictim.accepted && restartedVictim.code == "unknown_resume");
	assert(readmitRestarted.ValidateAndReserve("readmit-restart-tokenless", Hello(thirdVictim), 6121.0, true).accepted);

	const auto expiredVictim = readmitManager.ValidateAndReserve(
		"readmit-expired-conn", Hello(thirdVictim, readmitIssued.at(thirdVictim)), 7001.0, true);
	assert(!expiredVictim.accepted && expiredVictim.code == "invalid_resume");

	CountingTokenService retryTokens;
	CountingResumeStore retryStore;
	SessionIdentityManager retryManager(requirements, retryTokens, &retryStore);
	assert(retryManager.ValidateAndReserve("retry-victim-conn", Hello("retry-victim"), 8000.0, true).accepted);
	const auto retryToken = retryManager.PendingForReady("retry-victim-conn")->resumeToken;
	assert(retryManager.CommitReady("retry-victim-conn"));
	retryManager.ConnectionClosed("retry-victim-conn");
	for (std::size_t index = 1; index < MaximumResumeProfiles; ++index) {
		const auto connection = "retry-connection-" + std::to_string(index);
		assert(retryManager.ValidateAndReserve(connection, Hello("retry-profile-" + std::to_string(index)), 8000.0, true).accepted);
		assert(retryManager.CommitReady(connection));
		assert(retryManager.ConfirmReady(connection));
		retryManager.ConnectionClosed(connection);
	}
	assert(retryManager.ValidateAndReserve("retry-newcomer-conn", Hello("retry-newcomer"), 8060.0, true).accepted);
	assert(retryManager.CommitReady("retry-newcomer-conn"));
	retryManager.ConnectionClosed("retry-newcomer-conn");
	assert(!retryManager.ResumeForProfile("retry-victim"));
	assert(retryManager.ValidateAndReserve("retry-1", Hello("retry-victim", retryToken), 8061.0, true).accepted);
	retryManager.ConnectionClosed("retry-1");
	const auto retryFull = retryManager.ValidateAndReserve("retry-2", Hello("retry-victim", retryToken), 8120.0, false);
	assert(!retryFull.accepted && retryFull.code == "server_full");
	retryStore.failSave = true;
	const auto retrySaveFailed = retryManager.ValidateAndReserve("retry-3", Hello("retry-victim", retryToken), 8120.0, true);
	assert(!retrySaveFailed.accepted && retrySaveFailed.code == "resume_storage_failed");
	retryStore.failSave = false;
	assert(retryManager.ValidateAndReserve("retry-4", Hello("retry-victim", retryToken), 8120.0, true).accepted);
	assert(retryStore.records.at("retry-victim").previousTokenHash == retryTokens.HashToken(retryToken));

	CountingTokenService flushTokens;
	CountingResumeStore flushStore;
	SessionIdentityManager flushManager(requirements, flushTokens, &flushStore);
	std::unordered_map<std::string, std::string> flushIssued;
	for (std::size_t index = 0; index < MaximumResumeProfiles; ++index) {
		const auto profile = "flush-profile-" + std::to_string(index);
		const auto connection = "flush-connection-" + std::to_string(index);
		assert(flushManager.ValidateAndReserve(connection, Hello(profile), 8000.0, true).accepted);
		flushIssued[profile] = flushManager.PendingForReady(connection)->resumeToken;
		assert(flushManager.CommitReady(connection));
		flushManager.ConnectionClosed(connection);
	}
	assert(flushManager.ValidateAndReserve("flush-newcomer-conn", Hello("flush-newcomer"), 8060.0, true).accepted);
	flushManager.ConnectionClosed("flush-newcomer-conn");
	std::string flushVictim;
	for (const auto& entry : flushIssued) {
		if (!flushManager.ResumeForProfile(entry.first)) {
			flushVictim = entry.first;
		}
	}
	assert(!flushVictim.empty());
	for (std::size_t index = 0; index < F4T::Identity::MaximumReadmissionProfiles; ++index) {
		const auto connection = "flush-dropped-" + std::to_string(index);
		assert(flushManager.ValidateAndReserve(connection, Hello(connection), 8061.0, true).accepted);
		flushManager.ConnectionClosed(connection);
	}
	assert(flushManager.ValidateAndReserve("flush-victim-conn", Hello(flushVictim, flushIssued.at(flushVictim)), 8062.0, true).accepted);

	for (const bool withStore : { true, false }) {
		CountingTokenService ceilingTokens;
		CountingResumeStore ceilingStore;
		SessionIdentityManager ceilingManager(requirements, ceilingTokens, withStore ? &ceilingStore : nullptr);
		std::unordered_map<std::string, std::string> ceilingIssued;
		for (std::size_t index = 0; index < MaximumResumeProfiles; ++index) {
			const auto profile = "ceiling-" + std::to_string(index);
			const auto connection = "ceiling-connection-" + std::to_string(index);
			assert(ceilingManager.ValidateAndReserve(connection, Hello(profile), 9000.0 + static_cast<double>(index) * 0.001, true).accepted);
			ceilingIssued[profile] = ceilingManager.PendingForReady(connection)->resumeToken;
			assert(ceilingManager.CommitReady(connection));
			assert(ceilingManager.ConfirmReady(connection));
			if (index != 0) {
				ceilingManager.ConnectionClosed(connection);
			}
		}

		const auto newcomer = ceilingManager.ValidateAndReserve("ceiling-newcomer", Hello("ceiling-newcomer-profile"), 9100.0, true);
		assert(newcomer.accepted);
		assert(ceilingManager.ResumeForProfile("ceiling-0").has_value());
		assert(!ceilingManager.ResumeForProfile("ceiling-1").has_value());
		assert(ceilingManager.ResumeForProfile("ceiling-2").has_value());
		assert(ceilingManager.ResumeForProfile("ceiling-newcomer-profile").has_value());
		assert(!withStore || ceilingStore.records.contains("ceiling-1"));

		const auto returning = ceilingManager.ValidateAndReserve(
			"ceiling-returning", Hello("ceiling-1", ceilingIssued.at("ceiling-1")), 9101.0, true);
		assert(returning.accepted);
		assert(ceilingManager.ResumeForProfile("ceiling-1").has_value());
		assert(!ceilingManager.ResumeForProfile("ceiling-2").has_value());
		assert(ceilingManager.ResumeForProfile("ceiling-0").has_value());

		const auto forged = ceilingManager.ValidateAndReserve("ceiling-forged", Hello("ceiling-3", "forged-token"), 9102.0, true);
		assert(!forged.accepted && forged.code == "invalid_resume");
		const auto forgedEvicted = ceilingManager.ValidateAndReserve("ceiling-forged-2", Hello("ceiling-2", "forged-token"), 9102.0, true);
		assert(!forgedEvicted.accepted && forgedEvicted.code == (withStore ? "invalid_resume" : "unknown_resume"));
	}

	std::cout << "Protected records, rotation recovery, handoff and durable reset passed\n";

	return 0;
}

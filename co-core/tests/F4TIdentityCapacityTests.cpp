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
			return records.erase(std::string{ a_profileId }) > 0;
		}

		std::unordered_map<std::string, PersistedResumeCredential> records;
		std::uint32_t saves{};
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
		memoryManager.ConnectionClosed(connection);
	}
	assert(memoryTokens.created == MaximumResumeProfiles);
	const auto overflow = memoryManager.ValidateAndReserve(
		"memory-overflow",
		Hello("memory-profile-overflow"),
		3000.0,
		true);
	assert(!overflow.accepted);
	assert(overflow.code == "identity_capacity");
	assert(memoryTokens.created == MaximumResumeProfiles);

	const auto existingAtLimit = memoryManager.ValidateAndReserve(
		"memory-existing",
		Hello("memory-profile-0"),
		3001.0,
		true);
	assert(existingAtLimit.accepted);
	assert(memoryTokens.created == MaximumResumeProfiles + 1);

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
		if (index <= 1 || index == 3) {
			assert(protectedManager.CommitReady(connection));
		}
		if (index <= 1) {
			assert(protectedManager.ConfirmReady(connection));
		}
		if (index == 1 || index > 3) {
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
	std::cout << "Protected records, rotation recovery, handoff and durable reset passed\n";

	return 0;
}

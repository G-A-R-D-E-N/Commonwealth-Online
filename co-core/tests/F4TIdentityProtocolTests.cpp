#include "F4TIdentityProtocol.h"
#include "F4TContentManifest.h"
#include "F4TModpackChunks.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace
{
	using Json = nlohmann::json;
	using F4T::Identity::PersistedResumeCredential;
	using F4T::Identity::TokenHash;

	class FakeTokenService final : public F4T::Identity::TokenService
	{
	public:
		std::string CreateToken() override
		{
			return "token-" + std::to_string(++counter);
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

		std::uint32_t counter{};
	};

	class MemoryResumeStore final : public F4T::Identity::ResumeCredentialStore
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
			if (failNextSave) {
				failNextSave = false;
				throw std::runtime_error("resume store unavailable");
			}
			records[std::string{ a_profileId }] =
				PersistedResumeCredential{ a_tokenHash, a_expiresAt, a_previousTokenHash, a_confirmed };
			++saves;
		}

		bool DeleteResumeCredential(std::string_view a_profileId) override
		{
			return records.erase(std::string{ a_profileId }) > 0;
		}

		std::unordered_map<std::string, PersistedResumeCredential> records;
		bool failNextSave{};
		std::uint32_t saves{};
	};

	Json ValidHello(std::string profileId = "profile-001")
	{
		return Json{
			{ "type", "hello" },
			{ "protocolVersion", 2 },
			{ "clientVersion", "1.2.0" },
			{ "falloutRuntimeVersion", "1.11.240" },
			{ "profileId", std::move(profileId) },
			{ "contentManifestFingerprint", std::string(64, 'A') },
			{ "capabilities", Json::array({ "interest-v1", "player-state-v1" }) }
		};
	}

	F4T::Identity::Requirements MakeRequirements()
	{
		return F4T::Identity::Requirements{
			.clientVersion = std::string{ " 1.2.0 " },
			.falloutRuntimeVersion = std::string{ "1.11.240" },
			.contentManifestFingerprint = std::string(64, 'a'),
			.capabilities = { "interest-v1", "player-state-v1" },
			.resumeLifetimeSeconds = 100.0
		};
	}
}

int main()
{
	using namespace F4T::Identity;

	FakeTokenService tokens;
	MemoryResumeStore store;
	const auto requirements = MakeRequirements();

	const auto normalized = NormalizeHello(ValidHello(), requirements);
	assert(normalized.accepted);
	assert(normalized.identity.has_value());
	assert(normalized.identity->profileId == "profile-001");
	assert(normalized.identity->clientVersion == "1.2.0");
	assert(normalized.identity->contentManifestFingerprint == std::string(64, 'a'));
	assert(!normalized.identity->resumeToken.has_value());
	assert(!normalized.identity->saveAttestation.has_value());

	Json attestedHello = ValidHello("profile-attested");
	attestedHello["saveAttestation"] = Json{
		{ "saveFile", "Save3_AAAA1111_Nomad_Commonwealth_000020_20260814000303_1_2" },
		{ "sha256", std::string(64, 'B') }
	};
	const auto attested = NormalizeHello(attestedHello, requirements);
	assert(attested.accepted);
	assert(attested.identity.has_value());
	assert(attested.identity->saveAttestation.has_value());
	assert(attested.identity->saveAttestation->saveFile == "Save3_AAAA1111_Nomad_Commonwealth_000020_20260814000303_1_2");
	assert(attested.identity->saveAttestation->sha256Hex == std::string(64, 'b'));

	Json nullAttestation = ValidHello("profile-null-attestation");
	nullAttestation["saveAttestation"] = nullptr;
	const auto nullAttested = NormalizeHello(nullAttestation, requirements);
	assert(nullAttested.accepted);
	assert(nullAttested.identity.has_value());
	assert(!nullAttested.identity->saveAttestation.has_value());

	Json shortHash = ValidHello("profile-short-hash");
	shortHash["saveAttestation"] = Json{
		{ "saveFile", "Save3_AAAA1111_Nomad_Commonwealth_000020_20260814000303_1_2" },
		{ "sha256", "abcd" }
	};
	const auto shortHashResult = NormalizeHello(shortHash, requirements);
	assert(!shortHashResult.accepted);
	assert(shortHashResult.code == "save_attestation_invalid");

	Json missingName = ValidHello("profile-missing-name");
	missingName["saveAttestation"] = Json{ { "sha256", std::string(64, 'b') } };
	const auto missingNameResult = NormalizeHello(missingName, requirements);
	assert(!missingNameResult.accepted);
	assert(missingNameResult.code == "save_attestation_invalid");

	Json notAnObject = ValidHello("profile-not-object");
	notAnObject["saveAttestation"] = "Save3_AAAA1111_Nomad_Commonwealth_000020_20260814000303_1_2";
	const auto notAnObjectResult = NormalizeHello(notAnObject, requirements);
	assert(!notAnObjectResult.accepted);
	assert(notAnObjectResult.code == "save_attestation_invalid");

	Json aliases = ValidHello("profile-002");
	aliases.erase("clientVersion");
	aliases["pluginVersion"] = " 1.2.0 ";
	aliases.erase("profileId");
	aliases["characterId"] = "profile-002";
	aliases.erase("contentManifestFingerprint");
	aliases["contentFingerprint"] = std::string(64, 'A');
	assert(NormalizeHello(aliases, requirements).accepted);

	Json invalid = ValidHello();
	invalid["protocolVersion"] = 1;
	assert(NormalizeHello(invalid, requirements).code == "protocol_mismatch");
	invalid = ValidHello();
	invalid.erase("clientVersion");
	assert(NormalizeHello(invalid, requirements).code == "identity_required");
	invalid = ValidHello("bad/profile");
	assert(NormalizeHello(invalid, requirements).code == "identity_required");
	invalid = ValidHello();
	invalid["contentManifestFingerprint"] = std::string(63, 'a');
	assert(NormalizeHello(invalid, requirements).code == "identity_required");
	invalid = ValidHello();
	invalid["capabilities"] = Json::array({ "interest-v1", "interest-v1" });
	assert(NormalizeHello(invalid, requirements).code == "identity_required");
	invalid = ValidHello();
	invalid["clientVersion"] = "9.9.9";
	assert(NormalizeHello(invalid, requirements).code == "client_version_mismatch");
	invalid = ValidHello();
	invalid["falloutRuntimeVersion"] = "1.10.163";
	assert(NormalizeHello(invalid, requirements).code == "runtime_mismatch");
	invalid = ValidHello();
	invalid["contentManifestFingerprint"] = std::string(64, 'b');
	assert(NormalizeHello(invalid, requirements).code == "content_mismatch");
	invalid = ValidHello();
	invalid["capabilities"] = Json::array({ "interest-v1" });
	assert(NormalizeHello(invalid, requirements).code == "capability_mismatch");

	// Capabilities are optional claims: a hello may omit the field or send an
	// empty array, and both normalize to an empty claim set.
	Requirements openRequirements;
	Json noCapabilities = ValidHello();
	noCapabilities.erase("capabilities");
	const auto withoutField = NormalizeHello(noCapabilities, openRequirements);
	assert(withoutField.accepted);
	assert(withoutField.identity && withoutField.identity->capabilities.empty());

	Json emptyCapabilities = ValidHello();
	emptyCapabilities["capabilities"] = Json::array();
	const auto withEmptyField = NormalizeHello(emptyCapabilities, openRequirements);
	assert(withEmptyField.accepted);
	assert(withEmptyField.identity && withEmptyField.identity->capabilities.empty());

	const auto emptyAgainstPinned = NormalizeHello(emptyCapabilities, requirements);
	assert(emptyAgainstPinned.code == "capability_mismatch");

	Json notAnArray = ValidHello();
	notAnArray["capabilities"] = "interest-v1";
	assert(NormalizeHello(notAnArray, openRequirements).code == "identity_required");
	Json oversized = ValidHello();
	Json manyCapabilities = Json::array();
	for (std::uint32_t index = 0; index < 65; ++index) {
		manyCapabilities.push_back("cap-" + std::to_string(index));
	}
	oversized["capabilities"] = std::move(manyCapabilities);
	assert(NormalizeHello(oversized, openRequirements).code == "identity_required");

	Requirements passworded = requirements;
	passworded.serverPasswordSha256 = F4T::ContentManifest::Sha256Hex("correct-horse");
	assert(NormalizeHello(ValidHello(), passworded).code == "password_required");
	Json wrongPassword = ValidHello();
	wrongPassword["serverPassword"] = "wrong-horse";
	assert(NormalizeHello(wrongPassword, passworded).code == "password_invalid");
	Json tooLongPassword = ValidHello();
	tooLongPassword["serverPassword"] = std::string(129, 'a');
	assert(NormalizeHello(tooLongPassword, passworded).code == "password_invalid");
	Json nonStringPassword = ValidHello();
	nonStringPassword["serverPassword"] = 1234;
	assert(NormalizeHello(nonStringPassword, passworded).code == "password_invalid");
	Json correctPassword = ValidHello();
	correctPassword["serverPassword"] = "correct-horse";
	assert(NormalizeHello(correctPassword, passworded).accepted);
	Json unconfiguredPassword = ValidHello();
	unconfiguredPassword["serverPassword"] = "sent-anyway";
	assert(NormalizeHello(unconfiguredPassword, requirements).accepted);

	const std::string modpackManifest = R"({"entries":[{"kind":"native","name":"F4SE/Plugins/Foo.dll","sha256":")" +
		std::string(64, 'e') + R"("}],"version":1})";
	Requirements modpacked = requirements;
	modpacked.modpackFingerprint = std::string(64, 'c');
	modpacked.modpackManifestJson = modpackManifest;
	assert(NormalizeHello(ValidHello(), openRequirements).accepted);
	Json anyModpack = ValidHello();
	anyModpack["modpackFingerprint"] = std::string(64, 'd');
	assert(NormalizeHello(anyModpack, openRequirements).accepted);
	const auto missingModpack = NormalizeHello(ValidHello(), modpacked);
	assert(missingModpack.code == "modpack_mismatch");
	assert(missingModpack.requiredModpack == Json::parse(modpackManifest));
	Json wrongModpack = ValidHello();
	wrongModpack["modpackFingerprint"] = std::string(64, 'd');
	const auto rejectedModpack = NormalizeHello(wrongModpack, modpacked);
	assert(rejectedModpack.code == "modpack_mismatch");
	assert(rejectedModpack.requiredModpack.at("version") == 1);
	assert(rejectedModpack.reason.find("Modpack page:") == std::string::npos);
	Requirements linkedModpack = modpacked;
	linkedModpack.modpackUrl = "https://example.com/modpack";
	const auto linkedRejection = NormalizeHello(wrongModpack, linkedModpack);
	assert(linkedRejection.code == "modpack_mismatch");
	assert(linkedRejection.reason.find(F4T::Identity::kModpackPageMarker) != std::string::npos);
	assert(linkedRejection.reason.find("https://example.com/modpack") != std::string::npos);
	Requirements insecureLinkedModpack = linkedModpack;
	insecureLinkedModpack.modpackUrl = "ftp://example.com/modpack";
	assert(NormalizeHello(wrongModpack, insecureLinkedModpack).reason.find("ftp://") == std::string::npos);
	Requirements oversizedLinkedModpack = linkedModpack;
	oversizedLinkedModpack.modpackUrl = "https://example.com/" + std::string(300, 'a');
	assert(NormalizeHello(wrongModpack, oversizedLinkedModpack).reason.find("Modpack page:") == std::string::npos);
	Json oversizedModpack = ValidHello();
	oversizedModpack["modpackFingerprint"] = std::string(129, 'c');
	assert(NormalizeHello(oversizedModpack, modpacked).code == "modpack_mismatch");
	Json nonStringModpack = ValidHello();
	nonStringModpack["modpackFingerprint"] = 7;
	assert(NormalizeHello(nonStringModpack, modpacked).code == "modpack_mismatch");
	Json matchingModpack = ValidHello();
	matchingModpack["modpackFingerprint"] = std::string(64, 'c');
	assert(NormalizeHello(matchingModpack, modpacked).accepted);
	Requirements upperCaseModpacked = modpacked;
	upperCaseModpacked.modpackFingerprint = std::string(64, 'C');
	assert(NormalizeHello(matchingModpack, upperCaseModpacked).accepted);
	assert(NormalizeHello(wrongModpack, upperCaseModpacked).code == "modpack_mismatch");
	Requirements namedModpack = modpacked;
	namedModpack.modpackName = "Main List";
	const auto namedRejection = NormalizeHello(wrongModpack, namedModpack);
	assert(namedRejection.code == "modpack_mismatch");
	assert(namedRejection.modpackName == "Main List");
	assert(rejectedModpack.modpackName.empty());

	const std::string largeManifest = R"({"version":1,"entries":[]})" + std::string(F4T::Identity::kMaximumAdmissionManifestBytes, ' ');
	Requirements chunkedModpack = modpacked;
	chunkedModpack.modpackManifestJson = largeManifest;
	Json chunkCapable = wrongModpack;
	auto chunkCapabilities = chunkCapable.at("capabilities");
	chunkCapabilities.push_back(std::string{ F4T::ModpackChunks::kCapabilityName });
	chunkCapable["capabilities"] = std::move(chunkCapabilities);
	const auto chunkedRejection = NormalizeHello(chunkCapable, chunkedModpack);
	assert(chunkedRejection.code == "modpack_mismatch");
	assert(!chunkedRejection.requiredModpack.is_object());
	assert(chunkedRejection.modpackFingerprint == *chunkedModpack.modpackFingerprint);
	assert(chunkedRejection.modpackChunks.is_object());
	assert(chunkedRejection.modpackChunks.at("totalBytes") == largeManifest.size());
	assert(chunkedRejection.modpackChunks.at("sha256") == F4T::ContentManifest::Sha256Hex(largeManifest));
	const auto oldClientRejection = NormalizeHello(wrongModpack, chunkedModpack);
	assert(oldClientRejection.code == "modpack_mismatch");
	assert(!oldClientRejection.requiredModpack.is_object());
	assert(!oldClientRejection.modpackChunks.is_object());
	assert(oldClientRejection.reason.find("Update Commonwealth Online") != std::string::npos);
	const auto smallStillFull = NormalizeHello(wrongModpack, modpacked);
	assert(smallStillFull.requiredModpack.is_object());

	Requirements passwordedModpacked = passworded;
	passwordedModpacked.modpackFingerprint = std::string(64, 'c');
	passwordedModpacked.modpackManifestJson = modpackManifest;
	Json wrongPasswordWrongModpack = wrongModpack;
	wrongPasswordWrongModpack["serverPassword"] = "wrong-horse";
	assert(NormalizeHello(wrongPasswordWrongModpack, passwordedModpacked).code == "password_invalid");

	SessionIdentityManager manager(requirements, tokens, &store);
	const auto first = manager.ValidateAndReserve("conn-1", ValidHello(), 1000.0);
	assert(first.accepted);
	assert(manager.IsValidated("conn-1"));
	const auto pending1 = manager.PendingForReady("conn-1");
	assert(pending1.has_value());
	assert(pending1->resumeToken == "token-1");
	assert(pending1->resumeExpiresAt == 1100.0);
	// The rotation is stored before the token is handed out, and marked
	// unconfirmed until the client is known to have received it.
	assert(store.records.size() == 1);
	const auto reserved = manager.ResumeForProfile("profile-001");
	assert(reserved.has_value());
	assert(!reserved->confirmed);
	assert(!reserved->previousTokenHash.has_value());

	assert(manager.ValidateAndReserve("conn-1", ValidHello(), 1000.1).code == "duplicate_hello");
	assert(manager.ValidateAndReserve("conn-2", ValidHello(), 1000.1).code == "duplicate_identity");

	manager.ConnectionClosed("conn-1");
	assert(!manager.IsValidated("conn-1"));
	const auto second = manager.ValidateAndReserve("conn-2", ValidHello(), 1001.0);
	assert(second.accepted);
	const auto pending2 = manager.PendingForReady("conn-2");
	assert(pending2.has_value() && pending2->resumeToken == "token-2");
	assert(manager.CommitReady("conn-2"));
	assert(manager.ConfirmReady("conn-2"));
	assert(manager.IsValidated("conn-2"));
	assert(!manager.PendingForReady("conn-2").has_value());
	assert(manager.ResumeForProfile("profile-001").has_value());
	assert(store.records.contains("profile-001"));
	assert(manager.ValidateAndReserve("conn-3", ValidHello(), 1002.0).code == "duplicate_identity");

	std::string resetReason;
	assert(!manager.TryResetResumeCredential("profile-001", resetReason));
	assert(resetReason == "The profile is currently connected or completing a handshake.");

	manager.ConnectionClosed("conn-2");
	assert(manager.ValidateAndReserve("conn-3", ValidHello(), 1002.0).code == "resume_required");
	Json wrongResume = ValidHello();
	wrongResume["resumeToken"] = "wrong-token";
	assert(manager.ValidateAndReserve("conn-3", wrongResume, 1002.0).code == "invalid_resume");

	Json resume = ValidHello();
	resume["resumeToken"] = "token-2";
	assert(manager.ValidateAndReserve("conn-3", resume, 1002.0).accepted);
	const auto pending3 = manager.PendingForReady("conn-3");
	assert(pending3.has_value() && pending3->resumeToken == "token-3");
	manager.ConnectionClosed("conn-3");

	assert(manager.ValidateAndReserve("conn-4", resume, 1003.0).accepted);
	const auto pending4 = manager.PendingForReady("conn-4");
	assert(pending4.has_value() && pending4->resumeToken == "token-4");
	assert(manager.CommitReady("conn-4"));
	assert(manager.ConfirmReady("conn-4"));
	manager.ConnectionClosed("conn-4");

	assert(manager.ValidateAndReserve("conn-5", resume, 1004.0).code == "invalid_resume");
	Json rotated = ValidHello();
	rotated["resumeToken"] = "token-4";
	assert(manager.ValidateAndReserve("conn-5", rotated, 1004.0).accepted);
	assert(manager.CommitReady("conn-5"));
	assert(manager.ConfirmReady("conn-5"));
	manager.ConnectionClosed("conn-5");

	// A proven resume credential supersedes a session that is still live, so a
	// client that crashed does not have to wait out the transport keepalive.
	FakeTokenService evictionTokens;
	MemoryResumeStore evictionStore;
	SessionIdentityManager evictionManager(requirements, evictionTokens, &evictionStore);
	assert(evictionManager.ValidateAndReserve("live-1", ValidHello("profile-evict"), 3000.0).accepted);
	assert(evictionManager.CommitReady("live-1"));
	assert(evictionManager.ConfirmReady("live-1"));

	Json wrongWhileLive = ValidHello("profile-evict");
	wrongWhileLive["resumeToken"] = "not-the-token";
	const auto refusedWrong = evictionManager.ValidateAndReserve("live-2", wrongWhileLive, 3001.0);
	assert(refusedWrong.code == "invalid_resume");
	assert(!refusedWrong.evictConnectionKey.has_value());

	const auto refusedMissing = evictionManager.ValidateAndReserve("live-2", ValidHello("profile-evict"), 3001.0);
	assert(refusedMissing.code == "duplicate_identity");
	assert(!refusedMissing.evictConnectionKey.has_value());

	Json supersede = ValidHello("profile-evict");
	supersede["resumeToken"] = "token-1";
	const auto superseded = evictionManager.ValidateAndReserve("live-2", supersede, 3002.0);
	assert(superseded.accepted);
	assert(superseded.evictConnectionKey.has_value() && *superseded.evictConnectionKey == "live-1");
	assert(!evictionManager.IsValidated("live-1"));
	assert(evictionManager.CommitReady("live-2"));
	assert(evictionManager.ConfirmReady("live-2"));
	assert(evictionManager.IsValidated("live-2"));

	// The displaced connection closing afterwards must not disturb the session
	// that replaced it.
	evictionManager.ConnectionClosed("live-1");
	assert(evictionManager.IsValidated("live-2"));
	const auto stillActive = evictionManager.ResumeForProfile("profile-evict");
	assert(stillActive.has_value());

	assert(manager.TryResetResumeCredential("profile-001", resetReason));
	assert(resetReason == "Resume credential reset. The next validated hello for this profile will receive a new token.");
	assert(!store.records.contains("profile-001"));
	assert(!manager.TryResetResumeCredential("profile-001", resetReason));
	assert(resetReason == "No resume credential exists for that profile.");
	assert(!manager.TryResetResumeCredential("bad/profile", resetReason));
	assert(resetReason == "profileId is invalid.");

	Json unknownResume = ValidHello("profile-unknown");
	unknownResume["resumeToken"] = "unknown-token";
	assert(manager.ValidateAndReserve("unknown", unknownResume, 2000.0).code == "invalid_resume");

	// The point of storing before sending: if the store refuses, the caller is
	// rejected and never receives a token, so the credential it already holds
	// keeps working instead of diverging from disk.
	FakeTokenService rotationTokens;
	MemoryResumeStore rotationStore;
	rotationStore.SaveResumeCredential("profile-rot", rotationTokens.HashToken("old-token"), std::nullopt, true, 9000.0);
	SessionIdentityManager rotationManager(requirements, rotationTokens, &rotationStore);
	Json rotationHello = ValidHello("profile-rot");
	rotationHello["resumeToken"] = "old-token";

	rotationStore.failNextSave = true;
	const auto refused = rotationManager.ValidateAndReserve("rot-1", rotationHello, 8000.0);
	assert(!refused.accepted);
	assert(refused.code == "resume_storage_failed");
	assert(rotationStore.records.at("profile-rot").tokenHash == rotationTokens.HashToken("old-token"));

	// The same hello succeeds once the store recovers, and the superseded token
	// keeps working until the new one is confirmed.
	const auto rotatedResult = rotationManager.ValidateAndReserve("rot-2", rotationHello, 8000.0);
	assert(rotatedResult.accepted);
	const auto rotatedStored = rotationStore.records.at("profile-rot");
	assert(!rotatedStored.confirmed);
	assert(rotatedStored.previousTokenHash.has_value());
	assert(*rotatedStored.previousTokenHash == rotationTokens.HashToken("old-token"));
	assert(rotationManager.CommitReady("rot-2"));
	assert(!rotationManager.ResumeForProfile("profile-rot")->confirmed);
	assert(rotationManager.ResumeForProfile("profile-rot")->previousTokenHash.has_value());
	rotationManager.ConnectionClosed("rot-2");
	const auto retryWithOld = rotationManager.ValidateAndReserve("rot-3", rotationHello, 8000.0);
	assert(retryWithOld.accepted);
	rotationManager.ConnectionClosed("rot-3");

	FakeTokenService persistedTokens;
	MemoryResumeStore persistedStore;
	persistedStore.SaveResumeCredential("profile-store", persistedTokens.HashToken("stored-token"), std::nullopt, true, 5000.0);
	SessionIdentityManager persistedManager(requirements, persistedTokens, &persistedStore);
	Json persistedHello = ValidHello("profile-store");
	persistedHello["resumeToken"] = "stored-token";
	assert(persistedManager.ValidateAndReserve("stored", persistedHello, 4000.0).accepted);
	persistedManager.ConnectionClosed("stored");

	FakeTokenService expiredTokens;
	MemoryResumeStore expiredStore;
	SessionIdentityManager expiredManager(
		F4T::Identity::Requirements{
			.clientVersion = std::string{ "1.2.0" },
			.falloutRuntimeVersion = std::string{ "1.11.240" },
			.contentManifestFingerprint = std::string(64, 'a'),
			.capabilities = { "interest-v1", "player-state-v1" },
			.resumeLifetimeSeconds = 10.0
		},
		expiredTokens,
		&expiredStore);
	Json expiring = ValidHello("profile-expire");
	assert(expiredManager.ValidateAndReserve("expire-1", expiring, 100.0).accepted);
	const auto expiringPending = expiredManager.PendingForReady("expire-1");
	assert(expiringPending.has_value());
	expiredManager.CommitReady("expire-1");
	expiredManager.ConnectionClosed("expire-1");
	expiring["resumeToken"] = expiringPending->resumeToken;
	assert(expiredManager.ValidateAndReserve("expire-2", expiring, 110.0).code == "resume_expired");

	// Tokenless recovery exists only for a first-ever issuance that the client
	// may never have received. Once a rotation has a predecessor the client is
	// known to hold a working credential, so the window must be shut.
	FakeTokenService windowTokens;
	MemoryResumeStore windowStore;
	SessionIdentityManager windowManager(requirements, windowTokens, &windowStore);
	Json windowHello = ValidHello("profile-window");
	assert(windowManager.ValidateAndReserve("win-1", windowHello, 6000.0).accepted);
	windowManager.ConnectionClosed("win-1");
	assert(!windowManager.ResumeForProfile("profile-window")->confirmed);
	assert(!windowManager.ResumeForProfile("profile-window")->previousTokenHash.has_value());
	assert(windowManager.ValidateAndReserve("win-2", windowHello, 6001.0).accepted);
	const auto windowPending = windowManager.PendingForReady("win-2");
	assert(windowPending.has_value());
	// A tokenless retry of a first-ever issuance leaves it first-ever: nothing the
	// client can be held to has been established yet.
	assert(!windowManager.ResumeForProfile("profile-window")->previousTokenHash.has_value());
	assert(windowManager.CommitReady("win-2"));
	assert(windowManager.ConfirmReady("win-2"));
	windowManager.ConnectionClosed("win-2");
	assert(windowManager.ValidateAndReserve("win-3", windowHello, 6002.0).code == "resume_required");

	Json windowResume = ValidHello("profile-window");
	windowResume["resumeToken"] = windowPending->resumeToken;
	assert(windowManager.ValidateAndReserve("win-3", windowResume, 6003.0).accepted);
	windowManager.ConnectionClosed("win-3");
	// The rotation from win-3 is unconfirmed and has a predecessor, so a hello
	// with no token at all is refused rather than treated as recovery.
	assert(windowManager.ValidateAndReserve("win-4", windowHello, 6004.0).code == "resume_required");
	// The credential the client actually authenticated with is what was kept, so
	// repeated interrupted handshakes never strip it of a working token.
	assert(windowManager.ValidateAndReserve("win-4", windowResume, 6004.0).accepted);
	windowManager.ConnectionClosed("win-4");
	assert(windowManager.ValidateAndReserve("win-5", windowResume, 6005.0).accepted);
	assert(windowManager.CommitReady("win-5"));
	assert(windowManager.ConfirmReady("win-5"));
	windowManager.ConnectionClosed("win-5");

	// A confirmation that cannot be persisted must not be claimed in memory. The
	// caller ends the session, and a restart reloading the record finds the same
	// unconfirmed rotation instead of a reopened tokenless path.
	FakeTokenService commitTokens;
	MemoryResumeStore commitStore;
	commitStore.SaveResumeCredential("profile-commit", commitTokens.HashToken("held-token"), std::nullopt, true, 9000.0);
	SessionIdentityManager commitManager(requirements, commitTokens, &commitStore);
	Json commitHello = ValidHello("profile-commit");
	commitHello["resumeToken"] = "held-token";
	assert(commitManager.ValidateAndReserve("commit-1", commitHello, 8000.0).accepted);
	const auto commitPending = commitManager.PendingForReady("commit-1");
	assert(commitPending.has_value());
	assert(commitManager.CommitReady("commit-1"));
	commitStore.failNextSave = true;
	assert(!commitManager.ConfirmReady("commit-1"));
	assert(!commitStore.records.at("profile-commit").confirmed);
	assert(commitStore.records.at("profile-commit").previousTokenHash.has_value());
	assert(commitManager.PendingForReady("commit-1").has_value());
	assert(commitManager.IsValidated("commit-1"));
	commitManager.ConnectionClosed("commit-1");

	MemoryResumeStore reloadedStore;
	reloadedStore.records = commitStore.records;
	SessionIdentityManager restartedManager(requirements, commitTokens, &reloadedStore);
	assert(restartedManager.ValidateAndReserve("restart-1", ValidHello("profile-commit"), 8001.0).code == "resume_required");
	assert(restartedManager.ValidateAndReserve("restart-1", commitHello, 8001.0).accepted);

	// The token the failed commit had already handed out still authenticates too.
	MemoryResumeStore reloadedAgain;
	reloadedAgain.records = commitStore.records;
	SessionIdentityManager restartedWithNew(requirements, commitTokens, &reloadedAgain);
	Json commitRotated = ValidHello("profile-commit");
	commitRotated["resumeToken"] = commitPending->resumeToken;
	assert(restartedWithNew.ValidateAndReserve("restart-2", commitRotated, 8002.0).accepted);
	assert(restartedWithNew.CommitReady("restart-2"));
	assert(restartedWithNew.ConfirmReady("restart-2"));
	assert(reloadedAgain.records.at("profile-commit").confirmed);

	FakeTokenService passwordTokens;
	MemoryResumeStore passwordStore;
	passwordStore.SaveResumeCredential("profile-pass", passwordTokens.HashToken("pass-token"), std::nullopt, true, 9000.0);
	SessionIdentityManager passwordManager(passworded, passwordTokens, &passwordStore);
	const auto savesBeforeRejections = passwordStore.saves;
	Json passwordResume = ValidHello("profile-pass");
	passwordResume["resumeToken"] = "pass-token";
	assert(passwordManager.ValidateAndReserve("pw-1", passwordResume, 8000.0).code == "password_required");
	Json wrongPasswordResume = passwordResume;
	wrongPasswordResume["serverPassword"] = "wrong-horse";
	assert(passwordManager.ValidateAndReserve("pw-2", wrongPasswordResume, 8000.0).code == "password_invalid");
	assert(passwordStore.saves == savesBeforeRejections);
	assert(passwordStore.records.at("profile-pass").tokenHash == passwordTokens.HashToken("pass-token"));
	assert(!passwordStore.records.at("profile-pass").previousTokenHash.has_value());

	Json acceptedPasswordResume = passwordResume;
	acceptedPasswordResume["serverPassword"] = "correct-horse";
	assert(passwordManager.ValidateAndReserve("pw-3", acceptedPasswordResume, 8000.0).accepted);
	assert(passwordStore.records.at("profile-pass").previousTokenHash.has_value());
	assert(*passwordStore.records.at("profile-pass").previousTokenHash == passwordTokens.HashToken("pass-token"));
	passwordManager.ConnectionClosed("pw-3");

	FakeTokenService modpackTokens;
	MemoryResumeStore modpackStore;
	modpackStore.SaveResumeCredential("profile-modpack", modpackTokens.HashToken("modpack-token"), std::nullopt, true, 9000.0);
	SessionIdentityManager modpackManager(modpacked, modpackTokens, &modpackStore);
	const auto savesBeforeModpack = modpackStore.saves;
	Json modpackResume = ValidHello("profile-modpack");
	modpackResume["resumeToken"] = "modpack-token";
	assert(modpackManager.ValidateAndReserve("mp-1", modpackResume, 8000.0).code == "modpack_mismatch");
	modpackResume["modpackFingerprint"] = std::string(64, 'd');
	assert(modpackManager.ValidateAndReserve("mp-2", modpackResume, 8000.0).code == "modpack_mismatch");
	assert(modpackStore.saves == savesBeforeModpack);
	assert(modpackStore.records.at("profile-modpack").tokenHash == modpackTokens.HashToken("modpack-token"));
	assert(!modpackStore.records.at("profile-modpack").previousTokenHash.has_value());
	modpackResume["modpackFingerprint"] = std::string(64, 'c');
	assert(modpackManager.ValidateAndReserve("mp-3", modpackResume, 8000.0).accepted);
	assert(modpackStore.records.at("profile-modpack").previousTokenHash.has_value());
	assert(*modpackStore.records.at("profile-modpack").previousTokenHash == modpackTokens.HashToken("modpack-token"));
	modpackManager.ConnectionClosed("mp-3");

	const auto hashA = tokens.HashToken("same");
	const auto hashB = tokens.HashToken("same");
	const auto hashC = tokens.HashToken("different");
	assert(FixedTimeEquals(hashA, hashB));
	assert(!FixedTimeEquals(hashA, hashC));

	const auto url256 = std::string{ "https://example.com/" } + std::string(236, 'a');
	const auto url257 = std::string{ "https://example.com/" } + std::string(237, 'a');
	assert(url256.size() == 256);
	assert(url257.size() == 257);
	assert(IsAllowedModpackPageUrl(url256));
	assert(!IsAllowedModpackPageUrl(url257));
	assert(IsAllowedModpackPageUrl("https://example.com/modpack"));
	assert(IsAllowedModpackPageUrl("http://127.0.0.1:7777/modpack"));
	assert(IsAllowedModpackPageUrl("http://localhost/modpack"));
	assert(!IsAllowedModpackPageUrl("http://localhost.evil.com/modpack"));
	assert(!IsAllowedModpackPageUrl("http://127.0.0.1@evil.com/modpack"));
	assert(!IsAllowedModpackPageUrl("https://user@example.com/modpack"));
	assert(!IsAllowedModpackPageUrl("https://"));
	assert(!IsAllowedModpackPageUrl("https://:443"));
	assert(!IsAllowedModpackPageUrl("https://example.com/modpack\r\n"));
	assert(!IsAllowedModpackPageUrl("https://exa mple.com/modpack"));
	assert(!IsAllowedModpackPageUrl("https://evil.com\\.good.com/modpack"));
	assert(!IsAllowedModpackPageUrl(""));
	assert(!IsAllowedModpackPageUrl("example.com/modpack"));
	assert(!NormalizeModpackUrl(std::nullopt));
	assert(!NormalizeModpackUrl(url257));
	assert(NormalizeModpackUrl(url256) == url256);
	assert(!NormalizeModpackUrl(std::string{ "  " }));

	{
		std::string reason = "Modpack fingerprint mismatch. Modpack page: https://example.com/modpack server build";
		assert(ExtractModpackPageUrl(reason) == "https://example.com/modpack");
		assert(reason == "Modpack fingerprint mismatch. server build");
	}
	{
		std::string reason = "Modpack fingerprint mismatch.";
		assert(ExtractModpackPageUrl(reason).empty());
		assert(reason == "Modpack fingerprint mismatch.");
	}
	{
		std::string reason = "A Modpack page: https://a.example/x then Modpack page: https://b.example/y end";
		assert(ExtractModpackPageUrl(reason) == "https://a.example/x");
		assert(ExtractModpackPageUrl(reason) == "https://b.example/y");
		assert(reason == "A then end");
	}
	{
		std::string reason = "mismatch Modpack page: https:// example";
		assert(ExtractModpackPageUrl(reason).empty());
		assert(reason == "mismatch example");
	}

	return 0;
}

#include "IdentityGate.h"
#include "SaveAttestationAuthority.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::Delivery;
	using CommonwealthOnline::Server::MemorySaveAttestationAuthority;
	using CommonwealthOnline::Server::OutboundFrame;
	using F4T::Identity::TokenHash;

	class FakeTokenService final : public F4T::Identity::TokenService
	{
	public:
		std::string CreateToken() override
		{
			return "gate-token-" + std::to_string(++counter);
		}

		TokenHash HashToken(std::string_view a_token) override
		{
			TokenHash hash{};
			for (std::size_t index = 0; index < hash.size(); ++index) {
				hash[index] = static_cast<std::uint8_t>(
					(index * 29u + a_token.size() * 17u + (a_token.empty() ? 0u : static_cast<unsigned char>(a_token[index % a_token.size()]))) & 0xffu);
			}
			return hash;
		}

		std::uint32_t counter{};
	};

	std::uint32_t secretCounter{};

	std::string NextSecret()
	{
		std::string secret(32, '0');
		auto value = ++secretCounter;
		for (std::size_t index = secret.size(); index > 0 && value != 0; --index) {
			secret[index - 1] = "0123456789abcdef"[value & 0xfu];
			value >>= 4;
		}
		return secret;
	}

	std::string TestProfileSecret(std::string_view a_profileId)
	{
		static std::map<std::string, std::string> secrets;
		auto& secret = secrets[std::string{ a_profileId }];
		if (secret.empty()) {
			secret = NextSecret();
		}
		return secret;
	}

	std::vector<std::uint8_t> Encode(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	Json Decode(const OutboundFrame& a_frame)
	{
		return Json::parse(std::string{
			reinterpret_cast<const char*>(a_frame.payload.data()),
			a_frame.payload.size()
		});
	}

	Json Hello(std::string_view a_profileId, std::string_view a_token = {}, std::string_view a_secret = {})
	{
		Json hello{
			{ "type", "hello" },
			{ "protocolVersion", 2 },
			{ "clientVersion", "1.2.0" },
			{ "falloutRuntimeVersion", "1.11.240" },
			{ "profileId", a_profileId },
			{ "contentManifestFingerprint", std::string(64, 'a') },
			{ "capabilities", Json::array({ "interest-v1", "player-state-v1", "save-attestation-v1" }) }
		};
		if (!a_token.empty()) {
			hello["resumeToken"] = a_token;
		}
		if (!a_secret.empty()) {
			hello["profileSecret"] = a_secret;
		}
		return hello;
	}

	std::vector<std::uint8_t> ReadyAck(std::string_view a_token = {})
	{
		Json packet{
			{ "type", "keepAlive" },
			{ "sessionReadyAck", true }
		};
		if (!a_token.empty()) {
			packet["resumeTokenAck"] = a_token;
		}
		return Encode(packet);
	}

	Json Attestation(std::string_view a_saveFile, std::string_view a_hash, std::string_view a_secret)
	{
		Json packet{
			{ "type", "saveAttested" },
			{ "saveFile", std::string{ a_saveFile } },
			{ "sha256", std::string{ a_hash } }
		};
		if (!a_secret.empty()) {
			packet["profileSecret"] = std::string{ a_secret };
		}
		return packet;
	}

	std::string ReadySecret(CommonwealthOnline::Server::IdentityGate& a_gate, std::uint32_t a_connectionId)
	{
		OutboundFrame ready{ a_connectionId, Delivery::ReliableOrdered, Encode(Json{ { "type", "sessionReady" } }) };
		assert(a_gate.EnrichSessionReady(ready));
		const auto enriched = Decode(ready);
		assert(enriched.contains("profileSecret"));
		return enriched.at("profileSecret").get<std::string>();
	}
}

int main()
{
	using CommonwealthOnline::Server::IdentityGate;

	FakeTokenService tokens;
	F4T::Identity::Requirements requirements;
	requirements.resumeLifetimeSeconds = 100.0;
	IdentityGate gate(requirements, tokens);

	const std::array<std::uint8_t, 4> malformed{ 'C', 'O', 'G', '2' };
	assert(gate.InspectInbound(1, malformed, 1000.0).forward);

	const auto keepAlive = Encode(Json{ { "type", "keepAlive" } });
	assert(gate.InspectInbound(1, keepAlive, 1000.0).forward);
	assert(!gate.IsValidated(1));

	const auto earlyAck = gate.InspectInbound(1, ReadyAck("guessed-token"), 1000.0);
	assert(!earlyAck.forward);
	assert(earlyAck.code == "handshake_required");

	const auto gameplay = Encode(Json{ { "type", "transform" } });
	const auto handshakeRequired = gate.InspectInbound(1, gameplay, 1000.0);
	assert(!handshakeRequired.forward);
	assert(handshakeRequired.code == "handshake_required");

	const auto helloA = Encode(Hello("profile-001"));
	assert(gate.InspectInbound(1, helloA, 1000.0).forward);
	assert(gate.IsValidated(1));

	const auto duplicate = gate.InspectInbound(2, helloA, 1000.1);
	assert(!duplicate.forward);
	assert(duplicate.code == "duplicate_identity");

	OutboundFrame nonReady{ 1, Delivery::ReliableOrdered, Encode(Json{ { "type", "welcome" } }) };
	assert(!gate.EnrichSessionReady(nonReady));

	OutboundFrame ready{ 1, Delivery::ReliableOrdered, Encode(Json{
		{ "type", "sessionReady" },
		{ "playerId", 7 },
		{ "protocolVersion", 2 }
	}) };
	assert(gate.EnrichSessionReady(ready));
	const auto enriched = Decode(ready);
	assert(enriched.at("profileId") == "profile-001");
	assert(enriched.at("clientVersion") == "1.2.0");
	assert(enriched.at("falloutRuntimeVersion") == "1.11.240");
	assert(enriched.at("contentManifestFingerprint") == std::string(64, 'a'));
	assert(enriched.at("resumeToken") == "gate-token-1");
	assert(enriched.at("resumeExpiresAt") == 1100.0);
	assert(enriched.at("resumeAckRequired") == true);
	assert(enriched.at("resumeAckViaKeepAlive") == true);
	assert(!enriched.contains("profileSecret"));

	gate.ConnectionClosed(1);
	assert(!gate.IsValidated(1));
	assert(gate.InspectInbound(3, helloA, 1001.0).forward);
	OutboundFrame uncommittedReady{ 3, Delivery::ReliableOrdered, Encode(Json{ { "type", "sessionReady" } }) };
	assert(gate.EnrichSessionReady(uncommittedReady));
	const auto token2 = Decode(uncommittedReady).at("resumeToken").get<std::string>();
	assert(token2 == "gate-token-2");
	assert(gate.CommitReady(3));

	const auto booleanOnlyAck = gate.InspectInbound(3, ReadyAck(), 1001.05);
	assert(!booleanOnlyAck.forward);
	assert(booleanOnlyAck.code == "invalid_resume_ack");
	const auto wrongAck = gate.InspectInbound(3, ReadyAck("gate-token-wrong"), 1001.06);
	assert(!wrongAck.forward);
	assert(wrongAck.code == "invalid_resume_ack");
	assert(gate.InspectInbound(3, ReadyAck(token2), 1001.1).forward);
	gate.ConnectionClosed(3);

	const auto resumeRequired = gate.InspectInbound(4, helloA, 1002.0);
	assert(!resumeRequired.forward);
	assert(resumeRequired.code == "resume_required");

	const auto resumedHello = Encode(Hello("profile-001", token2));
	assert(gate.InspectInbound(4, resumedHello, 1002.0).forward);
	OutboundFrame resumedReady{ 4, Delivery::ReliableOrdered, Encode(Json{ { "type", "sessionReady" } }) };
	assert(gate.EnrichSessionReady(resumedReady));
	const auto token3 = Decode(resumedReady).at("resumeToken").get<std::string>();
	assert(token3 == "gate-token-3");
	assert(gate.CommitReady(4));
	assert(gate.InspectInbound(4, ReadyAck(token3), 1002.1).forward);
	gate.ConnectionClosed(4);

	const auto oldToken = gate.InspectInbound(5, resumedHello, 1003.0);
	assert(!oldToken.forward);
	assert(oldToken.code == "invalid_resume");

	const auto rotatedHello = Encode(Hello("profile-001", token3));
	assert(gate.InspectInbound(5, rotatedHello, 1003.0).forward);
	std::string resetReason;
	assert(!gate.TryResetResumeCredential("profile-001", resetReason));
	assert(!resetReason.empty());
	gate.ConnectionClosed(5);

	resetReason.clear();
	assert(gate.TryResetResumeCredential("profile-001", resetReason));
	assert(!resetReason.empty());

	assert(gate.InspectInbound(6, helloA, 1004.0).forward);
	OutboundFrame freshReady{ 6, Delivery::ReliableOrdered, Encode(Json{ { "type", "sessionReady" } }) };
	assert(gate.EnrichSessionReady(freshReady));
	const auto freshToken = Decode(freshReady).at("resumeToken").get<std::string>();
	assert(!freshToken.empty());
	assert(freshToken != token3);
	assert(gate.CommitReady(6));
	assert(gate.InspectInbound(6, ReadyAck(freshToken), 1004.1).forward);
	gate.ConnectionClosed(6);

	const auto staleAfterReset = gate.InspectInbound(7, rotatedHello, 1005.0);
	assert(!staleAfterReset.forward);
	assert(staleAfterReset.code == "invalid_resume");

	FakeTokenService bannedTokens;
	std::size_t profileBanLookups = 0;
	IdentityGate bannedGate(requirements, bannedTokens, nullptr, [&](std::string_view profileId) -> std::optional<std::string> {
		++profileBanLookups;
		assert(profileId == "profile-002");
		return std::string{ "policy" };
	});
	const auto profileBanned = bannedGate.InspectInbound(8, Encode(Hello(" profile-002 ")), 1006.0);
	assert(!profileBanned.forward);
	assert(profileBanned.code == "profile_banned");
	assert(profileBanned.reason == "policy");
	assert(profileBanned.profileId.empty());
	assert(profileBanLookups == 1);
	assert(bannedTokens.counter == 0);
	assert(!bannedGate.IsValidated(8));

	IdentityGate openGate(requirements, tokens);
	const auto noCapabilities = Encode(Json{
		{ "type", "hello" },
		{ "protocolVersion", 2 },
		{ "clientVersion", "1.2.0" },
		{ "falloutRuntimeVersion", "1.11.240" },
		{ "profileId", "empty-capabilities-profile" },
		{ "contentManifestFingerprint", std::string(64, 'a') },
		{ "capabilities", Json::array() }
	});
	const auto missingCapability = openGate.InspectInbound(90, noCapabilities, 2000.0);
	assert(!missingCapability.forward);
	assert(missingCapability.code == "save_attestation_required");
	assert(!missingCapability.reason.empty());
	assert(!openGate.IsValidated(90));
	assert(openGate.InspectInbound(90, Encode(Hello("empty-capabilities-profile")), 2000.0).forward);
	assert(openGate.IsValidated(90));

	F4T::Identity::Requirements modpacked = requirements;
	modpacked.modpackFingerprint = std::string(64, 'c');
	modpacked.modpackManifestJson = std::string{ R"({"entries":[],"version":1})" };
	modpacked.modpackName = "Main List";
	IdentityGate modpackGate(modpacked, tokens);
	const auto modpackMismatch = modpackGate.InspectInbound(91, Encode(Hello("modpack-profile")), 2001.0);
	assert(!modpackMismatch.forward);
	assert(modpackMismatch.code == "modpack_mismatch");
	assert(modpackMismatch.requiredModpack.is_object());
	assert(modpackMismatch.requiredModpack.at("version") == 1);
	assert(modpackMismatch.modpackName == "Main List");
	Json matchingModpack = Hello("modpack-profile");
	matchingModpack["modpackFingerprint"] = std::string(64, 'c');
	assert(modpackGate.InspectInbound(92, Encode(matchingModpack), 2001.0).forward);
	const auto duplicateHello = modpackGate.InspectInbound(92, Encode(matchingModpack), 2001.1);
	assert(!duplicateHello.forward);
	assert(duplicateHello.code == "duplicate_hello");
	assert(duplicateHello.requiredModpack.is_null());

	const std::string multiplayerSave{ "Save9_ABCD1234_Nomad_Vault111Cryo_20260814000808_1_2" };
	const std::string multiplayerHash{ std::string(64, 'b') };
	const std::string operatorSecret{ "operator-secret" };
	const std::string joinedAdmin{ "server-admin-0001" };
	const std::string unjoinedAdmin{ "server-admin-0002" };

	FakeTokenService saveTokens;
	MemorySaveAttestationAuthority authority(TestProfileSecret);
	IdentityGate saveGate(
		requirements,
		saveTokens,
		nullptr,
		{},
		&authority,
		[&](std::string_view a_profileId) -> bool {
			return a_profileId == joinedAdmin || a_profileId == unjoinedAdmin;
		});

	const auto noClaim = saveGate.InspectInbound(100, Encode(Hello("solo-player-000001")), 3000.0);
	assert(noClaim.forward);
	assert(saveGate.IsValidated(100));
	const auto firstJoinSecret = ReadySecret(saveGate, 100);
	assert(!firstJoinSecret.empty());

	const auto refusedAttestation = saveGate.InspectInbound(
		100,
		Encode(Attestation("Save1_FFFF0000_Nomad_Commonwealth_20260101000000_1_1", std::string(64, 'c'), std::string(64, 'f'))),
		3000.1);
	assert(refusedAttestation.forward);
	assert(refusedAttestation.consumed);
	assert(refusedAttestation.rejected);
	assert(refusedAttestation.code == "save_attestation_unauthenticated");
	assert(refusedAttestation.response->at("type") == "saveAttestedRejected");
	assert(saveGate.RejectedAttestationCount() == 1);
	assert(authority.GetSaveAttestations("solo-player-000001").empty());

	const auto missingSecret = saveGate.InspectInbound(
		100,
		Encode(Attestation(multiplayerSave, multiplayerHash, "")),
		3000.2);
	assert(missingSecret.rejected);
	assert(saveGate.RejectedAttestationCount() == 2);
	assert(authority.GetSaveAttestations("solo-player-000001").empty());

	const auto recorded = saveGate.InspectInbound(
		100,
		Encode(Attestation(multiplayerSave, std::string(64, 'B'), firstJoinSecret)),
		3000.3);
	assert(recorded.forward);
	assert(!recorded.rejected);
	assert(recorded.response->at("type") == "saveAttestedAck");
	assert(recorded.response->at("sha256") == multiplayerHash);
	assert(recorded.response->at("recorded") == true);
	assert(authority.GetSaveAttestations("solo-player-000001").size() == 1);
	assert(saveGate.RejectedAttestationCount() == 2);

	const auto duplicateRecord = saveGate.InspectInbound(
		100,
		Encode(Attestation(multiplayerSave, multiplayerHash, firstJoinSecret)),
		3000.4);
	assert(duplicateRecord.forward);
	assert(duplicateRecord.response->at("recorded") == false);
	assert(authority.GetSaveAttestations("solo-player-000001").size() == 1);
	saveGate.ConnectionClosed(100);

	Json adminClaim = Hello(joinedAdmin);	const auto adminFirstJoin = saveGate.InspectInbound(101, Encode(adminClaim), 3000.5);
	assert(adminFirstJoin.forward);
	const auto adminSecret = ReadySecret(saveGate, 101);
	assert(!adminSecret.empty());
	OutboundFrame adminReady{ 101, Delivery::ReliableOrdered, Encode(Json{ { "type", "sessionReady" } }) };
	assert(saveGate.EnrichSessionReady(adminReady));
	assert(!Decode(adminReady).contains("multiplayerSaveAdmin"));
	saveGate.ConnectionClosed(101);

	const auto adminReturn = saveGate.InspectInbound(102, Encode(Hello(joinedAdmin, {}, adminSecret)), 3000.6);
	assert(adminReturn.forward);
	OutboundFrame adminReturnReady{ 102, Delivery::ReliableOrdered, Encode(Json{ { "type", "sessionReady" } }) };
	assert(saveGate.EnrichSessionReady(adminReturnReady));
	assert(Decode(adminReturnReady).at("multiplayerSaveAdmin") == true);

	Json unjoinedAdminHello = Hello(unjoinedAdmin);
	unjoinedAdminHello["multiplayerSaveAdmin"] = true;
	const auto unjoinedAdminJoin = saveGate.InspectInbound(104, Encode(unjoinedAdminHello), 3000.8);
	assert(unjoinedAdminJoin.forward);
	OutboundFrame unjoinedReady{ 104, Delivery::ReliableOrdered, Encode(Json{ { "type", "sessionReady" } }) };
	assert(saveGate.EnrichSessionReady(unjoinedReady));
	assert(!Decode(unjoinedReady).contains("multiplayerSaveAdmin"));
	saveGate.ConnectionClosed(104);

	Json forgedAdmin = Hello(unjoinedAdmin, {}, "wrong-secret");
	forgedAdmin["multiplayerSaveAdmin"] = true;
	forgedAdmin["saveAttestation"] = Json{
		{ "saveFile", multiplayerSave },
		{ "sha256", multiplayerHash }
	};
	const auto forgedAdminJoin = saveGate.InspectInbound(103, Encode(forgedAdmin), 3000.7);
	assert(!forgedAdminJoin.forward);
	assert(forgedAdminJoin.code == std::string{ F4T::SaveProfileLogic::SaveNotMultiplayerCode });

	const auto operatorSecretRefused =
		saveGate.InspectInbound(105, Encode(Hello(unjoinedAdmin, {}, operatorSecret)), 3000.9);
	assert(operatorSecretRefused.forward);
	OutboundFrame operatorSecretReady{ 105, Delivery::ReliableOrdered, Encode(Json{ { "type", "sessionReady" } }) };
	assert(saveGate.EnrichSessionReady(operatorSecretReady));
	assert(!Decode(operatorSecretReady).contains("multiplayerSaveAdmin"));
	const auto firstJoinAdminSecret = ReadySecret(saveGate, 105);
	assert(!firstJoinAdminSecret.empty());
	assert(firstJoinAdminSecret != operatorSecret);
	saveGate.ConnectionClosed(105);

	const auto returningAdmin = saveGate.InspectInbound(112, Encode(Hello(unjoinedAdmin, {}, firstJoinAdminSecret)), 3000.95);
	assert(returningAdmin.forward);
	OutboundFrame returningAdminReady{ 112, Delivery::ReliableOrdered, Encode(Json{ { "type", "sessionReady" } }) };
	assert(saveGate.EnrichSessionReady(returningAdminReady));
	assert(Decode(returningAdminReady).at("multiplayerSaveAdmin") == true);
	saveGate.ConnectionClosed(112);

	const auto earlyAttestation = saveGate.InspectInbound(
		106,
		Encode(Attestation(multiplayerSave, multiplayerHash, adminSecret)),
		3001.0);
	assert(!earlyAttestation.forward);
	assert(earlyAttestation.code == "handshake_required");

	assert(saveGate.InspectInbound(107, Encode(Hello("fresh-profile-0001")), 3001.1).forward);
	const auto freshSecret = ReadySecret(saveGate, 107);
	saveGate.ConnectionClosed(107);
	Json freshClaim = Hello("fresh-profile-0001", {}, freshSecret);
	freshClaim["saveAttestation"] = Json{
		{ "saveFile", multiplayerSave },
		{ "sha256", multiplayerHash }
	};
	const auto restoredFreshClaim = saveGate.InspectInbound(108, Encode(freshClaim), 3001.2);
	assert(restoredFreshClaim.forward);
	assert(saveGate.IsValidated(108));
	assert(authority.GetSaveAttestations("fresh-profile-0001").size() == 1);
	saveGate.ConnectionClosed(108);

	Json foreignSecretClaim = Hello("fresh-profile-0001", {}, firstJoinSecret);
	foreignSecretClaim["saveAttestation"] = Json{
		{ "saveFile", "Save1_FFFF0000_Nomad_Commonwealth_20260101000000_1_1" },
		{ "sha256", std::string(64, 'e') }
	};
	const auto foreignSecret = saveGate.InspectInbound(109, Encode(foreignSecretClaim), 3001.25);
	assert(!foreignSecret.forward);
	assert(foreignSecret.code == std::string{ F4T::SaveProfileLogic::SaveNotMultiplayerCode });

	Json tamperedClaim = Hello("solo-player-000001", {}, firstJoinSecret);
	tamperedClaim["saveAttestation"] = Json{
		{ "saveFile", multiplayerSave },
		{ "sha256", std::string(64, 'd') }
	};
	const auto tampered = saveGate.InspectInbound(109, Encode(tamperedClaim), 3001.3);
	assert(!tampered.forward);
	assert(tampered.code == std::string{ F4T::SaveProfileLogic::SaveNotMultiplayerCode });
	assert(authority.GetSaveAttestations("solo-player-000001").front().sha256Hex == multiplayerHash);
	saveGate.ConnectionClosed(109);

	Json attestedClaim = Hello("solo-player-000001", {}, firstJoinSecret);
	attestedClaim["saveAttestation"] = Json{
		{ "saveFile", multiplayerSave },
		{ "sha256", std::string(64, 'B') }
	};
	const auto attestedJoin = saveGate.InspectInbound(110, Encode(attestedClaim), 3001.4);
	assert(attestedJoin.forward);
	assert(attestedJoin.profileId == "solo-player-000001");
	assert(saveGate.IsValidated(110));
	const auto returningSecret = ReadySecret(saveGate, 110);
	assert(returningSecret == firstJoinSecret);

	const auto conflictingAttestation = saveGate.InspectInbound(
		110,
		Encode(Attestation(multiplayerSave, std::string(64, 'd'), firstJoinSecret)),
		3001.45);
	assert(conflictingAttestation.forward);
	assert(conflictingAttestation.rejected);
	assert(conflictingAttestation.code == std::string{ F4T::SaveProfileLogic::SaveNotMultiplayerCode });
	assert(conflictingAttestation.response->at("type") == "saveAttestedRejected");
	assert(saveGate.RejectedAttestationCount() == 3);
	assert(authority.GetSaveAttestations("solo-player-000001").size() == 1);
	assert(authority.GetSaveAttestations("solo-player-000001").front().sha256Hex == multiplayerHash);

	assert(saveGate.InspectInbound(111, Encode(Hello("rate-limited-00001")), 3001.5).forward);
	const auto rateSecret = ReadySecret(saveGate, 111);
	for (std::uint32_t index = 0; index < 64; ++index) {
		const auto accepted = saveGate.InspectInbound(
			111,
			Encode(Attestation(
				"Save" + std::to_string(index) + "_AAAA1111_Nomad_Commonwealth_000020_20260814000303_1_2",
				multiplayerHash,
				rateSecret)),
			3001.6);
		assert(accepted.forward);
		assert(!accepted.rejected);
	}
	assert(authority.GetSaveAttestations("rate-limited-00001").size() == 64);
	const auto rateLimited = saveGate.InspectInbound(
		111,
		Encode(Attestation("Save99_AAAA1111_Nomad_Commonwealth_000020_20260814000303_1_2", multiplayerHash, rateSecret)),
		3001.7);
	assert(rateLimited.forward);
	assert(rateLimited.rejected);
	assert(rateLimited.code == "save_attestation_rate_limited");
	assert(saveGate.RateLimitedAttestationCount() == 1);
	assert(authority.GetSaveAttestations("rate-limited-00001").size() == 64);

	const auto knownAfterLimit = saveGate.InspectInbound(
		111,
		Encode(Attestation("Save0_AAAA1111_Nomad_Commonwealth_000020_20260814000303_1_2", multiplayerHash, rateSecret)),
		3001.8);
	assert(knownAfterLimit.forward);
	assert(!knownAfterLimit.rejected);
	assert(knownAfterLimit.response->at("recorded") == false);

	const auto nextWindow = saveGate.InspectInbound(
		111,
		Encode(Attestation("Save98_AAAA1111_Nomad_Commonwealth_000020_20260814000303_1_2", multiplayerHash, rateSecret)),
		3100.0);
	assert(nextWindow.forward);
	assert(!nextWindow.rejected);
	assert(nextWindow.response->at("recorded") == true);

	MemorySaveAttestationAuthority memoryOnly(TestProfileSecret);
	IdentityGate memoryGate(requirements, saveTokens, nullptr, {}, &memoryOnly);
	assert(memoryGate.InspectInbound(120, Encode(Hello("memory-profile-001")), 4000.0).forward);
	const auto memorySecret = ReadySecret(memoryGate, 120);
	assert(memoryGate.InspectInbound(
		120,
		Encode(Attestation(multiplayerSave, multiplayerHash, memorySecret)),
		4000.1).response->at("recorded") == true);
	memoryGate.ConnectionClosed(120);
	Json memoryClaim = Hello("memory-profile-001", {}, memorySecret);
	memoryClaim["saveAttestation"] = Json{
		{ "saveFile", "Save1_FFFF0000_Nomad_Commonwealth_20260101000000_1_1" },
		{ "sha256", std::string(64, 'e') }
	};
	const auto memoryRecorded = memoryGate.InspectInbound(121, Encode(memoryClaim), 4000.2);
	assert(memoryRecorded.forward);
	assert(memoryOnly.GetSaveAttestations("memory-profile-001").size() == 2);
	memoryGate.ConnectionClosed(121);
	Json memoryAttested = Hello("memory-profile-001", {}, memorySecret);
	memoryAttested["saveAttestation"] = Json{
		{ "saveFile", multiplayerSave },
		{ "sha256", std::string(64, 'B') }
	};
	assert(memoryGate.InspectInbound(122, Encode(memoryAttested), 4000.3).forward);

	MemorySaveAttestationAuthority restarted(TestProfileSecret);
	IdentityGate restartedGate(requirements, saveTokens, nullptr, {}, &restarted);
	Json restartClaim = Hello("solo-player-000001", {}, firstJoinSecret);
	restartClaim["saveAttestation"] = Json{
		{ "saveFile", multiplayerSave },
		{ "sha256", multiplayerHash }
	};
	assert(restartedGate.InspectInbound(130, Encode(restartClaim), 5000.0).forward);
	assert(restarted.GetSaveAttestations("solo-player-000001").size() == 1);
	restartedGate.ConnectionClosed(130);

	Json legacyClaim = Hello("legacy-player-0001");
	legacyClaim["saveAttestation"] = Json{
		{ "saveFile", multiplayerSave },
		{ "sha256", multiplayerHash }
	};
	const auto grandfathered = saveGate.InspectInbound(113, Encode(legacyClaim), 3002.0);
	assert(grandfathered.forward);
	assert(authority.GetSaveAttestations("legacy-player-0001").size() == 1);
	const auto legacySecret = ReadySecret(saveGate, 113);
	assert(legacySecret == TestProfileSecret("legacy-player-0001"));
	saveGate.ConnectionClosed(113);

	Json overwriteClaim = Hello("legacy-player-0001");
	overwriteClaim["saveAttestation"] = Json{
		{ "saveFile", "Save1_FFFF0000_Nomad_Commonwealth_20260101000000_1_1" },
		{ "sha256", std::string(64, 'e') }
	};
	const auto overwriteRefused = saveGate.InspectInbound(114, Encode(overwriteClaim), 3002.1);
	assert(!overwriteRefused.forward);
	assert(overwriteRefused.code == std::string{ F4T::SaveProfileLogic::SaveNotMultiplayerCode });
	assert(authority.GetSaveAttestations("legacy-player-0001").size() == 1);
	Json legacyReturn = Hello("legacy-player-0001");
	legacyReturn["saveAttestation"] = legacyClaim["saveAttestation"];
	assert(saveGate.InspectInbound(115, Encode(legacyReturn), 3002.2).forward);
	saveGate.ConnectionClosed(115);

	saveGate.ConnectionClosed(110);
	std::string nearMiss = firstJoinSecret;
	nearMiss.back() = nearMiss.back() == '0' ? '1' : '0';
	for (const std::string& presented : { std::string{ "short" }, nearMiss, std::string(129, 'a'), std::string(32, 'z'), TestProfileSecret("fresh-profile-0001") }) {
		const auto malformedSecret = saveGate.InspectInbound(132, Encode(Hello("solo-player-000001", {}, presented)), 3002.3);
		assert(!malformedSecret.forward);
		assert(malformedSecret.code == std::string{ F4T::SaveProfileLogic::SaveNotMultiplayerCode });
	}

	const auto claimlessRebind = saveGate.InspectInbound(133, Encode(Hello("solo-player-000001", {}, firstJoinSecret)), 3002.4);
	assert(claimlessRebind.forward);
	assert(saveGate.IsValidated(133));
	saveGate.ConnectionClosed(133);
	assert(authority.GetSaveAttestations("solo-player-000001").size() == 1);

	FakeTokenService readmitTokens;
	MemorySaveAttestationAuthority readmitAuthority(TestProfileSecret);
	IdentityGate readmitGate(requirements, readmitTokens, nullptr, {}, &readmitAuthority);
	const std::string readmitSave{ "Save5_ABCD0005_Nomad_Commonwealth_20260814000505_1_2" };
	Json droppedClaim = Hello("readmit-profile-01");
	droppedClaim["saveAttestation"] = Json{ { "saveFile", readmitSave }, { "sha256", std::string(64, 'd') } };
	assert(readmitGate.InspectInbound(200, Encode(droppedClaim), 6000.0).forward);
	assert(ReadySecret(readmitGate, 200) == TestProfileSecret("readmit-profile-01"));
	readmitGate.ConnectionClosed(200);

	Json secretlessClaim = Hello("readmit-profile-01");
	secretlessClaim["saveAttestation"] = droppedClaim["saveAttestation"];
	const auto readmitted = readmitGate.InspectInbound(201, Encode(secretlessClaim), 6000.5);
	assert(readmitted.forward);
	OutboundFrame readmittedReady{ 201, Delivery::ReliableOrdered, Encode(Json{ { "type", "sessionReady" } }) };
	assert(readmitGate.EnrichSessionReady(readmittedReady));
	assert(Decode(readmittedReady).at("profileSecret").get<std::string>() == TestProfileSecret("readmit-profile-01"));
	const auto readmittedToken = Decode(readmittedReady).at("resumeToken").get<std::string>();
	assert(readmitGate.CommitReady(201));
	assert(readmitGate.InspectInbound(201, ReadyAck(readmittedToken), 6000.6).forward);
	readmitGate.ConnectionClosed(201);

	const auto confirmedRefused = readmitGate.InspectInbound(202, Encode(secretlessClaim), 6001.0);
	assert(!confirmedRefused.forward);
	assert(confirmedRefused.code == std::string{ F4T::SaveProfileLogic::SaveNotMultiplayerCode });
	assert(!readmitGate.IsValidated(202));

	const std::string strangerSave{ "Save6_EEEE0006_Nomad_Commonwealth_20260814000606_1_2" };
	assert(readmitAuthority.RecordSaveAttestation("readmit-profile-03", F4T::SaveProfileLogic::AttestedSave{ strangerSave, std::string(64, 'e'), 0 }));
	const std::string pendingSave{ "Save7_FFFF0007_Nomad_Commonwealth_20260814000707_1_2" };
	Json pendingClaim = Hello("readmit-profile-02");
	pendingClaim["saveAttestation"] = Json{ { "saveFile", pendingSave }, { "sha256", std::string(64, 'f') } };
	assert(readmitGate.InspectInbound(210, Encode(pendingClaim), 6002.0).forward);
	readmitGate.ConnectionClosed(210);

	Json strangerOwnClaim = Hello("readmit-profile-03");
	strangerOwnClaim["saveAttestation"] = Json{ { "saveFile", strangerSave }, { "sha256", std::string(64, 'e') } };
	const auto strangerOwn = readmitGate.InspectInbound(212, Encode(strangerOwnClaim), 6002.5);
	assert(!strangerOwn.forward);
	assert(strangerOwn.code == std::string{ F4T::SaveProfileLogic::SaveNotMultiplayerCode });

	Json strangerPendingClaim = Hello("readmit-profile-03");
	strangerPendingClaim["saveAttestation"] = pendingClaim["saveAttestation"];
	const auto strangerPending = readmitGate.InspectInbound(213, Encode(strangerPendingClaim), 6002.6);
	assert(!strangerPending.forward);
	assert(strangerPending.code == std::string{ F4T::SaveProfileLogic::SaveNotMultiplayerCode });

	assert(readmitGate.InspectInbound(211, Encode(pendingClaim), 6003.0).forward);
	assert(ReadySecret(readmitGate, 211) == TestProfileSecret("readmit-profile-02"));
	readmitGate.ConnectionClosed(211);

	const std::string lockoutSave{ "Save9_ABCD0009_Nomad_Commonwealth_20260814000909_1_2" };
	Json lockoutClaim = Hello("readmit-profile-04");
	lockoutClaim["saveAttestation"] = Json{ { "saveFile", lockoutSave }, { "sha256", std::string(64, '9') } };
	assert(readmitGate.InspectInbound(300, Encode(lockoutClaim), 6010.0).forward);
	assert(ReadySecret(readmitGate, 300) == TestProfileSecret("readmit-profile-04"));
	readmitGate.ConnectionClosed(300);
	Json lockoutSecretless = Hello("readmit-profile-04");
	lockoutSecretless["saveAttestation"] = lockoutClaim["saveAttestation"];
	assert(readmitGate.InspectInbound(301, Encode(lockoutSecretless), 6010.5).forward);
	const auto spentReadmission = readmitGate.InspectInbound(303, Encode(lockoutSecretless), 6010.6);
	assert(!spentReadmission.forward);
	assert(spentReadmission.code == std::string{ F4T::SaveProfileLogic::SaveNotMultiplayerCode });
	readmitGate.ConnectionClosed(301);
	assert(readmitGate.InspectInbound(302, Encode(lockoutSecretless), 6011.0).forward);
	readmitGate.ConnectionClosed(302);

	FakeTokenService expiryTokens;
	MemorySaveAttestationAuthority expiryAuthority(TestProfileSecret);
	IdentityGate expiryGate(requirements, expiryTokens, nullptr, {}, &expiryAuthority);
	const std::string expirySave{ "Save8_ABCD0008_Nomad_Commonwealth_20260814000808_1_2" };
	Json expiredClaim = Hello("expiry-profile-01");
	expiredClaim["saveAttestation"] = Json{ { "saveFile", expirySave }, { "sha256", std::string(64, 'a') } };
	assert(expiryGate.InspectInbound(220, Encode(expiredClaim), 7000.0).forward);
	expiryGate.ConnectionClosed(220);
	const auto expiredRefused = expiryGate.InspectInbound(221, Encode(expiredClaim), 7101.0);
	assert(!expiredRefused.forward);
	assert(expiredRefused.code == std::string{ F4T::SaveProfileLogic::SaveNotMultiplayerCode });

	Json liveClaim = Hello("expiry-profile-02");
	liveClaim["saveAttestation"] = Json{ { "saveFile", expirySave }, { "sha256", std::string(64, 'b') } };
	assert(expiryGate.InspectInbound(222, Encode(liveClaim), 7000.0).forward);
	expiryGate.ConnectionClosed(222);
	assert(expiryGate.InspectInbound(223, Encode(liveClaim), 7050.0).forward);
	assert(ReadySecret(expiryGate, 223) == TestProfileSecret("expiry-profile-02"));
	expiryGate.ConnectionClosed(223);
	const auto secondDrop = expiryGate.InspectInbound(224, Encode(liveClaim), 7060.0);
	assert(!secondDrop.forward);
	assert(secondDrop.code == std::string{ F4T::SaveProfileLogic::SaveNotMultiplayerCode });
	assert(!expiryGate.IsValidated(224));

	return 0;
}

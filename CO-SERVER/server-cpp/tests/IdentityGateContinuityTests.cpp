#include "IdentityGate.h"
#include "SaveAttestationAuthority.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::Delivery;
	using CommonwealthOnline::Server::IdentityGate;
	using CommonwealthOnline::Server::MemorySaveAttestationAuthority;
	using CommonwealthOnline::Server::OutboundFrame;
	using F4T::Identity::TokenHash;

	class FakeTokenService final : public F4T::Identity::TokenService
	{
	public:
		std::string CreateToken() override { return "continuity-token-" + std::to_string(++counter); }

		TokenHash HashToken(std::string_view a_token) override
		{
			TokenHash hash{};
			for (std::size_t index = 0; index < hash.size(); ++index) {
				hash[index] = static_cast<std::uint8_t>((index * 31u + a_token.size() + static_cast<unsigned char>(a_token[index % a_token.size()])) & 0xffu);
			}
			return hash;
		}

		std::uint32_t counter{};
	};

	const std::string kSave1{ "Save1_1EA93AE8_6A6F686E_Commonwealth_000006_20261108190000_1_2" };
	const std::string kSave2{ "Save2_1EA93AE8_6A6F686E_Commonwealth_000010_20261109002406_1_2" };
	const std::string kSave3{ "Save3_1EA93AE8_6A6F686E_Commonwealth_000011_20261109010000_1_2" };
	const std::string kHash1(64, '1');
	const std::string kHash2(64, '2');
	const std::string kHash3(64, '3');

	std::vector<std::uint8_t> Encode(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	struct Session
	{
		FakeTokenService tokens;
		F4T::Identity::Requirements requirements;
		MemorySaveAttestationAuthority authority{ [](std::string_view) { return std::string(32, 'a'); } };
		IdentityGate gate{ requirements, tokens, nullptr, {}, &authority, [](std::string_view) { return false; } };
		std::string profile;
		std::uint32_t connection{};
		std::string secret;
		double now{ 1000.0 };

		Session(std::string_view a_profile, std::uint32_t a_connection) : profile(a_profile), connection(a_connection)
		{
			Json hello{
				{ "type", "hello" },
				{ "protocolVersion", 2 },
				{ "clientVersion", "1.2.0" },
				{ "falloutRuntimeVersion", "1.11.240" },
				{ "profileId", profile },
				{ "contentManifestFingerprint", std::string(64, 'a') },
				{ "capabilities", Json::array({ "interest-v1", "player-state-v1", "save-attestation-v1" }) }
			};
			assert(gate.InspectInbound(connection, Encode(hello), now).forward);
			OutboundFrame ready{ connection, Delivery::ReliableOrdered, Encode(Json{ { "type", "sessionReady" } }) };
			assert(gate.EnrichSessionReady(ready));
			const auto enriched = Json::parse(std::string{ reinterpret_cast<const char*>(ready.payload.data()), ready.payload.size() });
			secret = enriched.at("profileSecret").get<std::string>();
		}

		Json Packet(const std::string& a_save, const std::string& a_hash) const
		{
			return Json{ { "type", "saveAttested" }, { "saveFile", a_save }, { "sha256", a_hash }, { "profileSecret", secret } };
		}

		Json Continuation(const std::string& a_save, const std::string& a_hash, const std::string& a_parent, const std::string& a_parentHash) const
		{
			auto packet = Packet(a_save, a_hash);
			packet["continues"] = Json{ { "saveFile", a_parent }, { "sha256", a_parentHash } };
			return packet;
		}

		auto Send(const Json& a_packet)
		{
			now += 0.1;
			return gate.InspectInbound(connection, Encode(a_packet), now);
		}

		std::size_t Recorded() const { return authority.GetSaveAttestations(profile).size(); }
	};

	void ExpectRefused(const CommonwealthOnline::Server::IdentityIngressDecision& a_result, std::string_view a_code, bool a_carriesSave = true)
	{
		assert(a_result.consumed);
		assert(a_result.rejected);
		assert(a_result.code == a_code);
		assert(a_result.response->at("type") == "saveAttestedRejected");
		assert(a_result.response->contains("saveFile") == a_carriesSave && a_result.response->contains("sha256") == a_carriesSave);
	}

	void TestContinuationOfHeadIsRecordedAndReplayIsIdempotent()
	{
		Session session{ "continuity-profile-001", 1 };
		assert(!session.Send(session.Packet(kSave1, kHash1)).rejected);
		const auto accepted = session.Send(session.Continuation(kSave2, kHash2, kSave1, kHash1));
		assert(!accepted.rejected);
		assert(accepted.response->at("type") == "saveAttestedAck");
		assert(accepted.response->at("recorded") == true);
		assert(session.Recorded() == 2);

		const auto replay = session.Send(session.Continuation(kSave2, kHash2, kSave1, kHash1));
		assert(!replay.rejected);
		assert(replay.response->at("recorded") == false);
		assert(session.Recorded() == 2);
	}

	void TestForkOlderUnknownParentAndOtherCharacterAreRefusedAndRecordNothing()
	{
		Session session{ "continuity-profile-002", 2 };
		assert(!session.Send(session.Packet(kSave1, kHash1)).rejected);
		assert(!session.Send(session.Continuation(kSave2, kHash2, kSave1, kHash1)).rejected);
		assert(session.Recorded() == 2);

		session.now += 61.0;
		ExpectRefused(session.Send(session.Continuation(kSave3, kHash3, kSave1, kHash1)), "save_continuity_not_head");
		ExpectRefused(
			session.Send(session.Continuation("Save9_1EA93AE8_6A6F686E_Commonwealth_000001_20261108000000_1_2", kHash3, kSave2, kHash2)),
			"save_continuity_not_newer");
		ExpectRefused(session.Send(session.Continuation(kSave3, kHash3, kSave2, kHash1)), "save_continuity_unknown_parent");
		ExpectRefused(
			session.Send(session.Continuation("Save3_FFFF0000_6A6F686E_Commonwealth_000011_20261109010000_1_2", kHash3, kSave2, kHash2)),
			"save_continuity_character");
		assert(session.Recorded() == 2);
	}

	void TestMalformedOrUnauthenticatedContinuationIsRefused()
	{
		Session session{ "continuity-profile-003", 3 };
		assert(!session.Send(session.Packet(kSave1, kHash1)).rejected);
		auto malformed = session.Packet(kSave2, kHash2);
		malformed["continues"] = "not-an-object";
		ExpectRefused(session.Send(malformed), "save_continuity_invalid");
		auto shortHash = session.Continuation(kSave2, kHash2, kSave1, "abc");
		ExpectRefused(session.Send(shortHash), "save_continuity_invalid");

		auto unauthenticated = session.Continuation(kSave2, kHash2, kSave1, kHash1);
		unauthenticated.erase("profileSecret");
		ExpectRefused(session.Send(unauthenticated), "save_attestation_unauthenticated", false);
		assert(session.Recorded() == 1);
	}

	void TestContinuationAttemptsAreRateLimitedEvenWhenRefused()
	{
		Session session{ "continuity-profile-004", 4 };
		assert(!session.Send(session.Packet(kSave1, kHash1)).rejected);
		for (int attempt = 0; attempt < 4; ++attempt) {
			const auto name = "Save" + std::to_string(20 + attempt) + "_1EA93AE8_6A6F686E_Commonwealth_000020_2026110902000" + std::to_string(attempt) + "_1_2";
			ExpectRefused(session.Send(session.Continuation(name, kHash3, kSave2, kHash2)), "save_continuity_unknown_parent");
		}
		ExpectRefused(session.Send(session.Continuation(kSave2, kHash2, kSave1, kHash1)), "save_attestation_rate_limited");
		assert(session.Recorded() == 1);
		assert(session.gate.RateLimitedAttestationCount() == 1);

		session.now += 61.0;
		assert(!session.Send(session.Continuation(kSave2, kHash2, kSave1, kHash1)).rejected);
		assert(session.Recorded() == 2);
	}

	void TestPlainAttestationFromAnOldClientStillRecords()
	{
		Session session{ "continuity-profile-005", 5 };
		assert(!session.Send(session.Packet(kSave1, kHash1)).rejected);
		const auto plain = session.Send(session.Packet(kSave3, kHash3));
		assert(!plain.rejected);
		assert(plain.response->at("recorded") == true);
		assert(session.Recorded() == 2);
	}
}

int main()
{
	TestContinuationOfHeadIsRecordedAndReplayIsIdempotent();
	TestForkOlderUnknownParentAndOtherCharacterAreRefusedAndRecordNothing();
	TestMalformedOrUnauthenticatedContinuationIsRefused();
	TestContinuationAttemptsAreRateLimitedEvenWhenRefused();
	TestPlainAttestationFromAnOldClientStillRecords();
	return 0;
}

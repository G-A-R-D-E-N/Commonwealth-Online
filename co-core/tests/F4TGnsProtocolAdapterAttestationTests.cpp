#include "F4TGnsProtocolAdapter.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace
{
	using Json = nlohmann::json;
	using Session = F4T::GnsProtocolAdapter::Session;
	using ReadyIdentity = F4T::GnsProtocolAdapter::ReadyIdentity;

	constexpr auto kTestFingerprint =
		"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

	F4T::ClientCompatibility::Metadata CompleteMetadata()
	{
		return F4T::ClientCompatibility::Metadata{
			.buildTarget = F4T::ClientCompatibility::BuildTarget::Current,
			.clientVersion = std::string{ F4T::ClientCompatibility::kClientVersion },
			.falloutRuntimeVersion = std::string{ F4T::ClientCompatibility::kCurrentRuntimeVersion },
			.contentManifestFingerprint = kTestFingerprint,
			.capabilities = {}
		};
	}

	F4T::GnsProtocolAdapter::ClientIdentity IdentityFixture(
		std::optional<std::string> a_resumeToken = std::optional<std::string>{ "previous-server-token" })
	{
		return F4T::GnsProtocolAdapter::ClientIdentity{
			.profileId = "co-12345678",
			.metadata = CompleteMetadata(),
			.resumeToken = std::move(a_resumeToken)
		};
	}

	Json ReadyIdentityFixture(std::uint32_t a_playerId = 7)
	{
		return Json{
			{ "type", "sessionReady" },
			{ "playerId", a_playerId },
			{ "protocolVersion", 2 },
			{ "serverProtocolVersion", 2 },
			{ "worldStateHostPlayerId", a_playerId },
			{ "profileId", "co-12345678" },
			{ "clientVersion", std::string{ F4T::ClientCompatibility::kClientVersion } },
			{ "falloutRuntimeVersion", std::string{ F4T::ClientCompatibility::kCurrentRuntimeVersion } },
			{ "contentManifestFingerprint", kTestFingerprint },
			{ "resumeToken", "rotated-token" },
			{ "resumeExpiresAt", 1234567890.5 }
		};
	}

	void ConfigureIdentity(Session& a_session, bool a_commit = true)
	{
		a_session.SetClientIdentity(
			IdentityFixture(),
			[a_commit](const ReadyIdentity&, std::string& a_error) {
				if (a_commit) {
					return true;
				}
				a_error = "simulated profile-store commit failure";
				return false;
			});
	}

	void StartIdentityHandshake(Session& a_session, std::uint32_t a_playerId = 7)
	{
		const auto welcome = a_session.AdaptInbound(Json{
			{ "type", "welcome" },
			{ "playerId", a_playerId },
			{ "protocolVersion", 2 }
		}.dump());
		assert(!welcome.protocolViolation);
		assert(!welcome.localHandshakeFailure);
		assert(welcome.controlMessages.size() == 1);
	}

	void TestClientIdentityEnrichedHello()
	{
		Session session;
		ConfigureIdentity(session);
		const auto welcome = session.AdaptInbound(Json{
			{ "type", "welcome" },
			{ "playerId", 7 },
			{ "protocolVersion", 2 }
		}.dump());
		assert(!welcome.protocolViolation);
		const auto hello = Json::parse(welcome.controlMessages[0].payload);
		assert(hello.at("clientVersion") == std::string{ F4T::ClientCompatibility::kClientVersion });
		assert(hello.at("falloutRuntimeVersion") == std::string{ F4T::ClientCompatibility::kCurrentRuntimeVersion });
		assert(hello.at("contentManifestFingerprint") == kTestFingerprint);
		assert(hello.at("profileId") == "co-12345678");
		assert(hello.at("resumeToken") == "previous-server-token");
		assert(hello.at("capabilities").is_array());
		assert(!hello.contains("saveAttestation"));
	}

	void TestClientIdentitySaveAttestationIsSentOnlyWhenSet()
	{
		Session session;
		auto identity = IdentityFixture();
		identity.saveAttestation = F4T::SaveProfileLogic::AttestedSave{
			"Save3_AAAA1111_Nomad_Commonwealth_000020_20260814000303_1_2",
			std::string(64, 'c'),
			0
		};
		session.SetClientIdentity(std::move(identity), [](const ReadyIdentity&, std::string&) { return true; });
		const auto sent = session.AdaptInbound(Json{
			{ "type", "welcome" }, { "playerId", 7 }, { "protocolVersion", 2 }
		}.dump());
		assert(!sent.protocolViolation);
		const auto hello = Json::parse(sent.controlMessages[0].payload);
		assert(hello.at("saveAttestation").at("saveFile") == "Save3_AAAA1111_Nomad_Commonwealth_000020_20260814000303_1_2");
		assert(hello.at("saveAttestation").at("sha256") == std::string(64, 'c'));
	}

	void TestClientIdentityServerPasswordIsSentOnlyWhenSet()
	{
		Session withPassword;
		auto identity = IdentityFixture();
		identity.serverPassword = std::string{ "correct-horse" };
		withPassword.SetClientIdentity(std::move(identity), [](const ReadyIdentity&, std::string&) { return true; });
		const auto sent = withPassword.AdaptInbound(Json{
			{ "type", "welcome" }, { "playerId", 7 }, { "protocolVersion", 2 }
		}.dump());
		assert(!sent.protocolViolation);
		assert(Json::parse(sent.controlMessages[0].payload).at("serverPassword") == "correct-horse");

		Session withoutPassword;
		ConfigureIdentity(withoutPassword);
		const auto unsent = withoutPassword.AdaptInbound(Json{
			{ "type", "welcome" }, { "playerId", 7 }, { "protocolVersion", 2 }
		}.dump());
		assert(!unsent.protocolViolation);
		assert(!Json::parse(unsent.controlMessages[0].payload).contains("serverPassword"));
	}

	void TestClientIdentityModpackFingerprintIsSentOnlyWhenSet()
	{
		Session withModpack;
		auto identity = IdentityFixture();
		identity.modpackFingerprint = std::string(64, 'c');
		withModpack.SetClientIdentity(std::move(identity), [](const ReadyIdentity&, std::string&) { return true; });
		const auto sent = withModpack.AdaptInbound(Json{
			{ "type", "welcome" }, { "playerId", 7 }, { "protocolVersion", 2 }
		}.dump());
		assert(!sent.protocolViolation);
		assert(Json::parse(sent.controlMessages[0].payload).at("modpackFingerprint") == std::string(64, 'c'));

		Session withoutModpack;
		ConfigureIdentity(withoutModpack);
		const auto unsent = withoutModpack.AdaptInbound(Json{
			{ "type", "welcome" }, { "playerId", 7 }, { "protocolVersion", 2 }
		}.dump());
		assert(!unsent.protocolViolation);
		assert(!Json::parse(unsent.controlMessages[0].payload).contains("modpackFingerprint"));
	}

	void TestClientIdentityProfileSecretIsSentOnlyWhenSet()
	{
		Session withSecret;
		auto identity = IdentityFixture();
		identity.profileSecret = std::string(32, 'a');
		withSecret.SetClientIdentity(std::move(identity), [](const ReadyIdentity&, std::string&) { return true; });
		const auto sent = withSecret.AdaptInbound(Json{
			{ "type", "welcome" }, { "playerId", 7 }, { "protocolVersion", 2 }
		}.dump());
		assert(!sent.protocolViolation);
		assert(Json::parse(sent.controlMessages[0].payload).at("profileSecret") == std::string(32, 'a'));

		Session withoutSecret;
		ConfigureIdentity(withoutSecret);
		const auto unsent = withoutSecret.AdaptInbound(Json{
			{ "type", "welcome" }, { "playerId", 7 }, { "protocolVersion", 2 }
		}.dump());
		assert(!unsent.protocolViolation);
		assert(!Json::parse(unsent.controlMessages[0].payload).contains("profileSecret"));
	}

	void TestClientIdentitySteamIdIsSentOnlyWhenSet()
	{
		Session withSteamId;
		auto identity = IdentityFixture();
		identity.steamId = std::string{ "76561198000000000" };
		identity.steamTicket = std::string{ "14000000ABCDEF" };
		withSteamId.SetClientIdentity(std::move(identity), [](const ReadyIdentity&, std::string&) { return true; });
		const auto sent = withSteamId.AdaptInbound(Json{
			{ "type", "welcome" }, { "playerId", 7 }, { "protocolVersion", 2 }
		}.dump());
		assert(!sent.protocolViolation);
		assert(Json::parse(sent.controlMessages[0].payload).at("steamId") == "76561198000000000");
		assert(Json::parse(sent.controlMessages[0].payload).at("steamTicket") == "14000000ABCDEF");

		Session withoutSteamId;
		ConfigureIdentity(withoutSteamId);
		const auto unsent = withoutSteamId.AdaptInbound(Json{
			{ "type", "welcome" }, { "playerId", 7 }, { "protocolVersion", 2 }
		}.dump());
		assert(!unsent.protocolViolation);
		assert(!Json::parse(unsent.controlMessages[0].payload).contains("steamId"));
		assert(!Json::parse(unsent.controlMessages[0].payload).contains("steamTicket"));
	}

	void TestClientIdentityIncompleteFailsClosed()
	{
		Session session;
		auto identity = IdentityFixture();
		identity.metadata.contentManifestFingerprint.clear();
		session.SetClientIdentity(std::move(identity), [](const ReadyIdentity&, std::string&) { return true; });
		const auto welcome = session.AdaptInbound(Json{
			{ "type", "welcome" }, { "playerId", 7 }, { "protocolVersion", 2 }
		}.dump());
		assert(welcome.protocolViolation);
		assert(welcome.controlMessages.empty());
	}

	void TestClientIdentityMissingProfileIdFailsClosed()
	{
		Session session;
		auto identity = IdentityFixture();
		identity.profileId.clear();
		session.SetClientIdentity(std::move(identity), [](const ReadyIdentity&, std::string&) { return true; });
		const auto welcome = session.AdaptInbound(Json{
			{ "type", "welcome" }, { "playerId", 7 }, { "protocolVersion", 2 }
		}.dump());
		assert(welcome.protocolViolation);
		assert(welcome.controlMessages.empty());
	}

	void TestResetClearsClientIdentity()
	{
		Session session;
		ConfigureIdentity(session);
		session.Reset();
		const auto welcome = session.AdaptInbound(Json{
			{ "type", "welcome" }, { "playerId", 7 }, { "protocolVersion", 2 }
		}.dump());
		assert(!welcome.protocolViolation);
		assert(!Json::parse(welcome.controlMessages[0].payload).contains("clientVersion"));
	}

	void ExpectReadyIdentity(
		const Json& a_ready,
		bool a_expectedAdmin,
		const std::optional<std::string>& a_expectedSecret)
	{
		Session session;
		ReadyIdentity captured;
		bool committed = false;
		session.SetClientIdentity(
			IdentityFixture(),
			[&](const ReadyIdentity& a_identity, std::string&) {
				captured = a_identity;
				committed = true;
				return true;
			});
		StartIdentityHandshake(session);
		const auto result = session.AdaptInbound(a_ready.dump());
		assert(!result.protocolViolation);
		assert(!result.localHandshakeFailure);
		assert(committed);
		assert(captured.multiplayerSaveAdmin == a_expectedAdmin);
		assert(captured.profileSecret == a_expectedSecret);
	}

	void TestSessionReadyAdminFlagAndProfileSecret()
	{
		const std::string secret{ "0123456789abcdef0123456789abcdef" };
		ExpectReadyIdentity(ReadyIdentityFixture(), false, std::nullopt);

		auto truthy = ReadyIdentityFixture();
		truthy["multiplayerSaveAdmin"] = true;
		truthy["profileSecret"] = secret;
		ExpectReadyIdentity(truthy, true, secret);

		auto numeric = ReadyIdentityFixture();
		numeric["multiplayerSaveAdmin"] = 1;
		ExpectReadyIdentity(numeric, false, std::nullopt);

		auto textual = ReadyIdentityFixture();
		textual["multiplayerSaveAdmin"] = "true";
		textual["profileSecret"] = 32;
		ExpectReadyIdentity(textual, false, std::nullopt);

		auto emptySecret = ReadyIdentityFixture();
		emptySecret["profileSecret"] = "";
		ExpectReadyIdentity(emptySecret, false, std::nullopt);
	}

	void TestIdentityLessSessionReadyHasNoResume()
	{
		Session session;
		const auto welcome = session.AdaptInbound(Json{
			{ "type", "welcome" }, { "playerId", 7 }, { "protocolVersion", 2 }
		}.dump());
		assert(!welcome.protocolViolation);
		const auto ready = session.AdaptInbound(Json{
			{ "type", "sessionReady" },
			{ "playerId", 7 },
			{ "protocolVersion", 2 },
			{ "serverProtocolVersion", 2 },
			{ "worldStateHostPlayerId", 7 }
		}.dump());
		assert(!ready.protocolViolation);
		assert(!ready.localHandshakeFailure);
		assert(!ready.readyIdentity);
		assert(session.IsReady());
	}
}

int main()
{
	TestClientIdentityEnrichedHello();
	TestClientIdentitySaveAttestationIsSentOnlyWhenSet();
	TestClientIdentityServerPasswordIsSentOnlyWhenSet();
	TestClientIdentityModpackFingerprintIsSentOnlyWhenSet();
	TestClientIdentityProfileSecretIsSentOnlyWhenSet();
	TestClientIdentitySteamIdIsSentOnlyWhenSet();
	TestClientIdentityIncompleteFailsClosed();
	TestClientIdentityMissingProfileIdFailsClosed();
	TestResetClearsClientIdentity();
	TestSessionReadyAdminFlagAndProfileSecret();
	TestIdentityLessSessionReadyHasNoResume();
	return 0;
}

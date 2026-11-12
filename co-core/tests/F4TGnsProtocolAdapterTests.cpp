#include "F4TGnsProtocolAdapter.h"
#include "F4TNpcAuthority.h"
#include "F4TPlayerStateValidation.h"
#include "F4TRemotePlayerCapacity.h"

#include <nlohmann/json.hpp>

#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#include <cstdlib>
#endif

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <utility>
// TODO-verify: concept-level wire and plan checks, engine facts unverified; source:CodeNotes-GARDEN power-armor-sync.

void TestSurvivalPolicyInbound();

namespace
{
	using Json = nlohmann::json;
	using Session = F4T::GnsProtocolAdapter::Session;
	using Delivery = F4T::GnsProtocolAdapter::Delivery;
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

	std::string RemoteTransform(std::uint32_t a_playerId, double a_x, double a_y, double a_z, double a_angleZ)
	{
		return Json{
			{ "type", "transform" },
			{ "playerId", a_playerId },
			{ "x", a_x }, { "y", a_y }, { "z", a_z },
			{ "angleZ", a_angleZ },
			{ "cellId", "0000003C" },
			{ "worldspaceId", "0000003D" }
		}.dump();
	}

	void CompleteHandshake(Session& a_session, std::uint32_t a_playerId = 7, bool a_ordered = false)
	{
		const auto welcome = a_session.AdaptInbound(Json{
			{ "type", "welcome" },
			{ "playerId", a_playerId },
			{ "protocolVersion", 2 }
		}.dump());
		assert(!welcome.protocolViolation);
		assert(welcome.legacyMessages.empty());
		assert(welcome.controlMessages.size() == 1);
		assert(welcome.controlMessages[0].delivery == Delivery::ReliableOrdered);
		const auto hello = Json::parse(welcome.controlMessages[0].payload);
		assert(hello.at("type") == "hello");
		assert(hello.at("protocolVersion") == 2);
		assert(!hello.contains("clientVersion"));
		assert(!hello.contains("profileId"));

		const auto ready = a_session.AdaptInbound(Json{
			{ "type", "sessionReady" },
			{ "playerId", a_playerId },
			{ "protocolVersion", 2 },
			{ "serverProtocolVersion", 2 },
			{ "worldStateHostPlayerId", a_playerId },
			{ "orderedPlayerVisibility", a_ordered },
			{ "visibilityPauseSupported", a_ordered },
			{ "authoritativeDownedSupported", a_ordered },
			{ "consoleAllowed", true },
			{ "startingInventoryRecovery", nullptr }
		}.dump());
		assert(!ready.protocolViolation);
		assert(!ready.localHandshakeFailure);
		assert(ready.controlMessages.empty());
		assert(ready.legacyMessages.size() == 1);
		assert(a_session.IsReady());
		const auto exposedWelcome = Json::parse(ready.legacyMessages[0]);
		assert(exposedWelcome.at("playerId") == a_playerId);
		assert(exposedWelcome.at("worldStateHostPlayerId") == a_playerId);
		assert(exposedWelcome.at("consoleAllowed") == true);
		assert(exposedWelcome.at("startingInventoryRecovery").is_null());
		assert(exposedWelcome.at("resumed") == false);
	}

	void ExpectReadyProtocolViolation(Json a_ready)
	{
		Session session;
		ConfigureIdentity(session);
		StartIdentityHandshake(session);
		const auto result = session.AdaptInbound(a_ready.dump());
		assert(result.protocolViolation);
		assert(!result.localHandshakeFailure);
		assert(!result.rejection);
		assert(result.legacyMessages.empty());
		assert(!session.IsReady());
	}

	void ExpectPreReadyRejection(Json a_packet, std::string_view a_code, std::string_view a_reason)
	{
		Session session;
		const auto result = session.AdaptInbound(a_packet.dump());
		assert(!result.protocolViolation);
		assert(!result.localHandshakeFailure);
		assert(result.rejection);
		assert(result.rejection->code == a_code);
		assert(result.rejection->reason == a_reason);
		assert(result.legacyMessages.empty());
		assert(!session.IsReady());
	}

	void TestHandshakeBarrier()
	{
		Session session;
		assert(session.AdaptOutbound("{\"type\":\"keepAlive\"}\n").empty());
		CompleteHandshake(session);
		const auto messages = session.AdaptOutbound("{\"type\":\"keepAlive\"}\n");
		assert(messages.size() == 1);
		assert(messages[0].delivery == Delivery::ReliableOrdered);

		Session incompatible;
		const auto result = incompatible.AdaptInbound(Json{
			{ "type", "welcome" },
			{ "playerId", 1 },
			{ "protocolVersion", 1 }
		}.dump());
		assert(result.protocolViolation);
	}

	void TestTransformAndPlayerStateSplit()
	{
		Session session;
		CompleteHandshake(session);
		Json transform{
			{ "type", "transform" },
			{ "x", 1.0 },
			{ "y", 2.0 },
			{ "z", 3.0 },
			{ "angleZ", 4.0 },
			{ "cellId", "0000003C" },
			{ "worldspaceId", "0000003D" },
			{ "equippedItems", Json::array({ Json{ { "slot", "right" }, { "formId", "00001234" } } }) },
			{ "appearance", Json{ { "version", 4 } } },
			{ "actionEvents", Json::array({ Json{ { "type", 1 }, { "sequence", 2 }, { "eventName", "meleeattackStart" } } }) },
			{ "characterName", "Vault Dweller" }
		};
		const auto messages = session.AdaptOutbound(transform.dump() + "\n");
		assert(messages.size() == 2);
		assert(messages[0].delivery == Delivery::UnreliableSequenced);
		assert(messages[1].delivery == Delivery::ReliableOrdered);
		const auto wireTransform = Json::parse(messages[0].payload);
		assert(!wireTransform.contains("equippedItems"));
		assert(!wireTransform.contains("appearance"));
		assert(!wireTransform.contains("actionEvents"));
		assert(!wireTransform.contains("characterName"));
		const auto playerState = Json::parse(messages[1].payload);
		assert(playerState.at("characterName") == "Vault Dweller");
		assert(playerState.at("appearance").is_object());
		assert(playerState.at("actionEvents").size() == 1);
		transform.erase("actionEvents");
		const auto unchanged = session.AdaptOutbound(transform.dump());
		assert(unchanged.size() == 1);
		assert(unchanged[0].delivery == Delivery::UnreliableSequenced);
	}

	void TestOptionalPlayerStateFieldsAreOmitted()
	{
		Session session;
		CompleteHandshake(session);
		Json transform{
			{ "type", "transform" },
			{ "x", 1.0 },
			{ "y", 2.0 },
			{ "z", 3.0 },
			{ "angleZ", 4.0 },
			{ "cellId", "0000003C" },
			{ "worldspaceId", "0000003D" },
			{ "equippedItems", Json::array() },
			{ "actionEvents", Json::array() },
			{ "characterName", "" }
		};
		const auto messages = session.AdaptOutbound(transform.dump());
		assert(messages.size() == 2);
		const auto playerState = Json::parse(messages[1].payload);
		assert(playerState.at("equippedItems").is_array());
		assert(playerState.at("actionEvents").is_array());
		assert(!playerState.contains("appearance"));
		assert(!playerState.contains("characterName"));
		assert(!playerState.contains("powerArmor"));
		const auto relayed = F4T::PlayerStateValidation::NormalizePlayerState(playerState);
		assert(relayed.has_value());
		assert(!relayed->contains("powerArmor"));
	}

	void TestPowerArmorRoundTrip()
	{
		Session session;
		CompleteHandshake(session);
		const Json transform{
			{ "type", "transform" },
			{ "x", 1.0 },
			{ "y", 2.0 },
			{ "z", 3.0 },
			{ "angleZ", 4.0 },
			{ "cellId", "0000003C" },
			{ "worldspaceId", "0000003D" },
			{ "equippedItems", Json::array({ Json{ { "slot", "head" }, { "formId", "00001234" } } }) },
			{ "powerArmor", Json{
				{ "inFrame", true },
				{ "frameBaseFormId", "00005678" },
				{ "pieces", Json::array({ Json{ { "slot", "head" }, { "formId", "00001234" } } }) }
			} }
		};
		const auto messages = session.AdaptOutbound(transform.dump() + "\n");
		assert(messages.size() == 2);
		const auto wireTransform = Json::parse(messages[0].payload);
		assert(!wireTransform.contains("powerArmor"));
		const auto playerState = Json::parse(messages[1].payload);
		assert(playerState.at("powerArmor").at("inFrame") == true);
		assert(playerState.at("powerArmor").at("frameBaseFormId") == "00005678");
		assert(playerState.at("powerArmor").at("pieces").size() == 1);

		const auto relayed = F4T::PlayerStateValidation::NormalizePlayerState(playerState);
		assert(relayed.has_value());
		assert(relayed->at("powerArmor").at("pieces").size() == 1);

		Session peer;
		CompleteHandshake(peer);
		Json relayedState = *relayed;
		relayedState["playerId"] = 21;
		const auto stateFirst = peer.AdaptInbound(relayedState.dump());
		assert(stateFirst.legacyMessages.empty());
		const auto relayedTransform = peer.AdaptInbound(RemoteTransform(21, 1.0, 2.0, 3.0, 4.0));
		assert(relayedTransform.legacyMessages.size() == 1);
		const auto merged = Json::parse(relayedTransform.legacyMessages[0]);
		assert(merged.at("powerArmor").at("inFrame") == true);
		assert(merged.at("powerArmor").at("frameBaseFormId") == "00005678");
		assert(merged.at("powerArmor").at("pieces").size() == 1);
		assert(merged.at("equippedItems").size() == 1);
	}

	void TestRemotePlayerStateMerge()
	{
		Session session;
		CompleteHandshake(session);
		const auto stateBeforeTransform = session.AdaptInbound(Json{
			{ "type", "playerState" },
			{ "playerId", 11 },
			{ "equippedItems", Json::array({ Json{ { "slot", "right" }, { "formId", "00009999" } } }) },
			{ "appearance", Json{ { "version", 4 } } },
			{ "actionEvents", Json::array() },
			{ "characterName", "Remote" }, { "displayName", "Remote Shown" }
		}.dump());
		assert(stateBeforeTransform.legacyMessages.empty());
		const auto transform = session.AdaptInbound(RemoteTransform(11, 1.0, 2.0, 3.0, 4.0));
		assert(transform.legacyMessages.size() == 1);
		const auto merged = Json::parse(transform.legacyMessages[0]);
		assert(merged.at("characterName") == "Remote" && merged.at("displayName") == "Remote Shown");
		assert(merged.at("equippedItems").size() == 1);
		const auto action = session.AdaptInbound(Json{
			{ "type", "playerState" },
			{ "playerId", 11 },
			{ "actionEvents", Json::array({ Json{ { "type", 1 }, { "sequence", 9 }, { "eventName", "meleeattackStart" } } }) }
		}.dump());
		assert(action.legacyMessages.size() == 1);
		const auto actionMerged = Json::parse(action.legacyMessages[0]);
		assert(actionMerged.at("actionEvents").size() == 1);
		assert(actionMerged.at("characterName") == "Remote" && actionMerged.at("displayName") == "Remote Shown");
		assert(actionMerged.at("equippedItems").size() == 1);
		assert(actionMerged.at("appearance").is_object());
	}

	void TestRemoteActionsBeforeFirstTransform()
	{
		Session session;
		CompleteHandshake(session);
		const auto firstAction = session.AdaptInbound(Json{
			{ "type", "playerState" },
			{ "playerId", 12 },
			{ "characterName", "Early Action" },
			{ "actionEvents", Json::array({ Json{ { "type", 3 }, { "sequence", 1 }, { "eventName", "fireSingle" } } }) }
		}.dump());
		assert(firstAction.legacyMessages.empty());
		const auto secondAction = session.AdaptInbound(Json{
			{ "type", "playerState" },
			{ "playerId", 12 },
			{ "actionEvents", Json::array({ Json{ { "type", 3 }, { "sequence", 2 }, { "eventName", "fireSingle" } } }) }
		}.dump());
		assert(secondAction.legacyMessages.empty());
		const auto transform = session.AdaptInbound(RemoteTransform(12, 10.0, 20.0, 30.0, 1.0));
		assert(transform.legacyMessages.size() == 1);
		const auto merged = Json::parse(transform.legacyMessages[0]);
		assert(merged.at("characterName") == "Early Action");
		assert(merged.at("actionEvents").size() == 2);
		assert(merged.at("actionEvents")[0].at("sequence") == 1);
		assert(merged.at("actionEvents")[1].at("sequence") == 2);
		const auto laterTransform = session.AdaptInbound(RemoteTransform(12, 11.0, 20.0, 30.0, 1.0));
		assert(laterTransform.legacyMessages.size() == 1);
		assert(!Json::parse(laterTransform.legacyMessages[0]).contains("actionEvents"));
	}

	void TestWorldPolicyInbound()
	{
		Session session;
		CompleteHandshake(session, 7);
		assert(!session.GetWorldPolicy().has_value());

		Json packet{
			{ "type", "worldPolicy" },
			{ "mode", "free-roam" },
			{ "version", 4u },
			{ "disable", Json::array({ "01CA6E:Fallout4.esm", "01D162:Fallout4.esm" }) },
			{ "doors", "shared" },
			{ "suppressQuests", true }
		};
		const auto applied = session.AdaptInbound(packet.dump());
		assert(!applied.protocolViolation);
		assert(applied.legacyMessages.empty());
		const auto policy = session.GetWorldPolicy();
		assert(policy);
		assert(policy->mode == F4T::WorldPolicy::Mode::FreeRoam);
		assert(policy->version == 4u);
		assert(policy->doors == F4T::WorldPolicy::Doors::Shared);
		assert(policy->suppressQuests);
		assert(policy->disable.size() == 2);
		assert(policy->disable[0].plugin == "Fallout4.esm");
		assert(policy->disable[0].localId == 0x1CA6Eu);

		packet["mode"] = "coop-story";
		packet["suppressQuests"] = false;
		packet["disable"] = Json::array();
		assert(!session.AdaptInbound(packet.dump()).protocolViolation);
		assert(session.GetWorldPolicy()->mode == F4T::WorldPolicy::Mode::CoopStory);
		assert(!session.GetWorldPolicy()->suppressQuests);

		auto malformed = packet;
		malformed["mode"] = "sandbox";
		assert(!session.AdaptInbound(malformed.dump()).protocolViolation);
		assert(session.GetWorldPolicy()->mode == F4T::WorldPolicy::Mode::CoopStory);

		session.Reset();
		assert(!session.GetWorldPolicy().has_value());
	}

	void TestWorldPolicyChunkedInbound()
	{
		Session session;
		CompleteHandshake(session, 7);
		assert(!session.GetWorldPolicy().has_value());

		F4T::WorldPolicy::Policy policy;
		policy.mode = F4T::WorldPolicy::Mode::FreeRoam;
		policy.version = 3u;
		policy.suppressQuests = true;
		policy.doors = F4T::WorldPolicy::Doors::Vanilla;
		for (std::size_t index = 0; index < F4T::WorldPolicy::kMaximumDisableEntriesPerMessage * 2 + 1; ++index) {
			policy.disable.push_back(F4T::WorldPolicy::FormKey{
				"Fallout4.esm",
				static_cast<std::uint32_t>(index + 1) });
		}

		const auto messages = F4T::WorldPolicy::EncodeMessages(policy);
		assert(messages.size() == 3);
		for (std::size_t index = 0; index + 1 < messages.size(); ++index) {
			const auto applied = session.AdaptInbound(messages.at(index).dump());
			assert(!applied.protocolViolation);
			assert(applied.legacyMessages.empty());
			assert(!session.GetWorldPolicy().has_value());
		}

		const auto applied = session.AdaptInbound(messages.back().dump());
		assert(!applied.protocolViolation);
		assert(applied.legacyMessages.empty());
		const auto received = session.GetWorldPolicy();
		assert(received && *received == policy);

		const auto complete = session.AdaptInbound(F4T::WorldPolicy::ToJson(policy).dump());
		assert(!complete.protocolViolation);
		assert(session.GetWorldPolicy()->disable.size() == policy.disable.size());

		session.Reset();
		assert(!session.GetWorldPolicy().has_value());
		CompleteHandshake(session, 7);
		assert(!session.AdaptInbound(messages.back().dump()).protocolViolation);
		assert(!session.GetWorldPolicy().has_value());
	}

	void TestPreReadySessionEndedVariants()
	{
		ExpectPreReadyRejection(Json{ { "type", "sessionEnded" }, { "code", "server_full" }, { "reason", "Capacity reached" } }, "server_full", "Capacity reached");
		ExpectPreReadyRejection(Json{ { "type", "sessionEnded" }, { "code", "server_full" } }, "server_full", "");
		ExpectPreReadyRejection(Json{ { "type", "sessionEnded" }, { "reason", "Capacity reached" } }, "", "Capacity reached");
		ExpectPreReadyRejection(Json{ { "type", "sessionEnded" } }, "", "");
	}

	void TestPostReadySessionEndedRemainsLegacy()
	{
		Session session;
		CompleteHandshake(session);
		const auto result = session.AdaptInbound(Json{
			{ "type", "sessionEnded" }, { "reason", "maintenance" }
		}.dump());
		assert(!result.protocolViolation);
		assert(!result.localHandshakeFailure);
		assert(!result.rejection);
		assert(result.legacyMessages.size() == 1);
	}

	void TestUnrelatedPreReadyTrafficIsViolation()
	{
		Session session;
		const auto result = session.AdaptInbound(Json{
			{ "type", "transform" }, { "playerId", 2 }
		}.dump());
		assert(result.protocolViolation);
		assert(!result.rejection);
	}

	void TestSessionReadyCommitSuccess()
	{
		Session session;
		bool committed = false;
		session.SetClientIdentity(
			IdentityFixture(),
			[&](const ReadyIdentity& a_identity, std::string&) {
				assert(a_identity.profileId == "co-12345678");
				assert(a_identity.resumeToken == "rotated-token");
				assert(a_identity.resumeExpiresAt == 1234567890.5);
				committed = true;
				return true;
			});
		StartIdentityHandshake(session);
		auto packet = ReadyIdentityFixture();
		packet["startingInventoryRecovery"] = Json{ { "type", "startingInventoryRecovery" }, { "profileId", "co-12345678:slot:0" }, { "grantId", "starting-inventory" }, { "state", "intent" } };
		const auto ready = session.AdaptInbound(packet.dump());
		assert(Json::parse(ready.legacyMessages.at(0)).at("startingInventoryRecovery") == packet.at("startingInventoryRecovery"));
		assert(committed);
		assert(!ready.protocolViolation);
		assert(!ready.localHandshakeFailure);
		assert(ready.readyIdentity);
		assert(ready.legacyMessages.size() == 1);
		assert(session.IsReady());
	}

	void TestSessionReadyCommitFailureIsLocal()
	{
		Session session;
		ConfigureIdentity(session, false);
		StartIdentityHandshake(session);
		const auto ready = session.AdaptInbound(ReadyIdentityFixture().dump());
		assert(!ready.protocolViolation);
		assert(ready.localHandshakeFailure);
		assert(ready.error == "simulated profile-store commit failure");
		assert(!ready.readyIdentity);
		assert(ready.legacyMessages.empty());
		assert(!session.IsReady());
	}

	void TestSessionReadyIdentityMismatches()
	{
		auto ready = ReadyIdentityFixture();
		ready["profileId"] = "co-other";
		ExpectReadyProtocolViolation(ready);
		ready = ReadyIdentityFixture();
		ready["clientVersion"] = "wrong";
		ExpectReadyProtocolViolation(ready);
		ready = ReadyIdentityFixture();
		ready["falloutRuntimeVersion"] = "wrong";
		ExpectReadyProtocolViolation(ready);
		ready = ReadyIdentityFixture();
		ready["contentManifestFingerprint"] = std::string(64, 'b');
		ExpectReadyProtocolViolation(ready);
	}

	void TestSessionReadyCredentialValidation()
	{
		auto ready = ReadyIdentityFixture();
		ready.erase("resumeToken");
		ExpectReadyProtocolViolation(ready);
		ready = ReadyIdentityFixture();
		ready.erase("resumeExpiresAt");
		ExpectReadyProtocolViolation(ready);
		ready = ReadyIdentityFixture();
		ready["resumeExpiresAt"] = 0.0;
		ExpectReadyProtocolViolation(ready);
		ready = ReadyIdentityFixture();
		ready["resumeExpiresAt"] = -1.0;
		ExpectReadyProtocolViolation(ready);
		ready = ReadyIdentityFixture();
		ready["resumeExpiresAt"] = std::numeric_limits<double>::quiet_NaN();
		ExpectReadyProtocolViolation(ready);
		ready = ReadyIdentityFixture();
		ready["resumeExpiresAt"] = std::numeric_limits<double>::infinity();
		ExpectReadyProtocolViolation(ready);
	}

	void TestResumedSessionAdoptsServerPlayerId()
	{
		Session session;
		ConfigureIdentity(session);
		StartIdentityHandshake(session);
		auto ready = ReadyIdentityFixture(9);
		ready["resumed"] = true;
		const auto result = session.AdaptInbound(ready.dump());
		assert(!result.protocolViolation);
		assert(!result.localHandshakeFailure);
		assert(result.readyIdentity);
		assert(session.IsReady());
		assert(result.legacyMessages.size() == 1);
		assert(Json::parse(result.legacyMessages[0]).at("playerId") == 9);
		assert(Json::parse(result.legacyMessages[0]).at("resumed") == true);

		auto zeroId = ReadyIdentityFixture(0);
		zeroId["resumed"] = true;
		ExpectReadyProtocolViolation(zeroId);

		auto unflagged = ReadyIdentityFixture(9);
		ExpectReadyProtocolViolation(unflagged);
	}

	void TestNpcAuthority()
	{
		Session session;
		CompleteHandshake(session, 7);
		const auto authority = session.AdaptInbound(Json{
			{ "type", "npcAuthority" },
			{ "authorityPlayerId", 7 },
			{ "authorityEpoch", 3 },
			{ "authorityCellId", "0000003C" },
			{ "authorityWorldspaceId", "0000003D" }
		}.dump());
		assert(authority.legacyMessages.empty());
		const auto assignment = session.GetNpcAuthority("0000003C", "0000003D");
		assert(assignment);
		assert(assignment->playerId == 7);
		assert(assignment->epoch == 3);
		Json npcState{
			{ "type", "npcState" },
			{ "fullReplace", true },
			{ "npcs", Json::array({ Json{
				{ "npcId", 1 },
				{ "cellId", "0000003C" },
				{ "worldspaceId", "0000003D" }
			} }) }
		};
		const auto allowed = session.AdaptOutbound(npcState.dump());
		assert(allowed.size() == 1);
		assert(allowed[0].delivery == Delivery::UnreliableSequenced);
		const auto scoped = Json::parse(allowed[0].payload);
		assert(scoped.at("authorityEpoch") == 3);
		assert(scoped.at("authorityCellId") == "0000003C");
		assert(scoped.at("authorityWorldspaceId") == "0000003D");
		session.AdaptInbound(Json{
			{ "type", "npcAuthority" },
			{ "authorityPlayerId", 12 },
			{ "authorityEpoch", 4 },
			{ "authorityCellId", "0000003C" },
			{ "authorityWorldspaceId", "0000003D" }
		}.dump());
		assert(session.AdaptOutbound(npcState.dump()).empty());
	}

	void TestNpcAuthorityHolderZeroErasesScope()
	{
		Session session;
		CompleteHandshake(session, 7);
		session.AdaptInbound(Json{
			{ "type", "npcAuthority" },
			{ "authorityPlayerId", 7 },
			{ "authorityEpoch", 3 },
			{ "authorityCellId", "0000003C" },
			{ "authorityWorldspaceId", "0000003D" }
		}.dump());
		assert(session.GetNpcAuthority("0000003C", "0000003D"));
		session.AdaptInbound(Json{
			{ "type", "npcAuthority" },
			{ "authorityPlayerId", 0 },
			{ "authorityEpoch", 4 },
			{ "authorityCellId", "0000003C" },
			{ "authorityWorldspaceId", "0000003D" }
		}.dump());
		assert(!session.GetNpcAuthority("0000003C", "0000003D"));
	}

	void TestNpcAuthorityHolderZeroErasesTranslatedScope()
	{
		Session session;
		CompleteHandshake(session, 7);
		session.SetFormIdTranslator([](std::uint32_t a_playerId, std::uint32_t a_formId) {
			if (a_playerId != 12) {
				return a_formId;
			}
			if (a_formId == 0x0100ABCDu) {
				return 0x0200ABCDu;
			}
			if (a_formId == 0x0100BEEFu) {
				return 0x0300BEEFu;
			}
			return a_formId;
		});
		session.AdaptInbound(Json{
			{ "type", "npcAuthority" },
			{ "authorityPlayerId", 12 },
			{ "authorityEpoch", 5 },
			{ "authorityCellId", "0100ABCD" },
			{ "authorityWorldspaceId", "0100BEEF" }
		}.dump());
		session.AdaptInbound(Json{
			{ "type", "npcAuthority" },
			{ "authorityPlayerId", 7 },
			{ "authorityEpoch", 3 },
			{ "authorityCellId", "0000003C" },
			{ "authorityWorldspaceId", "0000003D" }
		}.dump());
		assert(session.GetNpcAuthority("0200ABCD", "0300BEEF"));
		session.AdaptInbound(Json{
			{ "type", "npcAuthority" },
			{ "authorityPlayerId", 0 },
			{ "authorityEpoch", 6 },
			{ "authorityCellId", "0100ABCD" },
			{ "authorityWorldspaceId", "0100BEEF" }
		}.dump());
		assert(!session.GetNpcAuthority("0200ABCD", "0300BEEF"));
		assert(session.GetNpcAuthority("0000003C", "0000003D"));
	}

	void TestNpcAuthorityRemovalSurvivesForgottenMapping()
	{
		Session session;
		CompleteHandshake(session, 7);
		session.SetFormIdTranslator([](std::uint32_t a_playerId, std::uint32_t a_formId) {
			if (a_playerId == 12 && a_formId == 0x0100ABCDu) {
				return 0x0200ABCDu;
			}
			return a_formId;
		});
		session.AdaptInbound(Json{
			{ "type", "npcAuthority" },
			{ "authorityPlayerId", 12 },
			{ "authorityEpoch", 5 },
			{ "authorityCellId", "0100ABCD" },
			{ "authorityWorldspaceId", "" }
		}.dump());
		assert(session.GetNpcAuthority("0200ABCD", ""));
		session.SetFormIdTranslator([](std::uint32_t, std::uint32_t a_formId) {
			return a_formId;
		});
		session.AdaptInbound(Json{
			{ "type", "npcAuthority" },
			{ "authorityPlayerId", 7 },
			{ "authorityEpoch", 6 },
			{ "authorityCellId", "0100ABCD" },
			{ "authorityWorldspaceId", "" }
		}.dump());
		assert(!session.GetNpcAuthority("0200ABCD", ""));
		const auto handedOff = session.GetNpcAuthority("0100ABCD", "");
		assert(handedOff && handedOff->playerId == 7);
		session.AdaptInbound(Json{
			{ "type", "npcAuthority" },
			{ "authorityPlayerId", 0 },
			{ "authorityEpoch", 7 },
			{ "authorityCellId", "0100ABCD" },
			{ "authorityWorldspaceId", "" }
		}.dump());
		assert(!session.GetNpcAuthority("0100ABCD", ""));
	}

	void TestNpcAuthorityScopeIsTranslated()
	{
		Session session;
		CompleteHandshake(session, 7);
		session.SetFormIdTranslator([](std::uint32_t a_playerId, std::uint32_t a_formId) {
			if (a_playerId != 12) {
				return a_formId;
			}
			if (a_formId == 0x0100ABCDu) {
				return 0x0200ABCDu;
			}
			if (a_formId == 0x0100BEEFu) {
				return 0x0300BEEFu;
			}
			return a_formId;
		});
		session.AdaptInbound(Json{
			{ "type", "npcAuthority" },
			{ "authorityPlayerId", 12 },
			{ "authorityEpoch", 5 },
			{ "authorityCellId", "0100ABCD" },
			{ "authorityWorldspaceId", "0100BEEF" }
		}.dump());
		const auto translated = session.GetNpcAuthority("0200ABCD", "0300BEEF");
		assert(translated);
		assert(translated->playerId == 12);
		assert(translated->epoch == 5);
		assert(!session.GetNpcAuthority("0100ABCD", "0100BEEF"));
	}

	void TestNpcStateAuthorityFieldsAreAdapterOwned()
	{
		Session session;
		CompleteHandshake(session, 7);
		session.AdaptInbound(Json{
			{ "type", "npcAuthority" },
			{ "authorityPlayerId", 7 },
			{ "authorityEpoch", 3 },
			{ "authorityCellId", "0000003C" },
			{ "authorityWorldspaceId", "0000003D" }
		}.dump());
		Json clientPacket{
			{ "type", "npcState" },
			{ "fullReplace", true },
			{ "authorityEpoch", 99 },
			{ "authorityCellId", "000000AA" },
			{ "authorityWorldspaceId", "000000BB" },
			{ "npcs", Json::array({ Json{
				{ "npcId", 1 },
				{ "cellId", "0000003C" },
				{ "worldspaceId", "0000003D" },
				{ "health", 80.0 },
				{ "maxHealth", 100.0 }
			} }) }
		};
		const auto scoped = session.AdaptOutbound(clientPacket.dump());
		assert(scoped.size() == 1);
		const auto packet = Json::parse(scoped[0].payload);
		assert(packet.at("authorityEpoch") == 3);
		assert(packet.at("authorityCellId") == "0000003C");
		assert(packet.at("authorityWorldspaceId") == "0000003D");
		assert(packet.at("npcs").size() == 1);
		assert(packet.at("npcs")[0].at("health") == 80.0);
		assert(packet.at("npcs")[0].at("maxHealth") == 100.0);

		clientPacket["npcs"][0]["cellId"] = "000000AA";
		assert(session.AdaptOutbound(clientPacket.dump()).empty());
	}

	void TestPlayerVitalsRoundTrip()
	{
		Session session;
		CompleteHandshake(session);

		const auto outbound = session.AdaptOutbound(Json{
			{ "type", "playerState" },
			{ "vitals", Json{ { "healthPercent", 87 }, { "isDowned", false }, { "isDead", false }, { "level", 42 } } }
		}.dump());
		assert(outbound.size() == 1);
		assert(outbound[0].delivery == Delivery::ReliableOrdered);
		const auto sent = Json::parse(outbound[0].payload);
		assert(sent.at("type") == "playerState");
		assert(sent.at("vitals").at("healthPercent") == 87);
		assert(sent.at("vitals").at("level") == 42);

		const auto vitalsBeforeTransform = session.AdaptInbound(Json{
			{ "type", "playerState" },
			{ "playerId", 21 },
			{ "vitals", Json{ { "healthPercent", 30 }, { "isDowned", true } } }
		}.dump());
		assert(vitalsBeforeTransform.legacyMessages.empty());

		const auto firstPendingVitals = session.AdaptInbound(Json{
			{ "type", "playerState" },
			{ "playerId", 22 },
			{ "vitals", Json{ { "healthPercent", 70 } } }
		}.dump());
		assert(firstPendingVitals.legacyMessages.empty());
		const auto secondPendingVitals = session.AdaptInbound(Json{
			{ "type", "playerState" },
			{ "playerId", 22 },
			{ "vitals", Json{ { "healthPercent", 55 }, { "isDowned", true } } }
		}.dump());
		assert(secondPendingVitals.legacyMessages.empty());
		const auto pendingVitalsTransform = session.AdaptInbound(RemoteTransform(22, 9.0, 2.0, 3.0, 4.0));
		assert(pendingVitalsTransform.legacyMessages.size() == 1);
		const auto mergedPendingVitals = Json::parse(pendingVitalsTransform.legacyMessages[0]);
		assert(mergedPendingVitals.at("vitals").at("healthPercent") == 55);
		assert(mergedPendingVitals.at("vitals").at("isDowned") == true);

		const auto transform = session.AdaptInbound(RemoteTransform(21, 1.0, 2.0, 3.0, 4.0));
		assert(transform.legacyMessages.size() == 1);
		const auto mergedTransform = Json::parse(transform.legacyMessages[0]);
		assert(mergedTransform.at("vitals").at("healthPercent") == 30);
		assert(mergedTransform.at("vitals").at("isDowned") == true);

		const auto vitalsAfterTransform = session.AdaptInbound(Json{
			{ "type", "playerState" },
			{ "playerId", 21 },
			{ "vitals", Json{ { "healthPercent", 12 }, { "isDead", true } } }
		}.dump());
		assert(vitalsAfterTransform.legacyMessages.size() == 1);
		const auto synthesized = Json::parse(vitalsAfterTransform.legacyMessages[0]);
		assert(synthesized.at("vitals").at("healthPercent") == 12);
		assert(synthesized.at("vitals").at("isDead") == true);
		assert(!synthesized.at("vitals").contains("isDowned"));
		assert(synthesized.at("x") == 1.0);

		const auto laterTransform = session.AdaptInbound(RemoteTransform(21, 5.0, 2.0, 3.0, 4.0));
		assert(laterTransform.legacyMessages.size() == 1);
		assert(!Json::parse(laterTransform.legacyMessages[0]).contains("vitals"));

		const auto nameOnly = session.AdaptInbound(Json{
			{ "type", "playerState" },
			{ "playerId", 21 },
			{ "characterName", "Nomad" }
		}.dump());
		assert(nameOnly.legacyMessages.size() == 1);
		assert(!Json::parse(nameOnly.legacyMessages[0]).contains("vitals"));
	}

	void TestSessionReadyAckFlagIsValidatedBeforeCommit()
	{
		Session session;
		bool committed = false;
		session.SetClientIdentity(IdentityFixture(), [&committed](const ReadyIdentity&, std::string&) {
			committed = true;
			return true;
		});
		StartIdentityHandshake(session);

		auto wrongAckType = ReadyIdentityFixture();
		wrongAckType["resumeAckRequired"] = "yes";
		const auto ackRejected = session.AdaptInbound(wrongAckType.dump());
		assert(ackRejected.protocolViolation);
		assert(!committed);
		assert(!ackRejected.readyIdentity);
		assert(ackRejected.legacyMessages.empty());
		assert(!session.IsReady());

		auto wrongAdminType = ReadyIdentityFixture();
		wrongAdminType["multiplayerSaveAdmin"] = 3;
		const auto adminRejected = session.AdaptInbound(wrongAdminType.dump());
		assert(adminRejected.protocolViolation);
		assert(!committed);
		assert(!adminRejected.readyIdentity);
		assert(!session.IsReady());

		auto accepted = ReadyIdentityFixture();
		accepted["resumeAckRequired"] = true;
		const auto ready = session.AdaptInbound(accepted.dump());
		assert(committed);
		assert(!ready.protocolViolation);
		assert(!ready.localHandshakeFailure);
		assert(ready.controlMessages.size() == 1);
		const auto ack = Json::parse(ready.controlMessages[0].payload);
		assert(ack.at("type") == "keepAlive");
		assert(ack.at("sessionReadyAck") == true);
		assert(ack.at("resumeTokenAck") == "rotated-token");
		assert(ready.legacyMessages.size() == 1);
		assert(session.IsReady());
	}

	void TestSessionReadyInvalidUtf8NeverReachesCommit()
	{
		Session session;
		bool committed = false;
		session.SetClientIdentity(IdentityFixture(), [&committed](const ReadyIdentity&, std::string&) {
			committed = true;
			return true;
		});
		StartIdentityHandshake(session);

		std::string payload = R"({"type":"sessionReady","playerId":7,"protocolVersion":2,"serverProtocolVersion":2,"profileId":"co-12345678","clientVersion":")";
		payload += std::string("\xC3\x28", 2);
		payload += R"("})";
		const auto rejected = session.AdaptInbound(payload);
		assert(rejected.protocolViolation);
		assert(!committed);
		assert(!rejected.readyIdentity);
		assert(!session.IsReady());
	}

	void TestRemoteDisconnectEvictsCaches()
	{
		Session session;
		CompleteHandshake(session);
		const auto state = session.AdaptInbound(Json{
			{ "type", "playerState" },
			{ "playerId", 31 },
			{ "characterName", "Departed" },
			{ "vitals", Json{ { "healthPercent", 40 } } }
		}.dump());
		assert(state.legacyMessages.empty());
		const auto firstTransform = session.AdaptInbound(RemoteTransform(31, 1.0, 2.0, 3.0, 4.0));
		assert(firstTransform.legacyMessages.size() == 1);
		assert(Json::parse(firstTransform.legacyMessages[0]).at("characterName") == "Departed");

		const Json disconnectPacket{
			{ "type", "disconnect" },
			{ "playerId", 31 },
			{ "serverTime", 1.0 }
		};
		const auto disconnect = session.AdaptInbound(disconnectPacket.dump());
		assert(disconnect.legacyMessages.size() == 1);
		assert(disconnect.legacyMessages[0] == disconnectPacket.dump());
		assert(!disconnect.protocolViolation);

		const auto freshTransform = session.AdaptInbound(RemoteTransform(31, 5.0, 6.0, 7.0, 8.0));
		assert(freshTransform.legacyMessages.size() == 1);
		const auto merged = Json::parse(freshTransform.legacyMessages[0]);
		assert(!merged.contains("characterName"));
		assert(!merged.contains("vitals"));
	}

	void TestVisibilityPauseOutbound()
	{
		Session session;
		CompleteHandshake(session, 7, true);
		const Json pause{ { "type", "visibilityPause" }, { "paused", true } };
		const auto sentPause = session.AdaptOutbound(pause.dump());
		assert(sentPause.size() == 1 && sentPause[0].delivery == Delivery::ReliableOrdered);
		assert(Json::parse(sentPause[0].payload).at("movementEpoch") == 1);
		const auto first = session.AdaptOutbound(RemoteTransform(7, 1.0, 2.0, 3.0, 4.0));
		assert(Json::parse(first[0].payload).at("movementEpoch") == 1);
		const auto sentResume = session.AdaptOutbound(Json{ { "type", "visibilityPause" }, { "paused", false } }.dump());
		assert(sentResume.size() == 1 && Json::parse(sentResume[0].payload).at("movementEpoch") == 2);
		auto transition = Json::parse(RemoteTransform(7, 1.0, 2.0, 3.0, 4.0));
		transition["movementType"] = "cell_change";
		const auto second = session.AdaptOutbound(transition.dump());
		assert(second.size() == 2);
		assert(Json::parse(second[0].payload).at("type") == "playerState");
		assert(second[0].delivery == Delivery::ReliableOrdered);
		assert(second[1].delivery == Delivery::ReliableOrdered);
		assert(Json::parse(second[1].payload).at("movementEpoch") == 2);
		assert(Json::parse(second[1].payload).at("movementSequence") == 1);
		assert(Json::parse(second[1].payload).at("resumeTransitionId") == 1);
		transition["movementType"] = "normal";
		session.AdaptInbound(Json{ { "type", "visibilityResumed" }, { "movementEpoch", 1 }, { "resumeTransitionId", 1 } }.dump());
		const auto retry = session.AdaptOutbound(transition.dump());
		assert(retry[0].delivery == Delivery::ReliableOrdered);
		assert(Json::parse(retry[0].payload).at("movementType") == "cell_change");
		assert(Json::parse(retry[0].payload).at("resumeRetry") == true);
		assert(Json::parse(retry[0].payload).at("movementSequence") == 2);
		assert(Json::parse(retry[0].payload).at("resumeTransitionId") == 1);
		transition["worldspaceId"] = "0000003E";
		transition["movementType"] = "worldspace_change";
		const auto movedAgain = session.AdaptOutbound(transition.dump());
		assert(Json::parse(movedAgain[0].payload).at("movementType") == "worldspace_change");
		assert(!Json::parse(movedAgain[0].payload).contains("resumeRetry"));
		assert(Json::parse(movedAgain[0].payload).at("resumeTransitionId") == 2);
		transition["movementType"] = "normal";
		session.AdaptInbound(Json{ { "type", "visibilityResumed" }, { "movementEpoch", 2 }, { "resumeTransitionId", 1 } }.dump());
		const auto retryAgain = session.AdaptOutbound(transition.dump());
		assert(retryAgain[0].delivery == Delivery::ReliableOrdered);
		assert(Json::parse(retryAgain[0].payload).at("movementType") == "worldspace_change");
		assert(Json::parse(retryAgain[0].payload).at("resumeRetry") == true);
		assert(Json::parse(retryAgain[0].payload).at("resumeTransitionId") == 2);
		session.AdaptInbound(Json{ { "type", "visibilityResumed" }, { "movementEpoch", 2 }, { "resumeTransitionId", 2 } }.dump());
		assert(session.AdaptOutbound(transition.dump())[0].delivery == Delivery::UnreliableSequenced);
		Session legacy;
		CompleteHandshake(legacy);
		assert(legacy.AdaptOutbound(pause.dump()).empty());
	}

	void TestResumeSendsChangedDurableStateFirst()
	{
		Session session;
		CompleteHandshake(session, 7, true);
		session.AdaptOutbound(RemoteTransform(7, 1.0, 2.0, 3.0, 4.0));
		session.AdaptOutbound(Json{ { "type", "visibilityPause" }, { "paused", true } }.dump());
		session.AdaptOutbound(Json{ { "type", "visibilityPause" }, { "paused", false } }.dump());
		auto transform = Json::parse(RemoteTransform(7, 1.0, 2.0, 3.0, 4.0));
		transform["equippedItems"] = Json::array({ Json{ { "slot", "rightHand" }, { "formId", "00001234" } } });
		const auto first = session.AdaptOutbound(transform.dump());
		assert(first.size() == 2);
		assert(Json::parse(first[0].payload).at("type") == "playerState");
		assert(first[0].delivery == Delivery::ReliableOrdered);
		assert(Json::parse(first[1].payload).at("type") == "transform");
		assert(first[1].delivery == Delivery::ReliableOrdered);
		const auto retry = session.AdaptOutbound(transform.dump());
		assert(retry.size() == 1 && Json::parse(retry[0].payload).at("type") == "transform");
	}

	void TestResumeWithinStickyIntervalResendsState()
	{
		Session session;
		CompleteHandshake(session, 7, true);
		auto transform = Json::parse(RemoteTransform(7, 1.0, 2.0, 3.0, 4.0));
		transform["characterName"] = "ResumeProbe";
		const auto initial = session.AdaptOutbound(transform.dump());
		assert(initial.size() == 2);
		assert(Json::parse(initial[0].payload).at("type") == "transform");
		assert(Json::parse(initial[1].payload).at("type") == "playerState");
		session.AdaptOutbound(Json{ { "type", "visibilityPause" }, { "paused", true } }.dump());
		session.AdaptOutbound(Json{ { "type", "visibilityPause" }, { "paused", false } }.dump());
		const auto resumed = session.AdaptOutbound(transform.dump());
		assert(resumed.size() == 2);
		assert(Json::parse(resumed[0].payload).at("type") == "playerState");
		assert(Json::parse(resumed[0].payload).at("characterName") == "ResumeProbe");
		assert(resumed[0].delivery == Delivery::ReliableOrdered);
		assert(Json::parse(resumed[1].payload).at("type") == "transform");
		assert(resumed[1].delivery == Delivery::ReliableOrdered);
	}

	void TestOrderedPeerVisibility()
	{
		Session session;
		CompleteHandshake(session, 7, true);
		auto transform = Json::parse(RemoteTransform(31, 1.0, 2.0, 3.0, 4.0));
		transform["visibilitySequence"] = 1;
		transform["playerIncarnation"] = 1;
		assert(session.AdaptInbound(transform.dump()).legacyMessages.size() == 1);
		const Json hidden{ { "type", "peerHidden" }, { "playerId", 31 }, { "playerIncarnation", 1 }, { "visibilitySequence", 2 } };
		assert(session.AdaptInbound(hidden.dump()).legacyMessages.size() == 1);
		assert(session.AdaptInbound(Json{ { "type", "playerState" }, { "playerId", 31 }, { "playerIncarnation", 1 }, { "characterName", "Hidden" } }.dump()).legacyMessages.empty());
		assert(session.AdaptInbound(transform.dump()).legacyMessages.empty());
		auto other = Json::parse(RemoteTransform(32, 1.0, 2.0, 3.0, 4.0));
		other["visibilitySequence"] = 1;
		other["playerIncarnation"] = 2;
		assert(session.AdaptInbound(other.dump()).legacyMessages.size() == 1);
		transform["visibilitySequence"] = 3;
		assert(session.AdaptInbound(transform.dump()).legacyMessages.size() == 1);
		assert(session.AdaptInbound(hidden.dump()).legacyMessages.empty());
		const Json disconnected{ { "type", "disconnect" }, { "playerId", 31 }, { "playerIncarnation", 1 }, { "visibilitySequence", 4 } };
		assert(session.AdaptInbound(disconnected.dump()).legacyMessages.size() == 1);
		assert(session.AdaptInbound(transform.dump()).legacyMessages.empty());
		transform["visibilitySequence"] = 5;
		assert(session.AdaptInbound(transform.dump()).legacyMessages.size() == 1);
		assert(session.AdaptInbound(disconnected.dump()).legacyMessages.empty());
		transform["visibilitySequence"] = 6;
		transform["playerIncarnation"] = 3;
		const auto rejoined = session.AdaptInbound(transform.dump());
		assert(rejoined.legacyMessages.size() == 2);
		assert(Json::parse(rejoined.legacyMessages[0]).at("type") == "disconnect");
		assert(!Json::parse(rejoined.legacyMessages[1]).contains("characterName"));
		assert(session.AdaptInbound(disconnected.dump()).legacyMessages.empty());
		assert(session.AdaptInbound(Json{ { "type", "playerState" }, { "playerId", 31 }, { "playerIncarnation", 1 }, { "characterName", "Old" } }.dump()).legacyMessages.empty());
		session.Reset();
		CompleteHandshake(session, 7, true);
		transform["visibilitySequence"] = 1;
		assert(session.AdaptInbound(transform.dump()).legacyMessages.size() == 1);
	}

	void TestRejoinBeforeOldDisconnectClearsDurableState()
	{
		Session session;
		CompleteHandshake(session, 7, true);
		const Json oldState{ { "type", "playerState" }, { "playerId", 31 }, { "playerIncarnation", 1 }, { "characterName", "Old" } };
		assert(session.AdaptInbound(oldState.dump()).legacyMessages.empty());
		auto transform = Json::parse(RemoteTransform(31, 1.0, 2.0, 3.0, 4.0));
		transform["playerIncarnation"] = 1;
		transform["visibilitySequence"] = 1;
		assert(Json::parse(session.AdaptInbound(transform.dump()).legacyMessages.at(0)).at("characterName") == "Old");
		const Json plugins{ { "type", "pluginList" }, { "playerId", 31 }, { "playerIncarnation", 2 }, { "data", "new" } };
		const auto updatedPlugins = session.AdaptInbound(plugins.dump());
		assert(updatedPlugins.legacyMessages.size() == 2);
		assert(Json::parse(updatedPlugins.legacyMessages.at(0)).at("type") == "disconnect");
		assert(Json::parse(updatedPlugins.legacyMessages.at(1)).at("type") == "pluginList");
		transform["playerIncarnation"] = 2;
		transform["visibilitySequence"] = 3;
		const auto rejoined = session.AdaptInbound(transform.dump());
		assert(rejoined.legacyMessages.size() == 1);
		assert(!Json::parse(rejoined.legacyMessages.at(0)).contains("characterName"));
		const Json oldPlugins{ { "type", "pluginList" }, { "playerId", 31 }, { "playerIncarnation", 1 }, { "data", "old" } };
		assert(session.AdaptInbound(oldPlugins.dump()).legacyMessages.empty());
		const Json oldDisconnect{ { "type", "disconnect" }, { "playerId", 31 }, { "playerIncarnation", 1 }, { "visibilitySequence", 2 } };
		assert(session.AdaptInbound(oldDisconnect.dump()).legacyMessages.empty());
		assert(session.AdaptInbound(oldState.dump()).legacyMessages.empty());
	}

	void TestAuthoritativeDownedCapability()
	{
		Session legacy;
		CompleteHandshake(legacy);
		auto transform = Json::parse(RemoteTransform(31, 1.0, 2.0, 3.0, 4.0));
		transform["serverDowned"] = Json{ { "downed", true }, { "dead", true } };
		const auto legacyResult = legacy.AdaptInbound(transform.dump());
		assert(!Json::parse(legacyResult.legacyMessages.at(0)).contains("serverDowned"));
		for (const auto* type : { "downedState", "playerDied" }) {
			const auto event = legacy.AdaptInbound(Json{ { "type", type }, { "playerId", 31 },
				{ "playerIncarnation", 1 }, { "serverDowned", Json{ { "revision", 1 }, { "downed", true }, { "dead", false } } } }.dump());
			const auto forwarded = Json::parse(event.legacyMessages.at(0));
			assert(!forwarded.contains("serverDowned") && !forwarded.contains("playerIncarnation"));
		}
		Session supported;
		CompleteHandshake(supported, 7, true);
		transform["playerIncarnation"] = 1;
		transform["visibilitySequence"] = 1;
		const auto supportedResult = supported.AdaptInbound(transform.dump());
		assert(Json::parse(supportedResult.legacyMessages.at(0)).at("serverDowned").at("dead") == true);
		const auto state = supported.AdaptInbound(Json{ { "type", "playerState" }, { "playerId", 31 },
			{ "playerIncarnation", 1 }, { "characterName", "Updated" } }.dump());
		assert(!Json::parse(state.legacyMessages.at(0)).contains("serverDowned"));
	}

	void TestLegacyPeerHiddenClearsCachedTransform()
	{
		Session session;
		CompleteHandshake(session);
		assert(session.AdaptInbound(RemoteTransform(31, 1.0, 2.0, 3.0, 4.0)).legacyMessages.size() == 1);
		assert(session.AdaptInbound(Json{ { "type", "peerHidden" }, { "playerId", 31 } }.dump()).legacyMessages.size() == 1);
		assert(session.AdaptInbound(Json{ { "type", "playerState" }, { "playerId", 31 }, { "characterName", "Hidden" } }.dump()).legacyMessages.empty());
	}

	void TestRosterPruneRequiresTwoAbsences()
	{
		Session session;
		CompleteHandshake(session);
		session.AdaptInbound(Json{ { "type", "playerState" }, { "playerId", 41 }, { "characterName", "Carried" } }.dump());
		session.AdaptInbound(RemoteTransform(41, 1.0, 2.0, 3.0, 4.0));
		session.AdaptInbound(Json{ { "type", "playerState" }, { "playerId", 42 }, { "characterName", "Present" } }.dump());
		session.AdaptInbound(RemoteTransform(42, 1.0, 2.0, 3.0, 4.0));

		const auto rosterWithout = session.AdaptInbound(Json{
			{ "type", "playerRoster" },
			{ "players", Json::array({ Json{ { "playerId", 42 }, { "name", "Present" }, { "tag", "" } } }) },
			{ "serverTime", 1.0 }
		}.dump());
		assert(rosterWithout.legacyMessages.size() == 1);
		assert(Json::parse(rosterWithout.legacyMessages[0]).at("type") == "playerRoster");

		const auto keptTransform = session.AdaptInbound(RemoteTransform(41, 2.0, 2.0, 3.0, 4.0));
		assert(Json::parse(keptTransform.legacyMessages[0]).at("characterName") == "Carried");

		session.AdaptInbound(Json{
			{ "type", "playerRoster" },
			{ "players", Json::array({ Json{ { "playerId", 42 }, { "name", "Present" }, { "tag", "" } } }) },
			{ "serverTime", 2.0 }
		}.dump());
		const auto evictedTransform = session.AdaptInbound(RemoteTransform(41, 3.0, 2.0, 3.0, 4.0));
		assert(evictedTransform.legacyMessages.size() == 1);
		assert(!Json::parse(evictedTransform.legacyMessages[0]).contains("characterName"));

		const auto memberTransform = session.AdaptInbound(RemoteTransform(42, 4.0, 2.0, 3.0, 4.0));
		assert(Json::parse(memberTransform.legacyMessages[0]).at("characterName") == "Present");
	}

	void TestRemotePlayerCacheCapacityRejectsNewIds()
	{
		Session session;
		CompleteHandshake(session);
		const auto maximum = static_cast<std::uint32_t>(F4T::RemotePlayerCapacity::MaximumTrackedRemotePlayers);
		constexpr std::uint32_t kHandshakePlayerId = 7;
		std::uint32_t admitted = 0;
		for (std::uint32_t index = 1; admitted < maximum; ++index) {
			if (index == kHandshakePlayerId) {
				continue;
			}
			session.AdaptInbound(Json{ { "type", "playerState" }, { "playerId", index }, { "characterName", "Bulk" } }.dump());
			++admitted;
		}
		const auto maximumAdmittedId = maximum + 1;

		const auto overflowId = maximumAdmittedId + 1;
		session.AdaptInbound(Json{ { "type", "playerState" }, { "playerId", overflowId }, { "characterName", "Rejected" } }.dump());
		const auto overflowTransform = session.AdaptInbound(RemoteTransform(overflowId, 1.0, 2.0, 3.0, 4.0));
		assert(overflowTransform.legacyMessages.size() == 1);
		assert(!Json::parse(overflowTransform.legacyMessages[0]).contains("characterName"));

		const auto existingTransform = session.AdaptInbound(RemoteTransform(1, 1.0, 2.0, 3.0, 4.0));
		assert(Json::parse(existingTransform.legacyMessages[0]).at("characterName") == "Bulk");

		const auto liveId = overflowId + 1;
		session.AdaptInbound(Json{
			{ "type", "playerRoster" },
			{ "players", Json::array({ Json{ { "playerId", liveId }, { "name", "Live" }, { "tag", "" } } }) },
			{ "serverTime", 1.0 }
		}.dump());
		session.AdaptInbound(Json{ { "type", "playerState" }, { "playerId", liveId }, { "characterName", "Member" } }.dump());
		const auto memberTransform = session.AdaptInbound(RemoteTransform(liveId, 1.0, 2.0, 3.0, 4.0));
		assert(memberTransform.legacyMessages.size() == 1);
		assert(Json::parse(memberTransform.legacyMessages[0]).at("characterName") == "Member");
	}

	void TestNpcAuthorityScopeCap()
	{
		Session session;
		CompleteHandshake(session, 7);
		for (std::size_t index = 0; index < F4T::NpcAuthority::MaximumTrackedEpochScopes; ++index) {
			char buffer[16]{};
			std::snprintf(buffer, sizeof(buffer), "%08X", static_cast<unsigned int>(index + 1));
			session.AdaptInbound(Json{
				{ "type", "npcAuthority" },
				{ "authorityPlayerId", 7 },
				{ "authorityEpoch", 1 },
				{ "authorityCellId", buffer }
			}.dump());
		}
		const auto lastAccepted = session.GetNpcAuthority("00002000", "");
		assert(lastAccepted);
		assert(lastAccepted->playerId == 7);
		session.AdaptInbound(Json{
			{ "type", "npcAuthority" },
			{ "authorityPlayerId", 7 },
			{ "authorityEpoch", 1 },
			{ "authorityCellId", "00002001" }
		}.dump());
		assert(!session.GetNpcAuthority("00002001", ""));
		session.AdaptInbound(Json{
			{ "type", "npcAuthority" },
			{ "authorityPlayerId", 7 },
			{ "authorityEpoch", 2 },
			{ "authorityCellId", "00000001" }
		}.dump());
		const auto updated = session.GetNpcAuthority("00000001", "");
		assert(updated && updated->epoch == 2);
	}
}

int main()
{
#if defined(_WIN32) && defined(_DEBUG)
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
	TestHandshakeBarrier();
	TestTransformAndPlayerStateSplit();
	TestOptionalPlayerStateFieldsAreOmitted();
	TestRemotePlayerStateMerge();
	TestPowerArmorRoundTrip();
	TestRemoteActionsBeforeFirstTransform();
	TestWorldPolicyInbound();
	TestSurvivalPolicyInbound();
	TestWorldPolicyChunkedInbound();
	TestPreReadySessionEndedVariants();
	TestPostReadySessionEndedRemainsLegacy();
	TestUnrelatedPreReadyTrafficIsViolation();
	TestSessionReadyCommitSuccess();
	TestSessionReadyCommitFailureIsLocal();
	TestSessionReadyIdentityMismatches();
	TestSessionReadyCredentialValidation();
	TestResumedSessionAdoptsServerPlayerId();
	TestNpcAuthority();
	TestNpcAuthorityHolderZeroErasesScope();
	TestNpcAuthorityHolderZeroErasesTranslatedScope();
	TestNpcAuthorityRemovalSurvivesForgottenMapping();
	TestNpcAuthorityScopeIsTranslated();
	TestNpcStateAuthorityFieldsAreAdapterOwned();
	TestPlayerVitalsRoundTrip();
	TestSessionReadyAckFlagIsValidatedBeforeCommit();
	TestSessionReadyInvalidUtf8NeverReachesCommit();
	TestRemoteDisconnectEvictsCaches();
	TestVisibilityPauseOutbound();
	TestResumeSendsChangedDurableStateFirst();
	TestResumeWithinStickyIntervalResendsState();
	TestOrderedPeerVisibility();
	TestRejoinBeforeOldDisconnectClearsDurableState();
	TestLegacyPeerHiddenClearsCachedTransform();
	TestAuthoritativeDownedCapability();
	TestRosterPruneRequiresTwoAbsences();
	TestRemotePlayerCacheCapacityRejectsNewIds();
	TestNpcAuthorityScopeCap();
	return 0;
}

#include "F4TGnsProtocolAdapter.h"

#include "F4TInventoryContract.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <string>
#include <utility>

namespace
{
	using Json = nlohmann::json;
	using Session = F4T::GnsProtocolAdapter::Session;

	Json InventoryFixture()
	{
		return Json{
			{ "initialized", true },
			{ "revision", 4u },
			{ "items", Json{ { "fallout4.esm:00000123", Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00000123" }, { "count", 3u } } } } }
		};
	}

	Json SessionReadyFixture(std::uint32_t a_playerId = 7)
	{
		return Json{
			{ "type", "sessionReady" },
			{ "playerId", a_playerId },
			{ "protocolVersion", 2 },
			{ "serverProtocolVersion", 2 },
			{ "worldStateHostPlayerId", a_playerId }
		};
	}

	Json CompleteHandshake(Session& a_session, Json a_sessionReady)
	{
		const auto welcome = a_session.AdaptInbound(Json{
			{ "type", "welcome" },
			{ "playerId", 7 },
			{ "protocolVersion", 2 }
		}.dump());
		assert(!welcome.protocolViolation);
		assert(!welcome.localHandshakeFailure);
		assert(welcome.legacyMessages.empty());
		assert(welcome.controlMessages.size() == 1);

		const auto ready = a_session.AdaptInbound(a_sessionReady.dump());
		assert(!ready.protocolViolation);
		assert(!ready.localHandshakeFailure);
		assert(!ready.rejection);
		assert(ready.error.empty());
		assert(ready.controlMessages.empty());
		assert(ready.legacyMessages.size() == 1);
		assert(a_session.IsReady());
		return Json::parse(ready.legacyMessages[0]);
	}

	void ExpectInvalidInventoryIsOmitted(Json a_inventory)
	{
		Session session;
		auto ready = SessionReadyFixture();
		ready["serverInventory"] = std::move(a_inventory);
		const auto exposed = CompleteHandshake(session, ready);
		assert(!exposed.contains("serverInventory"));
		assert(exposed.at("playerId") == 7);
		assert(exposed.at("worldStateHostPlayerId") == 7);
	}

	void TestValidServerInventoryIsForwarded()
	{
		Session session;
		auto ready = SessionReadyFixture();
		ready["serverInventory"] = InventoryFixture();
		const auto exposed = CompleteHandshake(session, ready);
		assert(exposed.contains("serverInventory"));
		assert(exposed.at("serverInventory") == InventoryFixture());
		const auto parsed = F4T::InventoryContract::ParseServerInventory(exposed.at("serverInventory"));
		assert(parsed);
		assert(parsed->revision == 4);
		assert(parsed->items.size() == 1);
		assert(parsed->items.at("fallout4.esm:00000123").plugin == "fallout4.esm");
		assert(parsed->items.at("fallout4.esm:00000123").localFormId == "00000123");
		assert(parsed->items.at("fallout4.esm:00000123").count == 3);
	}

	void TestHeaderOnlyServerInventoryIsForwarded()
	{
		Session session;
		auto ready = SessionReadyFixture();
		const Json header{ { "initialized", true }, { "revision", 4u }, { "itemCount", 900u }, { "chunkCount", 3u } };
		ready["serverInventory"] = header;
		const auto exposed = CompleteHandshake(session, ready);
		assert(exposed.contains("serverInventory"));
		assert(exposed.at("serverInventory") == header);
		const auto parsed = F4T::InventoryContract::ParseInventoryTransferHeader(exposed.at("serverInventory"));
		assert(parsed.has_value());
		assert(parsed->revision == 4 && parsed->itemCount == 900 && parsed->chunkCount == 3);
	}

	void TestEmptyServerInventoryIsForwarded()
	{
		Session session;
		auto ready = SessionReadyFixture();
		const Json empty{ { "initialized", true }, { "revision", 0u }, { "items", Json::object() } };
		ready["serverInventory"] = empty;
		const auto exposed = CompleteHandshake(session, ready);
		assert(exposed.contains("serverInventory"));
		assert(exposed.at("serverInventory") == empty);
	}

	void TestInvalidServerInventoryIsOmittedAndSessionSurvives()
	{
		auto notInitialized = InventoryFixture();
		notInitialized["initialized"] = false;
		ExpectInvalidInventoryIsOmitted(notInitialized);

		auto missingRevision = InventoryFixture();
		missingRevision.erase("revision");
		ExpectInvalidInventoryIsOmitted(missingRevision);

		auto negativeRevision = InventoryFixture();
		negativeRevision["revision"] = -1;
		ExpectInvalidInventoryIsOmitted(negativeRevision);

		auto extraField = InventoryFixture();
		extraField["unexpected"] = 1;
		ExpectInvalidInventoryIsOmitted(extraField);

		auto mismatchedKey = InventoryFixture();
		mismatchedKey["items"] = Json{ { "fallout4.esm:0000012a", Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00000123" }, { "count", 3u } } } };
		ExpectInvalidInventoryIsOmitted(mismatchedKey);

		auto zeroCount = InventoryFixture();
		zeroCount["items"]["fallout4.esm:00000123"]["count"] = 0;
		ExpectInvalidInventoryIsOmitted(zeroCount);

		ExpectInvalidInventoryIsOmitted(Json{ { "initialized", true }, { "revision", 1u }, { "items", Json::array() } });
		ExpectInvalidInventoryIsOmitted(Json::array());
	}

	void TestAbsentServerInventoryLeavesTheWelcomeUnchanged()
	{
		Session session;
		const auto exposed = CompleteHandshake(session, SessionReadyFixture());
		assert(!exposed.contains("serverInventory"));
		assert(exposed.at("playerId") == 7);
		assert(exposed.at("worldStateHostPlayerId") == 7);
		assert(exposed.at("protocolVersion") == 2);
		assert(exposed.at("serverProtocolVersion") == 2);
	}

	void TestOwnedResultPacketsAreForwardedVerbatim()
	{
		Session session;
		CompleteHandshake(session, SessionReadyFixture());

		const Json dropResult{
			{ "type", "inventoryDropResult" },
			{ "operationId", "session-nonce:1" },
			{ "status", "rejected" },
			{ "reason", "rateLimited" }
		};
		const auto forwardedDrop = session.AdaptInbound(dropResult.dump());
		assert(!forwardedDrop.protocolViolation);
		assert(!forwardedDrop.localHandshakeFailure);
		assert(!forwardedDrop.rejection);
		assert(forwardedDrop.controlMessages.empty());
		assert(forwardedDrop.legacyMessages.size() == 1);
		assert(Json::parse(forwardedDrop.legacyMessages[0]) == dropResult);
		const auto parsedDrop = F4T::InventoryContract::ParseOwnedResult(
			Json::parse(forwardedDrop.legacyMessages[0]), F4T::InventoryContract::OwnedOperation::Drop);
		assert(parsedDrop);
		assert(parsedDrop->operationId == "session-nonce:1");
		assert(!parsedDrop->committed);
		assert(parsedDrop->reason == F4T::InventoryContract::OwnedReason::RateLimited);

		const Json pickupResult{
			{ "type", "inventoryPickupResult" },
			{ "operationId", "session-nonce:2" },
			{ "status", "rejected" },
			{ "reason", "unknownEntity" }
		};
		const auto forwardedPickup = session.AdaptInbound(pickupResult.dump());
		assert(!forwardedPickup.protocolViolation);
		assert(!forwardedPickup.rejection);
		assert(forwardedPickup.controlMessages.empty());
		assert(forwardedPickup.legacyMessages.size() == 1);
		assert(Json::parse(forwardedPickup.legacyMessages[0]) == pickupResult);
		const auto parsedPickup = F4T::InventoryContract::ParseOwnedResult(
			Json::parse(forwardedPickup.legacyMessages[0]), F4T::InventoryContract::OwnedOperation::Pickup);
		assert(parsedPickup);
		assert(parsedPickup->operationId == "session-nonce:2");
		assert(parsedPickup->reason == F4T::InventoryContract::OwnedReason::UnknownEntity);
		assert(session.IsReady());
	}
}

int main()
{
	TestValidServerInventoryIsForwarded();
	TestEmptyServerInventoryIsForwarded();
	TestHeaderOnlyServerInventoryIsForwarded();
	TestInvalidServerInventoryIsOmittedAndSessionSurvives();
	TestAbsentServerInventoryLeavesTheWelcomeUnchanged();
	TestOwnedResultPacketsAreForwardedVerbatim();
	return 0;
}

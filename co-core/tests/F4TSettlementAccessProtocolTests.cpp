#include "F4TSettlementAccessProtocol.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstddef>
#include <string>
#include <vector>

int main()
{
	using Json = nlohmann::json;
	using F4T::SettlementAccess::ApplyStatus;
	using F4T::SettlementAccess::Settlement;
	namespace Protocol = F4T::SettlementAccessProtocol;

	const auto zone = Protocol::NormalizeGet(Json{ { "buildZoneId", 9U } });
	assert(zone.has_value() && *zone == 9);
	assert(!Protocol::NormalizeGet(Json{ { "buildZoneId", -1 } }).has_value());
	assert(!Protocol::NormalizeGet(Json{ { "buildZoneId", 9.5 } }).has_value());
	assert(!Protocol::NormalizeGet(Json{ { "buildZoneId", "9" } }).has_value());
	assert(!Protocol::NormalizeGet(Json::object()).has_value());

	const auto grant = Protocol::NormalizeUpdate(Json{ { "buildZoneId", 9U }, { "op", "grant" }, { "targetProfileId", "profile-friend" } });
	assert(grant.has_value() && grant->buildZoneId == 9 && grant->grant && grant->targetProfileId == "profile-friend");
	const auto revoke = Protocol::NormalizeUpdate(Json{ { "buildZoneId", 9U }, { "op", "revoke" }, { "targetProfileId", "profile-friend" } });
	assert(revoke.has_value() && !revoke->grant);
	assert(!Protocol::NormalizeUpdate(Json{ { "buildZoneId", 9U }, { "op", "toggle" }, { "targetProfileId", "profile-friend" } }).has_value());
	assert(!Protocol::NormalizeUpdate(Json{ { "buildZoneId", 9U }, { "op", "grant" } }).has_value());
	assert(!Protocol::NormalizeUpdate(Json{ { "buildZoneId", 9U }, { "op", "grant" }, { "targetProfileId", 7 } }).has_value());

	const Settlement settlement{ 9, "profile-owner", { "profile-friend" } };
	const Json state = Protocol::MakeState(settlement);
	assert(state.at("type") == "settlementAccessState");
	assert(state.at("buildZoneId") == 9);
	assert(state.at("ownerProfileId") == "profile-owner");
	assert(state.at("allowedProfileIds") == Json::array({ "profile-friend" }));

	const Json reject = Protocol::MakeReject(9, ApplyStatus::NotOwner);
	assert(reject.at("type") == "settlementAccessReject");
	assert(reject.at("buildZoneId") == 9);
	assert(reject.at("status") == "notOwner");

	assert(Protocol::StatusName(ApplyStatus::Committed) == "committed");
	assert(Protocol::StatusName(ApplyStatus::InvalidRequest) == "invalidRequest");
	assert(Protocol::StatusName(ApplyStatus::UnknownBuildZone) == "unknownBuildZone");
	assert(Protocol::StatusName(ApplyStatus::NotOwner) == "notOwner");
	assert(Protocol::StatusName(ApplyStatus::AlreadyAllowed) == "alreadyAllowed");
	assert(Protocol::StatusName(ApplyStatus::NotAllowed) == "notAllowed");
	assert(Protocol::StatusName(ApplyStatus::Full) == "full");
	assert(Protocol::StatusName(ApplyStatus::SettlementLimit) == "settlementLimit");
	assert(Protocol::StatusName(ApplyStatus::RateLimited) == "rateLimited");
	assert(Protocol::StatusName(ApplyStatus::PersistenceUnavailable) == "persistenceUnavailable");

	const std::vector<Settlement> settlements{ settlement, Settlement{ 11, "profile-other", {} } };
	const auto restored = Protocol::NormalizeStored(Protocol::MakeStoredJson(settlements));
	assert(restored.has_value() && restored->size() == 2);
	assert((*restored)[0].buildZoneId == 9);
	assert((*restored)[0].ownerProfileId == "profile-owner");
	assert((*restored)[0].allowedProfileIds == std::vector<std::string>{ "profile-friend" });
	assert((*restored)[1].allowedProfileIds.empty());
	const auto empty = Protocol::NormalizeStored(Json(nullptr));
	assert(empty.has_value() && empty->empty());
	assert(!Protocol::NormalizeStored(Json::object()).has_value());
	assert(!Protocol::NormalizeStored(Json::array({ Json{ { "buildZoneId", 9U }, { "ownerProfileId", "profile-owner" }, { "allowedProfileIds", Json::array({ 7 }) } } })).has_value());
	return 0;
}

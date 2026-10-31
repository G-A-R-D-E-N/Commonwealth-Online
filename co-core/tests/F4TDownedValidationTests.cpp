#include "F4TDownedValidation.h"
#include "F4TPlayerStateValidation.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <string>

namespace
{
	using Json = nlohmann::json;
	using F4T::DownedValidation::RequestError;

	Json ReviveRequest(const Json& a_target)
	{
		return Json{ { "type", "reviveRequest" }, { "target", a_target } };
	}

	Json RespawnRequest(const std::string& a_point)
	{
		return Json{ { "type", "respawnRequest" }, { "point", a_point } };
	}

	Json PluginVitals(bool a_isDowned, bool a_isDead, std::int32_t a_healthPercent = 37, std::int32_t a_level = 0)
	{
		Json vitals{
			{ "healthPercent", a_healthPercent },
			{ "isDowned", a_isDowned },
			{ "isDead", a_isDead }
		};
		if (a_level > 0) {
			vitals["level"] = a_level;
		}
		return vitals;
	}

	Json PluginStatePacket(bool a_isDowned, bool a_isDead, std::int32_t a_healthPercent = 37, std::int32_t a_level = 0)
	{
		return Json{
			{ "type", "playerState" },
			{ "vitals", PluginVitals(a_isDowned, a_isDead, a_healthPercent, a_level) }
		};
	}
}

int main()
{
	{
		const auto missing = F4T::DownedValidation::NormalizeReviveRequest(Json{ { "type", "reviveRequest" } });
		assert(missing.error == RequestError::Malformed);
		assert(F4T::DownedValidation::NormalizeReviveRequest(Json::array()).error == RequestError::Malformed);
		assert(F4T::DownedValidation::NormalizeReviveRequest(ReviveRequest(nullptr)).error == RequestError::Malformed);
		assert(F4T::DownedValidation::NormalizeReviveRequest(ReviveRequest("abc")).error == RequestError::InvalidTarget);
		assert(F4T::DownedValidation::NormalizeReviveRequest(ReviveRequest(0)).error == RequestError::InvalidTarget);
		assert(F4T::DownedValidation::NormalizeReviveRequest(ReviveRequest(-3)).error == RequestError::InvalidTarget);
		assert(F4T::DownedValidation::NormalizeReviveRequest(ReviveRequest(4294967296ULL)).error == RequestError::InvalidTarget);
		assert(F4T::DownedValidation::NormalizeReviveRequest(ReviveRequest(2.5)).error == RequestError::InvalidTarget);
		assert(F4T::DownedValidation::NormalizeReviveRequest(ReviveRequest(true)).error == RequestError::InvalidTarget);
		assert(F4T::DownedValidation::NormalizeReviveRequest(ReviveRequest(3.0)).targetPlayerId == 3);

		const auto numeric = F4T::DownedValidation::NormalizeReviveRequest(ReviveRequest(7));
		assert(!numeric.error);
		assert(numeric.targetPlayerId == 7);

		const auto text = F4T::DownedValidation::NormalizeReviveRequest(ReviveRequest("12"));
		assert(!text.error);
		assert(text.targetPlayerId == 12);

		const auto largest = F4T::DownedValidation::NormalizeReviveRequest(ReviveRequest(4294967295ULL));
		assert(!largest.error);
		assert(largest.targetPlayerId == 4294967295U);

		const auto extra = F4T::DownedValidation::NormalizeReviveRequest(
			Json{ { "type", "reviveRequest" }, { "target", 4 }, { "playerId", 99 } });
		assert(!extra.error);
		assert(extra.targetPlayerId == 4);
	}

	{
		const auto missing = F4T::DownedValidation::NormalizeRespawnRequest(Json{ { "type", "respawnRequest" } });
		assert(missing.error == RequestError::Malformed);
		assert(F4T::DownedValidation::NormalizeRespawnRequest(Json::object()).error == RequestError::Malformed);
		assert(F4T::DownedValidation::NormalizeRespawnRequest(RespawnRequest("")).error == RequestError::InvalidPoint);
		assert(F4T::DownedValidation::NormalizeRespawnRequest(RespawnRequest(std::string(33, 'a'))).error == RequestError::InvalidPoint);
		assert(F4T::DownedValidation::NormalizeRespawnRequest(Json{ { "type", "respawnRequest" }, { "point", 5 } }).error == RequestError::Malformed);

		const auto unknown = F4T::DownedValidation::NormalizeRespawnRequest(RespawnRequest("moon"));
		assert(!unknown.error);
		assert(unknown.point == "moon");
		assert(unknown.friendPlayerId == 0);

		const auto sanctuary = F4T::DownedValidation::NormalizeRespawnRequest(RespawnRequest("sanctuary"));
		assert(!sanctuary.error);
		assert(sanctuary.point == "sanctuary");

		const auto friendPoint = F4T::DownedValidation::NormalizeRespawnRequest(
			Json{ { "type", "respawnRequest" }, { "point", "friend" }, { "friendId", 6 } });
		assert(!friendPoint.error);
		assert(friendPoint.friendPlayerId == 6);

		const auto nullFriend = F4T::DownedValidation::NormalizeRespawnRequest(
			Json{ { "type", "respawnRequest" }, { "point", "friend" }, { "friendId", nullptr } });
		assert(!nullFriend.error);
		assert(nullFriend.friendPlayerId == 0);

		assert(F4T::DownedValidation::NormalizeRespawnRequest(
			Json{ { "type", "respawnRequest" }, { "point", "friend" }, { "friendId", 0 } }).error == RequestError::InvalidFriend);
		assert(F4T::DownedValidation::NormalizeRespawnRequest(
			Json{ { "type", "respawnRequest" }, { "point", "friend" }, { "friendId", "x" } }).error == RequestError::InvalidFriend);
	}

	{
		assert(!F4T::DownedValidation::CarriesVitals(Json{ { "type", "playerState" } }));
		assert(!F4T::DownedValidation::CarriesVitals(Json{ { "type", "playerState" }, { "characterName", "Nomad" } }));
		assert(!F4T::DownedValidation::CarriesVitals(Json::array()));
		assert(F4T::DownedValidation::CarriesVitals(Json{ { "type", "playerState" }, { "vitals", PluginVitals(true, false) } }));
		assert(!F4T::DownedValidation::CarriesVitals(Json{ { "type", "playerState" }, { "isDowned", true } }));
		assert(!F4T::DownedValidation::CarriesVitals(Json{ { "type", "playerState" }, { "vitals", 5 } }));

		const auto none = F4T::DownedValidation::ReadVitals(Json{ { "type", "playerState" } });
		assert(!none.isDowned);
		assert(!none.isDead);
		assert(!none.hasStateFlags);

		const auto downed = F4T::DownedValidation::ReadVitals(Json{ { "vitals", PluginVitals(true, false) } });
		assert(downed.isDowned);
		assert(!downed.isDead);
		assert(downed.hasStateFlags);

		const auto dead = F4T::DownedValidation::ReadVitals(Json{ { "vitals", PluginVitals(false, true) } });
		assert(!dead.isDowned);
		assert(dead.isDead);
		assert(dead.hasStateFlags);

		const auto partial = F4T::DownedValidation::ReadVitals(Json{ { "vitals", Json{ { "isDead", true } } } });
		assert(partial.isDead);
		assert(partial.hasStateFlags);

		const auto missingFlags = F4T::DownedValidation::ReadVitals(Json{ { "vitals", Json{ { "healthPercent", 37 } } } });
		assert(!missingFlags.isDowned);
		assert(!missingFlags.isDead);
		assert(!missingFlags.hasStateFlags);

		const auto wrongTypes = F4T::DownedValidation::ReadVitals(Json{ { "vitals", Json{ { "isDowned", "yes" }, { "isDead", 0 } } } });
		assert(!wrongTypes.isDowned);
		assert(!wrongTypes.isDead);
		assert(!wrongTypes.hasStateFlags);
	}

	{
		const auto normalized = F4T::PlayerStateValidation::NormalizePlayerState(PluginStatePacket(true, false));
		assert(normalized.has_value());
		assert(normalized->at("vitals").at("isDowned") == true);
		assert(normalized->at("vitals").at("isDead") == false);
		assert(normalized->at("vitals").at("healthPercent") == 37);
		assert(!normalized->contains("equippedItems"));

		const auto observed = F4T::DownedValidation::ReadVitals(*normalized);
		assert(observed.isDowned);
		assert(!observed.isDead);

		const auto deadRoundTrip = F4T::PlayerStateValidation::NormalizePlayerState(PluginStatePacket(false, true, 0, 12));
		assert(deadRoundTrip.has_value());
		assert(deadRoundTrip->at("vitals").at("isDead") == true);
		assert(deadRoundTrip->at("vitals").at("level") == 12);
		const auto deadObserved = F4T::DownedValidation::ReadVitals(*deadRoundTrip);
		assert(!deadObserved.isDowned);
		assert(deadObserved.isDead);

		const auto named = F4T::PlayerStateValidation::NormalizePlayerState(
			Json{ { "type", "playerState" }, { "vitals", PluginVitals(true, false) }, { "characterName", "Nomad" } });
		assert(named.has_value());
		assert(named->at("vitals").at("isDowned") == true);
		assert(named->at("characterName") == "Nomad");

		assert(!F4T::PlayerStateValidation::NormalizePlayerState(Json{ { "type", "playerState" }, { "vitals", 5 } }).has_value());
		assert(!F4T::PlayerStateValidation::NormalizePlayerState(
			Json{ { "type", "playerState" }, { "vitals", Json{ { "healthPercent", 101 } } } }).has_value());
		assert(!F4T::PlayerStateValidation::NormalizePlayerState(
			Json{ { "type", "playerState" }, { "vitals", Json{ { "healthPercent", -1 } } } }).has_value());
		assert(!F4T::PlayerStateValidation::NormalizePlayerState(
			Json{ { "type", "playerState" }, { "vitals", Json{ { "isDowned", true } } } }).has_value());
		assert(!F4T::PlayerStateValidation::NormalizePlayerState(
			Json{ { "type", "playerState" }, { "vitals", Json{ { "healthPercent", 10, "isDowned", 1 } } } }).has_value());
		assert(!F4T::PlayerStateValidation::NormalizePlayerState(
			Json{ { "type", "playerState" }, { "vitals", Json{ { "healthPercent", 10, "level", 0 } } } }).has_value());
		assert(!F4T::PlayerStateValidation::NormalizePlayerState(
			Json{ { "type", "playerState" }, { "vitals", Json{ { "healthPercent", 10, "level", 65536 } } } }).has_value());
		assert(!F4T::PlayerStateValidation::NormalizePlayerState(
			Json{ { "type", "playerState" }, { "vitals", Json{ { "healthPercent", 10.5 } } } }).has_value());
	}

	{
		const auto absent = F4T::DownedValidation::NormalizeReviveRequest(ReviveRequest(4));
		assert(!absent.error);
		assert(!absent.stimpaksAvailable.has_value());

		const auto counted = F4T::DownedValidation::NormalizeReviveRequest(
			Json{ { "type", "reviveRequest" }, { "target", 4 }, { "stimpaks", 3 } });
		assert(!counted.error);
		assert(counted.stimpaksAvailable.has_value());
		assert(*counted.stimpaksAvailable == 3);

		const auto none = F4T::DownedValidation::NormalizeReviveRequest(
			Json{ { "type", "reviveRequest" }, { "target", 4 }, { "stimpaks", 0 } });
		assert(!none.error);
		assert(none.stimpaksAvailable.has_value());
		assert(*none.stimpaksAvailable == 0);

		const auto nullCount = F4T::DownedValidation::NormalizeReviveRequest(
			Json{ { "type", "reviveRequest" }, { "target", 4 }, { "stimpaks", nullptr } });
		assert(!nullCount.error);
		assert(!nullCount.stimpaksAvailable.has_value());

		for (const auto& bad : { Json(-1), Json(100), Json(1.5), Json("3"), Json(true) }) {
			assert(F4T::DownedValidation::NormalizeReviveRequest(
				Json{ { "type", "reviveRequest" }, { "target", 4 }, { "stimpaks", bad } }).error == RequestError::InvalidStimpaks);
		}
	}

	{
		const auto missing = F4T::DownedValidation::NormalizeReviveCancelRequest(Json{ { "type", "reviveCancel" } });
		assert(missing.error == RequestError::Malformed);
		assert(F4T::DownedValidation::NormalizeReviveCancelRequest(Json::array()).error == RequestError::Malformed);
		assert(F4T::DownedValidation::NormalizeReviveCancelRequest(
			Json{ { "type", "reviveCancel" }, { "target", nullptr } }).error == RequestError::Malformed);
		assert(F4T::DownedValidation::NormalizeReviveCancelRequest(
			Json{ { "type", "reviveCancel" }, { "target", 0 } }).error == RequestError::InvalidTarget);
		assert(F4T::DownedValidation::NormalizeReviveCancelRequest(
			Json{ { "type", "reviveCancel" }, { "target", "abc" } }).error == RequestError::InvalidTarget);
		assert(F4T::DownedValidation::NormalizeReviveCancelRequest(
			Json{ { "type", "reviveCancel" }, { "target", 9 }, { "reason", 5 } }).error == RequestError::InvalidReason);
		assert(F4T::DownedValidation::NormalizeReviveCancelRequest(
			Json{ { "type", "reviveCancel" }, { "target", 9 }, { "reason", "" } }).error == RequestError::InvalidReason);
		assert(F4T::DownedValidation::NormalizeReviveCancelRequest(
			Json{ { "type", "reviveCancel" }, { "target", 9 }, { "reason", std::string(33, 'a') } }).error == RequestError::InvalidReason);

		const auto bare = F4T::DownedValidation::NormalizeReviveCancelRequest(
			Json{ { "type", "reviveCancel" }, { "target", 9 } });
		assert(!bare.error);
		assert(bare.targetPlayerId == 9);
		assert(bare.reason.empty());

		const auto canonical = F4T::DownedValidation::NormalizeReviveCancelRequest(
			Json{ { "type", "reviveCancel" }, { "target", "9" }, { "reason", "movedAway" } });
		assert(!canonical.error);
		assert(canonical.targetPlayerId == 9);
		assert(canonical.reason == "movedAway");

		const auto nullReason = F4T::DownedValidation::NormalizeReviveCancelRequest(
			Json{ { "type", "reviveCancel" }, { "target", 9 }, { "reason", nullptr } });
		assert(!nullReason.error);
		assert(nullReason.reason.empty());
	}

	{
		const auto saved = F4T::DownedValidation::NormalizeCheckpointRequest(Json{
			{ "type", "checkpoint" },
			{ "interior", true },
			{ "space", 107092 },
			{ "x", 10.5 },
			{ "y", -20.0 },
			{ "z", 30.0 },
			{ "rotZ", 90.0 } });
		assert(!saved.error);
		assert(saved.checkpoint.interior);
		assert(saved.checkpoint.space == 107092);
		assert(saved.checkpoint.x == 10.5);
		assert(saved.checkpoint.y == -20.0);
		assert(saved.checkpoint.z == 30.0);
		assert(saved.checkpoint.rotZ == 90.0);

		const auto noRot = F4T::DownedValidation::NormalizeCheckpointRequest(Json{
			{ "interior", false }, { "space", 1 }, { "x", 0 }, { "y", 0 }, { "z", 0 } });
		assert(!noRot.error);
		assert(noRot.checkpoint.rotZ == 0.0);

		assert(F4T::DownedValidation::NormalizeCheckpointRequest(Json::array()).error == RequestError::Malformed);
		assert(F4T::DownedValidation::NormalizeCheckpointRequest(
			Json{ { "space", 1 }, { "x", 0 }, { "y", 0 }, { "z", 0 } }).error == RequestError::Malformed);
		assert(F4T::DownedValidation::NormalizeCheckpointRequest(
			Json{ { "interior", "true" }, { "space", 1 }, { "x", 0 }, { "y", 0 }, { "z", 0 } }).error == RequestError::Malformed);
		assert(F4T::DownedValidation::NormalizeCheckpointRequest(
			Json{ { "interior", true }, { "space", nullptr }, { "x", 0 }, { "y", 0 }, { "z", 0 } }).error == RequestError::Malformed);
		assert(F4T::DownedValidation::NormalizeCheckpointRequest(
			Json{ { "interior", true }, { "space", 1 }, { "y", 0 }, { "z", 0 } }).error == RequestError::Malformed);
		assert(F4T::DownedValidation::NormalizeCheckpointRequest(
			Json{ { "interior", true }, { "space", 0 }, { "x", 0 }, { "y", 0 }, { "z", 0 } }).error == RequestError::InvalidSpace);
		assert(F4T::DownedValidation::NormalizeCheckpointRequest(
			Json{ { "interior", true }, { "space", 4294967297ULL }, { "x", 0 }, { "y", 0 }, { "z", 0 } }).error == RequestError::InvalidSpace);
		assert(F4T::DownedValidation::NormalizeCheckpointRequest(
			Json{ { "interior", true }, { "space", 1 }, { "x", 1.0e9 }, { "y", 0 }, { "z", 0 } }).error == RequestError::InvalidPosition);
		assert(F4T::DownedValidation::NormalizeCheckpointRequest(
			Json{ { "interior", true }, { "space", 1 }, { "x", "10" }, { "y", 0 }, { "z", 0 } }).error == RequestError::InvalidPosition);
		assert(F4T::DownedValidation::NormalizeCheckpointRequest(
			Json{ { "interior", true }, { "space", 1 }, { "x", 0 }, { "y", 0 }, { "z", 0 }, { "rotZ", "a" } }).error == RequestError::InvalidPosition);

		assert(F4T::DownedState::IsValidCheckpoint(saved.checkpoint));
	}

	return 0;
}

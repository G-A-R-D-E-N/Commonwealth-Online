#include "F4TSleepBedIdentity.h"
#include "F4TWorkshopProtocol.h"
#include "F4TRemotePlayerCapacity.h"

#include <string>

namespace F4T::SleepBedIdentity
{
	std::optional<Identity> ParseBedIdentity(const nlohmann::json& a_value)
	{
		if (!a_value.is_object()) return std::nullopt;
		const auto kind = a_value.find("kind");
		if (kind == a_value.end() || !kind->is_string()) return std::nullopt;
		if (*kind == "form") {
			const auto key = a_value.find("key");
			if (key == a_value.end() || !key->is_string()) return std::nullopt;
			const auto form = QuestProtocol::ParseFormKey(key->get_ref<const std::string&>());
			if (!form) return std::nullopt;
			return *form;
		}
		if (*kind == "workshop") {
			const auto zone = a_value.find("zone");
			const auto object = a_value.find("object");
			if (zone == a_value.end() || object == a_value.end()) return std::nullopt;
			const auto zoneId = WorkshopProtocol::Codec::ReadU64(*zone);
			const auto objectId = WorkshopProtocol::Codec::ReadU64(*object);
			if (!zoneId || !objectId || *zoneId == 0 || *objectId == 0) return std::nullopt;
			return WorkshopObject{ *zoneId, *objectId };
		}
		return std::nullopt;
	}

	std::optional<RelayedSleep> ParseRelayedSleep(const nlohmann::json& a_value)
	{
		if (!a_value.is_object()) return std::nullopt;
		const auto player = a_value.find("playerId");
		const auto action = a_value.find("action");
		if (player == a_value.end() || action == a_value.end() || !action->is_string()) return std::nullopt;
		const auto playerId = WorkshopProtocol::Codec::ReadU32(*player);
		if (!playerId || *playerId == 0) return std::nullopt;
		if (*action == "exit") return RelayedSleep{ *playerId, std::nullopt, 0 };
		if (*action != "enter") return std::nullopt;
		const auto bed = a_value.find("bed");
		const auto marker = a_value.find("marker");
		if (bed == a_value.end() || marker == a_value.end()) return std::nullopt;
		const auto identity = ParseBedIdentity(*bed);
		const auto markerId = WorkshopProtocol::Codec::ReadU32(*marker);
		if (!identity || !markerId || !IsValidMarker(*markerId)) return std::nullopt;
		return RelayedSleep{ *playerId, *identity, *markerId };
	}

	std::optional<nlohmann::json> MakeSleepEnter(const Identity& a_bed, std::uint32_t a_marker)
	{
		if (!IsValidMarker(a_marker)) return std::nullopt;
		nlohmann::json bed;
		if (const auto* form = std::get_if<QuestProtocol::FormKey>(&a_bed)) {
			if (form->localId == 0 || form->localId > 0x00FFFFFFu) return std::nullopt;
			bed = { { "kind", "form" }, { "key", QuestProtocol::FormatFormKey(*form) } };
		} else {
			const auto& object = std::get<WorkshopObject>(a_bed);
			bed = { { "kind", "workshop" }, { "zone", object.zone }, { "object", object.object } };
		}
		if (!ParseBedIdentity(bed)) return std::nullopt;
		return nlohmann::json{ { "type", "sleepUse" }, { "action", "enter" }, { "bed", std::move(bed) }, { "marker", a_marker } };
	}

	nlohmann::json MakeSleepExit()
	{
		return { { "type", "sleepUse" }, { "action", "exit" } };
	}

	bool RemoteStates::Apply(const RelayedSleep& a_state)
	{
		if (a_state.playerId == 0) return false;
		if (!a_state.bed) { Erase(a_state.playerId); return true; }
		if (!states_.contains(a_state.playerId) && states_.size() >= RemotePlayerCapacity::MaximumTrackedRemotePlayers) return false;
		states_.insert_or_assign(a_state.playerId, Entry{ a_state, ++revision_ });
		return true;
	}

	std::optional<RelayedSleep> RemoteStates::Find(std::uint32_t a_playerId) const
	{
		const auto found = states_.find(a_playerId);
		return found == states_.end() ? std::nullopt : std::optional{ found->second.state };
	}

	std::optional<std::uint64_t> RemoteStates::Revision(std::uint32_t a_playerId) const
	{
		const auto found = states_.find(a_playerId);
		return found == states_.end() ? std::nullopt : std::optional{ found->second.revision };
	}

	void RemoteStates::Erase(std::uint32_t a_playerId) { states_.erase(a_playerId); ++revision_; }
	void RemoteStates::Clear() { states_.clear(); ++revision_; }
}

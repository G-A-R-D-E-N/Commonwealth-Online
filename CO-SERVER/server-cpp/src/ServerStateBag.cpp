#include "ServerProtocolImpl.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace CommonwealthOnline::Server
{
	namespace StateBagProtocol = F4T::StateBagProtocol;

	namespace
	{
		std::size_t EntryBytes(std::string_view a_key, const Json& a_value)
		{
			return a_key.size() + StateBagProtocol::EncodedSize(a_value);
		}

		std::size_t PacketBytes(const Json& a_packet)
		{
			return a_packet.dump(-1, ' ', false, Json::error_handler_t::replace).size();
		}

		Json EncodeBag(const std::map<std::string, Json, std::less<>>& a_values)
		{
			Json object = Json::object();
			for (const auto& entry : a_values) {
				object[entry.first] = entry.second;
			}
			return object;
		}

		Json SnapshotPacket(Json a_bags)
		{
			return Json{
				{ "type", std::string{ StateBagProtocol::kPacketType } },
				{ "op", "snapshot" },
				{ "bags", std::move(a_bags) }
			};
		}
	}

	std::string StateBagStore::Set(const StateBagProtocol::BagId& a_bag, std::string_view a_key, const Json& a_value)
	{
		if (!StateBagProtocol::IsValidKey(a_key)) {
			return "invalid key";
		}
		if (a_value.is_null()) {
			(void)Erase(a_bag, a_key);
			return {};
		}
		if (!StateBagProtocol::IsValidValue(a_value)) {
			return "invalid value";
		}
		if (StateBagProtocol::EncodedSize(a_value) > StateBagProtocol::kMaximumValueBytes) {
			return "value too large";
		}
		const Bag empty{};
		const Bag* found = Find(a_bag);
		const Bag& current = found != nullptr ? *found : empty;
		const auto replaced = current.values.find(a_key);
		const bool isNew = replaced == current.values.end();
		if (isNew && current.values.size() >= StateBagProtocol::kMaximumKeysPerBag) {
			return "too many keys";
		}
		const std::size_t previousBytes = isNew ? 0 : EntryBytes(replaced->first, replaced->second);
		const std::size_t bytes = current.bytes - previousBytes + EntryBytes(a_key, a_value);
		if (bytes > StateBagProtocol::kMaximumBagBytes) {
			return "bag too large";
		}
		Bag& target = Ensure(a_bag);
		target.values.insert_or_assign(std::string{ a_key }, a_value);
		target.bytes = bytes;
		return {};
	}

	bool StateBagStore::Erase(const StateBagProtocol::BagId& a_bag, std::string_view a_key)
	{
		const auto found = Find(a_bag);
		if (found == nullptr || !found->values.contains(a_key)) {
			return false;
		}
		Bag& bag = Ensure(a_bag);
		const auto entry = bag.values.find(a_key);
		bag.bytes -= EntryBytes(entry->first, entry->second);
		bag.values.erase(entry);
		if (a_bag.kind == StateBagProtocol::BagKind::Player && bag.values.empty()) {
			players.erase(a_bag.playerId);
		}
		return true;
	}

	std::optional<Json> StateBagStore::Get(const StateBagProtocol::BagId& a_bag, std::string_view a_key) const
	{
		const auto found = Find(a_bag);
		if (found == nullptr) {
			return std::nullopt;
		}
		const auto entry = found->values.find(a_key);
		if (entry == found->values.end()) {
			return std::nullopt;
		}
		return entry->second;
	}

	std::vector<std::string> StateBagStore::Keys(const StateBagProtocol::BagId& a_bag) const
	{
		std::vector<std::string> keys;
		const auto found = Find(a_bag);
		if (found == nullptr) {
			return keys;
		}
		keys.reserve(found->values.size());
		for (const auto& entry : found->values) {
			keys.push_back(entry.first);
		}
		return keys;
	}

	bool StateBagStore::DropPlayer(std::uint32_t a_playerId)
	{
		return players.erase(a_playerId) > 0;
	}

	std::vector<Json> StateBagStore::SnapshotPackets(std::uint32_t a_ownerPlayerId) const
	{
		const auto globalName = std::string{ StateBagProtocol::kGlobalBagName };
		const auto ownerBagId = StateBagProtocol::PlayerBag(a_ownerPlayerId);
		const auto ownerName = StateBagProtocol::FormatBag(ownerBagId);
		const Bag* owner = Find(ownerBagId);
		Json bags = Json::object();
		bags[globalName] = EncodeBag(global.values);
		if (owner != nullptr) {
			bags[ownerName] = EncodeBag(owner->values);
		}
		Json full = SnapshotPacket(std::move(bags));
		if (PacketBytes(full) <= kMaximumStateBagFrameBytes) {
			return { std::move(full) };
		}
		Json skeletonBags = Json::object();
		skeletonBags[globalName] = Json::object();
		if (owner != nullptr) {
			skeletonBags[ownerName] = Json::object();
		}
		std::vector<Json> packets;
		packets.reserve(1 + global.values.size() + (owner != nullptr ? owner->values.size() : 0));
		packets.push_back(SnapshotPacket(std::move(skeletonBags)));
		for (const auto& entry : global.values) {
			packets.push_back(ChangePacket(StateBagProtocol::BagId{}, entry.first, entry.second));
		}
		if (owner != nullptr) {
			for (const auto& entry : owner->values) {
				packets.push_back(ChangePacket(ownerBagId, entry.first, entry.second));
			}
		}
		return packets;
	}

	Json StateBagStore::ChangePacket(const StateBagProtocol::BagId& a_bag, std::string_view a_key, const Json& a_value)
	{
		const auto bag = StateBagProtocol::FormatBag(a_bag);
		if (a_value.is_null()) {
			return Json{
				{ "type", std::string{ StateBagProtocol::kPacketType } },
				{ "op", "delete" },
				{ "bag", bag },
				{ "key", std::string{ a_key } }
			};
		}
		return Json{
			{ "type", std::string{ StateBagProtocol::kPacketType } },
			{ "op", "set" },
			{ "bag", bag },
			{ "key", std::string{ a_key } },
			{ "value", a_value }
		};
	}

	Json StateBagStore::ClearPacket(const StateBagProtocol::BagId& a_bag)
	{
		return Json{
			{ "type", std::string{ StateBagProtocol::kPacketType } },
			{ "op", "clear" },
			{ "bag", StateBagProtocol::FormatBag(a_bag) }
		};
	}

	const StateBagStore::Bag* StateBagStore::Find(const StateBagProtocol::BagId& a_bag) const
	{
		if (a_bag.kind == StateBagProtocol::BagKind::Global) {
			return &global;
		}
		const auto found = players.find(a_bag.playerId);
		return found == players.end() ? nullptr : &found->second;
	}

	StateBagStore::Bag& StateBagStore::Ensure(const StateBagProtocol::BagId& a_bag)
	{
		return a_bag.kind == StateBagProtocol::BagKind::Global ? global : players[a_bag.playerId];
	}

	std::vector<OutboundFrame> StateBagSnapshotFrames(
		std::uint32_t a_connectionId,
		const SessionTypes::Session& a_target,
		const StateBagStore& a_store)
	{
		if (!a_target.compatibilityCapabilities.Has(StateBagProtocol::kCapability)) {
			return {};
		}
		std::vector<OutboundFrame> frames;
		for (const auto& packet : a_store.SnapshotPackets(a_target.playerId)) {
			frames.push_back(ReliableFrame(a_connectionId, packet));
		}
		return frames;
	}

	std::vector<OutboundFrame> StateBagBroadcastFrames(
		const StateBagSessions& a_sessions,
		const StateBagProtocol::BagId& a_bag,
		const Json& a_packet)
	{
		std::vector<OutboundFrame> frames;
		for (const auto& [connectionId, session] : a_sessions) {
			if (!session.active || !session.compatibilityCapabilities.Has(StateBagProtocol::kCapability)) {
				continue;
			}
			if (a_bag.kind == StateBagProtocol::BagKind::Player && session.playerId != a_bag.playerId) {
				continue;
			}
			frames.push_back(ReliableFrame(connectionId, a_packet));
		}
		return frames;
	}

	std::vector<OutboundFrame> StateBagDisconnectFrames(StateBagStore& a_store, const StateBagSessions&, std::uint32_t a_playerId)
	{
		(void)a_store.DropPlayer(a_playerId);
		return {};
	}

	Json ProtocolHost::Impl::ReadStateBag(const StateBagProtocol::BagId& a_bag) const
	{
		Json values = Json::object();
		for (const auto& key : stateBag.Keys(a_bag)) {
			if (const auto value = stateBag.Get(a_bag, key)) values[key] = *value;
		}
		return values;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::SetStateBag(
		const StateBagProtocol::BagId& a_bag,
		std::string_view a_key,
		const Json& a_value,
		std::string& a_failure)
	{
		if (a_bag.kind == StateBagProtocol::BagKind::Player &&
			std::none_of(sessions.begin(), sessions.end(), [&a_bag](const auto& a_entry) { return a_entry.second.active && a_entry.second.playerId == a_bag.playerId; })) {
			a_failure = "player not connected";
			return {};
		}
		const auto previous = stateBag.Get(a_bag, a_key);
		a_failure = stateBag.Set(a_bag, a_key, a_value);
		if (!a_failure.empty()) {
			return {};
		}
		if (a_value.is_null() ? !previous.has_value() : (previous.has_value() && *previous == a_value)) {
			return {};
		}
		const auto packet = StateBagStore::ChangePacket(a_bag, a_key, a_value);
		events.Notify(ServerEvent{
			ServerEventKind::StateBagChange,
			0,
			a_bag.kind == StateBagProtocol::BagKind::Player ? a_bag.playerId : 0,
			{},
			&packet
		});
		return StateBagBroadcastFrames(sessions, a_bag, packet);
	}
}

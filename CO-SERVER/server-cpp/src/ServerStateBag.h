#pragma once

#include "F4TStateBagProtocol.h"
#include "ServerOutboundFrames.h"
#include "ServerSessionTypes.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CommonwealthOnline::Server
{
	inline constexpr std::size_t kMaximumStateBagFrameBytes = 48 * 1024;

	class StateBagStore
	{
	public:
		[[nodiscard]] std::string Set(const F4T::StateBagProtocol::BagId& a_bag, std::string_view a_key, const nlohmann::json& a_value);
		bool Erase(const F4T::StateBagProtocol::BagId& a_bag, std::string_view a_key);
		[[nodiscard]] std::optional<nlohmann::json> Get(const F4T::StateBagProtocol::BagId& a_bag, std::string_view a_key) const;
		[[nodiscard]] std::vector<std::string> Keys(const F4T::StateBagProtocol::BagId& a_bag) const;
		bool DropPlayer(std::uint32_t a_playerId);
		[[nodiscard]] std::vector<nlohmann::json> SnapshotPackets(std::uint32_t a_ownerPlayerId) const;
		[[nodiscard]] static nlohmann::json ChangePacket(
			const F4T::StateBagProtocol::BagId& a_bag,
			std::string_view a_key,
			const nlohmann::json& a_value);
		[[nodiscard]] static nlohmann::json ClearPacket(const F4T::StateBagProtocol::BagId& a_bag);

	private:
		struct Bag
		{
			std::map<std::string, nlohmann::json, std::less<>> values;
			std::size_t bytes{};
		};

		[[nodiscard]] const Bag* Find(const F4T::StateBagProtocol::BagId& a_bag) const;
		Bag& Ensure(const F4T::StateBagProtocol::BagId& a_bag);

		Bag global;
		std::map<std::uint32_t, Bag> players;
	};

	using StateBagSessions = std::unordered_map<std::uint32_t, SessionTypes::Session>;

	std::vector<OutboundFrame> StateBagSnapshotFrames(
		std::uint32_t a_connectionId,
		const SessionTypes::Session& a_target,
		const StateBagStore& a_store);

	std::vector<OutboundFrame> StateBagBroadcastFrames(
		const StateBagSessions& a_sessions,
		const F4T::StateBagProtocol::BagId& a_bag,
		const nlohmann::json& a_packet);

	std::vector<OutboundFrame> StateBagDisconnectFrames(StateBagStore& a_store, const StateBagSessions& a_sessions, std::uint32_t a_playerId);
}

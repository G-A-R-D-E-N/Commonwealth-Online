#pragma once

#include "ServerSessionTypes.h"
#include "F4TPlayerStateValidation.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace CommonwealthOnline::Server::Activity
{
	inline constexpr double kPositionEpsilon = 1.0;
	inline constexpr double kHeadingEpsilon = 0.01;

	[[nodiscard]] inline bool MovementChanged(
		const SessionTypes::StoredMovementState& a_current,
		const std::optional<SessionTypes::StoredMovementState>& a_previous)
	{
		if (!a_previous) {
			return true;
		}
		if (a_current.cellId != a_previous->cellId || a_current.worldspaceId != a_previous->worldspaceId) {
			return true;
		}
		return std::abs(a_current.x - a_previous->x) > kPositionEpsilon ||
		       std::abs(a_current.y - a_previous->y) > kPositionEpsilon ||
		       std::abs(a_current.z - a_previous->z) > kPositionEpsilon ||
		       std::abs(a_current.angleZ - a_previous->angleZ) > kHeadingEpsilon;
	}

	[[nodiscard]] inline bool IsTimerPacket(std::string_view a_type)
	{
		return a_type == "keepAlive" || a_type == "pluginList" || a_type == "worldState" ||
		       a_type == "npcState";
	}

	[[nodiscard]] inline bool PlayerStateChangedByPlayer(const nlohmann::json& a_packet)
	{
		for (const char* field : F4T::PlayerStateValidation::kRelayedPlayerStateFields) {
			if (a_packet.contains(field)) {
				return true;
			}
		}
		if (const auto actions = a_packet.find("actionEvents"); actions != a_packet.end()) {
			return actions->is_array() && !actions->empty();
		}
		return false;
	}

	[[nodiscard]] inline bool IsPlayerActivity(std::string_view a_type, const nlohmann::json& a_packet)
	{
		if (IsTimerPacket(a_type) || a_type == "transform") {
			return false;
		}
		if (a_type == "playerState") {
			return PlayerStateChangedByPlayer(a_packet);
		}
		return true;
	}

	class PlayerActivityCommit
	{
	public:
		PlayerActivityCommit(
			std::unordered_map<std::uint32_t, SessionTypes::Session>& a_sessions,
			std::uint32_t a_connectionId,
			bool a_activity,
			const std::uint64_t& a_rejectedCounter,
			const std::uint64_t& a_chatRejectedCounter) :
			sessions(a_sessions),
			connectionId(a_connectionId),
			activity(a_activity),
			rejectedCounter(a_rejectedCounter),
			chatRejectedCounter(a_chatRejectedCounter),
			rejectedBefore(Rejections())
		{}

		PlayerActivityCommit(const PlayerActivityCommit&) = delete;
		PlayerActivityCommit& operator=(const PlayerActivityCommit&) = delete;

		~PlayerActivityCommit()
		{
			const auto found = sessions.find(connectionId);
			if (found == sessions.end()) return;
			auto& session = found->second;
			if (activity && Rejections() == rejectedBefore) {
				session.playerActivityPending = true;
			}
			if (session.playerActivityPending) {
				session.afkExemption.streakStartUnixSeconds = 0.0;
			}
		}

	private:
		[[nodiscard]] std::uint64_t Rejections() const
		{
			return rejectedCounter + chatRejectedCounter;
		}

		std::unordered_map<std::uint32_t, SessionTypes::Session>& sessions;
		std::uint32_t connectionId;
		bool activity;
		const std::uint64_t& rejectedCounter;
		const std::uint64_t& chatRejectedCounter;
		std::uint64_t rejectedBefore;
	};
}

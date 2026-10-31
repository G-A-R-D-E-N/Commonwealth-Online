#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace F4T::Friends
{
	enum class Relationship : std::uint8_t
	{
		none,
		friends,
		incoming,
		outgoing
	};

	void NoteWelcome(const nlohmann::json& a_welcome);
	bool TryHandlePacket(std::string_view a_type, const nlohmann::json& a_packet);
	void Reset();

	[[nodiscard]] bool ServerSupports();
	[[nodiscard]] Relationship RelationshipWith(std::uint32_t a_playerId);
	[[nodiscard]] std::string IncomingRequestId(std::uint32_t a_playerId);
	[[nodiscard]] std::string FriendTokenFor(std::uint32_t a_playerId);
	[[nodiscard]] bool IsPlayerFriend(std::uint32_t a_playerId);
	[[nodiscard]] std::string BuildStateJson();

	void RequestFriend(std::uint32_t a_targetPlayerId);
	void RespondToRequest(const std::string& a_requestId, bool a_accept);
	void RemoveFriend(const std::string& a_friendId);
	void RequestList();
	void HandleAction(std::string_view a_actionJson);
}

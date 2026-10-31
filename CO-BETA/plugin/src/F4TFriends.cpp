#include "pch.h"

#include "F4TFriends.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TFactions.h"
#include "F4TInteractWheel.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TUIClient.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace F4T::Friends
{
	using namespace F4T::Networking::Detail;

	namespace
	{
		using Json = nlohmann::json;

		constexpr std::string_view kCapability{ "friends-v1" };
		constexpr std::string_view kAlertPrefix{ "friend-request-" };
		constexpr std::string_view kResultAlertPrefix{ "friend-result-" };
		constexpr std::size_t kMaximumFriendIdBytes = 64;
		constexpr std::size_t kMaximumNameBytes = 128;
		constexpr std::size_t kMaximumFriends = 200;
		constexpr std::size_t kMaximumFriendRowBytes = 896;
		constexpr std::size_t kFriendsOutputBudgetBytes = 176 * 1024;
		static_assert(kMaximumFriends * kMaximumFriendRowBytes <= kFriendsOutputBudgetBytes);
		constexpr std::uint32_t kResultAlertMs = 6000U;

		struct Friend
		{
			std::string friendId;
			std::string name;
			bool online{ false };
			std::uint32_t playerId{ 0 };
		};

		struct Incoming
		{
			std::string requestId;
			std::uint32_t fromPlayerId{ 0 };
			std::string displayName;
		};

		std::mutex g_mutex;
		bool g_supported = false;
		bool g_receivedState = false;
		std::vector<Friend> g_friends;
		std::vector<Incoming> g_incoming;
		std::vector<std::uint32_t> g_outgoing;

		void QueuePacket(Json a_packet)
		{
			F4T::Factions::QueueGameTask([packet = std::move(a_packet)]() {
				if (!F4T::Networking::IsConnectedToServer()) {
					return;
				}
				if (!SendPacketBytes(packet.dump())) {
					LogThrottledWarning("friend_send_failed", "Could not send a Commonwealth Online friend packet.");
				}
			});
		}

		void QueueStateChanged()
		{
			F4T::Factions::QueueGameTask([]() { F4T::UIClient::NotifyFactionStateChanged(); });
		}

		void QueueNameRefresh()
		{
			F4T::Factions::QueueKnownSetChanged();
		}

		COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1* AlertUi()
		{
			auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
			return ui && ui->ShowAlert ? ui : nullptr;
		}

		void DismissAlert(const std::string& a_alertId)
		{
			auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
			if (ui && ui->DismissAlert) {
				ui->DismissAlert(a_alertId.c_str());
			}
		}

		void OnRequestAlertResponse(const char* a_alertId, const char* a_actionId, void*)
		{
			if (!a_alertId || !a_actionId) {
				return;
			}
			const std::string_view alertId{ a_alertId };
			if (!alertId.starts_with(kAlertPrefix)) {
				return;
			}
			F4T::Friends::RespondToRequest(std::string{ alertId.substr(kAlertPrefix.size()) }, std::string_view{ a_actionId } == "accept");
		}

		void ShowRequestAlert(const Incoming& a_incoming, std::uint32_t a_expiresInSeconds)
		{
			auto* ui = AlertUi();
			if (!ui) {
				return;
			}
			const auto alertId = std::string{ kAlertPrefix } + a_incoming.requestId;
			std::array<COMMONWEALTH_ONLINE_UI_API::AlertAction, 2> actions{ {
				{ "accept", "Accept", "Y" },
				{ "decline", "Decline", "N" }
			} };
			const auto message = std::format("{} wants to add you as a friend", a_incoming.displayName);
			COMMONWEALTH_ONLINE_UI_API::AlertRequest request{};
			request.id = alertId.c_str();
			request.category = "Social";
			request.title = "Friend request";
			request.message = message.c_str();
			request.tone = "invite";
			request.durationMs = std::min<std::uint32_t>(a_expiresInSeconds, 60U) * 1000U;
			request.actions = actions.data();
			request.actionCount = actions.size();
			(void)ui->ShowAlert(&request, &OnRequestAlertResponse, nullptr, nullptr, 0);
		}

		void ShowResultAlert(const std::string& a_error)
		{
			auto* ui = AlertUi();
			if (!ui) {
				return;
			}
			const auto message = [&a_error]() -> std::string {
				if (a_error == "declined") return "Friend request declined.";
				if (a_error == "requestExpired") return "Friend request expired.";
				if (a_error == "friendListFull") return "Friend list is full.";
				if (a_error == "targetUnsupported") return "That player's game does not support friends.";
				if (a_error == "targetUnavailable") return "That player is no longer online.";
				if (a_error == "alreadyFriends") return "You are already friends with that player.";
				if (a_error == "requestExists") return "You already have a friend request pending.";
				if (a_error == "requestCapacity") return "That player has too many pending requests.";
				if (a_error == "rateLimited") return "Too many friend requests. Try again in a minute.";
				if (a_error == "requestNotFound") return "That friend request is no longer available.";
				if (a_error == "notFriends") return "You are not friends with that player.";
				if (a_error == "persistenceUnavailable") return "The server could not save the friend change.";
				if (a_error == "invalidTarget") return "That player cannot be added.";
				return "Friend action failed.";
			}();
			const auto alertId = std::string{ kResultAlertPrefix } + a_error;
			COMMONWEALTH_ONLINE_UI_API::AlertRequest request{};
			request.id = alertId.c_str();
			request.category = "Social";
			request.title = "Friends";
			request.message = message.c_str();
			request.tone = "warning";
			request.durationMs = kResultAlertMs;
			(void)ui->ShowAlert(&request, nullptr, nullptr, nullptr, 0);
		}

		Incoming ReadIncoming(const Json& a_json)
		{
			Incoming incoming;
			incoming.requestId = ReadString(a_json, "requestId").value_or("");
			incoming.fromPlayerId = ReadUint32(a_json, "fromPlayerId").value_or(0);
			incoming.displayName = ReadString(a_json, "displayName").value_or("Stranger");
			return incoming;
		}

		void HandleStatePacket(const Json& a_packet)
		{
			std::vector<Friend> friends;
			std::vector<Incoming> incoming;
			std::vector<std::uint32_t> outgoing;
			if (const auto list = a_packet.find("friends"); list != a_packet.end() && list->is_array()) {
				for (const auto& row : *list) {
					if (friends.size() >= kMaximumFriends) break;
					Friend entry;
					entry.friendId = ReadString(row, "friendId").value_or("");
					entry.name = ReadString(row, "name").value_or("");
					entry.online = ReadBool(row, "online").value_or(false);
					entry.playerId = ReadUint32(row, "playerId").value_or(0);
					if (entry.friendId.empty() || entry.friendId.size() > kMaximumFriendIdBytes || entry.name.empty() || entry.name.size() > kMaximumNameBytes) {
						continue;
					}
					friends.push_back(std::move(entry));
				}
			}
			if (const auto list = a_packet.find("pendingIncoming"); list != a_packet.end() && list->is_array()) {
				for (const auto& row : *list) {
					auto entry = ReadIncoming(row);
					if (entry.requestId.empty() || entry.requestId.size() > kMaximumFriendIdBytes) {
						continue;
					}
					if (entry.displayName.size() > kMaximumNameBytes) entry.displayName = entry.displayName.substr(0, kMaximumNameBytes);
					incoming.push_back(std::move(entry));
				}
			}
			if (const auto list = a_packet.find("pendingOutgoingPlayerIds"); list != a_packet.end() && list->is_array()) {
				for (const auto& row : *list) {
					if (const auto playerId = ReadUint32(Json{ { "playerId", row } }, "playerId")) outgoing.push_back(*playerId);
				}
			}

			std::vector<Incoming> added;
			std::vector<std::string> removed;
			{
				std::scoped_lock lock(g_mutex);
				for (const auto& candidate : incoming) {
					const bool known = std::any_of(g_incoming.begin(), g_incoming.end(), [&candidate](const Incoming& a_existing) {
						return a_existing.requestId == candidate.requestId;
					});
					if (!known) added.push_back(candidate);
				}
				for (const auto& existing : g_incoming) {
					const bool remains = std::any_of(incoming.begin(), incoming.end(), [&existing](const Incoming& a_candidate) {
						return a_candidate.requestId == existing.requestId;
					});
					if (!remains) removed.push_back(existing.requestId);
				}
				g_supported = true;
				g_receivedState = true;
				g_friends = std::move(friends);
				g_incoming = std::move(incoming);
				g_outgoing = std::move(outgoing);
			}
			for (const auto& request : added) ShowRequestAlert(request, 60U);
			for (const auto& requestId : removed) DismissAlert(std::string{ kAlertPrefix } + requestId);
			QueueStateChanged();
			QueueNameRefresh();
		}

		void HandleRequestReceivedPacket(const Json& a_packet)
		{
			auto entry = ReadIncoming(a_packet);
			if (entry.requestId.empty() || entry.requestId.size() > kMaximumFriendIdBytes) {
				return;
			}
			if (entry.displayName.size() > kMaximumNameBytes) entry.displayName = entry.displayName.substr(0, kMaximumNameBytes);
			const auto expiresInSeconds = ReadUint32(a_packet, "expiresInSeconds").value_or(60);
			{
				std::scoped_lock lock(g_mutex);
				const bool duplicate = std::any_of(g_incoming.begin(), g_incoming.end(), [&entry](const Incoming& a_existing) {
					return a_existing.requestId == entry.requestId;
				});
				if (!duplicate) g_incoming.push_back(entry);
			}
			ShowRequestAlert(entry, expiresInSeconds);
			QueueStateChanged();
		}

		void HandleResultPacket(const Json& a_packet)
		{
			const bool ok = ReadBool(a_packet, "ok").value_or(false);
			const auto error = ReadString(a_packet, "error").value_or("");
			{
				std::scoped_lock lock(g_mutex);
				g_supported = true;
			}
			if (!ok && !error.empty()) ShowResultAlert(error);
			QueueStateChanged();
			QueueNameRefresh();
		}
	}

	void NoteWelcome(const Json& a_welcome)
	{
		const auto capabilities = a_welcome.find("capabilities");
		const bool supported = capabilities != a_welcome.end() && capabilities->is_array() &&
			std::any_of(capabilities->begin(), capabilities->end(), [](const Json& a_capability) {
				return a_capability.is_string() && a_capability.get<std::string>() == kCapability;
			});
		bool firstSupport = false;
		{
			std::scoped_lock lock(g_mutex);
			firstSupport = supported && !g_supported;
			g_supported = supported;
		}
		if (firstSupport) QueuePacket(Json{ { "type", "friendListRequest" } });
		QueueStateChanged();
	}

	bool TryHandlePacket(std::string_view a_type, const Json& a_packet)
	{
		if (a_type == "friendState") {
			HandleStatePacket(a_packet);
		} else if (a_type == "friendRequestReceived") {
			HandleRequestReceivedPacket(a_packet);
		} else if (a_type == "friendResult") {
			HandleResultPacket(a_packet);
		} else {
			return false;
		}
		return true;
	}

	void Reset()
	{
		std::vector<std::string> alertIds;
		{
			std::scoped_lock lock(g_mutex);
			for (const auto& request : g_incoming) alertIds.push_back(std::string{ kAlertPrefix } + request.requestId);
			g_supported = false;
			g_receivedState = false;
			g_friends.clear();
			g_incoming.clear();
			g_outgoing.clear();
		}
		for (const auto& alertId : alertIds) DismissAlert(alertId);
		F4T::InteractWheel::Close("Disconnected");
		QueueStateChanged();
	}

	bool ServerSupports()
	{
		std::scoped_lock lock(g_mutex);
		return g_supported;
	}

	Relationship RelationshipWith(std::uint32_t a_playerId)
	{
		if (a_playerId == 0) return Relationship::none;
		std::scoped_lock lock(g_mutex);
		if (std::any_of(g_friends.begin(), g_friends.end(), [a_playerId](const Friend& a_friend) {
				return a_friend.online && a_friend.playerId == a_playerId;
			})) {
			return Relationship::friends;
		}
		if (std::any_of(g_incoming.begin(), g_incoming.end(), [a_playerId](const Incoming& a_request) {
				return a_request.fromPlayerId == a_playerId;
			})) {
			return Relationship::incoming;
		}
		if (std::find(g_outgoing.begin(), g_outgoing.end(), a_playerId) != g_outgoing.end()) return Relationship::outgoing;
		return Relationship::none;
	}

	std::string IncomingRequestId(std::uint32_t a_playerId)
	{
		std::scoped_lock lock(g_mutex);
		const auto found = std::find_if(g_incoming.begin(), g_incoming.end(), [a_playerId](const Incoming& a_request) {
			return a_request.fromPlayerId == a_playerId;
		});
		return found == g_incoming.end() ? std::string{} : found->requestId;
	}

	std::string FriendTokenFor(std::uint32_t a_playerId)
	{
		std::scoped_lock lock(g_mutex);
		const auto found = std::find_if(g_friends.begin(), g_friends.end(), [a_playerId](const Friend& a_friend) {
			return a_friend.online && a_friend.playerId == a_playerId;
		});
		return found == g_friends.end() ? std::string{} : found->friendId;
	}

	bool IsPlayerFriend(std::uint32_t a_playerId)
	{
		return RelationshipWith(a_playerId) == Relationship::friends;
	}

	std::string BuildStateJson()
	{
		Json state = Json::object();
		{
			std::scoped_lock lock(g_mutex);
			state["supported"] = g_supported;
			state["loaded"] = g_receivedState;
			Json friends = Json::array();
			for (const auto& entry : g_friends) {
				friends.push_back(Json{
					{ "friendId", entry.friendId },
					{ "name", entry.name },
					{ "online", entry.online }
				});
			}
			Json incoming = Json::array();
			for (const auto& request : g_incoming) {
				incoming.push_back(Json{
					{ "requestId", request.requestId },
					{ "fromPlayerId", request.fromPlayerId },
					{ "displayName", request.displayName }
				});
			}
			state["friends"] = std::move(friends);
			state["pendingIncoming"] = std::move(incoming);
			state["pendingOutgoingPlayerIds"] = g_outgoing;
		}
		return state.dump();
	}

	void RequestFriend(std::uint32_t a_targetPlayerId)
	{
		if (a_targetPlayerId == 0) return;
		QueuePacket(Json{ { "type", "friendRequest" }, { "targetPlayerId", a_targetPlayerId } });
	}

	void RespondToRequest(const std::string& a_requestId, bool a_accept)
	{
		if (a_requestId.empty()) return;
		{
			std::scoped_lock lock(g_mutex);
			const auto before = g_incoming.size();
			std::erase_if(g_incoming, [&a_requestId](const Incoming& a_request) { return a_request.requestId == a_requestId; });
			if (g_incoming.size() == before) return;
		}
		DismissAlert(std::string{ kAlertPrefix } + a_requestId);
		QueuePacket(Json{
			{ "type", "friendRequestResponse" },
			{ "requestId", a_requestId },
			{ "accept", a_accept }
		});
		QueueStateChanged();
	}

	void RemoveFriend(const std::string& a_friendId)
	{
		if (a_friendId.empty()) return;
		QueuePacket(Json{ { "type", "friendRemove" }, { "friendId", a_friendId } });
	}

	void RequestList()
	{
		QueuePacket(Json{ { "type", "friendListRequest" } });
	}

	void HandleAction(std::string_view a_actionJson)
	{
		const auto parsed = Json::parse(a_actionJson, nullptr, false);
		if (!parsed.is_object()) return;
		const auto op = ReadString(parsed, "op").value_or("");
		if (op == "friendAdd") {
			RequestFriend(ReadUint32(parsed, "playerId").value_or(0));
		} else if (op == "friendAccept") {
			RespondToRequest(ReadString(parsed, "requestId").value_or(""), true);
		} else if (op == "friendDecline") {
			RespondToRequest(ReadString(parsed, "requestId").value_or(""), false);
		} else if (op == "friendRemove") {
			RemoveFriend(ReadString(parsed, "friendId").value_or(""));
		} else if (op == "friendList") {
			RequestList();
		}
	}
}

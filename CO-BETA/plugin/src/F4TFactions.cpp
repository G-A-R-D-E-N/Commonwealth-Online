#include "pch.h"

#include "F4TFactions.h"

#include "F4TFactionList.h"
#include "F4TFactionPayload.h"
#include "F4TFriends.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TProxyActorController.h"
#include "F4TRemotePlayerState.h"
#include "F4TSocialNameplate.h"
#include "F4TUIClient.h"

#include "CommonwealthOnlineUI_API.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace F4T::Factions
{
	using namespace F4T::Networking::Detail;

	namespace
	{
		constexpr std::size_t kMaximumIdBytes = 64;
		constexpr std::size_t kMaximumProfileIdBytes = 128;
		constexpr std::size_t kMaximumFactionNameBytes = 64;
		constexpr std::size_t kMaximumPlayerNameBytes = 128;
		constexpr std::size_t kMaximumTagBytes = 8;
		constexpr std::size_t kMaximumReasonBytes = 64;
		constexpr std::size_t kMaximumActionJsonBytes = 4 * 1024;
		constexpr std::size_t kMaximumMembers = 16;
		constexpr std::size_t kMaximumPendingInvites = 16;
		constexpr std::size_t kMaximumRosterEntries = 16;
		constexpr std::string_view kFactionsCapability = "factions-v1";
		constexpr std::string_view kInviteAlertPrefix = "faction-invite-";
		constexpr std::array<std::string_view, 9> kResultOps{ "create", "invite", "inviteResponse", "leave", "kick", "promote", "disband", "setRank", "setProfile" };
		constexpr auto kRosterRefreshInterval = std::chrono::seconds{ 2 };
		constexpr auto kFactionListRequestInterval = std::chrono::seconds{ 1 };

		struct Member
		{
			std::string profileId;
			std::uint32_t playerId{ 0 };
			std::string name;
			bool online{ false };
			bool leader{ false };
			bool officer{ false };
			std::optional<std::uint32_t> health;
			bool downed{ false };
			bool dead{ false };
		};

		struct Faction
		{
			std::string factionId;
			std::string name;
			std::string tag;
			std::string description;
			std::string motd;
			std::string color;
			std::uint32_t leaderPlayerId{ 0 };
			std::vector<Member> members;
		};

		struct Invite
		{
			std::string inviteId;
			std::string factionName;
			std::string factionTag;
			std::string fromName;
			std::chrono::steady_clock::time_point expiresAt{};
		};

		struct RosterEntry
		{
			std::uint32_t playerId{ 0 };
			std::string name;
			std::string tag;
		};

		struct LastResult
		{
			std::string op;
			bool ok{ false };
			std::string reason;
		};

		std::mutex g_mutex;
		bool g_serverSupportsFactions = false;
		std::optional<Faction> g_faction;
		std::vector<Invite> g_pendingInvites;
		std::vector<RosterEntry> g_roster;
		std::vector<List::Entry> g_factionList;
		bool g_factionsLoaded = false;
		std::optional<LastResult> g_lastResult;
		std::optional<std::chrono::steady_clock::time_point> g_lastRosterRefresh;
		std::optional<std::chrono::steady_clock::time_point> g_lastFactionListRequest;

		std::optional<std::string> ReadToken(const Json& a_json, const char* a_field, std::size_t a_maximumBytes)
		{
			auto text = ReadString(a_json, a_field);
			if (!text || !Payload::IsSafeToken(*text, a_maximumBytes)) {
				return std::nullopt;
			}
			return text;
		}

		std::optional<std::string> ReadName(const Json& a_json, const char* a_field, std::size_t a_maximumBytes)
		{
			const auto text = ReadString(a_json, a_field);
			if (!text) {
				return std::nullopt;
			}
			auto clean = Payload::SanitizeText(*text, a_maximumBytes);
			if (clean.empty()) {
				return std::nullopt;
			}
			return clean;
		}

		std::optional<std::uint32_t> ReadExpiresInSeconds(const Json& a_json)
		{
			const auto seconds = ReadDouble(a_json, "expiresInSeconds");
			return seconds ? Payload::ToWholeSeconds(*seconds) : std::nullopt;
		}

		Json PlayerIdJson(std::uint32_t a_playerId)
		{
			return a_playerId == 0 ? Json(nullptr) : Json(a_playerId);
		}

		void QueuePacket(Json a_packet)
		{
			QueueGameTask([packet = std::move(a_packet)]() {
				if (!F4T::Networking::IsConnectedToServer()) {
					return;
				}
				if (!SendPacketBytes(packet.dump())) {
					LogThrottledWarning("faction_send_failed", "Could not send Commonwealth Online faction packet.");
				}
			});
		}

		void DismissInviteAlerts(std::vector<std::string> a_inviteIds)
		{
			if (a_inviteIds.empty()) {
				return;
			}
			QueueGameTask([inviteIds = std::move(a_inviteIds)]() {
				auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
					COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
				if (!ui || !ui->DismissAlert) {
					return;
				}
				for (const auto& inviteId : inviteIds) {
					const auto alertId = std::format("{}{}", kInviteAlertPrefix, inviteId);
					ui->DismissAlert(alertId.c_str());
				}
			});
		}

		void RespondToInvite(const std::string& a_inviteId, bool a_accept)
		{
			std::size_t removed = 0;
			{
				std::scoped_lock lock(g_mutex);
				removed = std::erase_if(g_pendingInvites, [&a_inviteId](const Invite& a_invite) { return a_invite.inviteId == a_inviteId; });
			}
			DismissInviteAlerts({ a_inviteId });
			if (removed == 0) {
				return;
			}
			QueuePacket(Json{
				{ "type", "factionInviteResponse" },
				{ "inviteId", a_inviteId },
				{ "accept", a_accept }
			});
			QueueStateChanged();
		}

		void OnInviteAlertResponse(const char* a_alertId, const char* a_actionId, void*)
		{
			if (!a_alertId || !a_actionId) {
				return;
			}

			const std::string_view alertId{ a_alertId };
			const std::string_view action{ a_actionId };
			if (!alertId.starts_with(kInviteAlertPrefix) || (action != "accept" && action != "decline")) {
				return;
			}

			RespondToInvite(std::string{ alertId.substr(kInviteAlertPrefix.size()) }, action == "accept");
		}

		void ShowInviteAlert(const Invite& a_invite, std::uint32_t a_expiresInSeconds)
		{
			auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
				COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
			if (!ui || !ui->ShowAlert) {
				LogWarningWithLocalPlayerPrefix("Cannot show the faction invite because the alert API is unavailable.");
				return;
			}

			const auto alertId = std::format("{}{}", kInviteAlertPrefix, a_invite.inviteId);
			const auto message = a_invite.factionTag.empty()
				? std::format("{} invited you to join {}", a_invite.fromName, a_invite.factionName)
				: std::format("{} invited you to join {} [{}]", a_invite.fromName, a_invite.factionName, a_invite.factionTag);
			std::array<COMMONWEALTH_ONLINE_UI_API::AlertAction, 2> actions{ {
				{ "accept", "Accept", "Y" },
				{ "decline", "Decline", "N" }
			} };

			COMMONWEALTH_ONLINE_UI_API::AlertRequest request{};
			request.id = alertId.c_str();
			request.category = "Social";
			request.title = "Faction invite";
			request.message = message.c_str();
			request.tone = "invite";
			request.durationMs = a_expiresInSeconds * 1000U;
			request.actions = actions.data();
			request.actionCount = actions.size();
			if (!ui->ShowAlert(&request, &OnInviteAlertResponse, nullptr, nullptr, 0)) {
				LogWarningWithLocalPlayerPrefix("Failed to show the faction invite alert.");
			}
		}

		void PruneExpiredInvitesLocked(std::chrono::steady_clock::time_point a_now)
		{
			std::erase_if(g_pendingInvites, [a_now](const Invite& a_invite) { return a_invite.expiresAt <= a_now; });
		}

		std::optional<std::uint32_t> ReadHealthPercent(const Json& a_member)
		{
			const auto found = a_member.find("health");
			if (found == a_member.end() || !found->is_number_integer()) {
				return std::nullopt;
			}
			const auto value = found->get<std::int64_t>();
			if (value < 0 || value > 100) {
				return std::nullopt;
			}
			return static_cast<std::uint32_t>(value);
		}

		std::optional<Member> ParseMember(const Json& a_member)
		{
			const auto profileId = ReadToken(a_member, "profileId", kMaximumProfileIdBytes);
			const auto name = F4T::SocialNameplate::ResolveShownName(
				ReadName(a_member, "displayName", kMaximumPlayerNameBytes),
				ReadName(a_member, "name", kMaximumPlayerNameBytes));
			if (!profileId || !name) {
				return std::nullopt;
			}

			return Member{
				*profileId,
				ReadPlayerId(a_member, "playerId").value_or(0),
				*name,
				ReadBool(a_member, "online").value_or(false),
				ReadBool(a_member, "leader").value_or(false),
				ReadBool(a_member, "officer").value_or(false),
				ReadHealthPercent(a_member),
				ReadBool(a_member, "downed").value_or(false),
				ReadBool(a_member, "dead").value_or(false)
			};
		}

		std::optional<Faction> ParseFaction(const Json& a_faction)
		{
			const auto factionId = ReadToken(a_faction, "factionId", kMaximumIdBytes);
			const auto name = ReadName(a_faction, "name", kMaximumFactionNameBytes);
			const auto members = a_faction.find("members");
			if (!factionId || !name || members == a_faction.end() || !members->is_array() || members->size() > kMaximumMembers) {
				return std::nullopt;
			}

			Faction faction{
				*factionId,
				*name,
				Payload::SanitizeText(ReadString(a_faction, "tag").value_or(""), kMaximumTagBytes),
				Payload::SanitizeText(ReadString(a_faction, "description").value_or(""), Payload::kMaximumDescriptionBytes),
				Payload::SanitizeText(ReadString(a_faction, "motd").value_or(""), Payload::kMaximumMotdBytes),
				Payload::SanitizeText(ReadString(a_faction, "color").value_or(""), Payload::kMaximumColorBytes),
				ReadPlayerId(a_faction, "leaderPlayerId").value_or(0),
				{}
			};
			for (const auto& memberJson : *members) {
				auto member = ParseMember(memberJson);
				if (!member) {
					return std::nullopt;
				}
				faction.members.push_back(std::move(*member));
			}
			return faction;
		}

		std::optional<Invite> ParseInvite(const Json& a_invite, std::chrono::steady_clock::time_point a_now)
		{
			const auto inviteId = ReadToken(a_invite, "inviteId", kMaximumIdBytes);
			const auto factionName = ReadName(a_invite, "factionName", kMaximumFactionNameBytes);
			const auto fromName = F4T::SocialNameplate::ResolveShownName(
				ReadName(a_invite, "displayName", kMaximumPlayerNameBytes),
				ReadName(a_invite, "fromName", kMaximumPlayerNameBytes));
			const auto factionTag = Payload::SanitizeText(ReadString(a_invite, "factionTag").value_or(""), kMaximumTagBytes);
			const auto expiresInSeconds = ReadExpiresInSeconds(a_invite);
			if (!inviteId || !factionName || !fromName || !expiresInSeconds) {
				return std::nullopt;
			}

			return Invite{ *inviteId, *factionName, factionTag, *fromName, a_now + std::chrono::seconds{ *expiresInSeconds } };
		}

		void HandleStatePacket(const Json& a_packet)
		{
			const auto factionJson = a_packet.find("faction");
			std::optional<Faction> faction;
			if (factionJson != a_packet.end() && !factionJson->is_null()) {
				faction = ParseFaction(*factionJson);
				if (!faction) {
					LogThrottledWarning("faction_state_faction", "Ignoring factionState with a malformed faction.");
					return;
				}
			}

			const auto invitesJson = a_packet.find("pendingInvites");
			if (invitesJson == a_packet.end() || !invitesJson->is_array() || invitesJson->size() > kMaximumPendingInvites) {
				LogThrottledWarning("faction_state_invites", "Ignoring factionState with malformed pendingInvites.");
				return;
			}

			const auto now = std::chrono::steady_clock::now();
			std::vector<Invite> invites;
			for (const auto& inviteJson : *invitesJson) {
				auto invite = ParseInvite(inviteJson, now);
				if (!invite) {
					LogThrottledWarning("faction_state_invite", "Ignoring factionState with a malformed pending invite.");
					return;
				}
				invites.push_back(std::move(*invite));
			}

			std::vector<std::string> dropped;
			{
				std::scoped_lock lock(g_mutex);
				for (const auto& pending : g_pendingInvites) {
					if (std::none_of(invites.begin(), invites.end(), [&pending](const Invite& a_invite) { return a_invite.inviteId == pending.inviteId; })) {
						dropped.push_back(pending.inviteId);
					}
				}
				g_faction = std::move(faction);
				g_pendingInvites = std::move(invites);
			}
			DismissInviteAlerts(std::move(dropped));
			QueueStateChanged();
			QueueKnownSetChanged();
		}

		void HandleInviteReceivedPacket(const Json& a_packet)
		{
			const auto inviteId = ReadToken(a_packet, "inviteId", kMaximumIdBytes);
			const auto factionName = ReadName(a_packet, "factionName", kMaximumFactionNameBytes);
			const auto factionTag = Payload::SanitizeText(ReadString(a_packet, "factionTag").value_or(""), kMaximumTagBytes);
			const auto fromName = F4T::SocialNameplate::ResolveShownName(
				ReadName(a_packet, "displayName", kMaximumPlayerNameBytes),
				ReadName(a_packet, "fromName", kMaximumPlayerNameBytes));
			const auto expiresInSeconds = ReadExpiresInSeconds(a_packet);
			if (!inviteId || !factionName || !fromName || !expiresInSeconds || *expiresInSeconds == 0) {
				LogThrottledWarning("faction_invite_malformed", "Ignoring malformed factionInviteReceived packet.");
				return;
			}

			const auto now = std::chrono::steady_clock::now();
			const Invite invite{
				*inviteId,
				*factionName,
				factionTag,
				*fromName,
				now + std::chrono::seconds{ *expiresInSeconds }
			};
			{
				std::scoped_lock lock(g_mutex);
				std::erase_if(g_pendingInvites, [&invite](const Invite& a_pending) { return a_pending.inviteId == invite.inviteId; });
				PruneExpiredInvitesLocked(now);
				if (g_pendingInvites.size() >= kMaximumPendingInvites) {
					LogThrottledWarning("faction_invite_full", "Dropped a faction invite because the pending invite list is full.");
					return;
				}
				g_pendingInvites.push_back(invite);
			}
			QueueGameTask([invite, expiresInSeconds = *expiresInSeconds]() { ShowInviteAlert(invite, expiresInSeconds); });
			QueueStateChanged();
		}

		void HandleResultPacket(const Json& a_packet)
		{
			const auto op = ReadString(a_packet, "op");
			const auto ok = ReadBool(a_packet, "ok");
			if (!op || !ok || std::find(kResultOps.begin(), kResultOps.end(), std::string_view{ *op }) == kResultOps.end()) {
				LogThrottledWarning("faction_result_malformed", "Ignoring malformed factionResult packet.");
				return;
			}

			const auto reason = Payload::SanitizeText(ReadString(a_packet, "reason").value_or(""), kMaximumReasonBytes);
			{
				std::scoped_lock lock(g_mutex);
				g_lastResult = LastResult{ *op, *ok, reason };
			}
			QueueStateChanged();
		}

		void HandleRosterPacket(const Json& a_packet)
		{
			const auto players = a_packet.find("players");
			if (players == a_packet.end() || !players->is_array() || players->size() > kMaximumRosterEntries) {
				LogThrottledWarning("player_roster_malformed", "Ignoring malformed playerRoster packet.");
				return;
			}

			std::vector<RosterEntry> roster;
			for (const auto& player : *players) {
				const auto playerId = ReadPlayerId(player, "playerId");
				const auto shown = F4T::SocialNameplate::ResolveShownName(
					ReadName(player, "displayName", kMaximumPlayerNameBytes),
					ReadName(player, "name", kMaximumPlayerNameBytes));
				if (!playerId || *playerId == 0 || !shown) {
					LogThrottledWarning("player_roster_entry", "Ignoring playerRoster packet with an invalid entry.");
					return;
				}
				roster.push_back(RosterEntry{
					*playerId,
					*shown,
					Payload::SanitizeText(ReadString(player, "tag").value_or(""), kMaximumTagBytes) });
			}

			{
				std::scoped_lock lock(g_mutex);
				g_roster = std::move(roster);
			}
			QueueStateChanged();
		}

		void HandleFactionListPacket(const Json& a_packet)
		{
			auto factions = List::ParseFactionList(a_packet);
			if (!factions) {
				LogThrottledWarning("faction_list_malformed", "Ignoring malformed factionList packet.");
				return;
			}
			{
				std::scoped_lock lock(g_mutex);
				g_factionList = std::move(*factions);
				g_factionsLoaded = true;
			}
			QueueStateChanged();
		}

		bool TakeThrottleSlot(std::optional<std::chrono::steady_clock::time_point>& a_last, std::chrono::steady_clock::duration a_interval)
		{
			const auto now = std::chrono::steady_clock::now();
			std::scoped_lock lock(g_mutex);
			if (a_last && now - *a_last < a_interval) {
				return false;
			}
			a_last = now;
			return true;
		}
	}

	void NoteWelcome(const Json& a_welcome)
	{
		const auto capabilities = a_welcome.find("capabilities");
		const bool supported = capabilities != a_welcome.end() && capabilities->is_array() &&
			std::any_of(capabilities->begin(), capabilities->end(), [](const Json& a_capability) {
				return a_capability.is_string() && a_capability.get<std::string>() == kFactionsCapability;
			});
		bool firstSupport = false;
		{
			std::scoped_lock lock(g_mutex);
			firstSupport = supported && !g_serverSupportsFactions;
			g_serverSupportsFactions = supported;
		}
		if (firstSupport) {
			QueuePacket(Json{ { "type", "factionList" } });
		}
		F4T::Friends::NoteWelcome(a_welcome);
		QueueStateChanged();
}

	bool TryHandlePacket(std::string_view a_type, const Json& a_packet)
	{
		if (a_type == "factionState") {
			HandleStatePacket(a_packet);
		} else if (a_type == "factionInviteReceived") {
			HandleInviteReceivedPacket(a_packet);
		} else if (a_type == "factionResult") {
			HandleResultPacket(a_packet);
		} else if (a_type == "playerRoster") {
			HandleRosterPacket(a_packet);
		} else if (a_type == "factionList") {
			HandleFactionListPacket(a_packet);
		} else {
			return false;
		}
		return true;
	}

	void Reset()
	{
		std::vector<std::string> inviteIds;
		{
			std::scoped_lock lock(g_mutex);
			for (const auto& invite : g_pendingInvites) {
				inviteIds.push_back(invite.inviteId);
			}
			g_serverSupportsFactions = false;
			g_faction.reset();
			g_pendingInvites.clear();
			g_roster.clear();
			g_factionList.clear();
			g_factionsLoaded = false;
			g_lastResult.reset();
			g_lastRosterRefresh.reset();
			g_lastFactionListRequest.reset();
		}
		DismissInviteAlerts(std::move(inviteIds));
		QueueStateChanged();
	}

	std::optional<std::string> MemberName(std::uint32_t a_playerId)
	{
		if (a_playerId == 0) {
			return std::nullopt;
		}
		std::scoped_lock lock(g_mutex);
		if (!g_faction) {
			return std::nullopt;
		}
		const auto member = std::find_if(g_faction->members.begin(), g_faction->members.end(), [a_playerId](const Member& a_member) { return a_member.playerId == a_playerId; });
		if (member == g_faction->members.end()) {
			return std::nullopt;
		}
		return member->name;
	}

	bool InFaction()
	{
		std::scoped_lock lock(g_mutex);
		return g_faction.has_value();
	}

	std::string BuildStateJson()
	{
		const bool connected = F4T::Networking::IsConnectedToServer();
		const auto localPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
		const auto now = std::chrono::steady_clock::now();
		std::scoped_lock lock(g_mutex);
		PruneExpiredInvitesLocked(now);

		Json state{
			{ "enabled", connected && g_serverSupportsFactions },
			{ "localPlayerId", PlayerIdJson(localPlayerId.value_or(0)) },
			{ "faction", nullptr },
			{ "pendingInvites", Json::array() },
			{ "roster", Json::array() },
			{ "factions", List::ToJson(g_factionList) },
			{ "factionsLoaded", g_factionsLoaded },
			{ "lastResult", nullptr }
		};

		if (g_faction) {
			auto members = Json::array();
			for (const auto& member : g_faction->members) {
				auto memberJson = Json{
					{ "profileId", member.profileId },
					{ "playerId", PlayerIdJson(member.playerId) },
					{ "name", member.name },
					{ "online", member.online },
					{ "leader", member.leader },
					{ "officer", member.officer }
				};
				if (member.health) {
					memberJson["health"] = *member.health;
					memberJson["downed"] = member.downed;
					memberJson["dead"] = member.dead;
				}
				members.push_back(std::move(memberJson));
			}
			state["faction"] = Json{
				{ "factionId", g_faction->factionId },
				{ "name", g_faction->name },
				{ "tag", g_faction->tag },
				{ "description", g_faction->description },
				{ "motd", g_faction->motd },
				{ "color", g_faction->color },
				{ "leaderPlayerId", PlayerIdJson(g_faction->leaderPlayerId) },
				{ "members", std::move(members) }
			};
		}

		for (const auto& invite : g_pendingInvites) {
			const auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(invite.expiresAt - now).count();
			if (remainingMs > 0) {
				state["pendingInvites"].push_back(Json{
					{ "inviteId", invite.inviteId },
					{ "factionName", invite.factionName },
					{ "factionTag", invite.factionTag },
					{ "fromName", invite.fromName },
					{ "expiresInSeconds", (remainingMs + 999) / 1000 }
				});
			}
		}

		for (const auto& entry : g_roster) {
			if (entry.playerId != localPlayerId.value_or(0)) {
				state["roster"].push_back(Json{ { "playerId", entry.playerId }, { "name", entry.name }, { "tag", entry.tag } });
			}
		}

		if (g_lastResult) {
			state["lastResult"] = Json{
				{ "op", g_lastResult->op },
				{ "ok", g_lastResult->ok },
				{ "reason", g_lastResult->reason.empty() ? Json(nullptr) : Json(g_lastResult->reason) }
			};
		}

		state["friends"] = Json::parse(F4T::Friends::BuildStateJson(), nullptr, false);
		return state.dump();
	}

	void HandleAction(std::string_view a_actionJson)
	{
		if (a_actionJson.empty() || a_actionJson.size() > kMaximumActionJsonBytes) {
			LogThrottledWarning("faction_action_size", "Ignoring a faction action outside the size limit.");
			return;
		}

		const auto action = Json::parse(std::string{ a_actionJson }, nullptr, false);
		if (!action.is_object()) {
			LogThrottledWarning("faction_action_shape", "Ignoring a faction action that is not a JSON object.");
			return;
		}

		const auto op = ReadString(action, "op").value_or("");
		if (op.starts_with("friend")) { F4T::Friends::HandleAction(a_actionJson); return; }
		Json packet;
		if (op == "create") {
			const auto name = ReadString(action, "name");
			if (!Payload::IsBoundedText(name, kMaximumFactionNameBytes)) {
				LogThrottledWarning("faction_action_malformed", "Ignoring a malformed faction action.");
				return;
			}
			packet = Json{ { "type", "factionCreate" }, { "name", *name } };
			if (const auto tag = ReadString(action, "tag"); tag && !tag->empty()) {
				if (tag->size() > kMaximumTagBytes) {
					LogThrottledWarning("faction_action_malformed", "Ignoring a malformed faction action.");
					return;
				}
				packet["tag"] = *tag;
			}
		} else if (op == "invite") {
			const auto target = ReadPlayerId(action, "targetPlayerId");
			if (!target || *target == 0) {
				LogThrottledWarning("faction_action_malformed", "Ignoring a malformed faction action.");
				return;
			}
			packet = Json{ { "type", "factionInvite" }, { "targetPlayerId", *target } };
		} else if (op == "inviteResponse") {
			const auto inviteId = ReadToken(action, "inviteId", kMaximumIdBytes);
			const auto accept = ReadBool(action, "accept");
			if (!inviteId || !accept) {
				LogThrottledWarning("faction_action_malformed", "Ignoring a malformed faction action.");
				return;
			}
			RespondToInvite(*inviteId, *accept);
			return;
		} else if (op == "leave") {
			packet = Json{ { "type", "factionLeave" } };
		} else if (op == "kick") {
			const auto target = ReadToken(action, "targetProfileId", kMaximumProfileIdBytes);
			if (!target) {
				LogThrottledWarning("faction_action_malformed", "Ignoring a malformed faction action.");
				return;
			}
			packet = Json{ { "type", "factionKick" }, { "targetProfileId", *target } };
		} else if (op == "promote") {
			const auto target = ReadToken(action, "targetProfileId", kMaximumProfileIdBytes);
			if (!target) {
				LogThrottledWarning("faction_action_malformed", "Ignoring a malformed faction action.");
				return;
			}
			packet = Json{ { "type", "factionPromote" }, { "targetProfileId", *target } };
		} else if (op == "setRank") {
			const auto target = ReadToken(action, "targetProfileId", kMaximumProfileIdBytes);
			const auto rank = ReadToken(action, "rank", kMaximumIdBytes);
			if (!target || !rank || (*rank != "officer" && *rank != "member")) {
				LogThrottledWarning("faction_action_malformed", "Ignoring a malformed faction action.");
				return;
			}
			packet = Json{ { "type", "factionSetRank" }, { "targetProfileId", *target }, { "rank", *rank } };
		} else if (op == "setProfile") {
			const auto description = Payload::ReadProfileDescription(action);
			const auto color = Payload::ReadProfileColor(action);
			const auto motd = Payload::ReadProfileMotd(action);
			if (!description || !color || !motd) {
				LogThrottledWarning("faction_action_malformed", "Ignoring a malformed faction action.");
				return;
			}
			packet = Json{ { "type", "factionSetProfile" }, { "description", *description }, { "color", *color } };
			if (motd->has_value()) {
				packet["motd"] = **motd;
			}
		} else if (op == "disband") {
			packet = Json{ { "type", "factionDisband" } };
		} else if (op == "refresh") {
			if (!TakeThrottleSlot(g_lastRosterRefresh, kRosterRefreshInterval)) {
				return;
			}
			QueuePacket(Json{ { "type", "playerRosterRequest" } });
			packet = Json{ { "type", "factionList" } };
		} else if (op == "listFactions") {
			if (!TakeThrottleSlot(g_lastFactionListRequest, kFactionListRequestInterval)) {
				return;
			}
			packet = Json{ { "type", "factionList" } };
		} else {
			LogThrottledWarning("faction_action_op", "Ignoring a faction action with an unknown op.");
			return;
		}

		QueuePacket(std::move(packet));
	}
}

#include "pch.h"

#include "F4TQuestUI.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TDebugLogging.h"
#include "F4TFactions.h"
#include "F4TFireteamClient.h"
#include "F4TNetworking.h"
#include "F4TQuestInbound.h"
#include "F4TQuestNotices.h"
#include "F4TQuestSync.h"
#include "F4TRemotePlayerCapacity.h"
#include "F4TRemotePlayerState.h"
#include "F4TSocialNameplate.h"
#include "F4TSessionCharacterSetupFlow.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace F4T::QuestUI
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr auto        kCheckInterval = std::chrono::milliseconds{ 250 };
		constexpr std::size_t kMaximumActionBytes = 1024;
		constexpr std::size_t kMaximumFriendsPerRow = 16;
		constexpr std::size_t kMaximumInvitable = F4T::RemotePlayerCapacity::MaximumSessionPlayers;

		std::atomic<bool>                       g_refreshRequested{ false };
		std::string                             g_lastSignature;
		std::chrono::steady_clock::time_point   g_nextCheck{};

		COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI11* Api()
		{
			static COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI11* api = nullptr;
			if (!api) {
				api = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
					COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI11>();
			}
			return api;
		}

		bool IsEnabled()
		{
			return F4T::Networking::IsConnectedToServer() && F4T::QuestInbound::IsActive() &&
				F4T::SessionCharacterSetupFlow::IsPodExitSignal();
		}

		std::unordered_map<std::uint32_t, std::string> RosterNames()
		{
			std::unordered_map<std::uint32_t, std::string> names;
			for (const auto& remote : F4T::RemotePlayerState::GetRemotePlayerSnapshot()) {
				const auto shown = F4T::SocialNameplate::ResolveDisplayName(remote.displayName, remote.characterName);
				if (remote.playerId != 0 && !shown.empty()) {
					names.emplace(remote.playerId, shown);
				}
			}
			return names;
		}

		std::string FriendName(const std::unordered_map<std::uint32_t, std::string>& a_names, std::uint32_t a_playerId)
		{
			const auto found = a_names.find(a_playerId);
			return found != a_names.end() ? found->second : "Player " + std::to_string(a_playerId);
		}

		const char* ScopeOf(const F4T::QuestSync::ViewQuest& a_row)
		{
			if (a_row.solo) {
				return "solo";
			}
			if (a_row.held) {
				return "held";
			}
			if (a_row.fireteam) {
				return "fireteam";
			}
			return a_row.party ? "party" : "shared";
		}

		std::optional<F4T::QuestLogic::Scope> ParseScope(std::string_view a_name)
		{
			if (a_name == "shared") {
				return F4T::QuestLogic::Scope::Shared;
			}
			if (a_name == "solo") {
				return F4T::QuestLogic::Scope::Solo;
			}
			if (a_name == "party") {
				return F4T::QuestLogic::Scope::Party;
			}
			if (a_name == "fireteam") {
				return F4T::QuestLogic::Scope::Fireteam;
			}
			return std::nullopt;
		}

		void WriteError(char* a_errorBuf, std::size_t a_errorBufSize, std::string_view a_error)
		{
			if (!a_errorBuf || a_errorBufSize == 0) {
				return;
			}
			const auto length = (std::min)(a_error.size(), a_errorBufSize - 1);
			std::memcpy(a_errorBuf, a_error.data(), length);
			a_errorBuf[length] = '\0';
		}

		const char* EventKindName(F4T::FireteamClient::EventKind a_kind)
		{
			switch (a_kind) {
				case F4T::FireteamClient::EventKind::Refusal:
					return "refusal";
				case F4T::FireteamClient::EventKind::Declined:
					return "declined";
				case F4T::FireteamClient::EventKind::Invited:
					return "invited";
				case F4T::FireteamClient::EventKind::Removed:
					return "removed";
				case F4T::FireteamClient::EventKind::None:
					break;
			}
			return "none";
		}

		F4T::FireteamClient::Context FireteamContext()
		{
			return F4T::FireteamClient::Context{
				F4T::QuestSync::ViewFireteamSupported(),
				F4T::Networking::IsConnectedToServer(),
				F4T::RemotePlayerState::GetAssignedPlayerId()
			};
		}

		std::uint32_t TargetField(const Json& a_action)
		{
			const auto found = a_action.find("target");
			if (found == a_action.end() || !found->is_number_unsigned()) {
				return 0;
			}
			const auto value = found->get<std::uint64_t>();
			return value <= (std::numeric_limits<std::uint32_t>::max)() ? static_cast<std::uint32_t>(value) : 0;
		}

		Json FireteamJson()
		{
			const auto names = RosterNames();
			const auto self = F4T::RemotePlayerState::GetAssignedPlayerId();
			const auto view = F4T::FireteamClient::Current(F4T::FireteamClient::MonotonicMilliseconds());

			Json team = nullptr;
			if (!view.team.fireteamId.empty()) {
				auto members = Json::array();
				for (const auto& member : view.team.members) {
					members.push_back(Json{ { "playerId", member.playerId }, { "name", member.name.empty() ? FriendName(names, member.playerId) : member.name } });
				}
				team = Json{ { "leaderPlayerId", view.team.leaderPlayerId }, { "members", members } };
			}

			auto invites = Json::array();
			for (const auto& invite : view.invites) {
				invites.push_back(Json{ { "inviteId", invite.inviteId }, { "fromPlayerId", invite.fromPlayerId }, { "fromName", invite.fromName } });
			}

			auto invitable = Json::array();
			for (const auto& remote : F4T::RemotePlayerState::GetRemotePlayerSnapshot()) {
				if (invitable.size() >= kMaximumInvitable) {
					break;
				}
				const bool teammate = std::any_of(view.team.members.begin(), view.team.members.end(), [&remote](const F4T::FireteamClient::Member& a_member) {
					return a_member.playerId == remote.playerId;
				});
				if (remote.playerId == 0 || teammate || (self && *self == remote.playerId)) {
					continue;
				}
				invitable.push_back(Json{ { "playerId", remote.playerId }, { "name", FriendName(names, remote.playerId) } });
			}

			return Json{
				{ "self", self.value_or(0) },
				{ "team", team },
				{ "invites", invites },
				{ "invitable", invitable },
				{ "event", Json{ { "kind", EventKindName(view.event.kind) }, { "reason", view.event.reason }, { "playerId", view.event.playerId }, { "name", FriendName(names, view.event.playerId) } } }
			};
		}

		void SendFireteam(const std::optional<Json>& a_packet)
		{
			if (a_packet && !F4T::QuestInbound::SendFireteamPacket(*a_packet)) {
				F4T::FireteamClient::ApplyEvent(F4T::QuestWire::FireteamEventIn{ std::string(F4T::FireteamClient::kReasonNotConnected) });
			}
			RequestRefresh();
		}

		std::string BuildSignature()
		{
			std::string signature = IsEnabled() ? "enabled" : "disabled";
			signature += F4T::SessionCharacterSetupFlow::IsPodExitSignal() ? ":ready:" : ":tutorial:";
			signature += F4T::Factions::InFaction() ? ":faction:" : ":solo:";
			signature += F4T::QuestSync::ViewMode();
			const auto names = RosterNames();
			for (const auto& row : F4T::QuestSync::ViewQuests()) {
				signature += '|';
				signature += row.quest;
				signature += '/';
				signature += std::to_string(row.stage);
				signature += row.completed ? 'c' : '.';
				signature += row.held ? 'h' : '.';
				signature += row.solo ? 's' : '.';
				signature += row.party ? 'p' : '.';
				signature += row.fireteam ? 'f' : '.';
				for (const auto& friendStage : row.friends) {
					signature += '@';
					signature += std::to_string(friendStage.playerId);
					signature += ':';
					signature += std::to_string(friendStage.stage);
					signature += friendStage.completed ? 'c' : '.';
					signature += FriendName(names, friendStage.playerId);
				}
			}
			signature += F4T::QuestSync::ViewFireteamSupported() ? 'F' : '.';
			signature += FireteamJson().dump();
			signature += '#';
			signature += std::to_string(F4T::QuestNotices::Revision());
			return signature;
		}

		void NotifyWindow()
		{
			auto* api = Api();
			if (api && api->NotifyQuestStateChanged) {
				api->NotifyQuestStateChanged();
			}
		}

		void NotifyIfChanged(bool a_force)
		{
			const auto signature = BuildSignature();
			if (!a_force && signature == g_lastSignature) {
				return;
			}
			g_lastSignature = signature;
			NotifyWindow();
		}

		bool GetStateJsonCallback(
			char*       a_jsonOut,
			std::size_t a_jsonOutSize,
			char*       a_errorBuf,
			std::size_t a_errorBufSize)
		{
			try {
				return BuildStateJson(a_jsonOut, a_jsonOutSize, a_errorBuf, a_errorBufSize);
			} catch (...) {
				WriteError(a_errorBuf, a_errorBufSize, "quest state JSON failed to build");
				return false;
			}
		}

		void SendActionCallback(const char* a_actionJson)
		{
			if (!a_actionJson) {
				return;
			}
			try {
				HandleAction(a_actionJson);
			} catch (...) {
				F4T::DebugLogging::Log("QuestWindow", "dropped an action after an internal error");
			}
		}

		const COMMONWEALTH_ONLINE_UI_API::QuestCallbacks kQuestCallbacks{
			GetStateJsonCallback,
			SendActionCallback
		};
	}

	bool Attach()
	{
		static bool attached = false;
		if (attached) {
			return true;
		}
		auto* api = Api();
		if (!api || !api->SetQuestCallbacks) {
			return false;
		}
		api->SetQuestCallbacks(&kQuestCallbacks);
		attached = true;
		F4T::DebugLogging::Log("QuestWindow", "registered the V11 quest callbacks");
		return true;
	}

	bool BuildStateJson(
		char*       a_jsonOut,
		std::size_t a_jsonOutSize,
		char*       a_errorBuf,
		std::size_t a_errorBufSize)
	{
		auto notices = Json::array();
		for (const auto& notice : F4T::QuestNotices::Snapshot()) {
			notices.push_back(Json{ { "text", notice.text }, { "time", notice.time } });
		}
		const bool questSyncLive =
			F4T::Networking::IsConnectedToServer() && F4T::QuestInbound::IsActive();
		Json state{
			{ "enabled", IsEnabled() },
			{ "tutorial", questSyncLive && !F4T::SessionCharacterSetupFlow::IsPodExitSignal() },
			{ "mode", std::string(F4T::QuestSync::ViewMode()) },
			{ "inFaction", F4T::Factions::InFaction() },
			{ "fireteamSupported", F4T::QuestSync::ViewFireteamSupported() },
			{ "quests", Json::array() },
			{ "notices", notices },
			{ "fireteam", FireteamJson() }
		};

		const auto names = RosterNames();
		for (const auto& row : F4T::QuestSync::ViewQuests()) {
			auto name = F4T::QuestSync::ViewName(row.quest);
			auto friends = Json::array();
			for (const auto& friendStage : row.friends) {
				if (friends.size() >= kMaximumFriendsPerRow) {
					break;
				}
				friends.push_back(Json{
					{ "id", friendStage.playerId },
					{ "name", FriendName(names, friendStage.playerId) },
					{ "stage", friendStage.stage }
				});
			}
			state["quests"].push_back(Json{
				{ "quest", row.quest },
				{ "name", name.empty() ? row.quest : name },
				{ "stage", row.stage },
				{ "scope", ScopeOf(row) },
				{ "category", "main" },
				{ "friends", friends }
			});
		}

		const auto nextStep = F4T::QuestSync::NextStoryQuest();
		if (!nextStep.empty()) {
			const auto nextName = F4T::QuestSync::ViewName(nextStep);
			state["quests"].push_back(Json{
				{ "quest", nextStep },
				{ "name", nextName.empty() ? nextStep : nextName },
				{ "stage", 0 },
				{ "scope", "shared" },
				{ "category", "main" },
				{ "friends", Json::array() },
				{ "nextStep", true }
			});
		}

		const auto text = state.dump();
		if (!a_jsonOut || a_jsonOutSize == 0) {
			WriteError(a_errorBuf, a_errorBufSize, "quest state buffer is missing");
			return false;
		}
		if (text.size() >= a_jsonOutSize) {
			WriteError(a_errorBuf, a_errorBufSize, "quest state JSON exceeds the caller buffer");
			return false;
		}
		std::memcpy(a_jsonOut, text.data(), text.size());
		a_jsonOut[text.size()] = '\0';
		return true;
	}

	void HandleAction(std::string_view a_actionJson)
	{
		if (a_actionJson.empty() || a_actionJson.size() > kMaximumActionBytes) {
			F4T::DebugLogging::Log("QuestWindow", "ignored an action outside the size limit");
			return;
		}

		const auto action = Json::parse(std::string(a_actionJson), nullptr, false);
		if (!action.is_object()) {
			F4T::DebugLogging::Log("QuestWindow", "ignored an action that is not a JSON object");
			return;
		}

		const auto name = action.value("action", "");
		if (name == "refresh") {
			NotifyIfChanged(true);
			return;
		}
		if (name == "fireteamLeave") {
			SendFireteam(F4T::FireteamClient::TryLeave(FireteamContext()));
			return;
		}
		if (name == "fireteamInvite") {
			SendFireteam(F4T::FireteamClient::TryInvite(TargetField(action), FireteamContext()));
			return;
		}
		if (name == "fireteamKick") {
			SendFireteam(F4T::FireteamClient::TryKick(TargetField(action), FireteamContext()));
			return;
		}
		if (name == "fireteamRespond") {
			const auto inviteId = action.find("inviteId");
			const auto accept = action.find("accept");
			if (inviteId == action.end() || !inviteId->is_string() || accept == action.end() || !accept->is_boolean()) {
				F4T::DebugLogging::Log("QuestWindow", "ignored a fireteamRespond action without an invite and accept");
				return;
			}
			SendFireteam(F4T::FireteamClient::TryRespond(
				inviteId->get_ref<const std::string&>(), accept->get<bool>(), FireteamContext(), F4T::FireteamClient::MonotonicMilliseconds()));
			return;
		}
		if (name == "setScope") {
			const auto quest = action.find("quest");
			const auto scope = action.find("scope");
			if (quest == action.end() || !quest->is_string() || scope == action.end() || !scope->is_string()) {
				F4T::DebugLogging::Log("QuestWindow", "ignored a setScope action without a quest and scope");
				return;
			}
			const auto& scopeName = scope->get_ref<const std::string&>();
			const auto requested = ParseScope(scopeName);
			if (!requested) {
				F4T::DebugLogging::Log("QuestWindow", "ignored setScope for scope={}", scopeName);
				return;
			}
			if (*requested == F4T::QuestLogic::Scope::Fireteam && !F4T::QuestSync::ViewFireteamSupported()) {
				F4T::DebugLogging::Log("QuestWindow", "ignored setScope fireteam: the server has no fireteam support");
				return;
			}
			F4T::QuestSync::RequestScope(quest->get_ref<const std::string&>(), *requested);
			return;
		}
		F4T::DebugLogging::Log(
			"QuestWindow",
			"ignored action={}: no quest packet handles it",
			name);
	}

	void RequestRefresh()
	{
		g_refreshRequested.store(true, std::memory_order_release);
	}

	void Tick()
	{
		const auto now = std::chrono::steady_clock::now();
		const bool requested = g_refreshRequested.exchange(false, std::memory_order_acq_rel);
		if (!requested && now < g_nextCheck) {
			return;
		}
		g_nextCheck = now + kCheckInterval;
		NotifyIfChanged(requested);
	}
}

#include "pch.h"

#include "F4TInteractWheel.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TDownedEngine.h"
#include "F4TDebugLogging.h"
#include "F4TFactionInviteOffer.h"
#include "F4TFactions.h"
#include "F4TFriends.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TProxyActorController.h"
#include "F4TRemotePlayerState.h"
#include "F4TSocialNameplate.h"
#include "F4TUIClient.h"
#include "F4TVoiceRuntime.h"

#include "RE/U/UI.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::InteractWheel
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr std::string_view kLogComponent{ "InteractWheel" };
		constexpr std::size_t kMaximumActionBytes = 1024;
		constexpr std::uint32_t kRangeUnits = static_cast<std::uint32_t>(F4T::FactionInviteOffer::kActivationRange);
		constexpr std::uint32_t kTicksBeforeViewCheck = 30;

		struct FactionView
		{
			bool localCanInvite{ false };
			std::uint32_t localPlayerId{ 0 };
			std::string factionName;
			std::vector<std::uint32_t> memberPlayerIds;
		};

		bool g_open = false;
		std::uint32_t g_targetPlayerId = 0;
		std::string g_targetName;
		std::string g_lastStateJson;
		std::uint32_t g_ticksSinceOpen = 0;

		bool VanillaMenuOpen()
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui) return false;
			const char* menus[]{ "PauseMenu", "DialogueMenu", "ContainerMenu", "BarterMenu", "TerminalMenu", "LooksMenu", "WorkshopMenu", "PipboyMenu", "LockpickingMenu" };
			return std::any_of(std::begin(menus), std::end(menus), [ui](const char* a_name) { return ui->GetMenuOpen(a_name); });
		}

		bool CoWindowOpen()
		{
			if (auto* menu = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>()) {
				if (menu->IsMainMenuVisible && menu->IsMainMenuVisible()) return true;
				if (menu->IsBrowserVisible && menu->IsBrowserVisible()) return true;
			}
			if (auto* social = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI3>()) {
				if (social->IsFactionWindowVisible && social->IsFactionWindowVisible()) return true;
			}
			if (auto* quests = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI11>()) {
				if (quests->IsQuestWindowVisible && quests->IsQuestWindowVisible()) return true;
			}
			if (auto* wheel = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI16>()) {
				if (wheel->IsChatVisible && wheel->IsChatVisible()) return true;
				if (wheel->IsCharacterSelectVisible && wheel->IsCharacterSelectVisible()) return true;
			}
			return false;
		}

		bool MenuBlocksWheel()
		{
			return VanillaMenuOpen() || CoWindowOpen();
		}

		bool IsDowned(std::uint32_t a_playerId)
		{
			for (const auto& remote : F4T::RemotePlayerState::GetRemotePlayerSnapshot()) {
				if (remote.playerId != a_playerId) continue;
				return remote.vitals && (remote.vitals->isDowned || remote.vitals->isDead);
			}
			return false;
		}

		std::string RemoteName(std::uint32_t a_playerId)
		{
			for (const auto& remote : F4T::RemotePlayerState::GetRemotePlayerSnapshot()) {
				if (remote.playerId != a_playerId) continue;
				if (auto shown = F4T::SocialNameplate::ResolveDisplayName(remote.displayName, remote.characterName); !shown.empty()) {
					return shown;
				}
			}
			return "Unknown player";
		}

		std::optional<FactionView> ReadFactionView()
		{
			if (!F4T::Factions::InFaction()) return std::nullopt;
			const auto state = Json::parse(F4T::Factions::BuildStateJson(), nullptr, false);
			if (!state.is_object()) return std::nullopt;
			const auto faction = state.find("faction");
			if (faction == state.end() || !faction->is_object()) return std::nullopt;
			FactionView view;
			view.localPlayerId = F4T::Networking::Detail::ReadUint32(state, "localPlayerId").value_or(0);
			view.factionName = F4T::Networking::Detail::ReadString(*faction, "name").value_or("");
			if (const auto members = faction->find("members"); members != faction->end() && members->is_array()) {
				for (const auto& member : *members) {
					const auto playerId = F4T::Networking::Detail::ReadUint32(member, "playerId");
					if (!playerId) continue;
					view.memberPlayerIds.push_back(*playerId);
					if (*playerId == view.localPlayerId) view.localCanInvite = member.value("leader", false) || member.value("officer", false);
				}
			}
			return view;
		}

		bool TargetInFaction(std::uint32_t a_playerId)
		{
			const auto view = ReadFactionView();
			return view && std::ranges::contains(view->memberPlayerIds, a_playerId);
		}

		std::optional<std::uint32_t> FindTarget()
		{
			auto* player = F4T::Downed::Engine::LocalPlayer();
			if (!player) return std::nullopt;
			const auto position = player->GetPosition();
			const auto heading = player->GetHeading();
			std::optional<std::uint32_t> target;
			float nearest = std::numeric_limits<float>::max();
			for (const auto& proxy : F4T::ProxyActorController::GetActiveProxyActors()) {
				if (!proxy.actor || proxy.remotePlayerId == 0) continue;
				const auto proxyPosition = proxy.actor->GetPosition();
				const auto distance = F4T::Downed::Engine::DistanceBetween(position, proxyPosition);
				const auto cosine = F4T::Downed::Engine::FacingCosine(position, heading, proxyPosition);
				if (!F4T::FactionInviteOffer::IsActivationAim(distance, cosine) || IsDowned(proxy.remotePlayerId) || distance >= nearest) {
					continue;
				}
				nearest = distance;
				target = proxy.remotePlayerId;
			}
			return target;
		}

		Json Slice(std::string_view a_id, std::string_view a_label, bool a_disabled = false)
		{
			Json slice{ { "id", a_id }, { "label", a_label } };
			if (a_disabled) slice["disabled"] = true;
			return slice;
		}

		Json BuildSlices()
		{
			Json slices = Json::array();
			if (F4T::Friends::ServerSupports()) {
				switch (F4T::Friends::RelationshipWith(g_targetPlayerId)) {
				case F4T::Friends::Relationship::friends:
					slices.push_back(Slice("removeFriend", "Remove Friend"));
					break;
				case F4T::Friends::Relationship::incoming:
					slices.push_back(Slice("acceptFriend", "Accept Friend Request"));
					break;
				case F4T::Friends::Relationship::outgoing:
					slices.push_back(Slice("requestSent", "Friend Request Sent", true));
					break;
				default:
					slices.push_back(Slice("addFriend", "Add Friend"));
					break;
				}
			}
			if (const auto faction = ReadFactionView(); faction && faction->localCanInvite && !TargetInFaction(g_targetPlayerId)) {
				slices.push_back(Slice("invite", "Invite to Faction"));
			}
			if (F4T::VoiceRuntime::IsPlayerMuted(g_targetPlayerId)) {
				slices.push_back(Slice("unmute", "Unmute Voice"));
			} else {
				slices.push_back(Slice("mute", "Mute Voice"));
			}
			slices.push_back(Slice("close", "Close"));
			return slices;
		}

		void PushState()
		{
			if (!g_open) return;
			auto json = BuildStateJson();
			if (json == g_lastStateJson) return;
			g_lastStateJson = std::move(json);
			F4T::UIClient::PushInteractWheelState(g_lastStateJson.c_str());
		}

		void CloseWheel(std::string_view a_reason)
		{
			if (!g_open) return;
			F4T::DebugLogging::Log(kLogComponent, "closing: {}", a_reason);
			g_open = false;
			g_targetPlayerId = 0;
			g_targetName.clear();
			g_lastStateJson.clear();
			g_ticksSinceOpen = 0;
			F4T::UIClient::CloseInteractWheel();
		}

		bool TargetStillValid()
		{
			if (!F4T::Networking::IsConnectedToServer()) return false;
			if (!F4T::Downed::Engine::CanTouchWorld()) return false;
			if (MenuBlocksWheel()) return false;
			auto* player = F4T::Downed::Engine::LocalPlayer();
			if (!player) return false;
			auto* proxy = F4T::ProxyActorController::TryGetProxyActorForRemotePlayerId(g_targetPlayerId);
			if (!proxy) return false;
			const auto distance = F4T::Downed::Engine::DistanceBetween(player->GetPosition(), proxy->GetPosition());
			if (distance > F4T::FactionInviteOffer::kActivationRange) return false;
			return !IsDowned(g_targetPlayerId);
		}
	}

	void Tick()
	{
		if (!g_open) return;
		if (!TargetStillValid()) {
			CloseWheel("TargetLost");
			return;
		}
		if (++g_ticksSinceOpen > kTicksBeforeViewCheck && !F4T::UIClient::IsInteractWheelVisible()) {
			CloseWheel("ViewGone");
			return;
		}
		const auto name = RemoteName(g_targetPlayerId);
		if (name != g_targetName) {
			g_targetName = name;
			g_lastStateJson.clear();
		}
		PushState();
	}

	void OpenForActivation()
	{
		if (g_open || !F4T::Downed::Engine::CanTouchWorld() || MenuBlocksWheel()) return;
		if (!F4T::Networking::IsConnectedToServer()) return;
		const auto target = FindTarget();
		if (!target) return;
		g_targetPlayerId = *target;
		g_targetName = RemoteName(*target);
		g_open = true;
		g_ticksSinceOpen = 0;
		g_lastStateJson.clear();
		F4T::DebugLogging::Log(kLogComponent, "opening for player {}", g_targetPlayerId);
		F4T::UIClient::OpenInteractWheel();
		PushState();
	}

	void Close(std::string_view a_reason)
	{
		CloseWheel(a_reason);
	}

	bool IsOpen()
	{
		return g_open;
	}

	std::string BuildStateJson()
	{
		Json state{
			{ "title", g_targetName.empty() ? std::string{ "Unknown player" } : g_targetName },
			{ "rangeUnits", kRangeUnits },
			{ "slices", BuildSlices() }
		};
		return state.dump();
	}

	void HandleAction(std::string_view a_actionJson)
	{
		if (a_actionJson.size() > kMaximumActionBytes) return;
		const auto parsed = Json::parse(a_actionJson, nullptr, false);
		if (!parsed.is_object()) return;
		const auto op = parsed.value("op", std::string{});
		if (!g_open) return;
		if (op == "close") {
			CloseWheel("WheelClose");
			return;
		}
		const auto target = g_targetPlayerId;
		if (op == "addFriend" && target != 0) {
			F4T::Friends::RequestFriend(target);
		} else if (op == "acceptFriend" && target != 0) {
			F4T::Friends::RespondToRequest(F4T::Friends::IncomingRequestId(target), true);
		} else if (op == "removeFriend" && target != 0) {
			F4T::Friends::RemoveFriend(F4T::Friends::FriendTokenFor(target));
		} else if (op == "invite" && target != 0) {
			F4T::Factions::HandleAction(Json{ { "op", "invite" }, { "targetPlayerId", target } }.dump());
		} else if ((op == "mute" || op == "unmute") && target != 0) {
			F4T::VoiceRuntime::SubmitAction(Json{ { "op", "mutePlayer" }, { "playerId", target }, { "muted", op == "mute" } }.dump());
		} else if (op != "requestSent") {
			return;
		}
		CloseWheel(op);
	}
}

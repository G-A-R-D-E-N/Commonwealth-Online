#include "pch.h"

#include "F4TInteractWheel.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TDownedEngine.h"
#include "F4TDownedGlue.h"
#include "F4TDebugLogging.h"
#include "F4TFactionInviteOffer.h"
#include "F4TFactions.h"
#include "F4TFriends.h"
#include "F4TInteractWheelGate.h"
#include "F4TInventorySync.h"
#include "F4TJoinFlow.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TProxyActorController.h"
#include "F4TRemotePlayerState.h"
#include "F4TSocialNameplate.h"
#include "F4TTradeClient.h"
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
		std::uint32_t g_openToken = 0;
		std::uint32_t g_targetPlayerId = 0;
		std::string g_targetName;
		std::string g_lastStateJson;
		std::string g_lastPromptJson;
		std::string_view g_openBlockReason;
		std::uint32_t g_ticksSinceOpen = 0;

	}

	bool VanillaMenuOpen(bool a_includePauseMenu)
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) return false;
		if (a_includePauseMenu && ui->GetMenuOpen("PauseMenu")) return true;
		const char* menus[]{
			"DialogueMenu", "ContainerMenu", "BarterMenu", "TerminalMenu", "LooksMenu", "WorkshopMenu", "PipboyMenu", "LockpickingMenu", "LoadingMenu",
			"Console", "VATSMenu", "ExamineMenu", "MessageBoxMenu", "SleepWaitMenu", "BookMenu", "PowerArmorModMenu", "SPECIALMenu", "LevelUpMenu",
			"CookingMenu", "RobotModMenu", "MultiActivateMenu", "ExamineConfirmMenu", "FavoritesMenu", "PipboyHolotapeMenu", "TerminalHolotapeMenu", "VertibirdMenu", "Workshop_CaravanMenu"
		};
		return std::any_of(std::begin(menus), std::end(menus), [ui](const char* a_name) { return ui->GetMenuOpen(a_name); });
	}

	bool CoWindowOpen()
	{
		if (F4T::JoinFlow::IsActive()) return true;
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
		if (auto* admin = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI18>()) {
			if (admin->IsAdminWindowVisible && admin->IsAdminWindowVisible()) return true;
		}
		if (auto* wheel = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI17>()) {
			if (wheel->IsChatVisible && wheel->IsChatVisible()) return true;
			if (wheel->IsSlotScreenVisible && wheel->IsSlotScreenVisible()) return true;
		}
		return false;
	}

	namespace
	{
		bool TradeWindowVisible()
		{
			auto* trade = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI20>();
			return trade && trade->IsTradeWindowVisible && trade->IsTradeWindowVisible();
		}

		bool MenuBlocksWheel()
		{
			return VanillaMenuOpen(true) || CoWindowOpen();
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
				if (!F4T::FactionInviteOffer::IsActivationAim(distance, cosine) || distance >= nearest) {
					continue;
				}
				nearest = distance;
				target = proxy.remotePlayerId;
			}
			// Constraint: the aimed player decides; a downed one leaves Activate to the revive hold.
			if (!target || IsDowned(*target)) return std::nullopt;
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
			if (F4T::InventorySync::TradeSupported()) {
				const auto trade = F4T::TradeClient::CurrentTrade();
				if (!trade) {
					slices.push_back(Slice("trade", "Trade"));
				} else if (trade->partnerPlayerId != g_targetPlayerId) {
					slices.push_back(Slice("trade", "Trade", true));
				} else if (trade->phase == "requested" && trade->role == "target") {
					slices.push_back(Slice("tradeAccept", "Accept Trade"));
					slices.push_back(Slice("tradeDecline", "Decline Trade"));
				} else if (trade->phase == "negotiating") {
					slices.push_back(Slice("tradeConfirm", "Confirm Trade"));
					slices.push_back(Slice("tradeCancel", "Cancel Trade"));
				} else {
					slices.push_back(Slice("tradeCancel", "Cancel Trade"));
				}
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

		bool LocalPlayerIncapacitated()
		{
			return F4T::Downed::IsLocalPlayerDowned() || F4T::Downed::IsLocalPlayerDead();
		}

		bool TargetStillValid()
		{
			if (!F4T::Networking::IsConnectedToServer()) return false;
			if (!F4T::Downed::Engine::CanTouchWorld()) return false;
			if (LocalPlayerIncapacitated()) return false;
			if (!F4T::InteractWheelGate::CanOpenWheel(MenuBlocksWheel(), TradeWindowVisible())) return false;
			auto* player = F4T::Downed::Engine::LocalPlayer();
			if (!player) return false;
			auto* proxy = F4T::ProxyActorController::TryGetProxyActorForRemotePlayerId(g_targetPlayerId);
			if (!proxy) return false;
			const auto distance = F4T::Downed::Engine::DistanceBetween(player->GetPosition(), proxy->GetPosition());
			if (distance > F4T::FactionInviteOffer::kActivationRange) return false;
			return !IsDowned(g_targetPlayerId);
		}

		std::string_view OpenBlockReason()
		{
			if (!F4T::Downed::Engine::CanTouchWorld()) return "the world is not ready";
			const bool menuBlocked = MenuBlocksWheel();
			if (!F4T::InteractWheelGate::CanOpenWheel(menuBlocked, TradeWindowVisible())) {
				return menuBlocked ? "a menu is open" : "a trade window is open";
			}
			if (!F4T::Networking::IsConnectedToServer()) return "not connected to a server";
			if (LocalPlayerIncapacitated()) return "the local player is downed";
			return {};
		}

		void LogOpenBlock(std::string_view a_reason)
		{
			if (a_reason == g_openBlockReason) return;
			g_openBlockReason = a_reason;
			F4T::DebugLogging::Log(kLogComponent, "not opening: {}", a_reason);
		}

		std::optional<std::uint32_t> PromptTarget()
		{
			if (!OpenBlockReason().empty()) return std::nullopt;
			return FindTarget();
		}

		void PushPrompt(const std::optional<std::uint32_t>& a_target)
		{
			auto json = a_target ?
				Json{ { "visible", true }, { "name", RemoteName(*a_target) } }.dump() :
				Json{ { "visible", false } }.dump();
			if (json == g_lastPromptJson) return;
			g_lastPromptJson = std::move(json);
			F4T::UIClient::PushInteractPrompt(g_lastPromptJson.c_str());
		}
	}

	bool IsBlockedByMenu()
	{
		return MenuBlocksWheel();
	}

	void Tick()
	{
		PushPrompt(g_open ? std::nullopt : PromptTarget());
		if (!g_open) return;
		if (F4T::InteractWheelGate::ShouldCloseWheelForTrade(g_open, TradeWindowVisible())) {
			CloseWheel("TradeOpen");
			return;
		}
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
		if (g_open) return;
		if (const auto reason = OpenBlockReason(); !reason.empty()) {
			LogOpenBlock(reason);
			return;
		}
		const auto target = FindTarget();
		if (!target) {
			LogOpenBlock("no player in the aim cone");
			return;
		}
		g_targetPlayerId = *target;
		g_targetName = RemoteName(*target);
		// Constraint: every open gets its own token so a late close cannot shut the next wheel.
		++g_openToken;
		g_open = true;
		g_ticksSinceOpen = 0;
		g_lastStateJson.clear();
		g_openBlockReason = {};
		F4T::DebugLogging::Log(kLogComponent, "opening for player {}", g_targetPlayerId);
		F4T::Downed::Engine::ClearActivateHeld();
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
			{ "token", g_openToken },
			{ "slices", BuildSlices() }
		};
		return state.dump();
	}

	std::uint32_t OpenToken()
	{
		return g_openToken;
	}

	void CloseForToken(std::uint32_t a_token, std::string_view a_reason)
	{
		if (!g_open || a_token != g_openToken) return;
		CloseWheel(a_reason);
	}

	void HandleAction(std::string_view a_actionJson)
	{
		if (a_actionJson.size() > kMaximumActionBytes) return;
		const auto parsed = Json::parse(a_actionJson, nullptr, false);
		if (!parsed.is_object()) return;
		const auto op = parsed.value("op", std::string{});
		if (!g_open) return;
		if (op == "close") {
			const auto token = parsed.value("token", g_openToken);
			CloseForToken(token, "WheelClose");
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
		} else if (op == "trade" && target != 0) {
			F4T::TradeClient::RequestTrade(target);
		} else if (op == "tradeAccept" && target != 0) {
			const auto trade = F4T::TradeClient::CurrentTrade();
			if (trade && trade->partnerPlayerId == target) F4T::TradeClient::RespondTrade(trade->tradeId, true);
		} else if (op == "tradeCancel" && target != 0) {
			const auto trade = F4T::TradeClient::CurrentTrade();
			if (trade && trade->partnerPlayerId == target) F4T::TradeClient::CancelTrade(trade->tradeId);
		} else if (op == "tradeDecline" && target != 0) {
			const auto trade = F4T::TradeClient::CurrentTrade();
			if (trade && trade->partnerPlayerId == target) F4T::TradeClient::RespondTrade(trade->tradeId, false);
		} else if (op == "tradeConfirm" && target != 0) {
			const auto trade = F4T::TradeClient::CurrentTrade();
			if (trade && trade->partnerPlayerId == target) F4T::TradeClient::ConfirmTrade(trade->tradeId, trade->offerVersion);
		} else if (op != "requestSent") {
			return;
		}
		CloseWheel(op);
	}
}

#include "pch.h"

#include "F4TSteamJoinPrompt.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TFactions.h"
#include "F4TSteamHostLinkClient.h"
#include "F4TSteamTunnelService.h"
#include "F4TUIPrefs.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::SteamJoinPrompt
{
	namespace
	{
		constexpr std::string_view kAlertPrefix{ "co-steam-join-" };
		constexpr std::uint32_t kAlertMs = static_cast<std::uint32_t>(Transport::kHostApprovalTimeoutSeconds * 1000.0);

		std::mutex g_mutex;
		std::map<std::string, std::promise<bool>> g_pending;
		std::uint64_t g_nextId{ 1 };

		bool Resolve(const std::string& a_id, bool a_accepted)
		{
			std::lock_guard lock{ g_mutex };
			const auto found = g_pending.find(a_id);
			if (found == g_pending.end()) return false;
			found->second.set_value(a_accepted);
			g_pending.erase(found);
			return true;
		}

		void DismissAlert(const std::string& a_id)
		{
			F4T::Factions::QueueGameTask([id = a_id]() {
				auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
				if (ui && ui->DismissAlert) ui->DismissAlert(id.c_str());
			});
		}

		void Abandon(const std::string& a_id)
		{
			if (Resolve(a_id, false)) DismissAlert(a_id);
		}

		void OnAlertResponse(const char* a_alertId, const char* a_actionId, void*)
		{
			if (!a_alertId || !a_actionId) return;
			const std::string_view alertId{ a_alertId };
			if (!alertId.starts_with(kAlertPrefix)) return;
			(void)Resolve(std::string{ alertId }, std::string_view{ a_actionId } == "accept");
		}

		void ShowJoinAlert(const std::string& a_id, const std::string& a_message)
		{
			auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
			if (!ui || !ui->ShowAlert) {
				Resolve(a_id, false);
				return;
			}
			std::array<COMMONWEALTH_ONLINE_UI_API::AlertAction, 2> actions{ {
				{ "accept", "Accept", "Y" },
				{ "decline", "Decline", "N" }
			} };
			COMMONWEALTH_ONLINE_UI_API::AlertRequest request{};
			request.id = a_id.c_str();
			request.category = "Social";
			request.title = "Join request";
			request.message = a_message.c_str();
			request.tone = "invite";
			request.durationMs = kAlertMs;
			request.actions = actions.data();
			request.actionCount = actions.size();
			if (!ui->ShowAlert(&request, &OnAlertResponse, nullptr, nullptr, 0)) Resolve(a_id, false);
		}

		SteamAdmission::JoinMode CurrentJoinMode()
		{
			return JoinModeFromPreference(F4T::UIPrefs::Get().allowFriendJoin);
		}
	}

	SteamAdmission::JoinMode JoinModeFromPreference(std::uint8_t a_preference)
	{
		switch (a_preference) {
		case 1: return SteamAdmission::JoinMode::AskFirst;
		case 2: return SteamAdmission::JoinMode::Never;
		default: return SteamAdmission::JoinMode::Always;
		}
	}

	std::string_view JoinPromptMessage(SteamAdmission::FriendCheck a_friendCheck)
	{
		switch (a_friendCheck) {
		case SteamAdmission::FriendCheck::Friend: return "A Steam friend wants to join your game.";
		case SteamAdmission::FriendCheck::NotFriend: return "A Steam player who is not on your friends list wants to join your game.";
		case SteamAdmission::FriendCheck::Unavailable: return "A Steam player wants to join your game. Their friend status could not be checked.";
		}
		return "A Steam player wants to join your game.";
	}

	bool Ask(const SteamAdmission::TunnelRequest& a_request, std::stop_token a_stop)
	{
		if (a_stop.stop_requested()) return false;
		std::string id;
		std::future<bool> answer;
		{
			std::lock_guard lock{ g_mutex };
			id = std::string{ kAlertPrefix } + std::to_string(g_nextId++);
			answer = g_pending[id].get_future();
		}
		std::string message{ JoinPromptMessage(a_request.friendCheck) };
		F4T::Factions::QueueGameTask([id, message = std::move(message)]() { ShowJoinAlert(id, message); });
		std::stop_callback abandon{ a_stop, [id]() { Abandon(id); } };
		if (answer.wait_for(std::chrono::duration<double>(Transport::kHostApprovalTimeoutSeconds)) != std::future_status::ready) {
			Abandon(id);
		}
		return answer.get();
	}

	void Install()
	{
		F4T::Steam::SetIdentityBinderFactory([](std::uint16_t a_port, std::uint64_t a_hostSteamId) {
			auto binder = SteamHostLink::MakeDefaultIdentityBinder(a_port, a_hostSteamId);
			binder.askHost = Ask;
			if (!binder.hostPolicy) binder.hostPolicy = SteamAdmission::HostPolicy{};
			binder.hostPolicy->joinMode = CurrentJoinMode();
			binder.refreshHostPolicy = [refresh = std::move(binder.refreshHostPolicy), fallback = *binder.hostPolicy]() -> std::optional<SteamAdmission::HostPolicy> {
				auto policy = refresh ? refresh() : std::nullopt;
				if (!policy) policy = fallback;
				policy->joinMode = CurrentJoinMode();
				return policy;
			};
			return binder;
		});
	}
}

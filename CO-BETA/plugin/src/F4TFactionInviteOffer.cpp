#include "pch.h"

#include "F4TFactionInviteOffer.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TDebugLogging.h"
#include "F4TDownedEngine.h"
#include "F4TFactions.h"
#include "F4TProxyActorController.h"
#include "F4TRemotePlayerState.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace F4T::FactionInviteOffer
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr std::string_view kAlertPrefix{ "faction-invite-offer-" };
		constexpr std::string_view kConfirmAction{ "confirm" };
		constexpr std::string_view kLogComponent{ "FactionInvite" };
		constexpr std::uint32_t kAlertDurationMs{ 10000U };

		bool g_activateWasHeld = false;

		struct FactionView
		{
			bool localCanInvite{ false };
			std::uint32_t localPlayerId{ 0 };
			std::string factionName;
			std::vector<std::uint32_t> memberPlayerIds;
		};

		std::optional<std::uint32_t> ReadPlayerId(const Json& a_json, const char* a_field)
		{
			const auto found = a_json.find(a_field);
			if (found == a_json.end() || !found->is_number_unsigned()) {
				return std::nullopt;
			}
			return found->get<std::uint32_t>();
		}

		std::optional<FactionView> ReadFactionView()
		{
			if (!F4T::Factions::InFaction()) {
				return std::nullopt;
			}
			const auto state = Json::parse(F4T::Factions::BuildStateJson(), nullptr, false);
			if (!state.is_object()) {
				return std::nullopt;
			}
			const auto faction = state.find("faction");
			if (faction == state.end() || !faction->is_object()) {
				return std::nullopt;
			}
			const auto members = faction->find("members");
			if (members == faction->end() || !members->is_array()) {
				return std::nullopt;
			}

			FactionView view;
			view.localPlayerId = ReadPlayerId(state, "localPlayerId").value_or(0);
			view.factionName = faction->value("name", std::string{});
			for (const auto& member : *members) {
				const auto playerId = ReadPlayerId(member, "playerId");
				if (!playerId) {
					continue;
				}
				view.memberPlayerIds.push_back(*playerId);
				if (*playerId == view.localPlayerId) {
					view.localCanInvite = member.value("leader", false) || member.value("officer", false);
				}
			}
			return view;
		}

		std::string RemoteName(std::uint32_t a_playerId)
		{
			for (const auto& remote : F4T::RemotePlayerState::GetRemotePlayerSnapshot()) {
				if (remote.playerId == a_playerId && !remote.characterName.empty()) {
					return remote.characterName;
				}
			}
			return "Unknown player";
		}

		bool IsDowned(std::uint32_t a_playerId)
		{
			for (const auto& remote : F4T::RemotePlayerState::GetRemotePlayerSnapshot()) {
				if (remote.playerId == a_playerId) {
					return remote.vitals && (remote.vitals->isDowned || remote.vitals->isDead);
				}
			}
			return false;
		}

		std::optional<std::uint32_t> FindOfferTarget(const FactionView& a_view)
		{
			auto* player = F4T::Downed::Engine::LocalPlayer();
			if (!player) {
				return std::nullopt;
			}
			const auto position = player->GetPosition();
			const auto heading = player->GetHeading();

			std::optional<std::uint32_t> target;
			float nearest = std::numeric_limits<float>::max();
			for (const auto& proxy : F4T::ProxyActorController::GetActiveProxyActors()) {
				if (!proxy.actor) {
					continue;
				}
				const auto proxyPosition = proxy.actor->GetPosition();
				const auto distance = F4T::Downed::Engine::DistanceBetween(position, proxyPosition);
				const auto cosine = F4T::Downed::Engine::FacingCosine(position, heading, proxyPosition);
				const bool inFaction = std::ranges::contains(a_view.memberPlayerIds, proxy.remotePlayerId);
				const bool downed = IsDowned(proxy.remotePlayerId);
				if (!IsActivationAim(distance, cosine) ||
				    !ShouldOffer(a_view.localCanInvite, a_view.localPlayerId, proxy.remotePlayerId, inFaction, downed) ||
				    distance >= nearest) {
					continue;
				}
				nearest = distance;
				target = proxy.remotePlayerId;
			}
			return target;
		}

		void OnOfferResponse(const char* a_alertId, const char* a_actionId, void*)
		{
			if (!a_alertId || !a_actionId || std::string_view{ a_actionId } != kConfirmAction) {
				return;
			}
			const std::string_view alertId{ a_alertId };
			if (!alertId.starts_with(kAlertPrefix)) {
				return;
			}
			const auto digits = alertId.substr(kAlertPrefix.size());
			std::uint32_t targetPlayerId = 0;
			const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), targetPlayerId);
			if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() || targetPlayerId == 0) {
				return;
			}
			F4T::Factions::HandleAction(Json{ { "op", "invite" }, { "targetPlayerId", targetPlayerId } }.dump());
		}

		void ShowOffer(std::uint32_t a_targetPlayerId, const FactionView& a_view)
		{
			auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
				COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
			if (!ui || !ui->ShowAlert) {
				F4T::DebugLogging::Write(kLogComponent, "Faction invite offer skipped: the alert API is unavailable.");
				return;
			}

			const auto alertId = std::format("{}{}", kAlertPrefix, a_targetPlayerId);
			const auto message = std::format("Invite {} to {}?", RemoteName(a_targetPlayerId), a_view.factionName);
			std::array<COMMONWEALTH_ONLINE_UI_API::AlertAction, 2> actions{ {
				{ "confirm", "Invite", "Y" },
				{ "cancel", "Cancel", "N" }
			} };

			COMMONWEALTH_ONLINE_UI_API::AlertRequest request{};
			request.id = alertId.c_str();
			request.category = "Social";
			request.title = "Faction invite";
			request.message = message.c_str();
			request.tone = "invite";
			request.durationMs = kAlertDurationMs;
			request.actions = actions.data();
			request.actionCount = actions.size();
			if (!ui->ShowAlert(&request, &OnOfferResponse, nullptr, nullptr, 0)) {
				F4T::DebugLogging::Write(kLogComponent, "Faction invite offer failed to show.");
			}
		}

		void OfferForActivation()
		{
			if (!F4T::Downed::Engine::CanTouchWorld()) {
				return;
			}
			const auto view = ReadFactionView();
			if (!view || !view->localCanInvite) {
				return;
			}
			const auto target = FindOfferTarget(*view);
			if (target) {
				ShowOffer(*target, *view);
			}
		}
	}

	void Tick()
	{
		const auto activateHeld = F4T::Downed::Engine::IsActivateHeld();
		const bool pressed = activateHeld && !g_activateWasHeld;
		g_activateWasHeld = activateHeld;
		if (pressed) {
			OfferForActivation();
		}
	}
}

#include "pch.h"

#include "F4TCampGround.h"
#include "F4TWorkshopReplicationClient.h"
#include "F4TWorkshopReplicationInternal.h"
#include "CommonwealthOnlineUI_API.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/S/SendHUDMessage.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <format>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>

namespace F4T::WorkshopReplicationClient
{
	using Json = nlohmann::json;

	namespace
	{
		constexpr std::string_view kCampMoveAlertPrefix = "camp-move:";
		constexpr std::uint32_t kCampMoveAlertMs = 30000;
		constexpr std::string_view kCampDeleteAlertPrefix = "camp-delete:";
		constexpr std::uint32_t kCampDeleteAlertMs = 30000;

		std::uint64_t NextCampOperationId()
		{
			std::scoped_lock lock(g_mutex);
			return g_nextOperationId++;
		}

		void OnCampDeleteAlertResponse(const char* a_alertId, const char* a_actionId, void*)
		{
			if (!a_alertId || !a_actionId || std::string_view{ a_actionId } != "confirm-delete") {
				return;
			}
			const std::string_view alertId{ a_alertId };
			if (!alertId.starts_with(kCampDeleteAlertPrefix)) {
				return;
			}
			const auto digits = alertId.substr(kCampDeleteAlertPrefix.size());
			Workshop::BuildZoneId zone{};
			if (std::from_chars(digits.data(), digits.data() + digits.size(), zone).ec != std::errc{}) {
				return;
			}
			const auto* taskInterface = F4SE::GetTaskInterface();
			if (!taskInterface) {
				return;
			}
			taskInterface->AddTask([zone]() {
				if (SubmitCampRemove(zone)) {
					RE::SendHUDMessage::ShowHUDMessage("Removing your camp...", nullptr, true, true);
				}
			});
		}

		void ShowCampDeleteAlert(Workshop::BuildZoneId a_buildZoneId)
		{
			auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
			if (!ui || !ui->ShowAlert) {
				return;
			}
			const auto alertId = std::format("{}{}", kCampDeleteAlertPrefix, a_buildZoneId);
			std::array<COMMONWEALTH_ONLINE_UI_API::AlertAction, 2> actions{ { { "confirm-delete", "Delete camp", nullptr }, { "keep", "Keep camp", nullptr } } };
			COMMONWEALTH_ONLINE_UI_API::AlertRequest request{};
			request.id = alertId.c_str();
			request.category = "Camp";
			request.title = "Delete camp?";
			request.message = "Deleting removes your camp from the world and cannot be undone. An active camp refunds one camp kit; a stored camp gives no kit back.";
			request.tone = "info";
			request.durationMs = kCampDeleteAlertMs;
			request.actions = actions.data();
			request.actionCount = actions.size();
			(void)ui->ShowAlert(&request, &OnCampDeleteAlertResponse, nullptr, nullptr, 0);
		}

		void OnCampMoveAlertResponse(const char* a_alertId, const char* a_actionId, void*)
		{
			if (!a_alertId || !a_actionId || (std::string_view{ a_actionId } != "move" && std::string_view{ a_actionId } != "delete")) {
				return;
			}
			const std::string_view alertId{ a_alertId };
			if (!alertId.starts_with(kCampMoveAlertPrefix)) {
				return;
			}
			const auto digits = alertId.substr(kCampMoveAlertPrefix.size());
			Workshop::BuildZoneId zone{};
			if (std::from_chars(digits.data(), digits.data() + digits.size(), zone).ec != std::errc{}) {
				return;
			}
			const auto* taskInterface = F4SE::GetTaskInterface();
			if (!taskInterface) {
				return;
			}
			const bool deleting = std::string_view{ a_actionId } == "delete";
			taskInterface->AddTask([zone, deleting]() {
				if (deleting) {
					ShowCampDeleteAlert(zone);
					return;
				}
				if (!SubmitCampMove(zone)) {
					RE::SendHUDMessage::ShowHUDMessage("Camp move could not start.", nullptr, true, true);
				}
			});
		}
	}

	bool SubmitCampMove(Workshop::BuildZoneId a_buildZoneId)
	{
		const auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return false;
		}
		const auto position = player->GetPosition();
		const auto ground = F4T::CampGround::SurfaceAt(position.x, position.y, position.z);
		QueuePacket(Json{
			{ "type", "campMove" }, { "operationId", NextCampOperationId() }, { "buildZoneId", a_buildZoneId },
			{ "x", position.x }, { "y", position.y }, { "z", position.z },
			{ "probe", Json{ { "height", ground ? ground->z : position.z }, { "slopeDegrees", ground ? ground->slopeDegrees : 0.0F }, { "navmeshHit", ground.has_value() } } }
		});
		return true;
	}

	bool SubmitCampRemove(Workshop::BuildZoneId a_buildZoneId)
	{
		const auto operationId = NextCampOperationId();
		RememberPendingCampRemove(operationId);
		QueuePacket(Json{ { "type", "campRemove" }, { "operationId", operationId }, { "buildZoneId", a_buildZoneId } });
		return true;
	}

	void ShowCampMoveAlert(Workshop::BuildZoneId a_buildZoneId)
	{
		auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
		if (!ui || !ui->ShowAlert) {
			return;
		}
		const auto alertId = std::format("{}{}", kCampMoveAlertPrefix, a_buildZoneId);
		std::array<COMMONWEALTH_ONLINE_UI_API::AlertAction, 2> actions{ { { "move", "Move camp here", nullptr }, { "delete", "Delete camp", nullptr } } };
		COMMONWEALTH_ONLINE_UI_API::AlertRequest request{};
		request.id = alertId.c_str();
		request.category = "Camp";
		request.title = "Your camp";
		request.message = "Stand where you want the camp, then choose Move camp here. Sneak and activate the camp to pack it up. Choose Delete camp to remove it; you will be asked to confirm.";
		request.tone = "info";
		request.durationMs = kCampMoveAlertMs;
		request.actions = actions.data();
		request.actionCount = actions.size();
		(void)ui->ShowAlert(&request, &OnCampMoveAlertResponse, nullptr, nullptr, 0);
	}
}

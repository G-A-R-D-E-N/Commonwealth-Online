#include "pch.h"

#include "F4TPrismaUI.h"
#include "F4TSettingsBridge.h"
#include "F4TMultiplayerProfile.h"
#include "F4TSettingsRegistry.h"
#include "F4TSettingsService.h"

#include <format>

namespace F4T::Settings
{
	namespace
	{
		PRISMA_UI_API::IVPrismaUI4* g_api = nullptr;

		std::string GetLogPrefix()
		{
			return "[SettingsBridge]";
		}

		void FileLog(std::string_view a_message)
		{
			REX::INFO("{}{}", GetLogPrefix(), a_message);
		}

		bool CanDispatch(PrismaView a_view)
		{
			return g_api && a_view != 0 && g_api->IsValid(a_view);
		}

		void DispatchEvent(PrismaView a_view, const nlohmann::json& a_event)
		{
			if (!CanDispatch(a_view)) {
				FileLog(std::format("Dispatch skipped - view not ready ({})", a_event.value("type", "")));
				return;
			}

			const auto payload = a_event.dump();
			FileLog(std::format("Dispatch {} ({} bytes)", a_event.value("type", ""), payload.size()));

			g_api->InteropCall(a_view, "dispatchCoSettingsEvent", payload.c_str());
		}

		void Reply(PrismaView a_view, const nlohmann::json& a_request, const SettingResult& a_result)
		{
			nlohmann::json event{
				{ "type", "settingsResult" },
				{ "requestId", a_request.value("requestId", "") },
				{ "action", a_request.value("action", "") },
				{ "success", a_result.success },
			};
			if (a_result.error) {
				event["error"] = a_result.error->ToJson();
			}
			if (!a_result.data.is_null()) {
				event["data"] = a_result.data;
			}
			DispatchEvent(a_view, event);
		}

		void SendAck(PrismaView a_view, const nlohmann::json& a_request, std::string_view a_stage)
		{
			DispatchEvent(a_view, nlohmann::json{
				{ "type", "settingsAck" },
				{ "requestId", a_request.value("requestId", "") },
				{ "action", a_request.value("action", "") },
				{ "stage", a_stage },
			});
		}

		void ReplyGetAllChunked(PrismaView a_view, const nlohmann::json& a_request)
		{
			SendAck(a_view, a_request, "received");
			FileLog("GetAllSettings streaming start");

			nlohmann::json batch = nlohmann::json::array();
			nlohmann::json allSettings = nlohmann::json::array();
			bool reset = true;
			std::size_t offset = 0;
			constexpr std::size_t kBatchSize = 5;

			for (const auto& definition : SettingsRegistry::GetAll()) {
				if (!definition.showInUI) {
					continue;
				}

				FileLog(std::format("Reading {}", definition.id));
				SettingsService::RefreshOne(definition.id);
				auto dto = SettingsService::BuildOneDto(definition.id);
				if (dto.is_null()) {
					continue;
				}

				batch.push_back(dto);
				allSettings.push_back(dto);

				if (batch.size() >= kBatchSize) {
					DispatchEvent(a_view, nlohmann::json{
						{ "type", "settingsBatch" },
						{ "requestId", a_request.value("requestId", "") },
						{ "action", "settingsGetAll" },
						{ "reset", reset },
						{ "settings", batch },
						{ "offset", offset },
					});
					offset += batch.size();
					batch = nlohmann::json::array();
					reset = false;
				}
			}

			if (!batch.empty()) {
				DispatchEvent(a_view, nlohmann::json{
					{ "type", "settingsBatch" },
					{ "requestId", a_request.value("requestId", "") },
					{ "action", "settingsGetAll" },
					{ "reset", reset },
					{ "settings", batch },
					{ "offset", offset },
				});
			}

			DispatchEvent(a_view, nlohmann::json{
				{ "type", "settingsResult" },
				{ "requestId", a_request.value("requestId", "") },
				{ "action", "settingsGetAll" },
				{ "success", true },
				{ "data", nlohmann::json{
					{ "settings", allSettings },
					{ "hasPendingChanges", SettingsService::HasPendingChanges() },
					{ "complete", true },
				} },
			});
			FileLog(std::format("GetAllSettings streaming done count={}", allSettings.size()));
		}
	}

	void SettingsBridge::Initialize(PRISMA_UI_API::IVPrismaUI4* a_api)
	{
		g_api = a_api;
		SettingsService::Initialize();
		FileLog("Initialised");
	}

	static void ProcessAction(const nlohmann::json& a_action, PrismaView a_view)
	{
		const auto actionName = a_action.value("action", "");
		if (!actionName.starts_with("settings")) {
			return;
		}

		FileLog(std::format("HandleAction {}", actionName));

		try {
			if (actionName == "settingsGetMultiplayerProfile") {
				Reply(a_view, a_action, SettingResult{ .success = true,
					.data = nlohmann::json{ { "displayName", F4T::MultiplayerProfile::GetDisplayName() } } });
				return;
			}
			if (actionName == "settingsSetDisplayName") {
				std::string error;
				if (!F4T::MultiplayerProfile::SaveDisplayName(a_action.value("value", std::string{}), error)) {
					Reply(a_view, a_action, SettingResult{ .success = false,
						.error = SettingError{ "PROFILE_SAVE_FAILED", error } });
				} else {
					Reply(a_view, a_action, SettingResult{ .success = true,
						.data = nlohmann::json{ { "displayName", F4T::MultiplayerProfile::GetDisplayName() } } });
				}
				return;
			}
			if (actionName == "settingsGetAll") {
				ReplyGetAllChunked(a_view, a_action);
				return;
			}
			if (actionName == "settingsGetCategory") {
				Reply(a_view, a_action, SettingsService::GetSettingsByCategory(a_action.value("category", "")));
				return;
			}
			if (actionName == "settingsSet") {
				Reply(a_view, a_action, SettingsService::SetSetting(
					a_action.value("id", ""),
					a_action.contains("value") ? a_action["value"] : nlohmann::json()));
				return;
			}
			if (actionName == "settingsApply") {
				Reply(a_view, a_action, SettingsService::ApplyPendingSettings());
				return;
			}
			if (actionName == "settingsCancel") {
				Reply(a_view, a_action, SettingsService::CancelPendingSettings());
				return;
			}
			if (actionName == "settingsResetCategory") {
				Reply(a_view, a_action, SettingsService::ResetCategory(a_action.value("category", "")));
				return;
			}
			if (actionName == "settingsResetAll") {
				Reply(a_view, a_action, SettingsService::ResetAllSettings());
				return;
			}
			if (actionName == "settingsCaptureBaseline") {
				Reply(a_view, a_action, SettingsService::CaptureBaseline());
				return;
			}
			if (actionName == "settingsShown") {
				SendAck(a_view, a_action, "shown");
				return;
			}

			Reply(a_view, a_action, SettingResult{
				.success = false,
				.error = SettingError{
					"SETTING_NOT_FOUND",
					std::format("Unknown settings action '{}'.", actionName)
				}
			});
		} catch (const std::exception& ex) {
			FileLog(std::format("Failed {}: {}", actionName, ex.what()));
			Reply(a_view, a_action, SettingResult{
				.success = false,
				.error = SettingError{ "ENGINE_NOT_READY", ex.what() }
			});
		} catch (...) {
			FileLog(std::format("Failed {} with unknown exception", actionName));
			Reply(a_view, a_action, SettingResult{
				.success = false,
				.error = SettingError{ "ENGINE_NOT_READY", "Unknown native settings exception." }
			});
		}
	}
	void SettingsBridge::HandleAction(const nlohmann::json& a_action, PrismaView a_view)
	{
		const auto* tasks = F4SE::GetTaskInterface();
		if (!tasks) {
			Reply(a_view, a_action, SettingResult{ .success = false,
				.error = SettingError{ "ENGINE_NOT_READY", "The game task interface is unavailable." } });
			return;
		}
		tasks->AddTask([action = a_action, a_view]() { ProcessAction(action, a_view); });
	}

}

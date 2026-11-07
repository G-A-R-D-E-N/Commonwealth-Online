#include "F4TPrismaUIInternal.h"
#include "F4TViewJson.h"
#include "F4TCaptureHud.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::CaptureHud
{
	namespace
	{
		using F4T::PrismaUI::Internal::g_api;
		using F4T::PrismaUI::Internal::g_prismaReady;

		constexpr auto kHtmlPath = "CommonwealthOnline/capture-hud/index.html";
		constexpr auto kEntryPoint = "coCaptureSetState";
		constexpr int kViewOrder = 525;
		constexpr std::size_t kMaxJsonBytes = 8 * 1024;

		F4T::PrismaViewState::Controller g_hudState{ "CaptureHud" };
		std::string g_lastJson;
		bool g_hasState = false;

		void LogWarn(std::string_view a_message)
		{
			REX::WARN("[COUI] [CaptureHud] {}", a_message);
		}

		void RunOnGameThread(std::function<void()> a_task)
		{
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask(std::move(a_task));
				return;
			}
			LogWarn("Dropped capture HUD task: game task interface unavailable.");
		}

		void PushScript()
		{
			if (!g_api || !g_hudState.IsDomReady() || !g_hudState.IsHandleValid()) {
				return;
			}
			const auto script = std::format(
				"if(window.{0}){{window.{0}({1});}}",
				kEntryPoint,
				F4T::ViewJson::Dump(nlohmann::json(g_lastJson)));
			g_api->Invoke(g_hudState.GetHandle(), script.c_str());
		}

		void OnDomReady(PrismaView a_view)
		{
			RunOnGameThread([a_view]() {
				if (!g_hudState.MarkDomReady(a_view) || !g_api) {
					return;
				}
				g_hudState.AssignRoleIfNeeded();
				g_hudState.ApplyState("DomReady");
				PushScript();
			});
		}
	}

	void Initialize()
	{
		g_hudState.SetApi(g_api);
		g_hudState.SetFocusArgs(false, true);
		g_hudState.SetDesiredRole(PRISMA_UI_API::ViewRole::kWidget);
		g_hudState.SetDesired(false, false);
	}

	void PushStateJson(const char* a_json)
	{
		if (!a_json) {
			return;
		}
		if (strnlen(a_json, kMaxJsonBytes + 1) > kMaxJsonBytes) {
			LogWarn("Dropped capture HUD state larger than 8 KiB.");
			return;
		}
		RunOnGameThread([payload = std::string(a_json)]() {
			const auto state = nlohmann::json::parse(payload, nullptr, false);
			if (!state.is_object()) {
				LogWarn("Dropped malformed capture HUD state.");
				return;
			}
			g_hasState = true;
			if (payload == g_lastJson) {
				return;
			}
			g_lastJson = payload;
			const auto nearZone = state.find("nearBuildZoneId");
			const auto rows = state.find("settlements");
			const auto selfRaid = state.find("selfRaidProtection");
			const bool selfProtected = selfRaid != state.end() && selfRaid->is_object() &&
				selfRaid->contains("active") && (*selfRaid)["active"].is_boolean() && (*selfRaid)["active"].get<bool>();
			const bool nearSettlement = nearZone != state.end() && nearZone->is_number_unsigned() &&
				rows != state.end() && rows->is_array() &&
				std::any_of(rows->begin(), rows->end(), [&](const auto& row) {
					const auto id = row.is_object() ? row.find("buildZoneId") : row.end();
					return id != row.end() && *id == *nearZone;
				});
			g_hudState.SetDesiredVisible(nearSettlement || selfProtected);
			g_hudState.ApplyState("HudState");
			PushScript();
		});
	}

	void Tick()
	{
		if (!g_api || !g_hasState) {
			return;
		}
		if (!g_hudState.IsHandleValid()) {
			if (g_hudState.EnsureCreated(kHtmlPath, OnDomReady, g_prismaReady) != 0) {
				g_api->SetOrder(g_hudState.GetHandle(), kViewOrder);
			}
		}
	}
}

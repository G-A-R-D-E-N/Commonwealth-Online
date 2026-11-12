#include "F4TPrismaUIInternal.h"
#include "F4TViewJson.h"
#include "F4TCaptureHud.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
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
		using F4T::PrismaUI::Internal::g_sessionLockoutActive;

		constexpr auto kHtmlPath = "CommonwealthOnline/capture-hud/index.html";
		constexpr auto kEntryPoint = "coCaptureSetState";
		constexpr int kViewOrder = 525;
		constexpr std::size_t kMaxJsonBytes = 8 * 1024;
		constexpr std::chrono::milliseconds kSelfProtectionFlash{ 15000 };
		constexpr std::chrono::seconds kSelfProtectionJump{ 60 };

		F4T::PrismaViewState::Controller g_hudState{ "CaptureHud" };
		std::string g_lastJson;
		bool g_hasState = false;
		bool g_nearSettlement = false;
		bool g_selfProtected = false;
		bool g_selfOwns = false;
		bool g_selfProtectionEnded = false;
		std::chrono::steady_clock::time_point g_selfProtectionEnd{};
		std::chrono::milliseconds g_selfProtectionFlashLeft{ 0 };
		std::chrono::steady_clock::time_point g_flashLastTick{};

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
			auto payload = nlohmann::json::parse(g_lastJson, nullptr, false);
			if (!payload.is_object()) {
				return;
			}
			payload["selfProtectionEnded"] = g_selfProtectionEnded;
			const auto script = std::format(
				"if(window.{0}){{window.{0}({1});}}",
				kEntryPoint,
				F4T::ViewJson::Dump(nlohmann::json(payload.dump())));
			g_api->Invoke(g_hudState.GetHandle(), script.c_str());
		}

		bool SurfaceClear()
		{
			if (F4T::PrismaUI::IsMainMenuVisible() || F4T::PrismaUI::IsBrowserVisible() ||
				F4T::PrismaUI::IsPauseMenuVisible() || g_sessionLockoutActive) {
				return false;
			}
			const auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return true;
			}
			static const RE::BSFixedString kBlockingMenus[]{ "LooksMenu", "SPECIALMenu", "MessageBoxMenu", "PipboyMenu", "ContainerMenu", "WorkshopMenu", "TerminalMenu", "DialogueMenu", "BarterMenu", "LockpickingMenu", "ExamineMenu", "LevelUpMenu", "BookMenu", "SleepWaitMenu", "PauseMenu", "LoadingMenu" };
			for (const auto& name : kBlockingMenus) {
				if (ui->GetMenuOpen(name)) {
					return false;
				}
			}
			return true;
		}

		void ApplyVisibility()
		{
			const auto now = std::chrono::steady_clock::now();
			const auto elapsed = g_flashLastTick == std::chrono::steady_clock::time_point{} ? std::chrono::milliseconds{ 0 } :
				(std::min)(std::chrono::duration_cast<std::chrono::milliseconds>(now - g_flashLastTick), std::chrono::milliseconds{ 1000 });
			g_flashLastTick = now;
			const auto clear = SurfaceClear();
			if (clear) {
				g_selfProtectionFlashLeft = (std::max)(g_selfProtectionFlashLeft - elapsed, std::chrono::milliseconds{ 0 });
			}
			const auto flashing = g_selfProtectionFlashLeft > std::chrono::milliseconds{ 0 };
			if (g_selfProtectionEnded && !flashing) {
				g_selfProtectionEnded = false;
				PushScript();
			}
			const auto wanted = (g_nearSettlement || flashing) && clear;
			if (wanted == g_hudState.IsDesiredVisible()) {
				return;
			}
			g_hudState.SetDesiredVisible(wanted);
			g_hudState.ApplyState(wanted ? "HudShow" : "HudHide");
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
			const auto ownsField = state.find("selfOwnsSettlement");
			const bool selfOwns = ownsField != state.end() && ownsField->is_boolean() && ownsField->get<bool>();
			const auto now = std::chrono::steady_clock::now();
			auto protectionEnd = std::chrono::steady_clock::time_point{};
			if (selfProtected && selfRaid->contains("secondsLeft") && (*selfRaid)["secondsLeft"].is_number_unsigned()) {
				protectionEnd = now + std::chrono::seconds{ (std::min)((*selfRaid)["secondsLeft"].get<std::uint64_t>(), std::uint64_t{ 31536000 }) };
			}
			const bool wasProtected = g_selfOwns && g_selfProtected;
			g_selfProtectionEnded = selfOwns && !selfProtected && (wasProtected || g_selfProtectionEnded);
			const bool changed = !g_selfOwns || selfProtected != g_selfProtected ||
				(selfProtected && g_selfProtected && (protectionEnd > g_selfProtectionEnd ? protectionEnd - g_selfProtectionEnd : g_selfProtectionEnd - protectionEnd) > kSelfProtectionJump);
			if (selfOwns && changed && (selfProtected || g_selfProtectionEnded)) {
				g_selfProtectionFlashLeft = kSelfProtectionFlash;
			}
			if (!selfOwns) {
				g_selfProtectionFlashLeft = std::chrono::milliseconds{ 0 };
			}
			g_selfOwns = selfOwns;
			g_selfProtected = selfProtected;
			g_selfProtectionEnd = protectionEnd;
			g_nearSettlement = nearSettlement;
			ApplyVisibility();
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
		ApplyVisibility();
	}
}

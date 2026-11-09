#include "F4TPrismaUIInternal.h"
#include "F4TViewJson.h"
#include "F4TInteractPromptHud.h"
#include "F4TInteractWheelView.h"
#include "F4TTradeView.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstring>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::InteractPromptHud
{
	namespace
	{
		using F4T::PrismaUI::Internal::g_api;
		using F4T::PrismaUI::Internal::g_prismaReady;

		constexpr auto kHtmlPath = "CommonwealthOnline/interact-prompt/index.html";
		constexpr auto kEntryPoint = "coInteractPromptSetState";
		constexpr int kViewOrder = 645;
		constexpr std::size_t kMaxJsonBytes = 8 * 1024;

		F4T::PrismaViewState::Controller g_promptState{ "InteractPrompt" };
		std::string g_lastJson;
		bool g_hasState = false;
		bool g_wantVisible = false;

		void LogWarn(std::string_view a_message)
		{
			REX::WARN("[COUI] [InteractPrompt] {}", a_message);
		}

		void RunOnGameThread(std::function<void()> a_task)
		{
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask(std::move(a_task));
				return;
			}
			LogWarn("Dropped interact prompt task: game task interface unavailable.");
		}

		void PushScript()
		{
			if (!g_api || !g_promptState.IsDomReady() || !g_promptState.IsHandleValid()) {
				return;
			}
			const auto script = std::format(
				"if(window.{0}){{window.{0}({1});}}",
				kEntryPoint,
				F4T::ViewJson::Dump(nlohmann::json(g_lastJson)));
			g_api->Invoke(g_promptState.GetHandle(), script.c_str());
		}

		void ApplyVisibility(const char* a_reason)
		{
			const bool shown = g_wantVisible &&
				!F4T::InteractWheelView::IsCurrentlyVisible() &&
				!F4T::TradeView::IsCurrentlyVisible() &&
				!F4T::PrismaUI::IsPipboyOpen();
			if (shown == g_promptState.IsDesiredVisible()) {
				return;
			}
			g_promptState.SetDesiredVisible(shown);
			g_promptState.ApplyState(a_reason);
		}

		void OnDomReady(PrismaView a_view)
		{
			RunOnGameThread([a_view]() {
				if (!g_promptState.MarkDomReady(a_view) || !g_api) {
					return;
				}
				g_promptState.AssignRoleIfNeeded();
				g_promptState.ApplyState("DomReady");
				PushScript();
			});
		}
	}

	void Initialize()
	{
		g_promptState.SetApi(g_api);
		g_promptState.SetFocusArgs(false, true);
		g_promptState.SetDesiredRole(PRISMA_UI_API::ViewRole::kWidget);
		g_promptState.SetDesired(false, false);
	}

	void PushStateJson(const char* a_json)
	{
		if (!a_json) {
			return;
		}
		if (strnlen(a_json, kMaxJsonBytes + 1) > kMaxJsonBytes) {
			LogWarn("Dropped interact prompt state larger than 8 KiB.");
			return;
		}
		RunOnGameThread([payload = std::string(a_json)]() {
			const auto state = nlohmann::json::parse(payload, nullptr, false);
			if (!state.is_object()) {
				LogWarn("Dropped malformed interact prompt state.");
				return;
			}
			g_hasState = true;
			if (payload == g_lastJson) {
				return;
			}
			g_lastJson = payload;
			const auto visible = state.find("visible");
			g_wantVisible = visible != state.end() && visible->is_boolean() && visible->get<bool>();
			ApplyVisibility("PromptState");
			PushScript();
		});
	}

	void Tick()
	{
		if (!g_api || !g_hasState) {
			return;
		}
		if (!g_promptState.IsHandleValid()) {
			if (g_promptState.EnsureCreated(kHtmlPath, OnDomReady, g_prismaReady) != 0) {
				g_api->SetOrder(g_promptState.GetHandle(), kViewOrder);
			}
			return;
		}
		ApplyVisibility("Visibility");
	}
}

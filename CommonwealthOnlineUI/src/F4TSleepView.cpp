#include "F4TPrismaUIInternal.h"
#include "F4TViewJson.h"
#include "F4TSleepView.h"
#include "F4TSleepAction.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <string>

namespace F4T::SleepView
{
	namespace
	{
		using F4T::PrismaUI::Internal::g_api;
		using F4T::PrismaUI::Internal::PassthroughControllerActions;
		using F4T::PrismaUI::Internal::SyncPassthroughControllerActions;
		F4T::PrismaViewState::Controller g_view{ "Sleep" };
		PassthroughControllerActions g_passthrough;
		Phase g_phase{ Phase::Closed };
		std::uint32_t g_token{};
		std::uint32_t g_hours{ 8 };
		std::uint32_t g_maxHours{ 24 };
		float g_elapsedHours{};
		ActionCallback g_callback{};

		void SyncEscapeOwnership()
		{
			auto* api = F4T::PrismaCapabilities::GetV12();
			const auto handle = g_view.GetHandle();
			if (api && handle && api->IsValid(handle)) {
				api->SetViewOwnsEscape(handle, g_view.IsDesiredVisible() && g_view.IsCurrentlyFocused());
			}
			SyncPassthroughControllerActions(g_view, g_passthrough, g_view.IsCurrentlyVisible());
		}

		void PushState()
		{
			if (!g_api || !g_view.IsDomReady() || !g_view.IsHandleValid()) return;
			const auto* phase = g_phase == Phase::Choose ? "choose" : g_phase == Phase::Resting ? "resting" : "closed";
			const auto script = std::format(
				"if(window.coSleepSetState){{window.coSleepSetState({{phase:'{}',token:{},hours:{},maxHours:{},elapsed:{:.3f}}});}}",
				phase, g_token, g_hours, g_maxHours, g_elapsedHours);
			g_api->Invoke(g_view.GetHandle(), script.c_str());
		}

		void HandleAction(const std::string& a_payload)
		{
			if (!g_callback) return;
			if (const auto action = ParseAction(a_payload, g_phase, g_token, g_maxHours)) {
				g_callback(action->action, g_token, action->hours);
			}
		}

		void OnAction(const char* a_payload)
		{
			if (!a_payload) return;
			const auto length = strnlen(a_payload, 1025);
			if (length > 1024) return;
			if (const auto* tasks = F4SE::GetTaskInterface()) {
				tasks->AddTask([payload = std::string(a_payload, length)] { HandleAction(payload); });
			}
		}

		void OnDomReady(PrismaView a_view)
		{
			if (const auto* tasks = F4SE::GetTaskInterface()) {
				tasks->AddTask([a_view] {
					if (!g_api || !g_view.MarkDomReady(a_view)) return;
					g_api->BindUIEvent(a_view, "coSleepAction", OnAction);
					g_api->SetOrder(a_view, 650);
					g_view.ApplyState("SleepDomReady");
					SyncEscapeOwnership();
					PushState();
				});
			}
		}
	}

	void Initialize()
	{
		g_view.SetApi(g_api);
		g_view.SetFocusArgs(false, true);
		g_view.SetDesiredRole(PRISMA_UI_API::ViewRole::kPanel);
		g_view.SetDesired(false, false);
	}

	void EnsureView()
	{
		if (g_view.DidCreateFail()) g_view.AllowCreateRetry();
		if (!g_view.IsHandleValid() && (!g_view.DidCreateFail() || g_view.CanRetryCreate())) {
			g_view.EnsureCreated("CommonwealthOnline/sleep/index.html", OnDomReady, true);
		}
	}

	bool Open(std::uint32_t a_token, std::uint32_t a_maxHours, ActionCallback a_callback)
	{
		if (!g_api || !g_view.IsHandleValid() || !g_view.IsDomReady() || g_phase != Phase::Closed || !a_callback || a_token == 0 || a_maxHours == 0 || a_maxHours > 24 ||
			F4T::PrismaUI::Internal::g_sessionLockoutActive) return false;
		g_token = a_token;
		g_maxHours = a_maxHours;
		g_hours = (std::min)(8U, g_maxHours);
		g_elapsedHours = 0.0F;
		g_callback = a_callback;
		g_phase = Phase::Choose;
		g_view.SetDesiredRole(PRISMA_UI_API::ViewRole::kPanel);
		g_view.SetDesired(true, true);
		g_view.ApplyState("SleepOpen");
		if (!g_view.IsFullyAppliedForVisibleSurface() || !g_view.IsCurrentlyFocused()) {
			Close();
			return false;
		}
		F4T::MenuInput::SetSleepActive(true);
		SyncEscapeOwnership();
		PushState();
		return true;
	}

	void SetPhase(Phase a_phase, std::uint32_t a_hours)
	{
		if (g_phase == Phase::Closed) return;
		if (a_phase == Phase::Closed) { Close(); return; }
		g_phase = a_phase;
		g_hours = (std::clamp)(a_hours, 1U, g_maxHours);
		g_elapsedHours = 0.0F;
		g_view.SetDesiredRole(PRISMA_UI_API::ViewRole::kPanel);
		g_view.SetDesired(true, true);
		g_view.ApplyState("SleepPhase");
		SyncEscapeOwnership();
		F4T::MenuInput::SetSleepActive(true);
		PushState();
	}

	void SetProgress(float a_elapsedHours)
	{
		if (g_phase != Phase::Resting || !std::isfinite(a_elapsedHours)) return;
		g_elapsedHours = (std::clamp)(a_elapsedHours, 0.0F, static_cast<float>(g_hours));
		PushState();
	}

	void SyncControllerActions()
	{
		if (g_view.IsDesiredVisible()) SyncEscapeOwnership();
	}

	void Close()
	{
		g_phase = Phase::Closed;
		g_callback = nullptr;
		PushState();
		g_view.SetDesired(false, false);
		g_view.ApplyState("SleepClose");
		SyncEscapeOwnership();
		F4T::MenuInput::SetSleepActive(false);
	}

	void Cancel()
	{
		if (g_phase == Phase::Closed) return;
		const auto callback = g_callback;
		const auto token = g_token;
		const auto hours = g_hours;
		Close();
		if (callback) callback(Action::Cancel, token, hours);
	}

	bool IsCurrentlyVisible() { return g_phase != Phase::Closed && g_view.IsCurrentlyVisible() && g_view.IsHandleValid(); }
	bool IsReady() { return g_view.IsFullyAppliedForVisibleSurface(); }

	bool TryDispatchControllerInput(const char* a_button, const char* a_phase)
	{
		if (g_phase == Phase::Closed) return false;
		if (!g_api || !IsCurrentlyVisible() || !IsReady() || F4T::PrismaUI::Internal::g_sessionLockoutActive) return true;
		const nlohmann::json button = a_button ? a_button : "";
		const nlohmann::json phase = a_phase ? a_phase : "";
		const auto script = std::format("if(window.coSleepController){{window.coSleepController({},{});}}", F4T::ViewJson::Dump(button), F4T::ViewJson::Dump(phase));
		g_api->Invoke(g_view.GetHandle(), script.c_str());
		return true;
	}
}

#include "F4TPrismaUIInternal.h"
#include "F4TViewJson.h"

#include "F4TMenuInput.h"
#include "F4TTradeView.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstring>
#include <exception>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::InteractWheelView
{
	namespace
	{
		using F4T::PrismaUI::Internal::g_api;
		using F4T::PrismaUI::Internal::g_sessionLockoutActive;
		using F4T::PrismaUI::Internal::PassthroughControllerActions;
		using F4T::PrismaUI::Internal::SyncPassthroughControllerActions;

		constexpr auto kHtmlPath = "CommonwealthOnline/interact-wheel/index.html";
		constexpr int kViewOrder = 650;
		constexpr std::size_t kStateBufferSize = 16 * 1024;
		constexpr std::size_t kErrorBufferSize = 512;
		constexpr std::size_t kActionMaxBytes = 1024;
		constexpr auto kDisabledStateJson = R"({"visible":false})";

		F4T::PrismaViewState::Controller g_viewState{ "InteractWheel" };
		PassthroughControllerActions g_passthrough;
		std::uint32_t g_openToken = 0;
		COMMONWEALTH_ONLINE_UI_API::InteractWheelCallbacks g_callbacks{};

		void LogInfo(std::string_view a_message)
		{
			REX::INFO("[COUI] [InteractWheel] {}", a_message);
		}

		void LogWarn(std::string_view a_message)
		{
			REX::WARN("[COUI] [InteractWheel] {}", a_message);
		}

		void RunOnGameThread(std::function<void()> a_task)
		{
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask(std::move(a_task));
				return;
			}
			LogWarn("Dropped interaction wheel task: game task interface unavailable.");
		}

		bool IsActionKnown(std::string_view a_op)
		{
			return a_op == "addFriend" || a_op == "acceptFriend" || a_op == "requestSent" ||
				a_op == "removeFriend" || a_op == "invite" || a_op == "mute" || a_op == "unmute" ||
				a_op == "trade" || a_op == "tradeAccept" || a_op == "tradeDecline" ||
				a_op == "tradeConfirm" || a_op == "tradeCancel" || a_op == "close";
		}

		void PushState()
		{
			if (!g_api || !g_viewState.IsDesiredVisible() || !g_viewState.IsDomReady() || !g_viewState.IsHandleValid()) {
				return;
			}
			std::string json(kDisabledStateJson);
			if (g_callbacks.GetStateJson) {
				std::string buffer(kStateBufferSize, '\0');
				std::string error(kErrorBufferSize, '\0');
				const auto ok = g_callbacks.GetStateJson(buffer.data(), buffer.size(), error.data(), error.size());
				const auto length = strnlen(buffer.data(), buffer.size());
				if (!ok) {
					LogWarn(std::format("State pull failed, showing not-available: {}", error.c_str()));
				} else if (length == buffer.size()) {
					LogWarn(std::format("State JSON filled the {} byte buffer, showing not-available.", kStateBufferSize));
				} else {
					json.assign(buffer.data(), length);
				}
			}
			try {
				const auto state = nlohmann::json::parse(json);
				if (state.contains("token")) g_openToken = state.value("token", 0U);
				const auto literal = F4T::ViewJson::Dump(state);
				const auto script = std::format("if(window.coInteractWheelSetState){{window.coInteractWheelSetState({});}}", literal);
				g_api->Invoke(g_viewState.GetHandle(), script.c_str());
			} catch (const std::exception& ex) {
				LogWarn(std::format("State push skipped: {}", ex.what()));
			}
		}

		void SyncEscapeOwnership()
		{
			const auto handle = g_viewState.GetHandle();
			if (handle == 0) {
				return;
			}
			auto* api = F4T::PrismaCapabilities::GetV12();
			if (!api || !api->IsValid(handle)) {
				return;
			}
			api->SetViewOwnsEscape(handle, g_viewState.IsDesiredVisible() && g_viewState.IsCurrentlyFocused());
			SyncPassthroughControllerActions(g_viewState, g_passthrough, g_viewState.IsCurrentlyVisible());
		}

		void CloseInternal(std::string_view a_reason)
		{
			if (!g_viewState.IsDesiredVisible()) {
				return;
			}
			LogInfo(std::format("Close reason={}", a_reason));
			g_viewState.SetDesired(false, false);
			g_viewState.ApplyState(a_reason);
			SyncEscapeOwnership();
			F4T::MenuInput::SetWheelActive(false);
			// Constraint: the plugin owns the open state, so every view-side close must tell it which open it ends.
			if (g_callbacks.SendAction) {
				g_callbacks.SendAction(std::format("{{\"op\":\"close\",\"token\":{}}}", g_openToken).c_str());
			}
		}

		void HandleAction(const std::string& a_json)
		{
			std::string op;
			try {
				op = nlohmann::json::parse(a_json).value("op", "");
			} catch (const std::exception& ex) {
				LogWarn(std::format("Dropped malformed wheel action: {}", ex.what()));
				return;
			}
			if (!IsActionKnown(op)) {
				LogWarn(std::format("Dropped wheel action with unknown op '{}'", op));
				return;
			}
			if (!g_callbacks.SendAction) {
				LogWarn(std::format("Dropped wheel action '{}': no action callback", op));
				return;
			}
			g_callbacks.SendAction(a_json.c_str());
		}

		void OnWheelAction(const char* a_json)
		{
			if (!a_json) {
				return;
			}
			if (strnlen(a_json, kActionMaxBytes + 1) > kActionMaxBytes) {
				LogWarn("Dropped wheel action larger than 1 KB.");
				return;
			}
			RunOnGameThread([payload = std::string(a_json)]() { HandleAction(payload); });
		}

		void FinishDomReady(PrismaView a_view)
		{
			if (!g_viewState.MarkDomReady(a_view) || !g_api) {
				return;
			}
			g_api->RegisterConsoleCallback(
				a_view,
				[](PrismaView, PRISMA_UI_API::ConsoleMessageLevel a_level, const char* a_message) {
					if (!a_message) {
						return;
					}
					switch (a_level) {
					case PRISMA_UI_API::ConsoleMessageLevel::Error:
						REX::CRITICAL("[COUI] [InteractWheel][JS] {}", a_message);
						break;
					case PRISMA_UI_API::ConsoleMessageLevel::Warning:
						REX::WARN("[COUI] [InteractWheel][JS] {}", a_message);
						break;
					default:
						REX::INFO("[COUI] [InteractWheel][JS] {}", a_message);
						break;
					}
				});
			g_api->BindUIEvent(a_view, "coInteractWheelAction", OnWheelAction);
			g_api->SetOrder(a_view, kViewOrder);
			g_viewState.ApplyState("DomReady");
			SyncEscapeOwnership();
			if (g_viewState.IsDesiredVisible()) {
				PushState();
			}
		}

		void OnDomReady(PrismaView a_view)
		{
			RunOnGameThread([a_view]() { FinishDomReady(a_view); });
		}

		void OpenInternal(const std::string& a_reason)
		{
			if (!g_api) {
				LogWarn("Open skipped: Prisma API unavailable.");
				return;
			}
			if (g_sessionLockoutActive) {
				return;
			}
			// Constraint: an open trade window takes the same input; the wheel never opens over it.
			if (F4T::TradeView::IsCurrentlyVisible()) {
				LogInfo(std::format("Open refused reason={}: trade window is open", a_reason));
				return;
			}
			if (g_viewState.IsDesiredVisible() && g_viewState.IsHandleValid()) {
				PushState();
				return;
			}
			LogInfo(std::format("Open reason={}", a_reason));
			g_viewState.SetDesired(true, true);
			if (g_viewState.EnsureCreated(kHtmlPath, OnDomReady, true) == 0) {
				LogWarn("Open failed: CreateView returned no handle.");
				g_viewState.SetDesired(false, false);
				return;
			}
			F4T::MenuInput::SetWheelActive(true);
			g_viewState.ApplyState(a_reason);
			SyncEscapeOwnership();
			if (g_viewState.IsDomReady()) {
				PushState();
			}
		}
	}

	void Initialize()
	{
		g_viewState.SetApi(g_api);
		g_viewState.SetFocusArgs(false, true);
		g_viewState.SetDesiredRole(PRISMA_UI_API::ViewRole::kPanel);
		g_viewState.SetDesired(false, false);
	}

	void SetCallbacks(const COMMONWEALTH_ONLINE_UI_API::InteractWheelCallbacks* a_callbacks)
	{
		RunOnGameThread([callbacks = a_callbacks ? *a_callbacks : COMMONWEALTH_ONLINE_UI_API::InteractWheelCallbacks{}]() {
			g_callbacks = callbacks;
		});
	}

	void Open(std::string_view a_reason)
	{
		RunOnGameThread([reason = std::string(a_reason)]() { OpenInternal(reason); });
	}

	void SyncControllerActions()
	{
		if (g_viewState.IsDesiredVisible()) SyncEscapeOwnership();
	}

	void Close(std::string_view a_reason)
	{
		CloseInternal(a_reason);
	}

	void QueueClose(std::string_view a_reason)
	{
		RunOnGameThread([reason = std::string(a_reason)]() { CloseInternal(reason); });
	}

	void NotifyStateChanged()
	{
		RunOnGameThread([]() { PushState(); });
	}

	bool IsVisible()
	{
		return g_viewState.IsDesiredVisible() || g_viewState.IsCurrentlyVisible();
	}

	bool IsCurrentlyVisible()
	{
		return g_viewState.IsCurrentlyVisible() && g_viewState.IsHandleValid();
	}

	void DispatchControllerInput(const char* a_button, const char* a_phase)
	{
		if (!g_api || !g_viewState.IsCurrentlyVisible() || !g_viewState.IsDomReady() || !g_viewState.IsHandleValid()) {
			return;
		}
		const auto script = std::format(
			"if(window.coInteractWheelController){{window.coInteractWheelController('{}', '{}');}}",
			a_button ? a_button : "",
			a_phase ? a_phase : "");
		g_api->Invoke(g_viewState.GetHandle(), script.c_str());
	}

	bool TryDispatchControllerInput(const char* a_button, const char* a_phase)
	{
		if (!IsCurrentlyVisible() || g_sessionLockoutActive) {
			return false;
		}
		DispatchControllerInput(a_button, a_phase);
		return true;
	}

	bool TryDispatchControllerAxes(float a_leftX, float a_leftY)
	{
		if (!IsCurrentlyVisible() || g_sessionLockoutActive) {
			return false;
		}
		DispatchControllerAxes(a_leftX, a_leftY);
		return true;
	}

	void DispatchControllerAxes(float a_leftX, float a_leftY)
	{
		if (!g_api || !g_viewState.IsCurrentlyVisible() || !g_viewState.IsDomReady() || !g_viewState.IsHandleValid()) {
			return;
		}
		const auto script = std::format("if(window.coInteractWheelAxes){{window.coInteractWheelAxes({:.4f}, {:.4f});}}", a_leftX, a_leftY);
		g_api->Invoke(g_viewState.GetHandle(), script.c_str());
	}
}

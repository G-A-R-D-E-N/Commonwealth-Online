#include "F4TPrismaUIInternal.h"
#include "F4TViewJson.h"
#include "F4TParkedSuitView.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstring>
#include <exception>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::ParkedSuitView
{
	namespace
	{
		using F4T::PrismaUI::Internal::g_api;
		using F4T::PrismaUI::Internal::g_prismaReady;
		using F4T::PrismaUI::Internal::g_sessionLockoutActive;

		constexpr auto kHtmlPath = "CommonwealthOnline/parked-suit/index.html";
		constexpr auto kEntryPoint = "coParkedSuitSetState";
		constexpr int kViewOrder = 652;
		constexpr std::size_t kStateBufferSize = 32 * 1024;
		constexpr std::size_t kErrorBufferSize = 512;
		constexpr std::size_t kActionMaxBytes = 1024;
		constexpr auto kFallbackStateJson = R"({"open":true,"suits":[]})";

		F4T::PrismaViewState::Controller g_view{ "ParkedSuit" };
		COMMONWEALTH_ONLINE_UI_API::ParkedSuitCallbacks g_callbacks{};

		void LogWarn(std::string_view a_message)
		{
			REX::WARN("[COUI] [ParkedSuit] {}{}", "", a_message);
		}

		void RunOnGameThread(std::function<void()> a_task)
		{
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask(std::move(a_task));
				return;
			}
			LogWarn("Dropped parked suit menu task: game task interface unavailable.");
		}

		void SyncEscapeOwnership()
		{
			const auto handle = g_view.GetHandle();
			if (handle == 0) {
				return;
			}
			auto* api = F4T::PrismaCapabilities::GetV12();
			if (!api || !api->IsValid(handle)) {
				return;
			}
			api->SetViewOwnsEscape(handle, g_view.IsDesiredVisible() && g_view.IsCurrentlyFocused());
		}

		void PushState()
		{
			if (!g_api || !g_view.IsDesiredVisible() || !g_view.IsDomReady() || !g_view.IsHandleValid()) {
				return;
			}
			std::string json(kFallbackStateJson);
			if (g_callbacks.GetStateJson) {
				std::string buffer(kStateBufferSize, '\0');
				std::string error(kErrorBufferSize, '\0');
				const auto ok = g_callbacks.GetStateJson(buffer.data(), buffer.size(), error.data(), error.size());
				const auto length = strnlen(buffer.data(), buffer.size());
				if (!ok) {
					LogWarn(std::format("State pull failed: {}", error.c_str()));
				} else if (length == buffer.size()) {
					LogWarn(std::format("State JSON filled the {} byte buffer, showing no suits.", kStateBufferSize));
				} else {
					json.assign(buffer.data(), length);
				}
			}
			std::string script;
			try {
				const auto state = nlohmann::json::parse(json);
				script = std::format("if(window.{0}){{window.{0}({1});}}", kEntryPoint, F4T::ViewJson::Dump(state));
			} catch (const std::exception& ex) {
				LogWarn(std::format("State push skipped: {}", ex.what()));
				return;
			}
			g_api->Invoke(g_view.GetHandle(), script.c_str());
		}

		void CloseInternal(std::string_view a_reason)
		{
			if (!g_view.IsDesiredVisible()) {
				return;
			}
			if (g_api && g_view.IsHandleValid() && g_view.IsDomReady()) {
				g_api->Invoke(g_view.GetHandle(), "if(window.coParkedSuitSetState){window.coParkedSuitSetState({open:false,suits:[]});}");
			}
			g_view.SetDesired(false, false);
			g_view.ApplyState(a_reason);
			SyncEscapeOwnership();
		}

		void HandleAction(const std::string& a_payload)
		{
			const auto parsed = nlohmann::json::parse(a_payload, nullptr, false);
			if (!parsed.is_object()) {
				return;
			}
			const auto op = parsed.value("op", std::string{});
			if (op == "close") {
				CloseInternal("ParkedSuitClose");
			} else if (op != "breachStart" && op != "hack" && op != "breachFailed" && op != "clearTarget" && op != "remove") {
				return;
			}
			if (g_callbacks.SendAction) {
				g_callbacks.SendAction(a_payload.c_str());
			}
		}

		void OnAction(const char* a_payload)
		{
			if (!a_payload) {
				return;
			}
			const auto length = strnlen(a_payload, kActionMaxBytes + 1);
			if (length > kActionMaxBytes) {
				return;
			}
			RunOnGameThread([payload = std::string(a_payload, length)] { HandleAction(payload); });
		}

		void OnDomReady(PrismaView a_view)
		{
			RunOnGameThread([a_view] {
				if (!g_api || !g_view.MarkDomReady(a_view)) {
					return;
				}
				g_api->BindUIEvent(a_view, "coParkedSuitAction", OnAction);
				g_api->SetOrder(a_view, kViewOrder);
				g_view.ApplyState("ParkedSuitDomReady");
				SyncEscapeOwnership();
				PushState();
			});
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
		if (g_view.DidCreateFail()) {
			g_view.AllowCreateRetry();
		}
		if (!g_view.IsHandleValid() && (!g_view.DidCreateFail() || g_view.CanRetryCreate())) {
			g_view.EnsureCreated(kHtmlPath, OnDomReady, g_prismaReady);
		}
	}

	void SetCallbacks(const COMMONWEALTH_ONLINE_UI_API::ParkedSuitCallbacks* a_callbacks)
	{
		RunOnGameThread([callbacks = a_callbacks ? *a_callbacks : COMMONWEALTH_ONLINE_UI_API::ParkedSuitCallbacks{}] {
			g_callbacks = callbacks;
		});
	}

	void Open(std::string_view a_reason)
	{
		RunOnGameThread([reason = std::string(a_reason)] {
			if (!g_api || g_sessionLockoutActive) {
				return;
			}
			g_view.SetDesired(true, true);
			if (g_view.EnsureCreated(kHtmlPath, OnDomReady, true) == 0) {
				LogWarn("Open failed: CreateView returned no handle.");
				g_view.SetDesired(false, false);
				return;
			}
			g_view.ApplyState(reason);
			SyncEscapeOwnership();
			if (g_view.IsDomReady()) {
				PushState();
			}
		});
	}

	void QueueClose(std::string_view a_reason)
	{
		RunOnGameThread([reason = std::string(a_reason)] { CloseInternal(reason); });
	}

	void NotifyStateChanged()
	{
		RunOnGameThread([] { PushState(); });
	}

	void SyncControllerActions()
	{
		if (g_view.IsDesiredVisible()) {
			SyncEscapeOwnership();
		}
	}

	bool IsVisible()
	{
		return g_view.IsDesiredVisible() || g_view.IsCurrentlyVisible();
	}

	bool IsCurrentlyVisible()
	{
		return g_view.IsCurrentlyVisible() && g_view.IsHandleValid();
	}
}

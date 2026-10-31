#include "F4TPrismaUIInternal.h"

#include "SessionCallbacks.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstring>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::CharacterSelectView
{
	namespace
	{
		using F4T::PrismaUI::Internal::g_api;

		constexpr auto kSelectHtmlPath = "CommonwealthOnline/character-select/index.html";
		constexpr int  kSelectViewOrder = 570;
		constexpr std::size_t kStateBufferSize = 32 * 1024;
		constexpr std::size_t kErrorBufferSize = 512;
		constexpr std::size_t kActionMaxBytes = 1024;
		constexpr auto kStateEntryPoint = "coCharacterSelectSetState";
		constexpr auto kHideEntryPoint = "coCharacterSelectHide";
		constexpr auto kActionEvent = "coCharacterSelectAction";
		constexpr std::string_view kDisabledStateJson = R"({"enabled":false})";
		constexpr unsigned kTickPullDivisor = 30;

		void OnDomReady(PrismaView a_view);
		void FinishDomReady(PrismaView a_view);

		F4T::PrismaViewState::Controller g_viewState{ "CharacterSelect" };
		COMMONWEALTH_ONLINE_UI_API::FactionCallbacks g_callbacks{};
		bool g_lastNeedsSelection = false;
		unsigned g_tickCounter = 0;
		bool g_createFailLogged = false;

		void LogInfo(std::string_view a_message)
		{
			REX::INFO("[COUI] [CharacterSelect] {}", a_message);
		}

		void LogWarn(std::string_view a_message)
		{
			REX::WARN("[COUI] [CharacterSelect] {}", a_message);
		}

		void RunOnGameThread(std::function<void()> a_task)
		{
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask(std::move(a_task));
				return;
			}
			LogWarn("Dropped character select task - game task interface unavailable.");
		}

		bool IsActionKnown(std::string_view a_op)
		{
			return a_op == "slotList" || a_op == "slotCreate" || a_op == "slotSelect" || a_op == "slotDelete" ||
				a_op == "slotRestore" || a_op == "slotDeleteCancel" || a_op == "refresh" || a_op == "close" || a_op == "disconnect";
		}

		bool PullStateJson(std::string& a_out)
		{
			if (!g_callbacks.GetStateJson) {
				return false;
			}
			std::string buffer(kStateBufferSize, '\0');
			std::string error(kErrorBufferSize, '\0');
			const auto ok = g_callbacks.GetStateJson(buffer.data(), buffer.size(), error.data(), error.size());
			const auto length = strnlen(buffer.data(), buffer.size());
			if (!ok || length == buffer.size()) {
				return false;
			}
			a_out.assign(buffer.data(), length);
			return true;
		}

		bool NeedsSelectionIn(std::string_view a_stateJson) noexcept
		{
			try {
				const auto state = nlohmann::json::parse(a_stateJson);
				if (!state.is_object()) {
					return false;
				}
				const auto slots = state.find("characterSlots");
				if (slots == state.end() || !slots->is_object()) {
					return false;
				}
				return slots->value("needsSelection", false) == true || slots->value("loading", false) == true;
			} catch (...) {
				return false;
			}
		}

		void PushStateLocked(const std::string& a_json)
		{
			if (!g_api || !g_viewState.IsHandleValid() || !g_viewState.IsDomReady()) {
				return;
			}
			try {
				const auto literal = nlohmann::json(a_json).dump();
				const auto script = std::format(
					"if(window.{0}){{window.{0}({1});}}",
					kStateEntryPoint,
					literal);
				g_api->Invoke(g_viewState.GetHandle(), script.c_str());
			} catch (const std::exception& ex) {
				LogWarn(std::format("State push skipped: {}", ex.what()));
			}
		}

		void PushState()
		{
			if (!g_viewState.IsDesiredVisible()) {
				return;
			}
			std::string json{ kDisabledStateJson };
			if (!PullStateJson(json)) {
				json.assign(kDisabledStateJson);
			}
			PushStateLocked(json);
		}

		void HideView()
		{
			if (!g_api || !g_viewState.IsHandleValid() || !g_viewState.IsDomReady()) {
				return;
			}
			const auto script = std::format("if(window.{0}){{window.{0}();}}", kHideEntryPoint);
			g_api->Invoke(g_viewState.GetHandle(), script.c_str());
		}

		void OpenInternal(const std::string& a_reason)
		{
			LogInfo(std::format("Open reason={}", a_reason));
			if (!g_api) {
				LogWarn("Open skipped - Prisma API unavailable.");
				return;
			}
			if (g_viewState.IsDesiredVisible() && g_viewState.IsHandleValid()) {
				PushState();
				return;
			}
			g_viewState.SetDesired(true, true);
			if (g_viewState.EnsureCreated(kSelectHtmlPath, OnDomReady, true) == 0) {
				if (!g_createFailLogged) {
					g_createFailLogged = true;
					LogWarn("Open failed - CreateView returned no handle.");
				}
				g_viewState.SetDesired(false, false);
				return;
			}
			g_viewState.ApplyState(a_reason);
			if (g_viewState.IsDomReady()) {
				PushState();
			}
		}

		void CloseInternal(const std::string& a_reason)
		{
			if (!g_viewState.IsDesiredVisible()) {
				return;
			}
			LogInfo(std::format("Close reason={}", a_reason));
			HideView();
			g_viewState.SetDesired(false, false);
			g_viewState.ApplyState(a_reason);
		}

		void HandleAction(const std::string& a_json)
		{
			std::string op;
			try {
				op = nlohmann::json::parse(a_json).value("op", "");
			} catch (const std::exception& ex) {
				LogWarn(std::format("Dropped malformed character select action: {}", ex.what()));
				return;
			}
			if (!IsActionKnown(op)) {
				LogWarn(std::format("Dropped character select action with unknown op '{}'", op));
				return;
			}
			if (op == "close" || op == "disconnect") {
				if (op == "disconnect" || g_lastNeedsSelection) {
					COUI::Session::Disconnect();
				}
				CloseInternal("CharacterSelectActionClose");
				return;
			}
			if (op == "refresh") {
				PushState();
			}
			if (!g_callbacks.SendAction) {
				LogWarn(std::format("Dropped character select action '{}': no action callback", op));
				return;
			}
			g_callbacks.SendAction(a_json.c_str());
		}

		void OnCharacterSelectAction(const char* a_json)
		{
			if (!a_json) {
				return;
			}
			if (strnlen(a_json, kActionMaxBytes + 1) > kActionMaxBytes) {
				LogWarn("Dropped character select action larger than 1 KB.");
				return;
			}
			RunOnGameThread([payload = std::string(a_json)]() {
				HandleAction(payload);
			});
		}

		void OnDomReady(PrismaView a_view)
		{
			RunOnGameThread([a_view]() {
				FinishDomReady(a_view);
			});
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
						REX::CRITICAL("[COUI] [CharacterSelect][JS] {}", a_message);
						break;
					case PRISMA_UI_API::ConsoleMessageLevel::Warning:
						REX::WARN("[COUI] [CharacterSelect][JS] {}", a_message);
						break;
					default:
						REX::INFO("[COUI] [CharacterSelect][JS] {}", a_message);
						break;
					}
				});
			g_api->BindUIEvent(a_view, kActionEvent, OnCharacterSelectAction);
			g_api->SetOrder(a_view, kSelectViewOrder);
			g_viewState.ApplyState("DomReady");
			if (g_viewState.IsDesiredVisible()) {
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

	void SetCallbacks(const COMMONWEALTH_ONLINE_UI_API::FactionCallbacks* a_callbacks)
	{
		RunOnGameThread([callbacks = a_callbacks ? *a_callbacks : COMMONWEALTH_ONLINE_UI_API::FactionCallbacks{}]() {
			g_callbacks = callbacks;
		});
	}

	void NotifyStateChanged()
	{
		RunOnGameThread([]() {
			PushState();
		});
	}

	void Tick()
	{
		if (!g_api || !g_callbacks.GetStateJson) {
			return;
		}
		if ((g_tickCounter++ % kTickPullDivisor) != 0) {
			return;
		}
		std::string json;
		if (!PullStateJson(json)) {
			return;
		}
		const bool needsSelection = NeedsSelectionIn(json);
		g_lastNeedsSelection = needsSelection;
		if (needsSelection && !g_viewState.IsDesiredVisible()) {
			RunOnGameThread([]() {
				OpenInternal("SlotGate");
			});
		} else if (!needsSelection && g_viewState.IsDesiredVisible()) {
			RunOnGameThread([]() {
				CloseInternal("SlotGateCleared");
			});
		} else if (g_viewState.IsDesiredVisible()) {
			PushStateLocked(json);
		}
	}

	bool IsVisible()
	{
		return g_viewState.IsDesiredVisible() || g_viewState.IsCurrentlyVisible();
	}

	bool IsCurrentlyVisible()
	{
		return g_viewState.IsCurrentlyVisible() && g_viewState.IsHandleValid();
	}

	void Close(std::string_view a_reason)
	{
		CloseInternal(std::string{ a_reason });
	}
}

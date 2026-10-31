#include "pch.h"

#include "F4TJoinOverlayBridge.h"
#include "F4TPrismaUIInternal.h"
#include "F4TPrismaViewState.h"

#include <nlohmann/json.hpp>

#include <cstring>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::JoinOverlay
{
	namespace
	{
		using F4T::PrismaUI::Internal::g_api;
		using F4T::PrismaUI::Internal::g_prismaReady;

		constexpr auto kOverlayHtmlPath = "CommonwealthOnline/join-overlay/index.html";
		constexpr auto kStateEntryPoint = "coJoinOverlaySetState";
		constexpr auto kHideEntryPoint = "coJoinOverlayHide";
		constexpr int  kOverlayViewOrder = 560;
		constexpr std::size_t kMaxJsonBytes = 4 * 1024;
		constexpr std::size_t kMaxStateLength = 32;
		constexpr std::size_t kMaxStatusLength = 96;

		struct State
		{
			std::string state{};
			std::string status{};
			double      progress{ 0.0 };
			bool        visible{ false };
			bool        dismissible{ false };
		};

		F4T::PrismaViewState::Controller g_view{ "JoinOverlay" };
		State                        g_state{};
		std::string                  g_lastAcceptedJson;
		std::string                  g_lastScript;
		PrismaView                   g_lastScriptHandle{ 0 };
		bool                         g_parseWarnLogged = false;
		bool                         g_createFailLogged = false;

		void LogInfo(std::string_view a_message)
		{
			REX::INFO("[COUI] [JoinOverlay] {}", a_message);
		}

		void LogWarn(std::string_view a_message)
		{
			REX::WARN("[COUI] [JoinOverlay] {}", a_message);
		}

		void RunOnGameThread(std::function<void()> a_task)
		{
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask(std::move(a_task));
				return;
			}
			LogWarn("Dropped join overlay task - game task interface unavailable.");
		}

		bool IsPrintableAscii(char a_ch) noexcept
		{
			const auto value = static_cast<unsigned char>(a_ch);
			return value >= 0x20 && value <= 0x7E;
		}

		std::string_view ReadString(const nlohmann::json& a_json, const char* a_key) noexcept
		{
			if (!a_json.is_object()) {
				return {};
			}
			const auto entry = a_json.find(a_key);
			if (entry == a_json.end() || !entry->is_string()) {
				return {};
			}
			return entry->get_ref<const std::string&>();
		}

		bool ReadFlag(const nlohmann::json& a_json, const char* a_key, bool a_default) noexcept
		{
			if (!a_json.is_object()) {
				return a_default;
			}
			const auto entry = a_json.find(a_key);
			if (entry == a_json.end() || !entry->is_boolean()) {
				return a_default;
			}
			return entry->get<bool>();
		}

		double ReadUnitInterval(const nlohmann::json& a_json, const char* a_key) noexcept
		{
			if (!a_json.is_object()) {
				return 0.0;
			}
			const auto entry = a_json.find(a_key);
			if (entry == a_json.end() || !entry->is_number()) {
				return 0.0;
			}
			const auto value = entry->get<double>();
			if (!(value > 0.0)) {
				return 0.0;
			}
			return value > 1.0 ? 1.0 : value;
		}

		std::string SanitizeText(std::string_view a_text, std::size_t a_maxLength)
		{
			std::string sanitized;
			sanitized.reserve(a_text.size() < a_maxLength ? a_text.size() : a_maxLength);
			for (const auto ch : a_text) {
				if (sanitized.size() >= a_maxLength) {
					break;
				}
				if (IsPrintableAscii(ch)) {
					sanitized.push_back(ch);
				}
			}
			while (!sanitized.empty() && sanitized.back() == ' ') {
				sanitized.pop_back();
			}
			return sanitized;
		}

		bool IsKnownState(std::string_view a_state) noexcept
		{
			return a_state == "Connecting" ||
				a_state == "Authenticating" ||
				a_state == "LoadingWorld" ||
				a_state == "FlyOver" ||
				a_state == "Spawning" ||
				a_state == "Ready" ||
				a_state == "Failed" ||
				a_state == "Cancelled";
		}

		bool IsTerminalState(std::string_view a_state) noexcept
		{
			return a_state == "Ready" || a_state == "Failed" || a_state == "Cancelled";
		}

		bool IsOverlayVisible() noexcept
		{
			return g_state.visible && IsKnownState(g_state.state) && !IsTerminalState(g_state.state);
		}

		std::optional<State> ParseState(std::string_view a_json)
		{
			if (a_json.empty() || a_json.size() > kMaxJsonBytes) {
				return std::nullopt;
			}

			const auto parsed = nlohmann::json::parse(a_json.begin(), a_json.end(), nullptr, false);
			if (parsed.is_discarded() || !parsed.is_object()) {
				return std::nullopt;
			}

			State state{};
			state.state = SanitizeText(ReadString(parsed, "state"), kMaxStateLength);
			if (!IsKnownState(state.state)) {
				return std::nullopt;
			}

			state.status = SanitizeText(ReadString(parsed, "status"), kMaxStatusLength);
			state.progress = ReadUnitInterval(parsed, "progress");
			state.visible = ReadFlag(parsed, "visible", true);
			state.dismissible = ReadFlag(parsed, "dismissible", false);
			return state;
		}

		std::string FormatSetStateScript(const State& a_state)
		{
			nlohmann::json payload{
				{ "state", a_state.state },
				{ "status", a_state.status },
				{ "progress", a_state.progress },
				{ "visible", a_state.visible },
				{ "dismissible", a_state.dismissible }
			};
			return std::format(
				"if(window.{0}){{window.{0}({1});}}",
				kStateEntryPoint,
				payload.dump());
		}

		std::string FormatHideScript()
		{
			return std::format("if(window.{0}){{window.{0}();}}", kHideEntryPoint);
		}

		void PushToView()
		{
			if (!g_api || !g_view.IsHandleValid() || !g_view.IsDomReady()) {
				return;
			}
			const auto handle = g_view.GetHandle();
			auto script = IsOverlayVisible() ? FormatSetStateScript(g_state) : FormatHideScript();
			if (handle == g_lastScriptHandle && script == g_lastScript) {
				return;
			}
			g_api->Invoke(handle, script.c_str());
			g_lastScriptHandle = handle;
			g_lastScript = std::move(script);
		}

		void ReconcileView();

		void FinishDomReady(PrismaView a_view)
		{
			if (!g_view.MarkDomReady(a_view) || !g_api) {
				return;
			}
			g_view.AssignRoleIfNeeded();
			g_view.ApplyState("JoinOverlayDomReady");
			g_lastScriptHandle = 0;
			g_lastScript.clear();
			PushToView();
		}

		void OnDomReady(PrismaView a_view)
		{
			RunOnGameThread([a_view]() {
				FinishDomReady(a_view);
			});
		}

		void ReconcileView()
		{
			if (!g_api) {
				return;
			}

			const bool wanted = IsOverlayVisible();
			if (wanted && !g_view.IsHandleValid()) {
				const auto handle = g_view.EnsureCreated(kOverlayHtmlPath, OnDomReady, g_prismaReady);
				if (handle != 0) {
					g_api->SetOrder(handle, kOverlayViewOrder);
					g_view.AssignRoleIfNeeded();
				} else if (g_view.DidCreateFail() && !g_createFailLogged) {
					g_createFailLogged = true;
					LogWarn("Join overlay view unavailable; overlay stays hidden.");
				}
			}

			if (wanted != g_view.IsDesiredVisible()) {
				g_view.SetDesiredVisible(wanted);
				g_view.ApplyState(wanted ? "JoinOverlayShow" : "JoinOverlayHide");
			}

			if (wanted &&
				g_view.IsDomReady() &&
				g_view.IsDesiredVisible() &&
				!g_view.IsCurrentlyVisible()) {
				g_view.ApplyState("JoinOverlayPendingShow");
			}

			PushToView();
		}

		void ApplyState(const std::string& a_json)
		{
			if (a_json == g_lastAcceptedJson) {
				return;
			}

			const auto parsed = ParseState(a_json);
			if (!parsed) {
				if (!g_parseWarnLogged) {
					g_parseWarnLogged = true;
					LogWarn("Kept previous join overlay state: malformed or unrecognized push.");
				}
				return;
			}

			g_parseWarnLogged = false;
			g_lastAcceptedJson = a_json;
			g_state = *parsed;

			LogInfo(std::format(
				"State '{}' visible={} progress={:.2f} dismissible={}",
				g_state.state,
				IsOverlayVisible(),
				g_state.progress,
				g_state.dismissible));

			PushToView();
			ReconcileView();
		}
	}

	void Initialize()
	{
		g_view.SetApi(g_api);
		g_view.SetFocusArgs(false, true);
		g_view.SetDesiredRole(PRISMA_UI_API::ViewRole::kWidget);
		g_view.SetDesired(false, false);
	}

	void EnsureView()
	{
		ReconcileView();
	}

	void AllowCreateRetry()
	{
		if (g_view.DidCreateFail()) {
			g_view.AllowCreateRetry();
			g_createFailLogged = false;
		}
	}

	bool DidCreateFail() noexcept
	{
		return g_view.DidCreateFail();
	}

	void PushStateJson(const char* a_stateJson)
	{
		if (!a_stateJson) {
			return;
		}
		if (strnlen(a_stateJson, kMaxJsonBytes + 1) > kMaxJsonBytes) {
			LogWarn("Dropped join overlay state larger than 4 KiB.");
			return;
		}
		RunOnGameThread([payload = std::string(a_stateJson)]() {
			ApplyState(payload);
		});
	}

	void Hide()
	{
		RunOnGameThread([]() {
			g_lastAcceptedJson.clear();
			g_state = State{};
			PushToView();
			ReconcileView();
		});
	}
}

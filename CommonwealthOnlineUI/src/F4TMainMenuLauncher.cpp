#include "pch.h"

#include "F4TMainMenuLauncher.h"

#include "F4TLauncherEventProvenance.h"
#include "F4TPrismaCapabilities.h"
#include "F4TPrismaUI.h"
#include "F4TPrismaViewState.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>

namespace F4T::MainMenuLauncher
{
	namespace
	{
		constexpr auto kHtmlPath = "CommonwealthOnline/launcher/index.html";
		constexpr int kViewOrder = 300;

		PRISMA_UI_API::IVPrismaUI6* g_api = nullptr;
		F4T::PrismaViewState::Controller g_viewState{ "MainMenuLauncher" };
		bool g_runtimeReady = false;
		bool g_pointerFocusActive = false;
		bool g_launcherRetryUsed = false;

		// Deque keeps element addresses stable across push_back; entries are immutable.
		std::deque<EventSource> g_bindings;
		std::uint64_t g_currentGeneration = 0;

		void DispatchToGameThread(std::function<void()> a_task)
		{
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask(std::move(a_task));
			}
		}

		EventSourceState CurrentEventState() noexcept
		{
			return EventSourceState{
				g_viewState.GetHandle(),
				g_currentGeneration,
				g_viewState.IsHandleValid(),
				g_viewState.IsDesiredVisible(),
				g_runtimeReady
			};
		}

		bool IsCurrentBinding(const EventSource& a_source) noexcept
		{
			return IsCurrentEventSource(a_source, CurrentEventState());
		}

		const EventSource* FindEventSource(std::uint64_t a_generation) noexcept
		{
			for (const auto& binding : g_bindings) {
				if (binding.generation == a_generation) {
					return &binding;
				}
			}
			return nullptr;
		}

		void ClearInputRegion()
		{
			if (!F4T::PrismaCapabilities::HasInputRegions()) {
				return;
			}
			auto* api = F4T::PrismaCapabilities::GetV10();
			if (!api || !g_viewState.IsHandleValid()) {
				return;
			}
			api->SetInputRegions(g_viewState.GetHandle(), nullptr, 0);
		}

		void ApplyInputRegion(const EventSource& a_source, const nlohmann::json& a_region)
		{
			if (!IsCurrentBinding(a_source) ||
				!F4T::PrismaCapabilities::HasInputRegions()) {
				return;
			}
			auto* api = F4T::PrismaCapabilities::GetV10();
			if (!api) {
				return;
			}

			const bool active = a_region.value("active", false);
			const PRISMA_UI_API::InputRegion region{
				a_region.value("left", 0),
				a_region.value("top", 0),
				a_region.value("width", 0),
				a_region.value("height", 0)
			};
			api->SetInputRegions(a_source.view, active ? &region : nullptr, active ? 1u : 0u);
		}

		void ApplyPointerFocus(const EventSource& a_source, bool a_focus)
		{
			if (!IsCurrentBinding(a_source)) {
				return;
			}
			if (a_focus == g_pointerFocusActive) {
				return;
			}
			g_pointerFocusActive = a_focus;
			g_viewState.SetDesiredFocused(a_focus);
			g_viewState.ApplyState(a_focus ? "LauncherPointerEnter" : "LauncherPointerLeave");
		}

		void ApplyRestore(const EventSource& a_source)
		{
			if (!IsCurrentBinding(a_source)) {
				return;
			}
			F4T::PrismaUI::RestoreMainMenuFromSinglePlayer();
		}

		// Prisma V11/V12 marshals the callback to the game thread with the emitting binding.
		void OnHitRegionBound(const char* a_json, void* a_userdata)
		{
			if (!a_json || !a_userdata) {
				return;
			}
			try {
				ApplyInputRegion(*static_cast<const EventSource*>(a_userdata), nlohmann::json::parse(a_json));
			} catch (...) {
			}
		}

		void OnPointerFocusBound(const char* a_json, void* a_userdata)
		{
			if (!a_json || !a_userdata) {
				return;
			}
			try {
				ApplyPointerFocus(*static_cast<const EventSource*>(a_userdata), nlohmann::json::parse(a_json).value("focus", false));
			} catch (...) {
			}
		}

		void OnActionBound(const char* a_json, void* a_userdata)
		{
			if (!a_json || !a_userdata) {
				return;
			}
			try {
				const auto payload = nlohmann::json::parse(a_json);
				if (payload.value("action", "") == "restore") {
					ApplyRestore(*static_cast<const EventSource*>(a_userdata));
				}
			} catch (...) {
			}
		}

		// Prisma V10 fallback: the payload's immutable generation token resolves to a binding.
		std::uint64_t ParseGeneration(const nlohmann::json& a_payload) noexcept
		{
			if (!a_payload.is_object()) {
				return 0;
			}
			const auto it = a_payload.find("generation");
			if (it == a_payload.end() || !it->is_number_unsigned()) {
				return 0;
			}
			return it->get<std::uint64_t>();
		}

		void OnHitRegionUnbound(const char* a_json)
		{
			if (!a_json) {
				return;
			}
			try {
				const auto region = nlohmann::json::parse(a_json);
				const auto generation = ParseGeneration(region);
				DispatchToGameThread([generation, region]() {
					if (const auto* source = FindEventSource(generation)) {
						ApplyInputRegion(*source, region);
					}
				});
			} catch (...) {
			}
		}

		void OnPointerFocusUnbound(const char* a_json)
		{
			if (!a_json) {
				return;
			}
			try {
				const auto payload = nlohmann::json::parse(a_json);
				const auto generation = ParseGeneration(payload);
				const bool focus = payload.value("focus", false);
				DispatchToGameThread([generation, focus]() {
					if (const auto* source = FindEventSource(generation)) {
						ApplyPointerFocus(*source, focus);
					}
				});
			} catch (...) {
			}
		}

		void OnActionUnbound(const char* a_json)
		{
			if (!a_json) {
				return;
			}
			try {
				const auto payload = nlohmann::json::parse(a_json);
				const auto generation = ParseGeneration(payload);
				if (payload.value("action", "") != "restore") {
					return;
				}
				DispatchToGameThread([generation]() {
					if (const auto* source = FindEventSource(generation)) {
						ApplyRestore(*source);
					}
				});
			} catch (...) {
			}
		}

		template <typename TBound, typename TUnbound>
		void BindEvent(PrismaView a_view, const char* a_event, TBound a_bound, TUnbound a_unbound, EventSource* a_binding)
		{
			if (auto* apiV11 = F4T::PrismaCapabilities::GetV11()) {
				if (apiV11->BindGameThreadUIEvent(a_view, a_event, a_bound, a_binding)) {
					return;
				}
			}
			g_api->BindUIEvent(a_view, a_event, a_unbound);
		}

		void BindDomHandlers(PrismaView a_view)
		{
			const auto generation = ++g_currentGeneration;
			g_bindings.push_back(EventSource{ a_view, generation });
			auto* binding = &g_bindings.back();

			const auto tokenScript =
				std::string("if(window.setLauncherGeneration){window.setLauncherGeneration(") +
				std::to_string(generation) + ");}";
			g_api->Invoke(a_view, tokenScript.c_str());

			BindEvent(a_view, "coLauncherHitRegion", &OnHitRegionBound, &OnHitRegionUnbound, binding);
			BindEvent(a_view, "coLauncherPointerFocus", &OnPointerFocusBound, &OnPointerFocusUnbound, binding);
			BindEvent(a_view, "coLauncherAction", &OnActionBound, &OnActionUnbound, binding);
		}

		void FinishDomReady(PrismaView a_view)
		{
			if (!g_viewState.MarkDomReady(a_view) || !g_api) {
				return;
			}
			BindDomHandlers(a_view);
			g_viewState.ApplyState("DomReady");
			g_api->Invoke(a_view, "if(window.updateLauncherRegion){window.updateLauncherRegion();}");
		}

		void OnDomReady(PrismaView a_view)
		{
			const auto* taskInterface = F4SE::GetTaskInterface();
			if (!taskInterface) {
				return;
			}
			taskInterface->AddTask([a_view]() {
				FinishDomReady(a_view);
			});
		}
	}

	void Initialize(PRISMA_UI_API::IVPrismaUI6* a_api)
	{
		g_api = a_api;
		g_viewState.SetApi(a_api);
		g_viewState.SetFocusArgs(false, true);
		g_viewState.SetDesiredRole(PRISMA_UI_API::ViewRole::kWidget);
		g_runtimeReady = true;
	}

	void EnsureView()
	{
		if (!g_api || !g_runtimeReady) {
			return;
		}
		g_viewState.SetDesiredRole(PRISMA_UI_API::ViewRole::kWidget);
		if (g_viewState.DidCreateFail() && !g_launcherRetryUsed) {
			g_viewState.AllowCreateRetry();
			g_launcherRetryUsed = true;
		}
		const auto previousHandle = g_viewState.GetHandle();
		const auto handle = g_viewState.EnsureCreated(kHtmlPath, OnDomReady, g_runtimeReady);
		if (handle != 0 && handle != previousHandle) {
			g_api->SetOrder(handle, kViewOrder);
			g_viewState.AssignRoleIfNeeded();
		}
	}

	void Reconcile(bool a_wanted, std::string_view a_reason)
	{
		if (a_wanted && !F4T::PrismaCapabilities::HasInputRegions()) {
			Hide(a_reason);
			return;
		}
		if (a_wanted) {
			EnsureView();
		}
		if (!g_viewState.IsHandleValid()) {
			return;
		}
		g_viewState.SetDesired(a_wanted, a_wanted && g_pointerFocusActive);
		const auto result = g_viewState.ApplyState(a_reason);
		if (!a_wanted) {
			g_pointerFocusActive = false;
			ClearInputRegion();
			return;
		}
		if (result.showRequested && g_viewState.IsDomReady()) {
			g_api->Invoke(g_viewState.GetHandle(), "if(window.updateLauncherRegion){window.updateLauncherRegion();}");
		}
	}

	void Hide(std::string_view a_reason)
	{
		g_pointerFocusActive = false;
		g_viewState.SetDesired(false, false);
		g_viewState.ApplyState(a_reason);
		ClearInputRegion();
	}
}

#include "F4TPrismaUIInternal.h"
#include "F4TViewJson.h"
#include "F4TResourceViewHost.h"

#include "F4TPrismaCapabilities.h"
#include "F4TPrismaViewState.h"
#include "F4TStateBagProtocol.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <deque>
#include <cstring>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace F4T::ResourceViewHost
{
	namespace
	{
		using F4T::PrismaUI::Internal::g_api;
		namespace StateBag = F4T::StateBagProtocol;

		constexpr std::size_t kMaximumStateBytes = 32 * StateBag::kMaximumBagBytes;
		constexpr std::size_t kMaximumMessageBytes = 1024;
		constexpr auto kOpenCooldown = std::chrono::milliseconds(500);
		constexpr auto kOpenWindow = std::chrono::seconds(1);
		constexpr std::size_t kMaximumOpensPerWindow = 10;
		constexpr std::size_t kMaximumOpenAttempts = 3;
		constexpr std::size_t kMaximumLiveViews = 8;
		constexpr auto kSendEvent = "coResourceSend";
		constexpr auto kStateEvent = "coState";
		constexpr auto kBagEvent = "coBag";

		struct ResourceView
		{
			explicit ResourceView(std::string a_resource) :
				resource(std::move(a_resource)),
				state(resource.c_str())
			{
			}

			std::string resource;
			std::string view;
			F4T::PrismaViewState::Controller state;
			bool synced{};
		};

		struct ViewBinding
		{
			std::string resource;
			PrismaView handle{};
		};

		struct PendingRequest
		{
			std::string view;
			bool open{};
			std::size_t attempts{};
		};

		std::map<std::string, std::unique_ptr<ResourceView>, std::less<>> g_views;
		std::deque<ViewBinding> g_bindings;
		std::map<std::string, nlohmann::json, std::less<>> g_bags;
		std::map<std::string, std::chrono::steady_clock::time_point, std::less<>> g_lastOpen;
		std::deque<std::chrono::steady_clock::time_point> g_recentOpens;
		std::map<std::string, PendingRequest, std::less<>> g_pending;
		COMMONWEALTH_ONLINE_UI_API::ResourceMessageFn g_messageCallback{ nullptr };

		void LogInfo(std::string_view a_message)
		{
			REX::INFO("[COUI] [ResourceView] {}", a_message);
		}

		void LogWarn(std::string_view a_message)
		{
			REX::WARN("[COUI] [ResourceView] {}", a_message);
		}

		void RunOnGameThread(std::function<void()> a_task)
		{
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask(std::move(a_task));
				return;
			}
			LogWarn("Dropped resource view task: game task interface unavailable.");
		}

		std::filesystem::path ViewFile(std::string_view a_resource, std::string_view a_view)
		{
			return std::filesystem::path("Data") / "PrismaUI_F4" / "views" / "co-resources" / std::string{ a_resource } / std::string{ a_view };
		}

		ResourceView* FindByHandle(PrismaView a_view)
		{
			for (const auto& entry : g_views) {
				if (entry.second->state.GetHandle() == a_view) {
					return entry.second.get();
				}
			}
			return nullptr;
		}

		bool Dispatch(const ResourceView& a_view, const std::string& a_script)
		{
			if (!g_api || !a_view.state.IsDomReady() || !a_view.state.IsHandleValid()) {
				return false;
			}
			g_api->Invoke(a_view.state.GetHandle(), a_script.c_str());
			return true;
		}

		void PushFullState(ResourceView& a_view)
		{
			if (g_bags.empty()) {
				return;
			}
			nlohmann::json state = nlohmann::json::object();
			for (const auto& [bag, value] : g_bags) {
				state[bag] = value;
			}
			a_view.synced = Dispatch(a_view, std::format("window.dispatchEvent(new CustomEvent('{}', {{detail: {}}}));", kStateEvent, F4T::ViewJson::Dump(state, -1, true)));
		}

		void OnResourceMessage(const char* a_json, void* a_userdata)
		{
			if (!a_json || a_userdata == nullptr) {
				return;
			}
			if (strnlen(a_json, kMaximumMessageBytes + 1) > kMaximumMessageBytes) {
				LogWarn("Dropped resource message larger than 1 KB.");
				return;
			}
			const auto* binding = static_cast<const ViewBinding*>(a_userdata);
			RunOnGameThread([resource = binding->resource, handle = binding->handle, payload = std::string(a_json)]() {
				const auto found = g_views.find(resource);
				if (!g_messageCallback || found == g_views.end() || found->second->state.GetHandle() != handle) {
					return;
				}
				g_messageCallback(found->second->resource.c_str(), payload.c_str());
			});
		}

		void OnDomReady(PrismaView a_view)
		{
			RunOnGameThread([a_view]() {
				auto* view = FindByHandle(a_view);
				if (!view || !g_api || !view->state.MarkDomReady(a_view)) {
					return;
				}
				g_bindings.push_back(ViewBinding{ view->resource, a_view });
				auto& binding = g_bindings.back();
				auto* apiV11 = F4T::PrismaCapabilities::GetV11();
				if (!apiV11 || !apiV11->BindGameThreadUIEvent(a_view, kSendEvent, OnResourceMessage, &binding)) {
					LogWarn(std::format("View '{}' cannot bind its message channel; messages are dropped.", view->resource));
				}
				view->state.ApplyState("DomReady");
				PushFullState(*view);
			});
		}

		void DestroyView(ResourceView& a_view)
		{
			a_view.state.SetDesired(false, false);
			a_view.state.ApplyState("ResourceClosed");
			if (g_api && a_view.state.IsHandleValid()) {
				g_api->Destroy(a_view.state.GetHandle());
			}
		}

		void CloseInternal(const std::string& a_resource)
		{
			const auto found = g_views.find(a_resource);
			if (found == g_views.end()) {
				return;
			}
			LogInfo(std::format("Close resource={}", a_resource));
			DestroyView(*found->second);
			g_views.erase(found);
		}

		bool OpenAllowed(const std::string& a_resource, std::chrono::steady_clock::time_point a_now)
		{
			const auto last = g_lastOpen.find(a_resource);
			if (last != g_lastOpen.end() && a_now - last->second < kOpenCooldown) {
				return false;
			}
			while (!g_recentOpens.empty() && a_now - g_recentOpens.front() >= kOpenWindow) {
				g_recentOpens.pop_front();
			}
			return g_recentOpens.size() < kMaximumOpensPerWindow;
		}

		void RecordOpenAttempt(const std::string& a_resource, std::chrono::steady_clock::time_point a_now)
		{
			g_lastOpen[a_resource] = a_now;
			g_recentOpens.push_back(a_now);
		}

		void ApplyPending(const std::string& a_resource)
		{
			const auto pending = g_pending.find(a_resource);
			if (pending == g_pending.end()) {
				return;
			}
			if (!pending->second.open) {
				CloseInternal(a_resource);
				g_pending.erase(pending);
				return;
			}
			const auto existing = g_views.find(a_resource);
			if (existing == g_views.end() && g_views.size() >= kMaximumLiveViews) {
				LogWarn(std::format("Open refused - {} resource views are already live.", g_views.size()));
				g_pending.erase(pending);
				return;
			}
			if (existing != g_views.end() &&
				existing->second->view == pending->second.view &&
				existing->second->state.IsHandleValid()) {
				g_pending.erase(pending);
				return;
			}
			if (!g_api) {
				LogWarn("Open skipped — Prisma API unavailable.");
				g_pending.erase(pending);
				return;
			}
			const auto now = std::chrono::steady_clock::now();
			if (!OpenAllowed(a_resource, now)) {
				return;
			}
			RecordOpenAttempt(a_resource, now);
			const auto viewName = pending->second.view;
			const auto attempt = ++pending->second.attempts;

			std::unique_ptr<ResourceView> previous;
			if (existing != g_views.end()) {
				previous = std::move(existing->second);
				g_views.erase(existing);
			}

			auto replacement = std::make_unique<ResourceView>(a_resource);
			replacement->view = viewName;
			replacement->state.SetApi(g_api);
			replacement->state.SetFocusArgs(false, true);
			replacement->state.SetDesiredRole(PRISMA_UI_API::ViewRole::kPanel);
			replacement->state.SetDesired(true, true);
			const auto relative = std::format("co-resources/{}/{}", a_resource, viewName);
			auto& installed = g_views[a_resource] = std::move(replacement);
			if (installed->state.EnsureCreated(relative.c_str(), OnDomReady, true) == 0) {
				LogWarn(std::format("Open failed — CreateView returned no handle for {} (attempt {}).", relative, attempt));
				g_views.erase(a_resource);
				if (previous) {
					g_views.emplace(a_resource, std::move(previous));
				}
				if (attempt >= kMaximumOpenAttempts) {
					LogWarn(std::format("Gave up opening {}; resource={} keeps its previous view.", relative, a_resource));
					g_pending.erase(a_resource);
				}
				return;
			}
			LogInfo(std::format("Open resource={} view={}", a_resource, viewName));
			if (previous) {
				DestroyView(*previous);
			}
			g_pending.erase(a_resource);
		}

		void RequestOpen(const std::string& a_resource, const std::string& a_view)
		{
			auto& request = g_pending[a_resource];
			request.view = a_view;
			request.open = true;
			request.attempts = 0;
			ApplyPending(a_resource);
		}

		void RequestClose(const std::string& a_resource)
		{
			auto& request = g_pending[a_resource];
			request.view.clear();
			request.open = false;
			request.attempts = 0;
			ApplyPending(a_resource);
		}
	}

	void Tick()
	{
		if (g_pending.empty()) {
			return;
		}
		std::vector<std::string> resources;
		resources.reserve(g_pending.size());
		for (const auto& entry : g_pending) {
			resources.push_back(entry.first);
		}
		for (const auto& resource : resources) {
			ApplyPending(resource);
		}
	}

	void SetResourceMessageCallback(COMMONWEALTH_ONLINE_UI_API::ResourceMessageFn a_callback)
	{
		g_messageCallback = a_callback;
	}

	void PushStateBag(const char* a_stateJson)
	{
		if (!a_stateJson) {
			return;
		}
		if (strnlen(a_stateJson, kMaximumStateBytes + 1) > kMaximumStateBytes) {
			LogWarn("Dropped state bag larger than 2 MB.");
			return;
		}
		RunOnGameThread([payload = std::string(a_stateJson)]() {
			const auto parsed = nlohmann::json::parse(payload, nullptr, false);
			if (parsed.is_discarded() || !parsed.is_object()) {
				LogWarn("Dropped malformed state bag.");
				return;
			}
			std::map<std::string, nlohmann::json, std::less<>> bags;
			for (const auto& item : parsed.items()) {
				bags.emplace(item.key(), item.value());
			}
			std::string script;
			const auto queue = [&script](const std::string& a_bag, const nlohmann::json& a_value) {
				const nlohmann::json detail{ { "bag", a_bag }, { "value", a_value } };
				script += std::format("window.dispatchEvent(new CustomEvent('{}', {{detail: {}}}));", kBagEvent, F4T::ViewJson::Dump(detail, -1, true));
			};
			for (const auto& [bag, value] : bags) {
				const auto previous = g_bags.find(bag);
				if (previous == g_bags.end() || previous->second != value) {
					queue(bag, value);
				}
			}
			for (const auto& entry : g_bags) {
				if (!bags.contains(entry.first)) {
					queue(entry.first, nlohmann::json{});
				}
			}
			g_bags = std::move(bags);
			for (auto& entry : g_views) {
				if (!entry.second->synced) {
					PushFullState(*entry.second);
				} else if (!script.empty()) {
					entry.second->synced = Dispatch(*entry.second, script);
				}
			}
		});
	}

	bool OpenResourceView(const char* a_resource, const char* a_view)
	{
		if (!a_resource || !a_view) {
			return false;
		}
		const std::string resource = a_resource;
		const std::string view = a_view;
		if (!StateBag::IsValidResourceName(resource) || !StateBag::IsValidViewPath(view)) {
			LogWarn("Refused a resource view with an invalid name or path.");
			return false;
		}
		std::error_code error;
		if (!std::filesystem::is_regular_file(ViewFile(resource, view), error)) {
			LogWarn(std::format("Refused resource view {}/{}: the file is missing.", resource, view));
			return false;
		}
		RunOnGameThread([resource, view]() {
			RequestOpen(resource, view);
		});
		return true;
	}

	void CloseResourceView(const char* a_resource)
	{
		if (!a_resource || !StateBag::IsValidResourceName(a_resource)) {
			return;
		}
		RunOnGameThread([resource = std::string(a_resource)]() {
			RequestClose(resource);
		});
	}
}

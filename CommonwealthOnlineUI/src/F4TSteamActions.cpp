#include "pch.h"

#include "F4TSteamActions.h"
#include "F4TServerBrowserBridgeInternal.h"
#include "SessionCallbacks.h"

#include <F4SE/Interfaces.h>

#include <chrono>
#include <cstring>
#include <exception>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::Steam
{
	namespace
	{
		constexpr std::size_t kEventMaxBytes = 32 * 1024;
		constexpr std::chrono::minutes kPendingJoinFailureMaxAge{ 2 };

		COMMONWEALTH_ONLINE_UI_API::SteamCallbacks g_callbacks{};
		nlohmann::json                             g_lastStatus;
		nlohmann::json                             g_pendingJoinFailure;
		std::chrono::steady_clock::time_point      g_pendingJoinFailureAt;

		std::string GetLogPrefix()
		{
			return std::string{ COUI::Session::GetLogPrefix() } + " [Steam]";
		}

		void LogWarn(std::string_view a_message)
		{
			const auto message = GetLogPrefix() + " " + std::string(a_message);
			REX::WARN(std::string_view{ message });
		}

		void RunOnGameThread(std::function<void()> a_task)
		{
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask(std::move(a_task));
				return;
			}
			LogWarn("Dropped steam task - game task interface unavailable.");
		}

		void EmitCapabilities()
		{
			F4T::ServerBrowser::detail::DispatchEvent(nlohmann::json{
				{ "type", "capabilities" },
				{ "steamP2p", g_callbacks.HostStart != nullptr }
			});
		}

		void DispatchSink(void*, const nlohmann::json& a_event)
		{
			F4T::ServerBrowser::detail::DispatchEvent(a_event);
		}
	}

	void SetCallbacks(const COMMONWEALTH_ONLINE_UI_API::SteamCallbacks* a_callbacks)
	{
		RunOnGameThread([callbacks = a_callbacks ? *a_callbacks : COMMONWEALTH_ONLINE_UI_API::SteamCallbacks{}]() {
			g_callbacks = callbacks;
			EmitCapabilities();
		});
	}

	void PushCapabilities()
	{
		EmitCapabilities();
		if (g_lastStatus.is_object()) {
			F4T::ServerBrowser::detail::DispatchEvent(g_lastStatus);
		}
		if (g_pendingJoinFailure.is_object()) {
			const bool fresh = std::chrono::steady_clock::now() - g_pendingJoinFailureAt < kPendingJoinFailureMaxAge;
			if (!fresh) {
				g_pendingJoinFailure = nlohmann::json();
			} else if (F4T::ServerBrowser::detail::CanDispatchToBrowser()) {
				F4T::ServerBrowser::detail::DispatchEvent(g_pendingJoinFailure);
				g_pendingJoinFailure = nlohmann::json();
			}
		}
	}

	void PushEvent(const char* a_json)
	{
		if (!a_json) {
			return;
		}
		if (strnlen(a_json, kEventMaxBytes + 1) > kEventMaxBytes) {
			LogWarn("Dropped steam event larger than 32 KiB.");
			return;
		}
		RunOnGameThread([payload = std::string(a_json)]() {
			try {
				auto event = nlohmann::json::parse(payload);
				const auto type = event.is_object() ? event.value("type", "") : std::string{};
				if (type == "steamStatus") {
					g_lastStatus = event;
				}
				if (type == "steamJoinFailed" && !F4T::ServerBrowser::detail::CanDispatchToBrowser()) {
					g_pendingJoinFailure = event;
					g_pendingJoinFailureAt = std::chrono::steady_clock::now();
					return;
				}
				if (type == "steamHostStarted") {
					g_pendingJoinFailure = nlohmann::json();
				}
				F4T::ServerBrowser::detail::DispatchEvent(event);
			} catch (const std::exception& ex) {
				LogWarn(std::format("Dropped malformed steam event: {}", ex.what()));
			}
		});
	}

	bool HandleAction(const nlohmann::json& a_action)
	{
		if (!a_action.is_object() || !IsSteamAction(a_action.value("action", ""))) {
			return false;
		}
		RunOnGameThread([payload = a_action]() {
			try {
				const auto action = payload.value("action", "");
				if (action == "steamJoin" || action == "steamHostStart") {
					g_pendingJoinFailure = nlohmann::json();
				}
				(void)RouteSteamAction(payload, g_callbacks, &DispatchSink, nullptr);
			} catch (const std::exception& ex) {
				LogWarn(std::format("Dropped malformed steam action: {}", ex.what()));
			}
		});
		return true;
	}
}

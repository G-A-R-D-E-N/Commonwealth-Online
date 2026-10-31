#include "pch.h"

#include "F4TServerLive.h"

#include <array>
#include <cstddef>
#include <mutex>
#include <optional>

namespace F4T::ServerLive
{
	namespace
	{
		std::mutex g_mutex;
		COMMONWEALTH_ONLINE_UI_API::ServerLiveCallbacks g_callbacks;
		bool g_registered{ false };

		constexpr std::size_t kMaximumReplyLength = 16 * 1024;

		std::optional<COMMONWEALTH_ONLINE_UI_API::ServerLiveCallbacks> RegisteredCallbacks()
		{
			std::scoped_lock lock(g_mutex);
			if (!g_registered) {
				return std::nullopt;
			}
			return g_callbacks;
		}

		LiveApply Call(const nlohmann::json& a_request, nlohmann::json& a_result, std::string& a_error)
		{
			a_error.clear();
			a_result = nullptr;
			const auto callbacks = RegisteredCallbacks();
			if (!callbacks || callbacks->ApplyServerSettings == nullptr) {
				return LiveApply::Unavailable;
			}
			const auto encoded = a_request.dump();
			std::array<char, kMaximumReplyLength> replyBuffer{};
			if (!callbacks->ApplyServerSettings(encoded.c_str(), replyBuffer.data(), replyBuffer.size())) {
				a_error = replyBuffer.data();
				return a_error.empty() ? LiveApply::Unavailable : LiveApply::Refused;
			}
			const auto parsed = nlohmann::json::parse(replyBuffer.data(), nullptr, false);
			if (parsed.is_discarded() || !parsed.is_object()) {
				a_error = "The running server sent a reply the panel could not read.";
				return LiveApply::Refused;
			}
			if (parsed.value("ok", false) != true) {
				a_error = parsed.value("error", std::string{ "The running server refused the settings change." });
				a_result = parsed;
				return LiveApply::Refused;
			}
			a_result = parsed;
			return LiveApply::Applied;
		}
	}

	void SetCallbacks(const COMMONWEALTH_ONLINE_UI_API::ServerLiveCallbacks* a_callbacks)
	{
		std::scoped_lock lock(g_mutex);
		if (a_callbacks == nullptr) {
			g_callbacks = {};
			g_registered = false;
			return;
		}
		g_callbacks = *a_callbacks;
		g_registered = g_callbacks.ApplyServerSettings != nullptr;
	}

	LiveApply ApplySettings(const nlohmann::json& a_request, nlohmann::json& a_result, std::string& a_error)
	{
		return Call(a_request, a_result, a_error);
	}

	LiveApply QueryRunningConfig(nlohmann::json& a_result, std::string& a_error)
	{
		return Call(nlohmann::json{ { "action", "config" } }, a_result, a_error);
	}
}

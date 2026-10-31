#include "pch.h"

#include "F4TServerLive.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TSteamHostLink.h"
#include "F4TSteamHostLinkClient.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace F4T::ServerLive
{
	namespace
	{
		constexpr std::uint16_t kHostLinkGamePort = 7777;
		constexpr std::size_t kMaximumResultLength = 16 * 1024;

		void CopyText(const std::string& a_text, char* a_buffer, std::size_t a_capacity)
		{
			if (a_buffer == nullptr || a_capacity == 0) {
				return;
			}
			const auto length = a_text.size() < a_capacity - 1 ? a_text.size() : a_capacity - 1;
			std::memcpy(a_buffer, a_text.data(), length);
			a_buffer[length] = '\0';
		}

		std::string ReplyError(const nlohmann::json& a_reply)
		{
			if (!a_reply.is_object()) {
				return {};
			}
			const auto found = a_reply.find("error");
			return found != a_reply.end() && found->is_string() ? found->get<std::string>() : std::string{};
		}

		bool ApplyServerSettings(const char* a_requestJson, char* a_resultBuffer, std::size_t a_resultBufferSize)
		{
			if (a_requestJson == nullptr || a_resultBuffer == nullptr || a_resultBufferSize < 64) {
				return false;
			}
			nlohmann::json request;
			try {
				request = nlohmann::json::parse(a_requestJson);
			} catch (const nlohmann::json::exception&) {
				return false;
			}
			if (!request.is_object()) {
				return false;
			}
			const auto action = request.value("action", std::string{});
			nlohmann::json linkRequest;
			if (action == "config") {
				linkRequest = nlohmann::json{ { "cmd", std::string{ SteamHostLink::kServerConfigCommand } } };
			} else if (action == "settings") {
				const auto document = request.find("document");
				if (document == request.end() || !document->is_object()) {
					return false;
				}
				linkRequest = nlohmann::json{ { "cmd", std::string{ SteamHostLink::kApplySettingsCommand } }, { "document", *document } };
			} else {
				return false;
			}
			const auto directory = SteamHostLink::Directory();
			if (!directory) {
				return false;
			}
			const auto endpoint = SteamHostLink::ReadEndpoint(*directory, kHostLinkGamePort);
			if (!endpoint) {
				return false;
			}
			nlohmann::json reply;
			if (!SteamHostLink::Send(*endpoint, linkRequest, &reply)) {
				// constraint: an endpoint file without an answer is a link failure, not a missing server
				auto error = ReplyError(reply);
				if (error.empty()) {
					error = "The running server did not answer the settings link in time.";
				}
				CopyText(nlohmann::json{ { "ok", false }, { "error", error } }.dump(), a_resultBuffer, a_resultBufferSize);
				return false;
			}
			const auto encoded = reply.dump();
			if (encoded.size() >= a_resultBufferSize || encoded.size() > kMaximumResultLength) {
				CopyText("{\"ok\":false,\"error\":\"The server reply was too large.\"}", a_resultBuffer, a_resultBufferSize);
				return true;
			}
			CopyText(encoded, a_resultBuffer, a_resultBufferSize);
			return true;
		}

		void Register()
		{
			const auto ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI12>();
			if (ui == nullptr || ui->SetServerLiveCallbacks == nullptr) {
				return;
			}
			static const COMMONWEALTH_ONLINE_UI_API::ServerLiveCallbacks kCallbacks{ ApplyServerSettings };
			ui->SetServerLiveCallbacks(&kCallbacks);
		}
	}

	void Attach()
	{
		Register();
	}
}

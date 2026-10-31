#include "pch.h"

#include "F4TChat.h"
#include "F4TSocialNameplate.h"
#include "F4TTextEntryMode.h"

#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TRemotePlayerState.h"

#include "CommonwealthOnlineUI_API.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

namespace F4T::Chat
{
	using namespace F4T::Networking::Detail;

	namespace
	{
		constexpr std::size_t kMaximumTextBytes = 1024;
		constexpr std::size_t kMaximumNameBytes = 128;
		constexpr std::size_t kMaximumHistoryMessages = 20;
		constexpr std::size_t kMaximumActionJsonBytes = 4 * 1024;
		constexpr std::size_t kMaximumChatSendBytes = 4 * 1024;
		constexpr std::size_t kMaximumEventBytes = 32 * 1024;
		constexpr std::string_view kChatCapability = "chat-v1";
		constexpr std::string_view kGlobalChannel = "global";
		constexpr std::string_view kFactionChannel = "faction";
		constexpr std::array<std::string_view, 7> kRejectReasons{
			"invalidText", "tooLong", "rateLimited", "channelUnavailable", "muted", "duplicate", "notInFaction"
		};
		constexpr auto kHistoryRequestWindow = std::chrono::seconds{ 2 };

		std::mutex g_mutex;
		bool g_enabled = false;
		std::optional<std::chrono::steady_clock::time_point> g_lastHistoryRequest;

		bool IsEnabled()
		{
			std::scoped_lock lock(g_mutex);
			return g_enabled;
		}

		bool IsKnownChannel(std::string_view a_channel)
		{
			return a_channel == kGlobalChannel || a_channel == kFactionChannel;
		}

		bool IsBoundedText(const std::optional<std::string>& a_text, std::size_t a_maximumBytes)
		{
			return a_text && !a_text->empty() && a_text->size() <= a_maximumBytes;
		}

		void QueueGameTask(std::function<void()> a_task)
		{
			const auto* taskInterface = F4SE::GetTaskInterface();
			if (!taskInterface) {
				LogWarningWithLocalPlayerPrefix("Cannot queue chat work: task interface unavailable.");
				return;
			}
			taskInterface->AddTask(std::move(a_task));
		}

		void QueueEvent(Json a_event)
		{
			QueueGameTask([event = std::move(a_event)]() {
				auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
					COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI3>();
				if (!ui || !ui->PushChatEvent) {
					LogThrottledWarning("chat_ui_unavailable", "Cannot push a chat event: the UI chat API is unavailable.");
					return;
				}
				const auto payload = event.dump();
				if (payload.size() > kMaximumEventBytes) {
					LogThrottledWarning("chat_event_size", "Dropped a chat event larger than 32 KiB.");
					return;
				}
				ui->PushChatEvent(payload.c_str());
			});
		}

		void QueuePacket(Json a_packet)
		{
			QueueGameTask([packet = std::move(a_packet)]() {
				if (!F4T::Networking::IsConnectedToServer()) {
					return;
				}
				if (!SendPacketBytes(packet.dump())) {
					LogThrottledWarning("chat_send_failed", "Could not send Commonwealth Online chat packet.");
				}
			});
		}

		void QueueRejected(std::string_view a_reason)
		{
			QueueEvent(Json{ { "kind", "rejected" }, { "reason", std::string{ a_reason } } });
		}

		void QueueState(bool a_enabled)
		{
			const auto localPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
			QueueEvent(Json{
				{ "kind", "state" },
				{ "enabled", a_enabled },
				{ "localPlayerId", localPlayerId ? Json(*localPlayerId) : Json(nullptr) }
			});
		}

		bool TakeHistoryRequestSlot()
		{
			const auto now = std::chrono::steady_clock::now();
			std::scoped_lock lock(g_mutex);
			if (g_lastHistoryRequest && now - *g_lastHistoryRequest < kHistoryRequestWindow) {
				return false;
			}
			g_lastHistoryRequest = now;
			return true;
		}

		void RequestHistory()
		{
			if (!TakeHistoryRequestSlot()) {
				return;
			}
			QueuePacket(Json{ { "type", "chatHistoryRequest" } });
		}

		std::optional<Json> ParseMessage(const Json& a_message)
		{
			const auto channel = ReadString(a_message, "channel");
			const auto shown = F4T::SocialNameplate::ResolveShownName(
				ReadString(a_message, "displayName"),
				ReadString(a_message, "fromName"));
			const auto text = ReadString(a_message, "text");
			const auto fromPlayerId = ReadPlayerId(a_message, "fromPlayerId");
			const auto serverTime = ReadDouble(a_message, "serverTime");
			const auto messageId = a_message.find("messageId");
			if (!channel || !IsKnownChannel(*channel) || !shown || !IsBoundedText(shown, kMaximumNameBytes) ||
				!IsBoundedText(text, kMaximumTextBytes) || !fromPlayerId || !serverTime ||
				messageId == a_message.end() || !messageId->is_number_unsigned()) {
				return std::nullopt;
			}

			return Json{
				{ "channel", *channel },
				{ "messageId", messageId->get<std::uint64_t>() },
				{ "fromPlayerId", *fromPlayerId },
				{ "displayName", *shown },
				{ "fromName", *shown },
				{ "text", *text },
				{ "serverTime", *serverTime }
			};
		}

		void HandleMessagePacket(const Json& a_packet)
		{
			auto message = ParseMessage(a_packet);
			if (!message) {
				LogThrottledWarning("chat_message_malformed", "Ignoring malformed chatMessage packet.");
				return;
			}
			QueueEvent(Json{ { "kind", "message" }, { "message", std::move(*message) } });
		}

		void HandleHistoryPacket(const Json& a_packet)
		{
			const auto messages = a_packet.find("messages");
			if (messages == a_packet.end() || !messages->is_array() || messages->size() > kMaximumHistoryMessages) {
				LogThrottledWarning("chat_history_malformed", "Ignoring malformed chatHistory packet.");
				return;
			}

			auto history = Json::array();
			for (const auto& messageJson : *messages) {
				auto message = ParseMessage(messageJson);
				if (!message) {
					LogThrottledWarning("chat_history_entry", "Ignoring chatHistory packet with a malformed message.");
					return;
				}
				history.push_back(std::move(*message));
			}
			QueueEvent(Json{ { "kind", "history" }, { "messages", std::move(history) } });
		}

		void HandleRejectedPacket(const Json& a_packet)
		{
			const auto reason = ReadString(a_packet, "reason");
			if (!reason || std::find(kRejectReasons.begin(), kRejectReasons.end(), std::string_view{ *reason }) == kRejectReasons.end()) {
				LogThrottledWarning("chat_rejected_malformed", "Ignoring malformed chatRejected packet.");
				return;
			}
			QueueRejected(*reason);
		}
	}

	void NoteWelcome(const Json& a_welcome)
	{
		const auto capabilities = a_welcome.find("capabilities");
		const bool supported = capabilities != a_welcome.end() && capabilities->is_array() &&
			std::any_of(capabilities->begin(), capabilities->end(), [](const Json& a_capability) {
				return a_capability.is_string() && a_capability.get<std::string>() == kChatCapability;
			});
		{
			std::scoped_lock lock(g_mutex);
			g_enabled = supported;
		}
		QueueState(supported);
		if (supported) {
			RequestHistory();
		}
	}

	bool TryHandleChatPacket(std::string_view a_type, const Json& a_packet)
	{
		if (a_type != "chatMessage" && a_type != "chatHistory" && a_type != "chatRejected") {
			return false;
		}
		if (!IsEnabled()) {
			return true;
		}

		if (a_type == "chatMessage") {
			HandleMessagePacket(a_packet);
		} else if (a_type == "chatHistory") {
			HandleHistoryPacket(a_packet);
		} else {
			HandleRejectedPacket(a_packet);
		}
		return true;
	}

	void Reset()
	{
		{
			std::scoped_lock lock(g_mutex);
			g_enabled = false;
			g_lastHistoryRequest.reset();
		}
		QueueState(false);
		QueueGameTask([]() { F4T::TextEntryMode::Reset(); });
	}

	void HandleAction(std::string_view a_actionJson)
	{
		if (a_actionJson.empty() || a_actionJson.size() > kMaximumActionJsonBytes) {
			LogThrottledWarning("chat_action_size", "Ignoring a chat action outside the size limit.");
			return;
		}
		const auto action = Json::parse(std::string{ a_actionJson }, nullptr, false);
		if (!action.is_object()) {
			LogThrottledWarning("chat_action_shape", "Ignoring a chat action that is not a JSON object.");
			return;
		}

		const auto op = ReadString(action, "op").value_or("");
		if (op == "focus") {
			const auto focused = action.find("focused");
			F4T::TextEntryMode::SetChatInputFocused(focused != action.end() && focused->is_boolean() && focused->get<bool>() && IsEnabled());
			return;
		}
		if (!IsEnabled() || !F4T::Networking::IsConnectedToServer()) {
			return;
		}
		if (op == "send") {
			const auto channel = ReadString(action, "channel");
			if (!channel || !IsKnownChannel(*channel)) {
				LogThrottledWarning("chat_action_channel", "Ignoring a chat action for an unknown channel.");
				return;
			}
			const auto text = ReadString(action, "text");
			if (!text || text->empty()) {
				QueueRejected("invalidText");
				return;
			}
			if (text->size() > kMaximumTextBytes) {
				QueueRejected("tooLong");
				return;
			}
			auto packet = Json{ { "type", "chatSend" }, { "channel", *channel }, { "text", *text } };
			if (packet.dump().size() > kMaximumChatSendBytes) {
				QueueRejected("tooLong");
				return;
			}
			QueuePacket(std::move(packet));
		} else if (op == "history") {
			RequestHistory();
		} else {
			LogThrottledWarning("chat_action_op", "Ignoring a chat action with an unknown op.");
		}
	}
}

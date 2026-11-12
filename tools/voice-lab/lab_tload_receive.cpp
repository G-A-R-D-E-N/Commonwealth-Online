#include "lab_tload_state.h"
#include "lab_tload_wire.h"

#include "F4TGnsSnapshotEnvelope.h"
#include "F4TVoiceWire.h"

#include <charconv>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace VoiceLab::Tload
{
	namespace
	{
		constexpr std::uint32_t kEventDisconnected = 2U;
		constexpr std::uint32_t kEventMessage = 3U;

		std::optional<std::size_t> ChatSequence(std::string_view a_text)
		{
			const auto space = a_text.rfind(' ');
			if (space == std::string_view::npos) {
				return std::nullopt;
			}
			const auto tail = a_text.substr(space + 1);
			std::size_t value = 0;
			const auto result = std::from_chars(tail.data(), tail.data() + tail.size(), value);
			if (result.ec != std::errc{} || result.ptr != tail.data() + tail.size()) {
				return std::nullopt;
			}
			return value;
		}

		void OnVoice(Harness& a_h, std::uint32_t a_size, double a_now)
		{
			if (!a_h.InWindow(a_now)) {
				return;
			}
			const auto bin = static_cast<std::size_t>(a_now - a_h.windowStart);
			if (bin < a_h.tally.voiceBins.size()) {
				a_h.tally.voiceBins[bin] += a_size;
				a_h.tally.voiceBytes += a_size;
				++a_h.tally.voiceDatagrams;
			}
		}

		void OnTransform(Harness& a_h, Player& a_receiver, std::string_view a_json, std::uint32_t a_size, double a_now)
		{
			double clientTime = 0.0;
			double playerId = 0.0;
			if (!FindNumber(a_json, "clientTime", clientTime) || !FindNumber(a_json, "playerId", playerId)) {
				++a_h.tally.malformed;
				return;
			}
			const auto sender = a_h.IndexOfPlayer(static_cast<std::uint32_t>(playerId));
			if (!sender || *sender == a_receiver.index) {
				return;
			}
			double& last = a_receiver.lastSeen[*sender];
			if (clientTime <= last) {
				return;
			}
			last = clientTime;
			if (a_h.InWindow(clientTime)) {
				++a_h.tally.transformDelivered;
				a_h.tally.transformBytes += a_size;
				a_h.tally.latencies.push_back(a_now - clientTime);
			}
		}

		void OnChat(Harness& a_h, Player& a_receiver, std::string_view a_json, double a_now)
		{
			std::string channel;
			std::string text;
			double fromPlayerId = 0.0;
			if (!FindString(a_json, "channel", channel) || !FindString(a_json, "text", text) || !FindNumber(a_json, "fromPlayerId", fromPlayerId)) {
				++a_h.tally.malformed;
				return;
			}
			const auto sender = a_h.IndexOfPlayer(static_cast<std::uint32_t>(fromPlayerId));
			if (channel != "global" || !sender || *sender == a_receiver.index) {
				return;
			}
			const auto sequence = ChatSequence(text);
			auto& messages = a_h.players[*sender].chatMessages;
			if (!sequence || *sequence == 0 || *sequence > messages.size()) {
				return;
			}
			ChatMessage& message = messages[*sequence - 1];
			if (a_h.InWindow(message.sent)) {
				++message.arrivals;
				++a_h.tally.chatDelivered;
				a_h.tally.chatLatencies.push_back(a_now - message.sent);
			}
		}

		void OnChatRejected(Harness& a_h, std::string_view a_json, double a_now)
		{
			if (!a_h.InWindow(a_now)) {
				return;
			}
			++a_h.tally.chatRejected;
			std::string reason;
			if (FindString(a_json, "reason", reason) && reason == "rateLimited") {
				++a_h.tally.chatRejectedRateLimited;
			}
		}

		void OnJson(Harness& a_h, Player& a_p, std::string_view a_json, double a_now)
		{
			std::string type;
			if (!FindString(a_json, "type", type)) {
				++a_h.tally.malformed;
				return;
			}
			if (type == "welcome") {
				double playerId = 0.0;
				if (FindNumber(a_json, "playerId", playerId)) {
					a_p.playerId = static_cast<std::uint32_t>(playerId);
					a_p.hasPlayerId = true;
				}
			} else if (type == "sessionReady") {
				FindString(a_json, "resumeToken", a_p.resumeToken);
				a_p.sessionReady = true;
			} else if (type == "chatMessage") {
				OnChat(a_h, a_p, a_json, a_now);
			} else if (type == "chatRejected") {
				OnChatRejected(a_h, a_json, a_now);
			} else if (type == "positionCorrection") {
				++a_h.tally.positionCorrections;
			} else if (type == "sessionEnded") {
				++a_h.tally.sessionEnded;
				a_p.failed = true;
			}
		}

		void OnMessage(Harness& a_h, Player& a_p, std::uint32_t a_size, double a_now)
		{
			if (a_size > a_h.buffer.size()) {
				++a_h.tally.malformed;
				return;
			}
			const auto bytes = std::span<const std::uint8_t>{ a_h.buffer.data(), a_size };
			const std::string_view text{ reinterpret_cast<const char*>(bytes.data()), bytes.size() };
			if (F4T::Voice::HasVoicePrefix(std::as_bytes(bytes))) {
				OnVoice(a_h, a_size, a_now);
			} else if (a_size >= F4T::GnsSnapshotEnvelope::HeaderSize &&
				std::memcmp(bytes.data(), F4T::GnsSnapshotEnvelope::Magic.data(), F4T::GnsSnapshotEnvelope::Magic.size()) == 0) {
				if (bytes[5] == static_cast<std::uint8_t>(F4T::GnsSnapshotEnvelope::Family::Transform)) {
					OnTransform(a_h, a_p, text.substr(F4T::GnsSnapshotEnvelope::HeaderSize), a_size, a_now);
				}
			} else if (!text.empty() && text.front() == '{') {
				OnJson(a_h, a_p, text, a_now);
			}
		}
	}

	void PollAll(Harness& a_h)
	{
		GnsEvent event{};
		for (Player& player : a_h.players) {
			int result = 0;
			while ((result = a_h.bridge.clientPoll(player.client.get(), &event, a_h.buffer.data(), static_cast<std::uint32_t>(a_h.buffer.size()))) > 0) {
				if (event.type == kEventDisconnected) {
					++a_h.tally.disconnects;
					player.failed = true;
				} else if (event.type == kEventMessage) {
					OnMessage(a_h, player, event.payloadSize, a_h.StampOf(Clock::now()));
				}
			}
			if (result < 0) {
				++a_h.tally.pollErrors;
				player.failed = true;
			}
		}
	}
}

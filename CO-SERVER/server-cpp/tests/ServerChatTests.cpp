// source:CO-BETA/docs/protocol/chat.md
#include "ServerProtocol.h"

#include "F4TGnsSnapshotEnvelope.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolHost;
	using F4T::GnsSnapshotEnvelope::DecodeResult;

	std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	Json DecodePacket(const OutboundFrame& a_frame)
	{
		std::span<const std::uint8_t> payload{ a_frame.payload };
		F4T::GnsSnapshotEnvelope::Header header{};
		if (F4T::GnsSnapshotEnvelope::Decode(a_frame.payload, header) == DecodeResult::Valid) {
			payload = payload.subspan(F4T::GnsSnapshotEnvelope::HeaderSize);
		}
		return Json::parse(std::string{
			reinterpret_cast<const char*>(payload.data()),
			payload.size()
		});
	}

	const OutboundFrame* FindFrame(
		const std::vector<OutboundFrame>& a_frames,
		std::uint32_t a_connectionId,
		std::string_view a_type)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && !frame.payload.empty() &&
				DecodePacket(frame).value("type", std::string{}) == a_type) {
				return &frame;
			}
		}
		return nullptr;
	}

	std::uint32_t JoinPlayer(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		assert(FindFrame(a_host.HandleMessage(a_connectionId, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "social-names-v1" }) } }), a_monotonicSeconds, a_unixTimeSeconds + 0.1), a_connectionId, "sessionReady") != nullptr);
		return DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
	}

	Json ChatSend(const std::string& a_text, const std::string& a_channel = "global")
	{
		return Json{ { "type", "chatSend" }, { "channel", a_channel }, { "text", a_text } };
	}

	Json ChatHistoryRequest()
	{
		return Json{ { "type", "chatHistoryRequest" } };
	}

	Json PlayerName(const std::string& a_name)
	{
		return Json{ { "type", "playerState" }, { "characterName", a_name } };
	}

	void AssertChatRejected(ProtocolHost& a_host, std::uint32_t a_connectionId, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds, std::string_view a_reason)
	{
		const auto before = a_host.Stats();
		const auto frames = a_host.HandleMessage(a_connectionId, EncodeJson(a_packet), a_monotonicSeconds, a_unixTimeSeconds);
		assert(frames.size() == 1);
		assert(frames.front().connectionId == a_connectionId);
		assert(DecodePacket(frames.front()).at("type") == "chatRejected");
		assert(DecodePacket(frames.front()).at("reason") == std::string{ a_reason });
		assert(a_host.Stats().chatRejected == before.chatRejected + 1);
	}

	void AssertChatSilent(ProtocolHost& a_host, std::uint32_t a_connectionId, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		const auto before = a_host.Stats();
		assert(a_host.HandleMessage(a_connectionId, EncodeJson(a_packet), a_monotonicSeconds, a_unixTimeSeconds).empty());
		assert(a_host.Stats().chatRejected == before.chatRejected + 1);
	}

	std::string Repeat(std::string_view a_unit, std::size_t a_count)
	{
		std::string output;
		for (std::size_t index = 0; index < a_count; ++index) {
			output += a_unit;
		}
		return output;
	}

	bool WelcomeHasCapability(ProtocolHost& a_host, std::uint32_t a_connectionId, std::string_view a_capability)
	{
		const auto welcome = DecodePacket(a_host.HandleConnected(a_connectionId, 100.0).front());
		for (const auto& capability : welcome.at("capabilities")) {
			if (capability == std::string{ a_capability }) return true;
		}
		return false;
	}
}

int main()
{
	{
		ProtocolHost host{ "Chat Disabled" };
		JoinPlayer(host, 10, 1.0, 100.0);
		const auto before = host.Stats();
		assert(host.HandleMessage(10, EncodeJson(ChatSend("hi")), 2.0, 100.2).empty());
		assert(host.HandleMessage(10, EncodeJson(ChatHistoryRequest()), 2.1, 100.3).empty());
		const auto after = host.Stats();
		assert(after.packetsRejected == before.packetsRejected + 2);
		assert(after.chatReceived == 0);
		assert(after.chatRelayed == 0);
		assert(after.chatRejected == 0);
	}

	{
		ProtocolHost host{ "Chat Relay" };
		host.SetChatEnabled(true);
		const auto alice = JoinPlayer(host, 10, 1.0, 100.0);
		JoinPlayer(host, 20, 1.1, 100.1);
		host.HandleConnected(30, 100.2);
		host.HandleMessage(10, EncodeJson(PlayerName("Nomad")), 1.5, 100.5);
		const auto frames = host.HandleMessage(10, EncodeJson(ChatSend("hello")), 2.0, 101.0);
		const auto* echo = FindFrame(frames, 10, "chatMessage");
		const auto* relayed = FindFrame(frames, 20, "chatMessage");
		assert(echo != nullptr);
		assert(relayed != nullptr);
		assert(FindFrame(frames, 30, "chatMessage") == nullptr);
		const auto message = DecodePacket(*echo);
		assert(message.at("channel") == "global");
		assert(message.at("messageId") == 1);
		assert(message.at("fromPlayerId") == alice);
		assert(message.at("fromName") == "Nomad");
		assert(message.at("displayName") == "Nomad");
		assert(message.at("text") == "hello");
		assert(message.at("serverTime") == 101.0);
		const auto relayedMessage = DecodePacket(*relayed);
		assert(relayedMessage.at("displayName") == "Stranger");
		assert(!relayedMessage.contains("fromName"));
		assert(relayedMessage.at("text") == "hello");
		assert(host.Stats().chatReceived == 1);
		assert(host.Stats().chatRelayed == 1);
		assert(host.Stats().chatRejected == 0);
	}

	{
		ProtocolHost host{ "Chat Identity" };
		host.SetChatEnabled(true);
		const auto alice = JoinPlayer(host, 10, 1.0, 100.0);
		JoinPlayer(host, 20, 1.1, 100.1);
		host.HandleMessage(10, EncodeJson(PlayerName("Nomad")), 1.5, 100.5);
		Json spoofed = ChatSend("spoof");
		spoofed["fromName"] = "Admin";
		spoofed["fromPlayerId"] = 999;
		const auto frames = host.HandleMessage(10, EncodeJson(spoofed), 2.0, 101.0);
		assert(FindFrame(frames, 20, "chatMessage") != nullptr);
		const auto message = DecodePacket(*FindFrame(frames, 20, "chatMessage"));
		assert(message.at("displayName") == "Stranger");
		assert(!message.contains("fromName"));
		assert(message.at("fromPlayerId") == alice);

		host.HandleMessage(20, EncodeJson(PlayerName("[ADMIN] Bob")), 2.1, 101.1);
		const auto named = host.HandleMessage(20, EncodeJson(ChatSend("hi")), 2.2, 101.2);
		assert(FindFrame(named, 10, "chatMessage") != nullptr);
		assert(DecodePacket(*FindFrame(named, 20, "chatMessage")).at("fromName") == "Bob");
		assert(DecodePacket(*FindFrame(named, 10, "chatMessage")).at("displayName") == "Stranger");
		assert(!DecodePacket(*FindFrame(named, 10, "chatMessage")).contains("fromName"));

		JoinPlayer(host, 30, 2.3, 101.3);
		const auto unnamed = host.HandleMessage(30, EncodeJson(ChatSend("anon")), 2.4, 101.4);
		assert(FindFrame(unnamed, 10, "chatMessage") != nullptr);
		assert(DecodePacket(*FindFrame(unnamed, 30, "chatMessage")).at("fromName") == "Player");
		assert(DecodePacket(*FindFrame(unnamed, 10, "chatMessage")).at("displayName") == "Stranger");
		assert(!DecodePacket(*FindFrame(unnamed, 10, "chatMessage")).contains("fromName"));
	}

	{
		ProtocolHost host{ "Chat Text Rules" };
		host.SetChatEnabled(true);
		JoinPlayer(host, 10, 1.0, 100.0);
		JoinPlayer(host, 20, 1.1, 100.1);
		AssertChatRejected(host, 10, ChatSend("   "), 2.0, 101.0, "invalidText");
		AssertChatRejected(host, 10, ChatSend(""), 2.1, 101.1, "invalidText");
		AssertChatRejected(host, 10, ChatSend(Repeat("a", 257)), 2.2, 101.2, "tooLong");
		AssertChatRejected(host, 10, ChatSend("hi", "party"), 2.3, 101.3, "channelUnavailable");

		const auto frames = host.HandleMessage(10, EncodeJson(ChatSend("  a\tb\n\x01 c  ")), 2.4, 101.4);
		assert(FindFrame(frames, 10, "chatMessage") != nullptr);
		assert(DecodePacket(*FindFrame(frames, 10, "chatMessage")).at("text") == "ab c");

		const auto longest = host.HandleMessage(20, EncodeJson(ChatSend(Repeat("\xc3\xa9", 256))), 2.5, 101.5);
		assert(FindFrame(longest, 10, "chatMessage") != nullptr);
		assert(DecodePacket(*FindFrame(longest, 10, "chatMessage")).at("text") == Repeat("\xc3\xa9", 256));

		const auto invalidBytes = std::string{ "{\"type\":\"chatSend\",\"channel\":\"global\",\"text\":\"\xff\"}" };
		const auto before = host.Stats();
		assert(host.HandleMessage(10, std::vector<std::uint8_t>(invalidBytes.begin(), invalidBytes.end()), 2.6, 101.6).empty());
		assert(host.Stats().packetsRejected == before.packetsRejected + 1);
		assert(host.Stats().chatReceived == before.chatReceived);
		const auto beforeMalformed = host.Stats();
		assert(host.HandleMessage(10, EncodeJson(Json{ { "type", "chatSend" }, { "channel", "global" } }), 2.7, 101.7).empty());
		assert(host.Stats().packetsRejected == beforeMalformed.packetsRejected + 1);
		assert(host.Stats().chatRejected == beforeMalformed.chatRejected + 1);
	}

	{
		ProtocolHost host{ "Chat Rate Limit" };
		host.SetChatEnabled(true);
		JoinPlayer(host, 10, 1.0, 100.0);
		for (int index = 1; index <= 5; ++index) {
			const auto frames = host.HandleMessage(10, EncodeJson(ChatSend("m" + std::to_string(index))), 10.0 + index * 0.1, 101.0);
			assert(FindFrame(frames, 10, "chatMessage") != nullptr);
		}
		AssertChatRejected(host, 10, ChatSend("m6"), 10.6, 101.6, "rateLimited");
		AssertChatSilent(host, 10, ChatSend("m7"), 10.7, 101.7);
		assert(FindFrame(host.HandleMessage(10, EncodeJson(ChatSend("m8")), 20.6, 102.0), 10, "chatMessage") != nullptr);
		for (int index = 1; index <= 4; ++index) {
			assert(FindFrame(host.HandleMessage(10, EncodeJson(ChatSend("n" + std::to_string(index))), 21.0 + index * 0.1, 102.5), 10, "chatMessage") != nullptr);
		}
		AssertChatRejected(host, 10, ChatSend("n5"), 21.5, 102.6, "rateLimited");
	}

	{
		ProtocolHost host{ "Chat Duplicate" };
		host.SetChatEnabled(true);
		JoinPlayer(host, 10, 1.0, 100.0);
		assert(FindFrame(host.HandleMessage(10, EncodeJson(ChatSend("same")), 10.0, 101.0), 10, "chatMessage") != nullptr);
		assert(FindFrame(host.HandleMessage(10, EncodeJson(ChatSend("same")), 10.5, 101.5), 10, "chatMessage") != nullptr);
		AssertChatRejected(host, 10, ChatSend("same"), 11.0, 102.0, "duplicate");
		assert(FindFrame(host.HandleMessage(10, EncodeJson(ChatSend("other")), 11.5, 102.5), 10, "chatMessage") != nullptr);
		AssertChatSilent(host, 10, ChatSend("same"), 12.0, 103.0);
		assert(FindFrame(host.HandleMessage(10, EncodeJson(ChatSend("same")), 41.0, 104.0), 10, "chatMessage") != nullptr);
	}

	{
		ProtocolHost host{ "Chat Mute" };
		host.SetChatEnabled(true);
		JoinPlayer(host, 30, 1.0, 100.0);
		JoinPlayer(host, 40, 1.1, 100.1);
		host.BindSessionProfile(30, "profile-dana");
		host.SetChatMuted("profile-dana", true);
		AssertChatRejected(host, 30, ChatSend("muted"), 2.0, 101.0, "muted");
		assert(FindFrame(host.HandleMessage(40, EncodeJson(ChatSend("open")), 2.1, 101.1), 30, "chatMessage") != nullptr);
		host.SetChatMuted("profile-dana", false);
		assert(FindFrame(host.HandleMessage(30, EncodeJson(ChatSend("back")), 2.2, 101.2), 40, "chatMessage") != nullptr);
	}

	{
		ProtocolHost host{ "Chat History" };
		host.SetChatEnabled(true);
		const auto empty = JoinPlayer(host, 90, 1.0, 100.0);
		assert(empty != 0);
		const auto emptyHistory = host.HandleMessage(90, EncodeJson(ChatHistoryRequest()), 1.5, 100.5);
		assert(FindFrame(emptyHistory, 90, "chatHistory") != nullptr);
		assert(DecodePacket(*FindFrame(emptyHistory, 90, "chatHistory")).at("messages").empty());
		for (std::uint32_t connection = 10; connection <= 50; connection += 10) {
			JoinPlayer(host, connection, 2.0, 101.0);
		}
		for (int index = 0; index < 25; ++index) {
			const auto connection = static_cast<std::uint32_t>(10 + (index / 5) * 10);
			const auto frames = host.HandleMessage(connection, EncodeJson(ChatSend("m" + std::to_string(index))), 5.0 + index * 0.01, 102.0);
			assert(FindFrame(frames, 10, "chatMessage") != nullptr);
		}
		const auto history = host.HandleMessage(10, EncodeJson(ChatHistoryRequest()), 6.0, 103.0);
		const auto* reply = FindFrame(history, 10, "chatHistory");
		assert(reply != nullptr);
		const auto messages = DecodePacket(*reply).at("messages");
		assert(messages.size() == 20);
		assert(messages.front().at("text") == "m5");
		assert(messages.back().at("text") == "m24");
		assert(messages.front().at("messageId") == 6);
		assert(messages.back().at("messageId") == 25);
		for (std::size_t index = 1; index < messages.size(); ++index) {
			assert(messages[index - 1].at("messageId").get<std::uint64_t>() < messages[index].at("messageId").get<std::uint64_t>());
		}

		AssertChatRejected(host, 10, ChatHistoryRequest(), 7.0, 103.5, "rateLimited");
		AssertChatSilent(host, 10, ChatHistoryRequest(), 7.5, 103.6);
		assert(FindFrame(host.HandleMessage(10, EncodeJson(ChatHistoryRequest()), 8.0, 104.0), 10, "chatHistory") != nullptr);
	}

	{
		ProtocolHost host{ "Chat Escaped Length" };
		host.SetChatEnabled(true);
		JoinPlayer(host, 10, 1.0, 100.0);
		std::string escaped = "{\"type\":\"chatSend\",\"channel\":\"global\",\"text\":\"";
		escaped += Repeat("\\uD83D\\uDE00", 256);
		escaped += "\"}";
		assert(escaped.size() > 2 * 1024 && escaped.size() < 4 * 1024);
		const auto frames = host.HandleMessage(10, std::vector<std::uint8_t>(escaped.begin(), escaped.end()), 2.0, 101.0);
		assert(FindFrame(frames, 10, "chatMessage") != nullptr);
		assert(DecodePacket(*FindFrame(frames, 10, "chatMessage")).at("text") == Repeat("\xf0\x9f\x98\x80", 256));
	}

	{
		ProtocolHost host{ "Chat Rejected Not Stored" };
		host.SetChatEnabled(true);
		JoinPlayer(host, 10, 1.0, 100.0);
		assert(FindFrame(host.HandleMessage(10, EncodeJson(ChatSend("kept")), 2.0, 101.0), 10, "chatMessage") != nullptr);
		AssertChatRejected(host, 10, ChatSend(Repeat("a", 257)), 2.1, 101.1, "tooLong");
		AssertChatRejected(host, 10, ChatSend("hidden", "party"), 2.2, 101.2, "channelUnavailable");
		AssertChatRejected(host, 10, ChatSend("   "), 2.3, 101.3, "invalidText");
		const auto history = host.HandleMessage(10, EncodeJson(ChatHistoryRequest()), 3.0, 101.4);
		assert(FindFrame(history, 10, "chatHistory") != nullptr);
		const auto messages = DecodePacket(*FindFrame(history, 10, "chatHistory")).at("messages");
		assert(messages.size() == 1);
		assert(messages.front().at("text") == "kept");
	}

	{
		ProtocolHost host{ "Chat Inactive" };
		host.SetChatEnabled(true);
		host.HandleConnected(60, 100.0);
		const auto before = host.Stats();
		assert(host.HandleMessage(60, EncodeJson(ChatSend("ghost")), 1.0, 100.1).empty());
		assert(host.HandleMessage(60, EncodeJson(ChatHistoryRequest()), 1.1, 100.2).empty());
		assert(host.Stats().packetsRejected == before.packetsRejected + 2);
		assert(host.Stats().chatReceived == before.chatReceived);
	}

	{
		ProtocolHost disabled{ "Chat Capability Disabled" };
		assert(!WelcomeHasCapability(disabled, 10, "chat-v1"));
		assert(!WelcomeHasCapability(disabled, 10, "faction-chat-v1"));
	}

	{
		ProtocolHost enabled{ "Chat Capability Enabled" };
		enabled.SetChatEnabled(true);
		assert(WelcomeHasCapability(enabled, 10, "chat-v1"));
		assert(!WelcomeHasCapability(enabled, 10, "faction-chat-v1"));
	}

	{
		ProtocolHost both{ "Chat And Factions Capabilities" };
		both.SetChatEnabled(true);
		assert(both.EnableFactions(nullptr));
		assert(WelcomeHasCapability(both, 10, "chat-v1"));
		assert(WelcomeHasCapability(both, 20, "factions-v1"));
		assert(WelcomeHasCapability(both, 30, "faction-chat-v1"));
	}

	return 0;
}

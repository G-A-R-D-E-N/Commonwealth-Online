// source:CO-BETA/docs/protocol/chat.md
#include "F4TChatValidation.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <string>

namespace
{
	using Json = nlohmann::json;
	using F4T::ChatValidation::ChatSendError;
	using F4T::ChatValidation::NormalizeChatSend;
	using F4T::ChatValidation::SanitizeChatDisplayName;

	Json Send(const std::string& a_text, const std::string& a_channel = "global")
	{
		return Json{ { "type", "chatSend" }, { "channel", a_channel }, { "text", a_text } };
	}

	std::string Accepted(const std::string& a_text)
	{
		const auto result = NormalizeChatSend(Send(a_text));
		assert(!result.error.has_value());
		return result.text;
	}

	ChatSendError Rejected(const std::string& a_text)
	{
		const auto result = NormalizeChatSend(Send(a_text));
		assert(result.error.has_value());
		assert(result.text.empty());
		return *result.error;
	}

	std::string Repeat(const std::string& a_unit, std::size_t a_count)
	{
		std::string output;
		for (std::size_t index = 0; index < a_count; ++index) {
			output += a_unit;
		}
		return output;
	}
}

int main()
{
	assert(Accepted("hello") == "hello");
	assert(Accepted("  a  \t b\n  ") == "a b");
	assert(Accepted("a\x01\x7f\xc2\x85\xc2\x9f" "b") == "ab");
	assert(Accepted("a\x0b\x0c" "b\r") == "ab");

	assert(Rejected("") == ChatSendError::InvalidText);
	assert(Rejected("   \t\n") == ChatSendError::InvalidText);
	assert(Rejected("\x01\x1f") == ChatSendError::InvalidText);

	assert(Accepted("a\xe2\x80\x8b" "b") == "ab");
	assert(Accepted("a\xe2\x80\x8c" "b") == "ab");
	assert(Accepted("a\xe2\x80\x8d" "b") == "ab");
	assert(Accepted("a\xe2\x80\x8e" "b") == "ab");
	assert(Accepted("a\xe2\x80\x8f" "b") == "ab");
	assert(Accepted("a\xe2\x80\xaa" "b") == "ab");
	assert(Accepted("a\xe2\x80\xae" "b") == "ab");
	assert(Accepted("a\xe2\x81\xa0" "b") == "ab");
	assert(Accepted("a\xe2\x81\xa4" "b") == "ab");
	assert(Accepted("a\xe2\x81\xa6" "b") == "ab");
	assert(Accepted("a\xe2\x81\xa9" "b") == "ab");
	assert(Accepted("a\xef\xbb\xbf" "b") == "ab");
	assert(Accepted("\xe2\x80\xae" "abc\xe2\x80\xac") == "abc");
	assert(Rejected("\xe2\x80\x8b   \xef\xbb\xbf") == ChatSendError::InvalidText);
	assert(Rejected("\xe2\x80\x8b\xe2\x80\xae") == ChatSendError::InvalidText);

	assert(Accepted("a\xd8\x9c" "b") == "ab");
	assert(Accepted("a\xd8\x80" "b") == "ab");
	assert(Accepted("a\xf0\x91\x82\xbd" "b") == "ab");
	assert(Accepted("a\xc2\xad" "b") == "ab");
	assert(Accepted("a\xcd\x8f" "b") == "ab");
	assert(Accepted("a\xe1\x85\x9f" "b") == "ab");
	assert(Accepted("a\xe1\x85\xa0" "b") == "ab");
	assert(Accepted("a\xe1\x9e\xb4" "b") == "ab");
	assert(Accepted("a\xe1\x9e\xb5" "b") == "ab");
	assert(Accepted("a\xe1\xa0\x8b" "b") == "ab");
	assert(Accepted("a\xe1\xa0\x8f" "b") == "ab");
	assert(Accepted("a\xe2\x80\xa8" "b") == "ab");
	assert(Accepted("a\xe2\x80\xa9" "b") == "ab");
	assert(Accepted("a\xe2\xa0\x80" "b") == "ab");
	assert(Accepted("a\xe3\x85\xa4" "b") == "ab");
	assert(Accepted("a\xef\xbe\xa0" "b") == "ab");
	assert(Accepted("a\xef\xbf\xb9" "b") == "ab");
	assert(Accepted("a\xef\xbf\xbb" "b") == "ab");
	assert(Accepted("a\xf3\xa0\x80\x81" "b") == "ab");
	assert(Accepted("a\xf3\xa0\x81\xbf" "b") == "ab");
	assert(Rejected("\xe3\x85\xa4") == ChatSendError::InvalidText);
	assert(Rejected("\xe3\x80\x80") == ChatSendError::InvalidText);
	assert(Rejected("\xe2\xa0\x80") == ChatSendError::InvalidText);
	assert(Rejected("\xe2\x81\xa5") == ChatSendError::InvalidText);
	assert(Rejected("\xef\xbf\xb0") == ChatSendError::InvalidText);
	assert(Rejected("\xe2\x80\x8b\xe3\x85\xa4\xe3\x80\x80") == ChatSendError::InvalidText);

	assert(Accepted("a\xe3\x80\x80" "b") == "a b");
	assert(Accepted("a\xc2\xa0\xc2\xa0" "b") == "a b");
	assert(Accepted("a\xe2\x80\x82" "b") == "a b");
	assert(Accepted("a\xe2\x80\x82\xe2\x80\x8b\xe2\x80\x82" "b") == "a b");
	assert(Accepted("a\xe2\x80\x80\xe2\x80\x85\xe2\x81\x9f" "b") == "a b");

	assert(Accepted("a" + Repeat("\xcc\x81", 5)) == "a" + Repeat("\xcc\x81", 4));
	assert(Accepted("a" + Repeat("\xcc\x81", 4) + " " + Repeat("\xcc\x81", 2)) == "a" + Repeat("\xcc\x81", 4) + " " + Repeat("\xcc\x81", 2));

	assert(Rejected("\xe0\x80\xaf") == ChatSendError::InvalidText);
	assert(Rejected("\xf0\x80\x80\xaf") == ChatSendError::InvalidText);
	assert(Rejected("\xf5\x80\x80\x80") == ChatSendError::InvalidText);
	assert(Rejected("\xf0\x9f\x98") == ChatSendError::InvalidText);
	assert(Rejected("\xe2\x28\xa1") == ChatSendError::InvalidText);
	assert(Rejected("\x80") == ChatSendError::InvalidText);
	assert(Rejected("\xc0\xaf") == ChatSendError::InvalidText);
	assert(Rejected("\xed\xa0\x80") == ChatSendError::InvalidText);
	assert(Rejected("\xed\xbf\xbf") == ChatSendError::InvalidText);
	assert(Rejected("\xf4\x90\x80\x80") == ChatSendError::InvalidText);
	assert(Rejected("\xe2\x82") == ChatSendError::InvalidText);
	assert(Rejected("\xff") == ChatSendError::InvalidText);
	assert(Accepted("\xf4\x8f\xbf\xbf") == "\xf4\x8f\xbf\xbf");
	assert(Accepted("\xef\xbf\xbf") == "\xef\xbf\xbf");

	assert(Accepted(Repeat("a", 256)) == Repeat("a", 256));
	assert(Rejected(Repeat("a", 257)) == ChatSendError::TooLong);
	assert(Accepted("  " + Repeat("a", 256) + "  ") == Repeat("a", 256));
	assert(Accepted(Repeat("\xc3\xa9", 256)) == Repeat("\xc3\xa9", 256));
	assert(Rejected(Repeat("\xc3\xa9", 257)) == ChatSendError::TooLong);
	assert(Accepted(Repeat("\xf0\x9f\x98\x80", 256)) == Repeat("\xf0\x9f\x98\x80", 256));
	assert(Rejected(Repeat("\xf0\x9f\x98\x80", 257)) == ChatSendError::TooLong);

	assert(NormalizeChatSend(Send("hi")).channel == "global");
	assert(NormalizeChatSend(Send("  hi  ", "faction")).error == std::nullopt);
	assert(NormalizeChatSend(Send("  hi  ", "faction")).channel == "faction");
	assert(NormalizeChatSend(Send("  hi  ", "faction")).text == "hi");
	assert(NormalizeChatSend(Send("hi", "party")).error == ChatSendError::ChannelUnavailable);
	assert(NormalizeChatSend(Send("hi", "Faction")).error == ChatSendError::ChannelUnavailable);
	assert(NormalizeChatSend(Send("hi", "")).error == ChatSendError::ChannelUnavailable);
	assert(NormalizeChatSend(Json{ { "type", "chatSend" }, { "text", "hi" } }).error == ChatSendError::Malformed);
	assert(NormalizeChatSend(Json{ { "type", "chatSend" }, { "channel", "global" } }).error == ChatSendError::Malformed);
	assert(NormalizeChatSend(Json{ { "type", "chatSend" }, { "channel", "global" }, { "text", 5 } }).error == ChatSendError::Malformed);
	assert(NormalizeChatSend(Json{ { "type", "chatSend" }, { "channel", 1 }, { "text", "hi" } }).error == ChatSendError::Malformed);

	assert(SanitizeChatDisplayName("[ADMIN] Bob") == "Bob");
	assert(SanitizeChatDisplayName("(server)[mod] x") == "x");
	assert(SanitizeChatDisplayName("  {Discord}  <dev>  Nomad") == "Nomad");
	assert(SanitizeChatDisplayName("[administrator] Ann") == "Ann");
	assert(SanitizeChatDisplayName("[[admin]] Bob") == "Bob");
	assert(SanitizeChatDisplayName("[[[ADMIN]]] Bob") == "Bob");
	assert(SanitizeChatDisplayName("[ [ mod ] ] Bob") == "Bob");
	assert(SanitizeChatDisplayName("[Moderator]") == "Player");
	assert(SanitizeChatDisplayName("[Bob] Ann") == "[Bob] Ann");
	assert(SanitizeChatDisplayName("Bo\x01" "b\xe2\x80\x8b") == "Bob");
	assert(SanitizeChatDisplayName("\xe2\x80\xae" "Bob") == "Bob");
	assert(SanitizeChatDisplayName("") == "Player");
	assert(SanitizeChatDisplayName("\x01\xe2\x80\x8b") == "Player");
	assert(SanitizeChatDisplayName("\xe3\x85\xa4") == "Player");
	assert(SanitizeChatDisplayName("\xe3\x80\x80") == "Player");
	assert(SanitizeChatDisplayName("\xe2\xa0\x80") == "Player");
	assert(SanitizeChatDisplayName("\xff") == "Player");
	assert(SanitizeChatDisplayName(Repeat("n", 40)) == Repeat("n", 32));
	assert(SanitizeChatDisplayName(Repeat("\xc3\xa9", 40)) == Repeat("\xc3\xa9", 32));
	assert(SanitizeChatDisplayName(Repeat("a", 31) + "\xf0\x9f\x98\x80" + "bbb") == Repeat("a", 31) + "\xf0\x9f\x98\x80");

	assert(Rejected("\xef\xb8\x8f\xef\xb8\x8f\xef\xb8\x8f") == ChatSendError::InvalidText);
	assert(SanitizeChatDisplayName("\xef\xb8\x8f\xef\xb8\x8f\xef\xb8\x8f") == "Player");
	assert(Rejected("\xe2\x81\xaa") == ChatSendError::InvalidText);
	assert(SanitizeChatDisplayName("\xe2\x81\xaa") == "Player");
	assert(Rejected("\xcc\x81") == ChatSendError::InvalidText);
	assert(SanitizeChatDisplayName("\xcc\x81") == "Player");
	assert(Accepted("\xd7\x90" + Repeat("\xd6\xb8", 6)) == "\xd7\x90" + Repeat("\xd6\xb8", 4));
	assert(Accepted("\xe0\xb8\x81" + Repeat("\xe0\xb9\x88", 6)) == "\xe0\xb8\x81" + Repeat("\xe0\xb9\x88", 4));
	assert(Accepted("a\xe2\x81\xaa" "b") == "ab");
	assert(Accepted("a\xe2\x81\xaf" "b") == "ab");
	assert(Accepted("a\xf0\x9d\x85\xb3" "b") == "ab");
	assert(Accepted("a\xf0\x93\x90\xb0" "b") == "ab");
	assert(Accepted("a\xf0\x9b\xb2\xa0" "b") == "ab");
	assert(Accepted("\xe0\xb8\xb2") == "\xe0\xb8\xb2");
	assert(Accepted("\xd7\x83") == "\xd7\x83");
	assert(SanitizeChatDisplayName("System") == "Player");
	assert(SanitizeChatDisplayName("Server") == "Player");
	assert(SanitizeChatDisplayName("admin") == "Player");
	assert(SanitizeChatDisplayName("Bob") == "Bob");
	assert(SanitizeChatDisplayName("[admin] \xef\xb8\x8f") == "Player");
	assert(SanitizeChatDisplayName("[admin]\xcc\x81") == "Player");
	assert(SanitizeChatDisplayName("admin\xef\xb8\x8f") == "Player");
	assert(SanitizeChatDisplayName("admin\xf3\xa0\x82\x80") == "Player");
	assert(SanitizeChatDisplayName("\xf3\xa0\x87\xb0") == "Player");

	return 0;
}

#ifndef F4T_QUEST_CALLBACKS_TESTING
#include "pch.h"
#endif

#include "QuestCallbacks.h"

#include <cstring>
#include <format>
#include <nlohmann/json.hpp>

namespace F4T::Quests
{
	namespace
	{
		constexpr std::string_view kHexDigits = "0123456789abcdefABCDEF";
		// Wire caps; must stay equal to co-core/include/F4TQuestProtocol.h:kMaximumQuestKeyChars (6 + 1 + 128).
		constexpr std::size_t      kQuestKeyHexLength = 6;
		constexpr std::size_t      kQuestKeyPluginMaxChars = 128;
		constexpr std::size_t      kQuestKeyMaxLength = kQuestKeyHexLength + 1 + kQuestKeyPluginMaxChars;
		constexpr std::size_t      kErrorBufferSize = 512;

		IQuestHost* g_host = nullptr;

		char ToLowerAscii(char a_char)
		{
			return (a_char >= 'A' && a_char <= 'Z') ? static_cast<char>(a_char - 'A' + 'a') : a_char;
		}

		bool EqualsIgnoreCase(std::string_view a_left, std::string_view a_right)
		{
			if (a_left.size() != a_right.size()) {
				return false;
			}
			for (std::size_t index = 0; index < a_left.size(); ++index) {
				if (ToLowerAscii(a_left[index]) != ToLowerAscii(a_right[index])) {
					return false;
				}
			}
			return true;
		}

		bool HasPluginExtension(std::string_view a_plugin)
		{
			if (a_plugin.size() < 5) {
				return false;
			}
			const auto extension = a_plugin.substr(a_plugin.size() - 4);
			return EqualsIgnoreCase(extension, ".esm") ||
				EqualsIgnoreCase(extension, ".esp") ||
				EqualsIgnoreCase(extension, ".esl");
		}
	}

	bool IsValidQuestKey(std::string_view a_key)
	{
		if (a_key.empty() || a_key.size() > kQuestKeyMaxLength) {
			return false;
		}

		const auto separator = a_key.find(':');
		if (separator != kQuestKeyHexLength) {
			return false;
		}
		for (std::size_t index = 0; index < kQuestKeyHexLength; ++index) {
			if (kHexDigits.find(a_key[index]) == std::string_view::npos) {
				return false;
			}
		}

		const auto plugin = a_key.substr(separator + 1);
		if (plugin.find(':') != std::string_view::npos ||
			plugin.find('/') != std::string_view::npos ||
			plugin.find('\\') != std::string_view::npos) {
			return false;
		}
		return HasPluginExtension(plugin);
	}

	const char* ScopeName(Scope a_scope)
	{
		switch (a_scope) {
		case Scope::Shared:
			return "shared";
		case Scope::Solo:
			return "solo";
		case Scope::Party:
			return "party";
		case Scope::Held:
			return "held";
		}
		return "held";
	}

	std::optional<Scope> ParseScope(std::string_view a_scope)
	{
		if (a_scope == "shared") {
			return Scope::Shared;
		}
		if (a_scope == "solo") {
			return Scope::Solo;
		}
		if (a_scope == "party") {
			return Scope::Party;
		}
		if (a_scope == "held") {
			return Scope::Held;
		}
		return std::nullopt;
	}

	std::optional<Action> ParseAction(std::string_view a_json)
	{
		nlohmann::json parsed;
		try {
			parsed = nlohmann::json::parse(std::string(a_json));
		} catch (const std::exception&) {
			return std::nullopt;
		}
		if (!parsed.is_object()) {
			return std::nullopt;
		}

		const auto name = parsed.value("action", "");
		if (name == "close") {
			return Action{ ActionKind::Close, Scope::Shared, {} };
		}
		if (name == "refresh") {
			return Action{ ActionKind::Refresh, Scope::Shared, {} };
		}

		const auto quest = parsed.value("quest", "");
		if (!IsValidQuestKey(quest)) {
			return std::nullopt;
		}

		if (name == "setScope") {
			const auto scope = ParseScope(parsed.value("scope", ""));
			if (!scope || *scope == Scope::Held) {
				return std::nullopt;
			}
			return Action{ ActionKind::SetScope, *scope, quest };
		}

		return std::nullopt;
	}

	std::string FormatActionJson(const Action& a_action)
	{
		nlohmann::json out;
		switch (a_action.kind) {
		case ActionKind::SetScope:
			out = { { "action", "setScope" }, { "quest", a_action.quest }, { "scope", ScopeName(a_action.scope) } };
			break;
		case ActionKind::Close:
			out = { { "action", "close" } };
			break;
		case ActionKind::Refresh:
			out = { { "action", "refresh" } };
			break;
		}
		return out.dump();
	}

	std::string UnavailableStateJson()
	{
		return nlohmann::json{
			{ "enabled", false },
			{ "mode", "" },
			{ "quests", nlohmann::json::array() },
			{ "notices", nlohmann::json::array() }
		}.dump();
	}

	std::string FormatStateScript(std::string_view a_stateJson)
	{
		std::string normalized;
		try {
			normalized = nlohmann::json::parse(std::string(a_stateJson)).dump();
		} catch (const std::exception&) {
			normalized = UnavailableStateJson();
		}
		return std::format("if(window.coQuestSetState){{window.coQuestSetState({});}}", normalized);
	}

	void SetHost(IQuestHost* a_host)
	{
		g_host = a_host;
	}

	IQuestHost* GetHost()
	{
		return g_host;
	}

	std::string PullStateJson(std::string& a_error)
	{
		a_error.clear();
		auto* host = g_host;
		if (!host) {
			a_error = "no quest host registered";
			return UnavailableStateJson();
		}

		std::string buffer(kStateJsonMaxBytes, '\0');
		std::string error(kErrorBufferSize, '\0');
		const auto ok = host->GetStateJson(buffer.data(), buffer.size(), error.data(), error.size());
		const auto* terminator = static_cast<const char*>(std::memchr(buffer.data(), '\0', buffer.size()));
		const auto length = terminator ? static_cast<std::size_t>(terminator - buffer.data()) : buffer.size();
		if (!ok) {
			a_error = error.c_str();
			return UnavailableStateJson();
		}
		if (length == buffer.size()) {
			a_error = std::format("state JSON filled the {} byte buffer", kStateJsonMaxBytes);
			return UnavailableStateJson();
		}
		return std::string(buffer.data(), length);
	}

	void ForwardAction(const Action& a_action)
	{
		auto* host = g_host;
		if (!host) {
			return;
		}
		host->SendAction(FormatActionJson(a_action).c_str());
	}
}

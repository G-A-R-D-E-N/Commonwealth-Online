#ifndef F4T_QUEST_CALLBACKS_TESTING
#include "pch.h"
#endif
#include "F4TViewJson.h"

#include "QuestCallbacks.h"

#include <cstring>
#include <format>
#include <limits>
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
		// Wire cap; must stay equal to co-core/src/protocol/F4TQuestProtocol.cpp:kMaximumInviteIdChars.
		constexpr std::size_t      kInviteIdMaxLength = 64;
		constexpr std::string_view kInviteIdCharacters = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-";

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

		std::string StringField(const nlohmann::json& a_object, const char* a_key)
		{
			const auto found = a_object.find(a_key);
			if (found == a_object.end() || !found->is_string()) {
				return {};
			}
			return found->get<std::string>();
		}

		std::optional<std::uint32_t> ParseTarget(const nlohmann::json& a_object)
		{
			const auto found = a_object.find("target");
			if (found == a_object.end() || !found->is_number_unsigned()) {
				return std::nullopt;
			}
			const auto value = found->get<std::uint64_t>();
			if (value == 0 || value > (std::numeric_limits<std::uint32_t>::max)()) {
				return std::nullopt;
			}
			return static_cast<std::uint32_t>(value);
		}

		std::optional<std::string> ParseInviteId(const nlohmann::json& a_object)
		{
			const auto id = StringField(a_object, "inviteId");
			if (id.empty() || id.size() > kInviteIdMaxLength) {
				return std::nullopt;
			}
			for (const char character : id) {
				if (kInviteIdCharacters.find(character) == std::string_view::npos) {
					return std::nullopt;
				}
			}
			return id;
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
		case Scope::Fireteam:
			return "fireteam";
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
		if (a_scope == "fireteam") {
			return Scope::Fireteam;
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

		const auto name = StringField(parsed, "action");
		if (name == "close") {
			return Action{ ActionKind::Close, Scope::Shared, {} };
		}
		if (name == "refresh") {
			return Action{ ActionKind::Refresh, Scope::Shared, {} };
		}
		if (name == "fireteamLeave") {
			return Action{ ActionKind::FireteamLeave, Scope::Shared, {} };
		}
		if (name == "fireteamInvite" || name == "fireteamKick") {
			const auto target = ParseTarget(parsed);
			if (!target) {
				return std::nullopt;
			}
			const auto kind = name == "fireteamInvite" ? ActionKind::FireteamInvite : ActionKind::FireteamKick;
			return Action{ kind, Scope::Shared, {}, *target };
		}
		if (name == "fireteamRespond") {
			const auto inviteId = ParseInviteId(parsed);
			const auto accept = parsed.find("accept");
			if (!inviteId || accept == parsed.end() || !accept->is_boolean()) {
				return std::nullopt;
			}
			return Action{ ActionKind::FireteamRespond, Scope::Shared, {}, 0, *inviteId, accept->get<bool>() };
		}

		const auto quest = StringField(parsed, "quest");
		if (!IsValidQuestKey(quest)) {
			return std::nullopt;
		}

		if (name == "setScope") {
			const auto scope = ParseScope(StringField(parsed, "scope"));
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
		case ActionKind::FireteamInvite:
			out = { { "action", "fireteamInvite" }, { "target", a_action.target } };
			break;
		case ActionKind::FireteamKick:
			out = { { "action", "fireteamKick" }, { "target", a_action.target } };
			break;
		case ActionKind::FireteamRespond:
			out = { { "action", "fireteamRespond" }, { "inviteId", a_action.inviteId }, { "accept", a_action.accept } };
			break;
		case ActionKind::FireteamLeave:
			out = { { "action", "fireteamLeave" } };
			break;
		}
		return F4T::ViewJson::Dump(out);
	}

	std::string UnavailableStateJson()
	{
		return F4T::ViewJson::Dump(nlohmann::json{
			{ "enabled", false },
			{ "mode", "" },
			{ "quests", nlohmann::json::array() },
			{ "notices", nlohmann::json::array() }
		});
	}

	std::string FormatStateScript(std::string_view a_stateJson)
	{
		std::string normalized;
		try {
			normalized = F4T::ViewJson::Dump(nlohmann::json::parse(std::string(a_stateJson)));
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

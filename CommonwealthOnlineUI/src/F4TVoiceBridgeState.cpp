#ifndef F4T_VOICE_BRIDGE_TESTING
#include "pch.h"
#endif

#include "F4TVoiceBridgeState.h"

#include <algorithm>
#include <array>
#include <format>

namespace F4T::VoiceBridge
{
	namespace
	{
		constexpr std::array<std::string_view, 4> kAllowedActionOps{
			"setSetting",
			"beginKeyCapture",
			"mutePlayer",
			"setPlayerVolume"
		};

		constexpr std::array<std::string_view, 6> kEventKinds{
			"status",
			"settings",
			"players",
			"speakers",
			"keyCapture",
			"level"
		};

		std::string_view ReadString(const nlohmann::json& a_json, const char* a_key) noexcept
		{
			if (!a_json.is_object()) {
				return {};
			}
			const auto entry = a_json.find(a_key);
			if (entry == a_json.end() || !entry->is_string()) {
				return {};
			}
			return entry->get_ref<const std::string&>();
		}

		bool IsTrueMember(const std::optional<nlohmann::json>& a_event, const char* a_key)
		{
			if (!a_event) {
				return false;
			}
			const auto entry = a_event->find(a_key);
			return entry != a_event->end() && entry->is_boolean() && entry->get<bool>();
		}

		constexpr std::size_t kMaxNestingDepth = 8;

		bool WithinDepth(const nlohmann::json& a_node, std::size_t a_depth)
		{
			if (!a_node.is_structured()) {
				return true;
			}
			if (a_depth > kMaxNestingDepth) {
				return false;
			}
			return std::all_of(a_node.begin(), a_node.end(), [a_depth](const nlohmann::json& a_child) {
				return WithinDepth(a_child, a_depth + 1);
			});
		}

		std::optional<nlohmann::json> ParseObject(std::string_view a_json)
		{
			auto parsed = nlohmann::json::parse(a_json.begin(), a_json.end(), nullptr, false);
			if (parsed.is_discarded() || !parsed.is_object() || !WithinDepth(parsed, 1)) {
				return std::nullopt;
			}
			return parsed;
		}
	}

	std::optional<std::string> AllowedActionOp(std::string_view a_json)
	{
		if (a_json.size() > kActionMaxBytes) {
			return std::nullopt;
		}
		const auto action = ParseObject(a_json);
		if (!action) {
			return std::nullopt;
		}
		const auto op = ReadString(*action, "op");
		if (!std::ranges::contains(kAllowedActionOps, op)) {
			return std::nullopt;
		}
		return std::string(op);
	}

	std::optional<nlohmann::json> ParseEvent(std::string_view a_json)
	{
		if (a_json.size() > kEventMaxBytes) {
			return std::nullopt;
		}
		auto event = ParseObject(a_json);
		if (!event || !std::ranges::contains(kEventKinds, ReadString(*event, "kind"))) {
			return std::nullopt;
		}
		return event;
	}

	std::string EventScript(const nlohmann::json& a_event)
	{
		return std::format("if(window.coVoiceEvent){{window.coVoiceEvent({});}}", a_event.dump(-1, ' ', true));
	}

	void EventCache::Store(const nlohmann::json& a_event)
	{
		const auto kind = ReadString(a_event, "kind");
		if (kind == "status") {
			m_status = a_event;
		} else if (kind == "settings") {
			m_settings = a_event;
		} else if (kind == "players") {
			m_players = a_event;
		} else if (kind == "speakers") {
			m_speakers = a_event;
		}
	}

	std::vector<std::string> EventCache::ReplayScripts() const
	{
		std::vector<std::string> scripts;
		for (const auto* cached : { &m_status, &m_settings, &m_players, &m_speakers }) {
			if (*cached) {
				scripts.push_back(EventScript(**cached));
			}
		}
		return scripts;
	}

	bool EventCache::SessionActive() const
	{
		return IsTrueMember(m_status, "active");
	}

	bool EventCache::MicHot() const
	{
		return IsTrueMember(m_status, "micHot");
	}

	bool HudWanted(const EventCache& a_cache, bool a_mainMenuVisible, bool a_browserVisible, bool a_sessionLockout)
	{
		return a_cache.SessionActive() && (a_cache.MicHot() || (!a_mainMenuVisible && !a_browserVisible && !a_sessionLockout));
	}
}

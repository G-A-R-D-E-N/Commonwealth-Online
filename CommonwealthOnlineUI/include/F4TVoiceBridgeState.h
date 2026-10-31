#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::VoiceBridge
{
	constexpr std::size_t kActionMaxBytes = 4 * 1024;
	constexpr std::size_t kEventMaxBytes = 32 * 1024;

	std::optional<std::string> AllowedActionOp(std::string_view a_json);
	std::optional<nlohmann::json> ParseEvent(std::string_view a_json);
	std::string EventScript(const nlohmann::json& a_event);

	class EventCache
	{
	public:
		void Store(const nlohmann::json& a_event);
		[[nodiscard]] std::vector<std::string> ReplayScripts() const;
		[[nodiscard]] bool SessionActive() const;
		[[nodiscard]] bool MicHot() const;

	private:
		std::optional<nlohmann::json> m_status;
		std::optional<nlohmann::json> m_settings;
		std::optional<nlohmann::json> m_players;
		std::optional<nlohmann::json> m_speakers;
	};

	bool HudWanted(const EventCache& a_cache, bool a_mainMenuVisible, bool a_browserVisible, bool a_sessionLockout);
}

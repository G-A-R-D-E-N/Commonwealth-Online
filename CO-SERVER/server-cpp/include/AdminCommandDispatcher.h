#pragma once

#include "F4TQuestProtocol.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace CommonwealthOnline::Server
{
	struct AdminActionResult
	{
		bool ok{};
		std::string message;
		nlohmann::json data{ nullptr };
	};

	inline constexpr std::string_view kProfileSelectorPrefix = "profile:";

	using KnownProfileLookup = std::function<bool(std::string_view)>;

	[[nodiscard]] std::optional<std::string> ResolveProfileSelector(
		std::string_view a_selector,
		const std::function<std::optional<std::string>(std::uint32_t)>& a_lookupByPlayerId,
		const KnownProfileLookup& a_isKnownProfile = {});

	struct AdminCommandHooks
	{
		std::function<AdminActionResult()> shutdown;
		std::function<nlohmann::json()> status;
		std::function<nlohmann::json()> clients;
		std::function<nlohmann::json()> bans;
		std::function<AdminActionResult(std::uint32_t, std::string_view)> kick;
		std::function<AdminActionResult(std::uint32_t, std::string_view)> banPlayer;
		std::function<AdminActionResult(std::string_view, std::string_view)> banIp;
		std::function<AdminActionResult(std::string_view)> unban;
		std::function<AdminActionResult(std::string_view, std::string_view)> banProfile;
		std::function<AdminActionResult(std::string_view)> unbanProfile;
		std::function<AdminActionResult(std::string_view)> resetIdentity;
		std::function<AdminActionResult(std::uint32_t)> revive;
		std::function<AdminActionResult(std::uint32_t)> killPlayer;
		std::function<AdminActionResult(std::optional<std::uint32_t>, std::string_view, bool, std::optional<double>)> voiceMute;
		std::function<AdminActionResult(std::string_view)> setWorldTime;
		std::function<AdminActionResult(std::string_view)> setWorldWeather;
		std::function<AdminActionResult(std::string_view)> questMode;
		std::function<AdminActionResult(const F4T::QuestProtocol::FormKey&, std::uint16_t, bool)> questGate;
		std::function<AdminActionResult(std::string_view, const F4T::QuestProtocol::FormKey&, bool)> questSolo;
		std::function<nlohmann::json()> worldMode;
		std::function<AdminActionResult(std::string_view)> setWorldMode;
		std::function<AdminActionResult()> reloadWorldPolicy;
		std::function<nlohmann::json()> factions;
		std::function<AdminActionResult(std::string_view)> disbandFaction;
		std::function<AdminActionResult(std::string_view, std::string_view)> promoteFaction;
		std::function<AdminActionResult(std::string_view, std::string_view)> kickFaction;
		std::function<AdminActionResult(std::string_view, std::string_view)> steamLookup;
		std::function<AdminActionResult(std::string_view, std::string_view)> banSteamId;
		std::function<AdminActionResult(std::uint32_t, std::string_view)> banPlayerSteamId;
		std::function<AdminActionResult(std::string_view)> unbanSteamId;
		std::function<nlohmann::json()> steamFlags;
		std::function<AdminActionResult(std::string_view, std::string_view)> clearSteamFlags;
	};

	class AdminCommandDispatcher
	{
	public:
		explicit AdminCommandDispatcher(AdminCommandHooks a_hooks);
		[[nodiscard]] nlohmann::json Dispatch(const nlohmann::json& a_request) const;

	private:
		[[nodiscard]] nlohmann::json Action(const AdminActionResult& a_result, const nlohmann::json& a_request) const;

		AdminCommandHooks _hooks;
	};
}

#pragma once

#include "ServerEvents.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace CommonwealthOnline::Server::Scripting
{
	using ScriptEventFilter = std::function<bool(const nlohmann::json&)>;

	struct ScriptHost
	{
		std::function<nlohmann::json()> listPlayers;
		std::function<nlohmann::json(std::uint32_t)> getPlayer;
		std::function<bool(std::uint32_t, std::string_view)> sendChat;
		std::function<void(std::string_view)> broadcastChat;
		std::function<nlohmann::json()> readWorld;
		std::function<nlohmann::json()> readPolicy;
		std::function<bool(const nlohmann::json&, std::string&)> setWorldPolicy;
		std::function<nlohmann::json()> listFactions;
		std::function<bool(std::string_view, std::string&)> disbandFaction;
		std::function<bool(std::string_view, std::string_view, std::string&)> promoteFaction;
		std::function<bool(std::string_view, std::string_view, std::string&)> kickFaction;
		std::function<nlohmann::json()> questStatus;
		std::function<bool(std::string_view)> setQuestMode;
		std::function<bool(std::string_view, std::uint16_t, bool)> setQuestGate;
		std::function<bool(std::string_view, std::string_view, bool)> setQuestSolo;
		std::function<bool(std::uint64_t, std::string_view, std::string&)> setSettlementOwner;
		std::function<nlohmann::json(std::string_view)> readState;
		std::function<bool(std::string_view, std::string_view, const nlohmann::json&, std::string&)> setState;
		std::function<std::uint64_t(ServerEventKind, ScriptEventFilter)> subscribeEvent;
		std::function<void(std::uint64_t)> unsubscribeEvent;
		std::function<bool(std::string_view, std::uint32_t, std::string_view, std::string&)> openView;
		std::function<bool(std::string_view, std::uint32_t, std::string&)> closeView;
		std::function<bool(std::uint32_t, std::string&)> revivePlayer;
		std::function<bool(std::uint32_t, std::string&)> killPlayer;
		std::function<bool(std::uint32_t, std::string_view)> messagePlayer;
		std::function<nlohmann::json(std::uint64_t)> settlementOwner;
		std::function<double()> nowMonotonicSeconds;
	};
}

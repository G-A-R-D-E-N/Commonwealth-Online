#pragma once

#include "F4TTradeLedger.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace F4T::TradeClient
{
	using Json = nlohmann::json;
	using Line = F4T::Trade::Line;

	struct Trade
	{
		std::string tradeId;
		std::string phase;
		std::string role;
		std::uint32_t partnerPlayerId{};
		std::vector<Line> ownOffer;
		bool ownConfirmed{};
		std::vector<Line> partnerOffer;
		bool partnerConfirmed{};
		std::uint32_t offerVersion{};
		bool confirmSent{};
	};

	bool RequestTrade(std::uint32_t a_targetPlayerId);
	bool RespondTrade(const std::string& a_tradeId, bool a_accept);
	bool SetOffer(const std::string& a_tradeId, const std::vector<Line>& a_lines);
	bool ConfirmTrade(const std::string& a_tradeId, std::uint32_t a_offerVersion);
	bool CancelTrade(const std::string& a_tradeId);
	void HandlePacket(const Json& a_packet);
	[[nodiscard]] std::optional<Trade> CurrentTrade();
	[[nodiscard]] bool HasPendingCommits();
	void Tick();
	void Reset();
}

#pragma once

#include <nlohmann/json.hpp>

#include "F4TVendorTrade.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace F4T::VendorTradeEscrow
{
	namespace VT = F4T::VendorTrade;

	using Clock = std::chrono::steady_clock;

	inline constexpr auto kResendDelay = std::chrono::seconds{ 5 };
	inline constexpr std::size_t kMaximumContextEntries = 64;
	inline constexpr std::size_t kMaximumParkedEntries = 256;
	inline constexpr std::size_t kMaximumParkedBytes = 1024U * 1024U;

	struct Context
	{
		std::string serverKey;
		std::string worldId;
		std::string profileId;
		std::optional<std::uint32_t> characterSlot;

		bool operator==(const Context&) const = default;
	};

	struct Escrow
	{
		std::string operationId;
		std::vector<VT::StockLine> taken;
		std::uint32_t capsTaken{};
		std::vector<VT::StockLine> grant;
		std::uint32_t capsGrant{};
		nlohmann::json packet;
		std::optional<Clock::time_point> nextResend;
		Context context;
	};

	struct Payout
	{
		Escrow escrow;
		bool grant{};
	};

	struct Parked
	{
		std::map<std::string, Escrow> unresolved;
		std::vector<Payout> payouts;
	};

	[[nodiscard]] bool MayReconcile(const Context& a_recorded, const Context& a_current);
	[[nodiscard]] std::vector<std::pair<std::string, nlohmann::json>> DueResends(std::map<std::string, Escrow>& a_unresolved, const Context& a_current, bool a_reconnected, Clock::time_point a_now);
	[[nodiscard]] std::optional<Escrow> TakeResolved(std::map<std::string, Escrow>& a_unresolved, const std::string& a_operationId, const Context& a_current);
	[[nodiscard]] std::vector<Payout> TakeDuePayouts(std::vector<Payout>& a_pending, const Context& a_current);
	[[nodiscard]] std::size_t CountParkedFor(const std::map<std::string, Escrow>& a_unresolved, const std::vector<Payout>& a_payouts, const Context& a_current);
	[[nodiscard]] std::optional<std::string> EncodeParked(const Parked& a_parked);
	[[nodiscard]] bool FitsParked(const Parked& a_parked, const Escrow& a_escrow);
	[[nodiscard]] std::optional<Parked> DecodeParked(const std::string& a_text);
}

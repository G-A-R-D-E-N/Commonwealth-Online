#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace F4T::VendorTrade
{
	inline constexpr std::size_t MaximumStockLines = 512;
	inline constexpr std::size_t MaximumSoldLines = 256;
	inline constexpr std::size_t MaximumTradeLines = 128;
	inline constexpr std::size_t MaximumOperationIdLength = 64;
	inline constexpr std::uint32_t MaximumCount = 65535;
	inline constexpr std::uint32_t MaximumCaps = 10000000;
	inline constexpr std::uint32_t MaximumSeedCaps = 5000;
	inline constexpr std::uint32_t MaximumSeedCount = 1000;
	inline constexpr std::uint32_t SeedValueBudget = 100000;
	inline constexpr std::uint32_t DefaultRestockHours = 24;
	inline constexpr std::uint32_t MinimumRestockHours = 1;
	inline constexpr std::uint32_t MaximumRestockHours = 720;

	struct Identity
	{
		std::uint32_t npcId{};
		std::uint32_t sourceFormId{};
		std::uint32_t cellId{};
		std::uint32_t worldspaceId{};
		std::uint32_t authorityEpoch{};

		bool operator==(const Identity&) const = default;
	};

	struct StockLine
	{
		std::uint32_t formId{};
		std::uint32_t count{};

		bool operator==(const StockLine&) const = default;
	};

	struct LineDelta
	{
		std::uint32_t formId{};
		std::int32_t delta{};

		bool operator==(const LineDelta&) const = default;
	};

	struct StockSnapshot
	{
		Identity identity;
		std::uint32_t revision{};
		std::uint32_t authorityPlayerId{};
		std::uint32_t caps{};
		std::vector<StockLine> lines;

		bool operator==(const StockSnapshot&) const = default;
	};

	struct TradeRequest
	{
		Identity identity;
		std::string operationId;
		std::uint32_t baseRevision{};
		std::vector<LineDelta> lineDeltas;
		std::int32_t capsDelta{};
	};

	struct Outcome
	{
		bool accepted{};
		std::string reason;
		std::optional<StockSnapshot> stock;
	};

	struct TradeResult
	{
		std::string operationId;
		Outcome outcome;
	};

	using ItemValues = std::map<std::uint32_t, std::uint32_t>;

	[[nodiscard]] std::optional<StockSnapshot> ParseStockPublish(const nlohmann::json& a_packet);
	[[nodiscard]] nlohmann::json StockPublishToJson(const StockSnapshot& a_stock);
	[[nodiscard]] std::optional<StockSnapshot> ParseStockSnapshot(const nlohmann::json& a_packet);
	[[nodiscard]] nlohmann::json StockSnapshotToJson(const StockSnapshot& a_stock);
	[[nodiscard]] std::optional<TradeRequest> ParseTradeRequest(const nlohmann::json& a_packet);
	[[nodiscard]] nlohmann::json TradeRequestToJson(const TradeRequest& a_trade);
	[[nodiscard]] std::optional<TradeResult> ParseTradeResult(const nlohmann::json& a_packet);
	[[nodiscard]] nlohmann::json TradeResultToJson(const TradeResult& a_result);
	[[nodiscard]] bool ParseStockLines(const nlohmann::json& a_object, std::vector<StockLine>& a_out);
	[[nodiscard]] nlohmann::json StockLinesToJson(const std::vector<StockLine>& a_lines);

	[[nodiscard]] Outcome ApplyPublish(
		const std::optional<StockSnapshot>& a_current,
		const StockSnapshot& a_incoming,
		std::uint32_t a_publisherId,
		std::uint32_t a_hostPlayerId,
		const ItemValues& a_values);
	[[nodiscard]] std::optional<ItemValues> ParseItemValues(const nlohmann::json& a_table);
	[[nodiscard]] std::uint32_t SeedCountLimit(std::uint32_t a_baseValue);
	[[nodiscard]] std::uint32_t ClampRestockHours(std::int64_t a_hours);
	[[nodiscard]] std::uint32_t BuyPrice(std::uint32_t a_baseValue);
	[[nodiscard]] std::uint32_t SellPrice(std::uint32_t a_baseValue);
	[[nodiscard]] std::optional<std::int64_t> ExpectedCapsDelta(const TradeRequest& a_trade, const ItemValues& a_values);
	[[nodiscard]] Outcome ApplyTrade(const std::optional<StockSnapshot>& a_current, const TradeRequest& a_trade, const ItemValues& a_values);
}

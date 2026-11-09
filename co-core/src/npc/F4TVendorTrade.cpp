#include "F4TVendorTrade.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <set>
#include <system_error>
#include <utility>

namespace F4T::VendorTrade
{
	namespace
	{
		using Json = nlohmann::json;

		std::optional<std::uint32_t> ReadU32(const Json& a_packet, const char* a_key, std::uint32_t a_max = std::numeric_limits<std::uint32_t>::max())
		{
			const auto it = a_packet.find(a_key);
			if (it == a_packet.end() || !it->is_number_integer()) {
				return std::nullopt;
			}
			const auto value = it->get<std::int64_t>();
			if (value < 0 || value > static_cast<std::int64_t>(a_max)) {
				return std::nullopt;
			}
			return static_cast<std::uint32_t>(value);
		}

		std::optional<Identity> ParseIdentity(const Json& a_packet)
		{
			if (!a_packet.is_object()) {
				return std::nullopt;
			}
			const auto npcId = ReadU32(a_packet, "npcId");
			const auto cellId = ReadU32(a_packet, "cellId");
			if (!npcId || *npcId == 0 || !cellId) {
				return std::nullopt;
			}
			Identity identity;
			identity.npcId = *npcId;
			identity.cellId = *cellId;
			identity.sourceFormId = ReadU32(a_packet, "sourceFormId").value_or(0);
			identity.worldspaceId = ReadU32(a_packet, "worldspaceId").value_or(0);
			identity.authorityEpoch = ReadU32(a_packet, "authorityEpoch").value_or(0);
			return identity;
		}

		Json IdentityToJson(const Identity& a_identity)
		{
			return Json{
				{ "npcId", a_identity.npcId },
				{ "sourceFormId", a_identity.sourceFormId },
				{ "cellId", a_identity.cellId },
				{ "worldspaceId", a_identity.worldspaceId },
				{ "authorityEpoch", a_identity.authorityEpoch }
			};
		}

		bool ParseLines(const Json& a_packet, std::vector<StockLine>& a_out)
		{
			const auto it = a_packet.find("items");
			if (it == a_packet.end() || !it->is_array() || it->size() > MaximumStockLines) {
				return false;
			}
			std::set<std::uint32_t> seen;
			for (const auto& entry : *it) {
				if (!entry.is_object()) {
					return false;
				}
				const auto formId = ReadU32(entry, "formId");
				const auto count = ReadU32(entry, "count", MaximumCount);
				if (!formId || *formId == 0 || !count || *count == 0 || !seen.insert(*formId).second) {
					return false;
				}
				a_out.push_back(StockLine{ *formId, *count });
			}
			std::sort(a_out.begin(), a_out.end(), [](const StockLine& a_left, const StockLine& a_right) { return a_left.formId < a_right.formId; });
			return true;
		}

		Json LinesToJson(const std::vector<StockLine>& a_lines)
		{
			Json items = Json::array();
			for (const auto& line : a_lines) {
				items.push_back(Json{ { "formId", line.formId }, { "count", line.count } });
			}
			return items;
		}

		std::optional<StockSnapshot> ParseStock(const Json& a_packet)
		{
			auto identity = ParseIdentity(a_packet);
			const auto caps = ReadU32(a_packet, "caps", MaximumCaps);
			if (!identity || !caps) {
				return std::nullopt;
			}
			StockSnapshot stock;
			stock.identity = *identity;
			stock.caps = *caps;
			if (!ParseLines(a_packet, stock.lines)) {
				return std::nullopt;
			}
			return stock;
		}

		Outcome Reject(const std::optional<StockSnapshot>& a_current, const char* a_reason)
		{
			return Outcome{ .accepted = false, .reason = a_reason, .stock = a_current };
		}
	}

	bool ParseStockLines(const Json& a_object, std::vector<StockLine>& a_out)
	{
		return ParseLines(a_object, a_out);
	}

	Json StockLinesToJson(const std::vector<StockLine>& a_lines)
	{
		return LinesToJson(a_lines);
	}

	std::optional<StockSnapshot> ParseStockPublish(const Json& a_packet)
	{
		return a_packet.is_object() ? ParseStock(a_packet) : std::nullopt;
	}

	Json StockPublishToJson(const StockSnapshot& a_stock)
	{
		Json packet = IdentityToJson(a_stock.identity);
		packet["caps"] = a_stock.caps;
		packet["items"] = LinesToJson(a_stock.lines);
		return packet;
	}

	std::optional<StockSnapshot> ParseStockSnapshot(const Json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		auto stock = ParseStock(a_packet);
		const auto revision = ReadU32(a_packet, "revision");
		const auto authority = ReadU32(a_packet, "authorityPlayerId");
		if (!stock || !revision || !authority) {
			return std::nullopt;
		}
		stock->revision = *revision;
		stock->authorityPlayerId = *authority;
		return stock;
	}

	Json StockSnapshotToJson(const StockSnapshot& a_stock)
	{
		Json packet = StockPublishToJson(a_stock);
		packet["revision"] = a_stock.revision;
		packet["authorityPlayerId"] = a_stock.authorityPlayerId;
		return packet;
	}

	std::optional<TradeRequest> ParseTradeRequest(const Json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto identity = ParseIdentity(a_packet);
		const auto base = ReadU32(a_packet, "baseRevision");
		const auto operation = a_packet.find("operationId");
		const auto capsIt = a_packet.find("capsDelta");
		const auto deltas = a_packet.find("deltas");
		if (!identity || !base || operation == a_packet.end() || !operation->is_string() || capsIt == a_packet.end() ||
			!capsIt->is_number_integer() || deltas == a_packet.end() || !deltas->is_array()) {
			return std::nullopt;
		}
		TradeRequest trade;
		trade.identity = *identity;
		trade.baseRevision = *base;
		trade.operationId = operation->get<std::string>();
		const auto caps = capsIt->get<std::int64_t>();
		if (trade.operationId.empty() || trade.operationId.size() > MaximumOperationIdLength || caps < -static_cast<std::int64_t>(MaximumCaps) ||
			caps > static_cast<std::int64_t>(MaximumCaps) || deltas->empty() || deltas->size() > MaximumTradeLines) {
			return std::nullopt;
		}
		trade.capsDelta = static_cast<std::int32_t>(caps);
		std::set<std::uint32_t> seen;
		for (const auto& entry : *deltas) {
			if (!entry.is_object()) {
				return std::nullopt;
			}
			const auto formId = ReadU32(entry, "formId");
			const auto it = entry.find("delta");
			if (!formId || *formId == 0 || it == entry.end() || !it->is_number_integer() || !seen.insert(*formId).second) {
				return std::nullopt;
			}
			const auto delta = it->get<std::int64_t>();
			if (delta == 0 || delta < -static_cast<std::int64_t>(MaximumCount) || delta > static_cast<std::int64_t>(MaximumCount)) {
				return std::nullopt;
			}
			trade.lineDeltas.push_back(LineDelta{ *formId, static_cast<std::int32_t>(delta) });
		}
		return trade;
	}

	Json TradeRequestToJson(const TradeRequest& a_trade)
	{
		Json packet = IdentityToJson(a_trade.identity);
		Json deltas = Json::array();
		for (const auto& line : a_trade.lineDeltas) {
			deltas.push_back(Json{ { "formId", line.formId }, { "delta", line.delta } });
		}
		packet["operationId"] = a_trade.operationId;
		packet["baseRevision"] = a_trade.baseRevision;
		packet["capsDelta"] = a_trade.capsDelta;
		packet["deltas"] = std::move(deltas);
		return packet;
	}

	std::optional<TradeResult> ParseTradeResult(const Json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto operation = a_packet.find("operationId");
		const auto accepted = a_packet.find("accepted");
		const auto reason = a_packet.find("reason");
		if (operation == a_packet.end() || !operation->is_string() || accepted == a_packet.end() || !accepted->is_boolean() ||
			reason == a_packet.end() || !reason->is_string()) {
			return std::nullopt;
		}
		TradeResult result;
		result.operationId = operation->get<std::string>();
		result.outcome.accepted = accepted->get<bool>();
		result.outcome.reason = reason->get<std::string>();
		const auto stock = a_packet.find("stock");
		if (stock != a_packet.end() && !stock->is_null()) {
			result.outcome.stock = ParseStockSnapshot(*stock);
			if (!result.outcome.stock) {
				return std::nullopt;
			}
		}
		if (result.outcome.accepted && !result.outcome.stock) {
			return std::nullopt;
		}
		return result;
	}

	Json TradeResultToJson(const TradeResult& a_result)
	{
		Json packet{
			{ "operationId", a_result.operationId },
			{ "accepted", a_result.outcome.accepted },
			{ "reason", a_result.outcome.reason }
		};
		if (a_result.outcome.stock) {
			packet["stock"] = StockSnapshotToJson(*a_result.outcome.stock);
		}
		return packet;
	}

	Outcome ApplyPublish(
		const std::optional<StockSnapshot>& a_current,
		const StockSnapshot& a_incoming,
		std::uint32_t a_publisherId,
		std::uint32_t a_hostPlayerId,
		const ItemValues& a_values)
	{
		StockSnapshot next = a_incoming;
		std::erase_if(next.lines, [&](const StockLine& a_line) { return !a_values.contains(a_line.formId); });
		if (!a_current) {
			next.caps = std::min(next.caps, MaximumSeedCaps);
			for (auto& line : next.lines) {
				line.count = std::min(line.count, SeedCountLimit(a_values.at(line.formId)));
			}
			next.revision = 1;
			next.authorityPlayerId = a_hostPlayerId;
			return Outcome{ .accepted = true, .reason = "accepted", .stock = std::move(next) };
		}
		if (a_publisherId != a_current->authorityPlayerId) {
			return Reject(a_current, "notAuthority");
		}
		next.caps = std::min(next.caps, a_current->caps);
		for (auto& line : next.lines) {
			const auto held = std::find_if(a_current->lines.begin(), a_current->lines.end(), [&](const StockLine& a_held) { return a_held.formId == line.formId; });
			line.count = held == a_current->lines.end() ? 0 : std::min(line.count, held->count);
		}
		std::erase_if(next.lines, [](const StockLine& a_line) { return a_line.count == 0; });
		next.revision = a_current->revision + 1;
		next.authorityPlayerId = a_current->authorityPlayerId;
		return Outcome{ .accepted = true, .reason = "accepted", .stock = std::move(next) };
	}

	std::optional<ItemValues> ParseItemValues(const Json& a_table)
	{
		if (!a_table.is_object()) {
			return std::nullopt;
		}
		ItemValues values;
		for (const auto& entry : a_table.items()) {
			const auto& key = entry.key();
			std::uint32_t itemId{};
			const auto parsed = std::from_chars(key.data(), key.data() + key.size(), itemId);
			const auto value = ReadU32(a_table, key.c_str());
			if (parsed.ec != std::errc{} || parsed.ptr != key.data() + key.size() || itemId == 0 || !value || !values.emplace(itemId, *value).second) {
				return std::nullopt;
			}
		}
		if (values.empty()) {
			return std::nullopt;
		}
		return values;
	}

	std::uint32_t SeedCountLimit(std::uint32_t a_baseValue)
	{
		return std::clamp<std::uint32_t>(SeedValueBudget / std::max<std::uint32_t>(1, a_baseValue), 1, MaximumSeedCount);
	}

	std::uint32_t ClampRestockHours(std::int64_t a_hours)
	{
		return static_cast<std::uint32_t>(std::clamp<std::int64_t>(a_hours, MinimumRestockHours, MaximumRestockHours));
	}

	std::uint32_t BuyPrice(std::uint32_t a_baseValue)
	{
		return std::max<std::uint32_t>(1, a_baseValue);
	}

	std::uint32_t SellPrice(std::uint32_t a_baseValue)
	{
		return std::max<std::uint32_t>(1, a_baseValue / 2);
	}

	std::optional<std::int64_t> ExpectedCapsDelta(const TradeRequest& a_trade, const ItemValues& a_values)
	{
		std::int64_t expected = 0;
		for (const auto& [itemId, delta] : a_trade.lineDeltas) {
			const auto value = a_values.find(itemId);
			if (value == a_values.end()) {
				return std::nullopt;
			}
			if (delta < 0) {
				expected += static_cast<std::int64_t>(BuyPrice(value->second)) * -static_cast<std::int64_t>(delta);
			} else {
				expected -= static_cast<std::int64_t>(SellPrice(value->second)) * static_cast<std::int64_t>(delta);
			}
		}
		return expected;
	}

	Outcome ApplyTrade(const std::optional<StockSnapshot>& a_current, const TradeRequest& a_trade, const ItemValues& a_values)
	{
		if (!a_current) {
			return Reject(a_current, "unseededVendor");
		}
		if (a_trade.baseRevision != a_current->revision) {
			return Reject(a_current, "staleRevision");
		}
		StockSnapshot next = *a_current;
		for (const auto& delta : a_trade.lineDeltas) {
			auto it = std::find_if(next.lines.begin(), next.lines.end(), [&](const StockLine& a_line) { return a_line.formId == delta.formId; });
			const std::int64_t have = it == next.lines.end() ? 0 : it->count;
			const std::int64_t after = have + delta.delta;
			if (after < 0) {
				return Reject(a_current, "insufficientStock");
			}
			const auto value = a_values.find(delta.formId);
			if (delta.delta > 0 && after > (value == a_values.end() ? MaximumCount : SeedCountLimit(value->second))) {
				return Reject(a_current, "stockLimit");
			}
			if (it == next.lines.end()) {
				if (next.lines.size() >= MaximumSoldLines) {
					return Reject(a_current, "stockLimit");
				}
				next.lines.push_back(StockLine{ delta.formId, static_cast<std::uint32_t>(after) });
			} else if (after == 0) {
				next.lines.erase(it);
			} else {
				it->count = static_cast<std::uint32_t>(after);
			}
		}
		if (next.lines.size() > MaximumStockLines) {
			return Reject(a_current, "stockLimit");
		}
		const auto expected = ExpectedCapsDelta(a_trade, a_values);
		if (!expected) {
			return Reject(a_current, "unknownItem");
		}
		if (a_trade.capsDelta != *expected) {
			return Reject(a_current, "mispriced");
		}
		const std::int64_t caps = static_cast<std::int64_t>(next.caps) + a_trade.capsDelta;
		if (caps < 0) {
			return Reject(a_current, "insufficientCaps");
		}
		if (caps > MaximumCaps) {
			return Reject(a_current, "capsLimit");
		}
		next.caps = static_cast<std::uint32_t>(caps);
		std::sort(next.lines.begin(), next.lines.end(), [](const StockLine& a_left, const StockLine& a_right) { return a_left.formId < a_right.formId; });
		next.revision = a_current->revision + 1;
		return Outcome{ .accepted = true, .reason = "accepted", .stock = std::move(next) };
	}
}

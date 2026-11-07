#include "ServerVendorTrade.h"

namespace CommonwealthOnline::Server
{
	namespace
	{
		using Json = nlohmann::json;
		namespace VT = F4T::VendorTrade;

		Json StatePacket(const VT::StockSnapshot& a_stock)
		{
			Json packet = VT::StockSnapshotToJson(a_stock);
			packet["type"] = "vendorStockState";
			return packet;
		}

		Json IdentityPacket(const char* a_type, const VT::Identity& a_identity, const char* a_reason)
		{
			Json packet = VT::StockPublishToJson(VT::StockSnapshot{ .identity = a_identity });
			packet.erase("items");
			packet.erase("caps");
			packet["type"] = a_type;
			packet["reason"] = a_reason;
			return packet;
		}
	}

	std::string VendorTradeBook::KeyOf(const VT::Identity& a_identity)
	{
		return std::to_string(a_identity.worldspaceId) + ":" + std::to_string(a_identity.cellId) + ":" + std::to_string(a_identity.npcId);
	}

	std::vector<VendorReply> VendorTradeBook::Publish(std::uint32_t a_playerId, const Json& a_packet, const AuthorityActive& a_isActive)
	{
		const auto incoming = VT::ParseStockPublish(a_packet);
		if (!incoming) {
			return {};
		}
		const auto key = KeyOf(incoming->identity);
		auto found = stocks.find(key);
		std::optional<VT::StockSnapshot> current;
		if (found != stocks.end()) {
			current = found->second;
			if (a_isActive && !a_isActive(current->authorityPlayerId)) {
				current->authorityPlayerId = a_playerId;
			}
		} else if (stocks.size() >= kMaximumVendorStocks) {
			stocks.erase(stocks.begin());
		}
		auto outcome = VT::ApplyPublish(current, *incoming, a_playerId, a_playerId);
		if (!outcome.accepted || !outcome.stock) {
			return { VendorReply{ false, IdentityPacket("vendorStockRejected", incoming->identity, outcome.reason.c_str()) } };
		}
		stocks[key] = *outcome.stock;
		return { VendorReply{ true, StatePacket(*outcome.stock) } };
	}

	std::vector<VendorReply> VendorTradeBook::Query(const Json& a_packet) const
	{
		if (!a_packet.is_object()) {
			return {};
		}
		Json identityOnly = a_packet;
		identityOnly["items"] = Json::array();
		identityOnly["caps"] = 0;
		const auto request = VT::ParseStockPublish(identityOnly);
		if (!request) {
			return {};
		}
		const auto found = stocks.find(KeyOf(request->identity));
		if (found == stocks.end()) {
			return { VendorReply{ false, IdentityPacket("vendorStockUnseeded", request->identity, "unseededVendor") } };
		}
		return { VendorReply{ false, StatePacket(found->second) } };
	}

	std::vector<VendorReply> VendorTradeBook::Trade(const std::string& a_profileKey, const Json& a_packet)
	{
		// Constraint: a keyless trade gets no reply; a refusal would make the client refund escrow the server may already have applied.
		if (a_profileKey.empty()) {
			return {};
		}
		const auto request = VT::ParseTradeRequest(a_packet);
		if (!request) {
			const auto operation = a_packet.is_object() ? a_packet.find("operationId") : a_packet.end();
			if (a_packet.is_object() && operation != a_packet.end() && operation->is_string() && !operation->get<std::string>().empty() &&
				operation->get<std::string>().size() <= VT::MaximumOperationIdLength) {
				Json refusal{ { "type", "vendorTradeResult" }, { "operationId", *operation }, { "accepted", false }, { "reason", "malformed" } };
				return { VendorReply{ false, std::move(refusal) } };
			}
			return {};
		}
		const auto now = std::chrono::steady_clock::now();
		std::erase_if(operations, [&](auto& a_entry) {
			auto& log = a_entry.second;
			while (!log.empty() && now - log.front().at > kVendorOperationRetention) {
				log.pop_front();
			}
			return log.empty();
		});
		auto& log = operations[a_profileKey];
		for (const auto& operation : log) {
			if (operation.operationId == request->operationId) {
				return { VendorReply{ false, operation.result } };
			}
		}
		const auto key = KeyOf(request->identity);
		const auto found = stocks.find(key);
		std::optional<VT::StockSnapshot> current;
		if (found != stocks.end()) {
			current = found->second;
		}
		const auto outcome = VT::ApplyTrade(current, *request);
		Json result = VT::TradeResultToJson(VT::TradeResult{ .operationId = request->operationId, .outcome = outcome });
		result["type"] = "vendorTradeResult";
		log.push_back(Operation{ request->operationId, result, now });
		while (log.size() > kMaximumVendorOperationsPerProfile) {
			log.pop_front();
		}
		std::vector<VendorReply> replies{ VendorReply{ false, std::move(result) } };
		if (outcome.accepted && outcome.stock) {
			stocks[key] = *outcome.stock;
			replies.push_back(VendorReply{ true, StatePacket(*outcome.stock) });
		}
		return replies;
	}
}

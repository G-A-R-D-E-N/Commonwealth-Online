#include "ServerVendorTrade.h"

#include "DurableStateFileStore.h"
#include "F4TInventorySnapshot.h"
#include "F4TUtf8Path.h"

#include <algorithm>
#include <fstream>
#include <iostream>

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

		bool IsResend(const Json& a_packet)
		{
			if (!a_packet.is_object()) {
				return false;
			}
			const auto resend = a_packet.find("resend");
			return resend != a_packet.end() && resend->is_boolean() && resend->get<bool>();
		}

		std::uint32_t CountOf(const std::vector<VT::StockLine>& a_lines, std::uint32_t a_formId)
		{
			const auto found = std::find_if(a_lines.begin(), a_lines.end(), [&](const VT::StockLine& a_line) { return a_line.formId == a_formId; });
			return found == a_lines.end() ? 0u : found->count;
		}

		std::string PublishNote(const std::optional<VT::StockSnapshot>& a_current, const VT::StockSnapshot& a_incoming, const VT::StockSnapshot& a_result, const VT::ItemValues& a_values)
		{
			std::string note;
			if (a_current) {
				bool raised = a_incoming.caps > a_current->caps;
				for (const auto& line : a_incoming.lines) {
					raised = raised || line.count > CountOf(a_current->lines, line.formId);
				}
				if (raised) {
					note += " raise ignored";
				}
			}
			const auto& base = a_current ? *a_current : a_incoming;
			bool lowered = a_result.caps < base.caps;
			for (const auto& line : base.lines) {
				lowered = lowered || (a_values.contains(line.formId) && CountOf(a_result.lines, line.formId) < line.count);
			}
			if (lowered) {
				note += a_current ? " ledger lowered" : " first seed clamped";
			}
			return note;
		}

		std::vector<VendorReply> Joined(std::vector<VendorReply> a_first, std::vector<VendorReply> a_second)
		{
			a_first.insert(a_first.end(), std::make_move_iterator(a_second.begin()), std::make_move_iterator(a_second.end()));
			return a_first;
		}

		std::vector<VendorReply> Refusal(const Json& a_packet, const char* a_reason)
		{
			const auto operation = a_packet.is_object() ? a_packet.find("operationId") : a_packet.end();
			if (a_packet.is_object() && operation != a_packet.end() && operation->is_string() && !operation->get<std::string>().empty() &&
				operation->get<std::string>().size() <= VT::MaximumOperationIdLength) {
				Json refusal{ { "type", "vendorTradeResult" }, { "operationId", *operation }, { "accepted", false }, { "reason", a_reason } };
				return { VendorReply{ false, std::move(refusal) } };
			}
			return {};
		}
	}

	std::string VendorTradeBook::KeyOf(const VT::Identity& a_identity)
	{
		return std::to_string(a_identity.worldspaceId) + ":" + std::to_string(a_identity.cellId) + ":" + std::to_string(a_identity.npcId);
	}

	bool VendorTradeBook::LoadItemValues(const Json& a_table)
	{
		const auto parsed = VT::ParseItemValues(a_table);
		itemValues = parsed.value_or(VT::ItemValues{});
		return parsed.has_value();
	}

	bool VendorTradeBook::LoadItemValuesFile(const std::filesystem::path& a_file)
	{
		std::ifstream input(a_file, std::ios::binary);
		const auto loaded = LoadItemValues(input ? Json::parse(input, nullptr, false) : Json{});
		const auto name = F4T::Utf8Path::Encode(a_file);
		if (loaded) {
			std::cout << "Vendor item values loaded: " << itemValues.size() << " entries from " << name << std::endl;
		} else {
			std::cerr << "Vendor item values: " << name << " is missing or invalid; every vendor trade is refused until repaired.\n";
		}
		return loaded;
	}

	std::vector<VendorReply> VendorTradeBook::Publish(std::uint32_t a_playerId, const Json& a_packet, const AuthorityActive& a_isActive)
	{
		auto settled = Settle();
		const auto incoming = VT::ParseStockPublish(a_packet);
		if (!incoming) {
			return settled;
		}
		const auto key = KeyOf(incoming->identity);
		auto found = stocks.find(key);
		std::optional<VT::StockSnapshot> current;
		if (found != stocks.end()) {
			current = found->second;
			if (a_isActive && !a_isActive(current->authorityPlayerId)) {
				current->authorityPlayerId = a_playerId;
			}
		}
		auto outcome = VT::ApplyPublish(current, *incoming, a_playerId, a_playerId, itemValues);
		if (!outcome.accepted || !outcome.stock) {
			return Joined(std::move(settled), { VendorReply{ false, IdentityPacket("vendorStockRejected", incoming->identity, outcome.reason.c_str()) } });
		}
		std::string dropped;
		for (const auto& line : incoming->lines) {
			if (!itemValues.contains(line.formId)) {
				dropped += " " + std::to_string(line.formId);
			}
		}
		if (!dropped.empty()) {
			std::cerr << "Vendor stock from player " << a_playerId << " at npc " << incoming->identity.npcId << " in cell " << incoming->identity.cellId << " dropped unlisted items:" << dropped << ".\n";
		}
		if (!current) {
			if (stocks.size() >= kMaximumVendorStocks) {
				seeds.erase(stocks.begin()->first);
				stocks.erase(stocks.begin());
			}
			std::cerr << "Vendor stock first seed accepted from player " << a_playerId << " for npc " << incoming->identity.npcId << " in cell " << incoming->identity.cellId << " (" << outcome.stock->lines.size() << " lines, " << outcome.stock->caps << " caps).\n";
		}
		if (const auto note = PublishNote(current, *incoming, *outcome.stock, itemValues); !note.empty()) {
			std::cerr << "Vendor stock from player " << a_playerId << " at npc " << incoming->identity.npcId << " in cell " << incoming->identity.cellId << ":" << note << ".\n";
		}
		stocks[key] = *outcome.stock;
		if (!current) {
			seeds[key] = Seed{ outcome.stock->caps, outcome.stock->lines, Now() };
			std::sort(seeds[key].lines.begin(), seeds[key].lines.end(), [](const VT::StockLine& a_left, const VT::StockLine& a_right) { return a_left.formId < a_right.formId; });
		}
		Persist();
		return Joined(std::move(settled), { VendorReply{ true, StatePacket(*outcome.stock) } });
	}

	std::vector<VendorReply> VendorTradeBook::Query(const Json& a_packet)
	{
		auto settled = Settle();
		if (!a_packet.is_object()) {
			return settled;
		}
		Json identityOnly = a_packet;
		identityOnly["items"] = Json::array();
		identityOnly["caps"] = 0;
		const auto request = VT::ParseStockPublish(identityOnly);
		if (!request) {
			return settled;
		}
		const auto found = stocks.find(KeyOf(request->identity));
		if (found == stocks.end()) {
			return Joined(std::move(settled), { VendorReply{ false, IdentityPacket("vendorStockUnseeded", request->identity, "unseededVendor") } });
		}
		return Joined(std::move(settled), { VendorReply{ false, StatePacket(found->second) } });
	}

	std::vector<VendorReply> VendorTradeBook::Trade(const std::string& a_profileKey, const Json& a_packet, const ItemKeyOf& a_itemKeyOf)
	{
		auto settled = Settle();
		// Constraint: keyless resends stay silent; a keyless first send is refused because the server never applied it.
		if (a_profileKey.empty()) {
			return Joined(std::move(settled), IsResend(a_packet) ? std::vector<VendorReply>{} : Refusal(a_packet, "noProfile"));
		}
		const auto request = VT::ParseTradeRequest(a_packet);
		if (!request) {
			return Joined(std::move(settled), Refusal(a_packet, "malformed"));
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
				return Joined(std::move(settled), { VendorReply{ false, operation.result } });
			}
		}
		const auto key = KeyOf(request->identity);
		const auto found = stocks.find(key);
		std::optional<VT::StockSnapshot> current;
		if (found != stocks.end()) {
			current = found->second;
		}
		auto outcome = VT::ApplyTrade(current, *request, itemValues);
		std::vector<PendingSale> sales;
		if (outcome.accepted && !CheckSales(a_profileKey, *request, key, a_itemKeyOf, sales)) {
			std::cerr << "Vendor sale refused as notOwned at npc " << request->identity.npcId << " in cell " << request->identity.cellId << ": the sale could not be checked against the seller's server inventory, or the seller does not hold the items or owes caps.\n";
			outcome = VT::Outcome{ .accepted = false, .reason = "notOwned", .stock = current };
		}
		if (!outcome.accepted && outcome.reason == "mispriced") {
			std::cerr << "Vendor trade refused as mispriced at npc " << request->identity.npcId << " in cell " << request->identity.cellId << ": claimed capsDelta " << request->capsDelta << ", server requires " << VT::ExpectedCapsDelta(*request, itemValues).value_or(0) << ".\n";
		}
		if (!outcome.accepted && outcome.reason == "unknownItem") {
			for (const auto& [itemId, delta] : request->lineDeltas) {
				if (!itemValues.contains(itemId)) {
					std::cerr << "Vendor trade refused for unknown item " << itemId << " at npc " << request->identity.npcId << " in cell " << request->identity.cellId << ".\n";
					break;
				}
			}
		}
		Json result = VT::TradeResultToJson(VT::TradeResult{ .operationId = request->operationId, .outcome = outcome });
		result["type"] = "vendorTradeResult";
		log.push_back(Operation{ request->operationId, result, now });
		while (log.size() > kMaximumVendorOperationsPerProfile) {
			log.pop_front();
		}
		std::vector<VendorReply> replies{ VendorReply{ false, std::move(result) } };
		if (outcome.accepted && outcome.stock) {
			stocks[key] = *outcome.stock;
			pendingSales.insert(pendingSales.end(), sales.begin(), sales.end());
			Persist();
			replies.push_back(VendorReply{ true, StatePacket(*outcome.stock) });
		}
		return Joined(std::move(settled), std::move(replies));
	}

	double VendorTradeBook::Now() const
	{
		return hooks.now ? hooks.now() : std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
	}

	void VendorTradeBook::Persist() const
	{
		if (hooks.persist && !hooks.persist(LedgerToJson())) {
			std::cerr << "Vendor ledger could not be saved; it stays in memory and is saved again on the next change.\n";
		}
	}

	void VendorTradeBook::SetHooks(VendorLedgerHooks a_hooks)
	{
		hooks = std::move(a_hooks);
	}

	bool VendorTradeBook::CheckSales(const std::string& a_profileKey, const VT::TradeRequest& a_request, const std::string& a_vendorKey,
		const ItemKeyOf& a_itemKeyOf, std::vector<PendingSale>& a_out) const
	{
		std::map<std::uint32_t, std::uint32_t> sold;
		for (const auto& [formId, delta] : a_request.lineDeltas) {
			if (delta > 0) {
				sold[formId] += static_cast<std::uint32_t>(delta);
			}
		}
		if (sold.empty()) {
			return true;
		}
		// Constraint: with no inventory hooks nothing can be verified, so sales are refused, never accepted unchecked.
		if (!hooks.held || !hooks.removed) {
			std::cerr << "Vendor sale refused: the inventory hooks are not attached, so the sale cannot be verified.\n";
			return false;
		}
		if (debts.contains(a_profileKey)) {
			std::cerr << "Vendor sale refused: profile " << a_profileKey << " owes " << debts.at(a_profileKey).amount << " caps from a reversed sale.\n";
			return false;
		}
		const auto profileSales = static_cast<std::size_t>(std::count_if(pendingSales.begin(), pendingSales.end(), [&](const PendingSale& a_sale) { return a_sale.profileId == a_profileKey; }));
		if (pendingSales.size() + sold.size() > kMaximumPendingVendorSales || profileSales + sold.size() > kMaximumPendingVendorSalesPerProfile) {
			return false;
		}
		for (const auto& [formId, count] : sold) {
			const auto itemKey = a_itemKeyOf ? a_itemKeyOf(formId) : std::nullopt;
			const auto held = itemKey ? hooks.held(a_profileKey, *itemKey) : std::nullopt;
			if (!held) {
				return false;
			}
			std::uint64_t outstanding = 0;
			std::uint64_t target = hooks.removed(a_profileKey, *itemKey);
			for (const auto& sale : pendingSales) {
				if (sale.profileId == a_profileKey && sale.itemKey == *itemKey) {
					outstanding += sale.count;
					target = std::max(target, sale.target);
				}
			}
			if (*held < outstanding + count) {
				return false;
			}
			a_out.push_back(PendingSale{ a_profileKey, *itemKey, a_vendorKey, formId, count, VT::SellPrice(itemValues.at(formId)) * count, target + count, Now() + kSaleConfirmSeconds });
		}
		return true;
	}

	std::vector<VendorReply> VendorTradeBook::Settle()
	{
		std::vector<VendorReply> replies;
		const auto now = Now();
		bool changed = false;
		const std::string capsKey{ F4T::InventorySnapshot::kCapsItemKey };
		for (auto debt = debts.begin(); debt != debts.end();) {
			if (hooks.removed && hooks.removed(debt->first, capsKey) >= debt->second.target) {
				std::cerr << "Vendor caps debt of profile " << debt->first << " repaid by a caps removal in the inventory snapshot.\n";
				debt = debts.erase(debt);
				changed = true;
			} else {
				++debt;
			}
		}
		for (auto it = pendingSales.begin(); it != pendingSales.end();) {
			if (hooks.removed && hooks.removed(it->profileId, it->itemKey) >= it->target) {
				it = pendingSales.erase(it);
				changed = true;
				continue;
			}
			if (now < it->deadline) {
				++it;
				continue;
			}
			const auto stock = stocks.find(it->vendorKey);
			if (stock != stocks.end()) {
				auto& ledger = stock->second;
				const auto line = std::find_if(ledger.lines.begin(), ledger.lines.end(), [&](const VT::StockLine& a_line) { return a_line.formId == it->formId; });
				if (line != ledger.lines.end()) {
					line->count -= std::min(line->count, it->count);
					if (line->count == 0) {
						ledger.lines.erase(line);
					}
				}
				ledger.caps = std::min(VT::MaximumCaps, ledger.caps + it->capsPaid);
				++ledger.revision;
				replies.push_back(VendorReply{ true, StatePacket(ledger) });
			}
			auto& debt = debts[it->profileId];
			if (debt.amount == 0) {
				debt.target = hooks.removed ? hooks.removed(it->profileId, capsKey) : 0;
			}
			debt.amount = std::min(VT::MaximumCaps, debt.amount + it->capsPaid);
			debt.target += it->capsPaid;
			std::cerr << "Vendor sale of item " << it->formId << " x" << it->count << " by profile " << it->profileId << " never showed as a removal; reversed, " << it->capsPaid << " caps now owed.\n";
			it = pendingSales.erase(it);
			changed = true;
		}
		const auto restockSeconds = static_cast<double>(restockHours) * 3600.0;
		for (auto& [key, ledger] : stocks) {
			const auto seed = seeds.find(key);
			if (seed == seeds.end() || now - seed->second.lastRestock < restockSeconds) {
				continue;
			}
			seed->second.lastRestock = now;
			changed = true;
			bool raised = ledger.caps < seed->second.caps;
			ledger.caps = std::max(ledger.caps, seed->second.caps);
			for (const auto& seedLine : seed->second.lines) {
				const auto line = std::find_if(ledger.lines.begin(), ledger.lines.end(), [&](const VT::StockLine& a_line) { return a_line.formId == seedLine.formId; });
				if (line != ledger.lines.end() && line->count < seedLine.count) {
					line->count = seedLine.count;
					raised = true;
				} else if (line == ledger.lines.end() && ledger.lines.size() < VT::MaximumStockLines) {
					ledger.lines.push_back(seedLine);
					raised = true;
				}
			}
			if (!raised) {
				continue;
			}
			std::sort(ledger.lines.begin(), ledger.lines.end(), [](const VT::StockLine& a_left, const VT::StockLine& a_right) { return a_left.formId < a_right.formId; });
			++ledger.revision;
			std::cerr << "Vendor npc " << ledger.identity.npcId << " in cell " << ledger.identity.cellId << " restocked to its seed.\n";
			replies.push_back(VendorReply{ true, StatePacket(ledger) });
		}
		if (changed) {
			Persist();
		}
		return replies;
	}

	Json VendorTradeBook::LedgerToJson() const
	{
		Json stored = Json::array();
		for (const auto& entry : stocks) {
			stored.push_back(VT::StockSnapshotToJson(entry.second));
		}
		Json sales = Json::array();
		for (const auto& sale : pendingSales) {
			sales.push_back(Json{ { "profileId", sale.profileId }, { "itemKey", sale.itemKey }, { "vendorKey", sale.vendorKey }, { "formId", sale.formId },
				{ "count", sale.count }, { "capsPaid", sale.capsPaid }, { "target", sale.target }, { "deadline", sale.deadline } });
		}
		Json seedList = Json::array();
		for (const auto& [key, seed] : seeds) {
			Json lines = Json::array();
			for (const auto& line : seed.lines) {
				lines.push_back(Json{ { "formId", line.formId }, { "count", line.count } });
			}
			seedList.push_back(Json{ { "vendorKey", key }, { "caps", seed.caps }, { "lines", std::move(lines) }, { "lastRestock", seed.lastRestock } });
		}
		Json owed = Json::array();
		for (const auto& [profile, debt] : debts) {
			owed.push_back(Json{ { "profileId", profile }, { "amount", debt.amount }, { "target", debt.target } });
		}
		return Json{ { "version", 1 }, { "stocks", std::move(stored) }, { "pendingSales", std::move(sales) }, { "seeds", std::move(seedList) }, { "debts", std::move(owed) } };
	}

	bool VendorTradeBook::LoadLedger(const Json& a_ledger)
	{
		if (!a_ledger.is_object() || !a_ledger.contains("stocks") || !a_ledger.at("stocks").is_array()) {
			return false;
		}
		std::map<std::string, VT::StockSnapshot> loaded;
		for (const auto& entry : a_ledger.at("stocks")) {
			const auto stock = VT::ParseStockSnapshot(entry);
			if (!stock || loaded.size() >= kMaximumVendorStocks) {
				return false;
			}
			loaded[KeyOf(stock->identity)] = *stock;
		}
		std::vector<PendingSale> sales;
		try {
			for (const auto& entry : a_ledger.value("pendingSales", Json::array())) {
				sales.push_back(PendingSale{ entry.at("profileId").get<std::string>(), entry.at("itemKey").get<std::string>(), entry.at("vendorKey").get<std::string>(),
					entry.at("formId").get<std::uint32_t>(), entry.at("count").get<std::uint32_t>(), entry.at("capsPaid").get<std::uint32_t>(),
					entry.at("target").get<std::uint64_t>(), entry.at("deadline").get<double>() });
			}
		} catch (const Json::exception&) {
			return false;
		}
		std::map<std::string, CapsDebt> storedDebts;
		std::map<std::string, Seed> storedSeeds;
		try {
			for (const auto& entry : a_ledger.value("debts", Json::array())) {
				storedDebts[entry.at("profileId").get<std::string>()] = CapsDebt{ entry.at("amount").get<std::uint32_t>(), entry.at("target").get<std::uint64_t>() };
			}
			for (const auto& entry : a_ledger.value("seeds", Json::array())) {
				const auto key = entry.at("vendorKey").get<std::string>();
				if (!loaded.contains(key)) {
					continue;
				}
				Seed seed{ entry.at("caps").get<std::uint32_t>(), {}, entry.at("lastRestock").get<double>() };
				for (const auto& line : entry.at("lines")) {
					seed.lines.push_back(VT::StockLine{ line.at("formId").get<std::uint32_t>(), line.at("count").get<std::uint32_t>() });
				}
				storedSeeds[key] = std::move(seed);
			}
		} catch (const Json::exception&) {
			return false;
		}
		for (const auto& [key, stock] : loaded) {
			if (!storedSeeds.contains(key)) {
				storedSeeds[key] = Seed{ std::min(stock.caps, VT::MaximumSeedCaps), stock.lines, Now() };
			}
		}
		stocks = std::move(loaded);
		seeds = std::move(storedSeeds);
		pendingSales = std::move(sales);
		debts = std::move(storedDebts);
		return true;
	}

	void VendorTradeBook::AttachStore(DurableStateFileStore& a_store)
	{
		VendorLedgerHooks wired;
		wired.held = [&a_store](const std::string& a_profile, const std::string& a_itemKey) -> std::optional<std::uint32_t> {
			const auto inventory = a_store.GetInventory(a_profile);
			if (!inventory || !inventory->contains("items")) {
				return std::nullopt;
			}
			const auto& items = inventory->at("items");
			const auto found = items.find(a_itemKey);
			if (found == items.end()) {
				return 0u;
			}
			const auto count = found->find("count");
			return count != found->end() && count->is_number_unsigned() ? count->get<std::uint32_t>() : 0u;
		};
		wired.removed = [&a_store](const std::string& a_profile, const std::string& a_itemKey) { return a_store.SnapshotRemovals(a_profile, a_itemKey); };
		wired.persist = [&a_store](const Json& a_ledger) { return a_store.SetVendorLedger(a_ledger); };
		wired.now = [&a_store] { return a_store.Now(); };
		SetHooks(std::move(wired));
		const auto stored = a_store.GetVendorLedger();
		if (stored.is_null()) {
			return;
		}
		if (LoadLedger(stored)) {
			std::cout << "Vendor ledger loaded: " << stocks.size() << " vendors, " << pendingSales.size() << " pending sales.\n";
		} else {
			std::cerr << "Vendor ledger in the save is unreadable; vendors start unseeded and the stored ledger is overwritten on the next change.\n";
		}
	}
}

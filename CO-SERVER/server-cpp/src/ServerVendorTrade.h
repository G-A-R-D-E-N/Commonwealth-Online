#pragma once

#include "F4TVendorTrade.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace CommonwealthOnline::Server
{
	inline constexpr std::size_t kMaximumVendorStocks = 4096;
	inline constexpr std::size_t kMaximumVendorOperationsPerProfile = 64;
	inline constexpr auto kVendorOperationRetention = std::chrono::hours{ 24 };
	inline constexpr std::size_t kMaximumVendorBytes = 48 * 1024;

	inline constexpr std::size_t kMaximumPendingVendorSales = 4096;
	inline constexpr std::size_t kMaximumPendingVendorSalesPerProfile = 64;
	inline constexpr double kSaleConfirmSeconds = 300.0;

	class DurableStateFileStore;

	struct VendorReply
	{
		bool broadcast{};
		nlohmann::json packet;
	};

	struct VendorLedgerHooks
	{
		std::function<std::optional<std::uint32_t>(const std::string&, const std::string&)> held;
		std::function<std::uint64_t(const std::string&, const std::string&)> removed;
		std::function<bool(const nlohmann::json&)> persist;
		std::function<double()> now;
	};

	class VendorTradeBook
	{
	public:
		using AuthorityActive = std::function<bool(std::uint32_t)>;
		using ItemKeyOf = std::function<std::optional<std::string>(std::uint32_t)>;

		[[nodiscard]] std::vector<VendorReply> Publish(std::uint32_t a_playerId, const nlohmann::json& a_packet, const AuthorityActive& a_isActive);
		[[nodiscard]] std::vector<VendorReply> Query(const nlohmann::json& a_packet);
		[[nodiscard]] std::vector<VendorReply> Trade(const std::string& a_profileKey, const nlohmann::json& a_packet, const ItemKeyOf& a_itemKeyOf = {});
		[[nodiscard]] bool LoadItemValues(const nlohmann::json& a_table);
		[[nodiscard]] bool LoadItemValuesFile(const std::filesystem::path& a_file);
		void SetHooks(VendorLedgerHooks a_hooks);
		void SetRestockHours(std::uint32_t a_hours) noexcept { restockHours = F4T::VendorTrade::ClampRestockHours(a_hours); }
		[[nodiscard]] std::uint32_t RestockHours() const noexcept { return restockHours; }
		void AttachStore(DurableStateFileStore& a_store);
		[[nodiscard]] bool LoadLedger(const nlohmann::json& a_ledger);
		[[nodiscard]] nlohmann::json LedgerToJson() const;

		[[nodiscard]] std::size_t Size() const noexcept { return stocks.size(); }
		[[nodiscard]] std::size_t PendingSales() const noexcept { return pendingSales.size(); }
		[[nodiscard]] std::size_t Debts() const noexcept { return debts.size(); }

	private:
		struct PendingSale
		{
			std::string profileId;
			std::string itemKey;
			std::string vendorKey;
			std::uint32_t formId{};
			std::uint32_t count{};
			std::uint32_t capsPaid{};
			std::uint64_t target{};
			double deadline{};
		};

		struct CapsDebt
		{
			std::uint32_t amount{};
			std::uint64_t target{};
		};

		struct Seed
		{
			std::uint32_t caps{};
			std::vector<F4T::VendorTrade::StockLine> lines;
			double lastRestock{};
		};

		struct Operation
		{
			std::string operationId;
			nlohmann::json result;
			std::chrono::steady_clock::time_point at;
		};

		static std::string KeyOf(const F4T::VendorTrade::Identity& a_identity);
		[[nodiscard]] double Now() const;
		void Persist() const;
		[[nodiscard]] std::vector<VendorReply> Settle();
		[[nodiscard]] bool CheckSales(const std::string& a_profileKey, const F4T::VendorTrade::TradeRequest& a_request, const std::string& a_vendorKey,
			const ItemKeyOf& a_itemKeyOf, std::vector<PendingSale>& a_out) const;

		std::map<std::string, F4T::VendorTrade::StockSnapshot> stocks;
		std::map<std::string, std::deque<Operation>> operations;
		F4T::VendorTrade::ItemValues itemValues;
		std::map<std::string, Seed> seeds;
		std::uint32_t restockHours{ F4T::VendorTrade::DefaultRestockHours };
		std::vector<PendingSale> pendingSales;
		std::map<std::string, CapsDebt> debts;
		VendorLedgerHooks hooks;
	};
}

#pragma once

#include "F4TVendorTrade.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
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

	struct VendorReply
	{
		bool broadcast{};
		nlohmann::json packet;
	};

	class VendorTradeBook
	{
	public:
		using AuthorityActive = std::function<bool(std::uint32_t)>;

		[[nodiscard]] std::vector<VendorReply> Publish(std::uint32_t a_playerId, const nlohmann::json& a_packet, const AuthorityActive& a_isActive);
		[[nodiscard]] std::vector<VendorReply> Query(const nlohmann::json& a_packet) const;
		[[nodiscard]] std::vector<VendorReply> Trade(const std::string& a_profileKey, const nlohmann::json& a_packet);

		[[nodiscard]] std::size_t Size() const noexcept { return stocks.size(); }

	private:
		struct Operation
		{
			std::string operationId;
			nlohmann::json result;
			std::chrono::steady_clock::time_point at;
		};

		static std::string KeyOf(const F4T::VendorTrade::Identity& a_identity);

		std::map<std::string, F4T::VendorTrade::StockSnapshot> stocks;
		std::map<std::string, std::deque<Operation>> operations;
	};
}

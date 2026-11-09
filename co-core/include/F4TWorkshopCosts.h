#pragma once

#include "F4TTradeLedger.h"
#include "F4TWorkshopTransactions.h"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace F4T::WorkshopCosts
{
	struct Component
	{
		Workshop::FormKey item;
		std::uint32_t count{};
	};

	class Table
	{
	public:
		[[nodiscard]] bool LoadFromJson(const nlohmann::json& a_root);
		[[nodiscard]] std::size_t Size() const noexcept { return m_costs.size(); }
		[[nodiscard]] const std::vector<Component>* Find(const Workshop::FormKey& a_form) const;

	private:
		std::map<std::pair<std::string, std::uint32_t>, std::vector<Component>> m_costs;
	};

	struct Refund
	{
		std::vector<Trade::Line> lines;
		std::size_t pricedPieces{};
		std::size_t unpricedPieces{};
	};

	[[nodiscard]] Refund HalfRefund(const Table& a_table, const std::vector<Workshop::ObjectState>& a_culled);
}

#pragma once

#include <cstdint>
#include <string_view>

namespace F4T::AdminItems
{
	inline constexpr std::string_view kCapability = "admin-items-v1";

	enum class QueueResult : std::uint8_t
	{
		kQueued,
		kInvalid,
		kBusy
	};

	QueueResult QueueItemOp(std::string_view a_op, std::string_view a_itemId, std::uint32_t a_count, std::uint32_t a_opId);
	QueueResult QueueInventoryRequest(std::uint32_t a_opId);
	void ApplyPendingAdminItems();
}

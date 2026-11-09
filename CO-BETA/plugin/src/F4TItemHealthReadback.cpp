#include "pch.h"
#include "F4TItemHealthReadback.h"
#include "F4TItemHealthSnapshot.h"
#include "RE/E/ExtraHealth.h"
namespace F4T::ItemHealthReadback
{
	std::optional<nlohmann::json> Capture(const RE::BGSInventoryItem::Stack& a_stack)
	{
		if (!a_stack.extra) return std::nullopt;
		const RE::BSAutoReadLock lock{ a_stack.extra->extraRWLock };
		const auto& list = a_stack.extra->extraData;
		for (std::int32_t i = 0; i < static_cast<std::int32_t>(RE::EXTRA_DATA_TYPE::kTotal); ++i) {
			const auto type = static_cast<RE::EXTRA_DATA_TYPE>(i);
			if (type != RE::ExtraHealth::TYPE && list.HasType(type)) return std::nullopt;
		}
		const auto* health = static_cast<const RE::ExtraHealth*>(list.GetByType(RE::ExtraHealth::TYPE));
		return health && health->flags == 0 ? F4T::ItemHealthSnapshot::Encode(health->health) : std::nullopt;
	}
	RE::BSTSmartPointer<RE::ExtraDataList> CreateExtra(const nlohmann::json& a_snapshot)
	{
		const auto health = F4T::ItemHealthSnapshot::Decode(a_snapshot);
		if (!health) return {};
		RE::BSTSmartPointer<RE::ExtraDataList> result{ new RE::ExtraDataList() };
		result->AddExtra(new RE::ExtraHealth(*health));
		return result;
	}
}

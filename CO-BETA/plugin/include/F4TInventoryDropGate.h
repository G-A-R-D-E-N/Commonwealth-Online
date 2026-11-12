#pragma once

#include "F4TDropLedger.h"
#include <RE/Fallout.h>
#include <cstdint>
#include <optional>
#include <string>

namespace F4T::InventoryDropGate
{
	using Original = RE::ObjectRefHandle* (*)(
		RE::Actor*,
		RE::ObjectRefHandle*,
		const RE::BGSObjectInstance&,
		RE::BSTSmallArray<std::uint32_t, 4>*,
		std::int32_t,
		const RE::NiPoint3*,
		const RE::NiPoint3*);
	bool Hold(
		const RE::BGSObjectInstance& a_object,
		RE::BSTSmallArray<std::uint32_t, 4>* a_stackData,
		std::int32_t a_count,
		const RE::NiPoint3* a_point,
		const RE::NiPoint3* a_rotate,
		Original a_original);
	void Complete(const std::string& a_operationId, bool a_committed, std::optional<F4T::Drops::EntityId> a_entityId);
	[[nodiscard]] std::optional<std::string> TranslateItemKey(const RE::TESForm* a_form);
	[[nodiscard]] bool PlayerCanPlaceDrop();
	[[nodiscard]] bool PumpReplays(std::uint64_t a_nowMs);
	void ResetRetries();
}

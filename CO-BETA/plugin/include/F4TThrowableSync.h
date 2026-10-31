#pragma once

#include "F4TRemotePlayerState.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace RE
{
	class Actor;
}

namespace F4T::ProxyActorController::detail
{
	struct ProxyActorSlot;
}

namespace F4T::ThrowableSync
{
	using RemotePlayerState::RemoteEquippedItem;
	using RemotePlayerState::RemoteItemMod;

	inline constexpr std::string_view kThrowableSlot{ "throwable" };

	// TODO(RE): equip index semantics are concept-level/unverified on OG, NG and AE; verify before use.
	inline constexpr std::uint32_t kRightHandEquipIndex = 0;

	enum class ApplyAction
	{
		kSkip,
		kApply,
		kClear
	};

	struct ApplyPlan
	{
		ApplyAction action{ ApplyAction::kSkip };
		std::uint32_t formId{ 0 };
		bool operator==(const ApplyPlan&) const = default;
	};

	enum class ApplyOutcome
	{
		kUnchanged,
		kApplied,
		kFailed
	};

	inline bool IsThrowableSlot(std::string_view a_slot)
	{
		return a_slot == kThrowableSlot;
	}

	// constraint: a thrown weapon at the right-hand index is published by the throwable slot, never by rightHand.
	inline std::uint32_t RightHandEntryFormId(std::uint32_t a_equippedFormId, bool a_isThrownWeapon)
	{
		return a_isThrownWeapon ? 0 : a_equippedFormId;
	}

	// TODO(RE): concept-level/unverified; the equipped-throwable slot identity needs runtime proof.
	inline bool IsValidThrowableItem(const RemoteEquippedItem& a_item)
	{
		return a_item.formId != 0 && a_item.slot == kThrowableSlot;
	}

	inline std::optional<RemoteEquippedItem> FindThrowableEntry(std::span<const RemoteEquippedItem> a_items)
	{
		for (const auto& item : a_items) {
			if (IsValidThrowableItem(item)) {
				return item;
			}
		}
		return std::nullopt;
	}

	inline std::optional<RemoteEquippedItem> CaptureFromThrownWeapon(std::uint32_t a_formId, bool a_isThrownWeapon)
	{
		if (a_formId == 0 || !a_isThrownWeapon) {
			return std::nullopt;
		}
		return RemoteEquippedItem{ std::string{ kThrowableSlot }, a_formId, std::nullopt };
	}

	inline bool HasThrowableRecipe(
		const std::optional<RemoteEquippedItem>& a_throwable,
		bool a_formResolvesAsThrowable)
	{
		return a_throwable && IsValidThrowableItem(*a_throwable) && a_formResolvesAsThrowable;
	}

	// TODO(RE): concept-level/unverified; an unresolved throwable stands down only when the remote publishes no right-hand weapon.
	inline bool ThrowableUnresolved(
		const std::optional<RemoteEquippedItem>& a_throwable,
		std::uint32_t a_rightHandFormId,
		bool a_formResolvesAsThrowable)
	{
		return a_rightHandFormId == 0 && a_throwable && IsValidThrowableItem(*a_throwable) && !a_formResolvesAsThrowable;
	}

	inline bool ThrowableOwnsRightHand(
		const std::optional<RemoteEquippedItem>& a_throwable,
		std::uint32_t a_rightHandFormId,
		bool a_formResolvesAsThrowable)
	{
		return a_rightHandFormId == 0 && HasThrowableRecipe(a_throwable, a_formResolvesAsThrowable);
	}

	inline ApplyPlan ResolveApplyPlan(
		const std::optional<RemoteEquippedItem>& a_throwable,
		std::uint32_t a_rightHandFormId,
		bool a_formResolvesAsThrowable)
	{
		if (!HasThrowableRecipe(a_throwable, a_formResolvesAsThrowable)) {
			return {};
		}
		if (a_throwable->formId == a_rightHandFormId) {
			return {};
		}
		if (a_rightHandFormId == 0) {
			return { ApplyAction::kApply, a_throwable->formId };
		}
		return { ApplyAction::kClear, a_throwable->formId };
	}

	std::optional<RemoteEquippedItem> CaptureEquippedThrowable(RE::Actor& a_actor);

	[[nodiscard]] ApplyOutcome ApplyThrowablePlan(
		RE::Actor& a_proxy,
		ProxyActorController::detail::ProxyActorSlot& a_slot,
		const ApplyPlan& a_plan,
		bool a_weaponDrawn,
		const std::vector<RemoteItemMod>* a_mods);
}

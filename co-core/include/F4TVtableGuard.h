#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace F4T::VtableGuard
{
	struct AddressRange
	{
		std::uintptr_t begin{ 0 };
		std::uintptr_t end{ 0 };
	};

	enum class SlotVerdict
	{
		kOk,
		kVtableUnresolved,
		kModuleSectionsUnresolved,
		kSlotOutsideVtableSection,
		kSlotEmpty,
		kAlreadyInstalled,
		kSlotNotExecutable,
	};

	struct SlotRequest
	{
		std::uintptr_t vtableAddress{ 0 };
		std::size_t    slotIndex{ 0 };
		std::uintptr_t slotValue{ 0 };
		std::uintptr_t replacement{ 0 };
		AddressRange   vtableSection{};
		AddressRange   codeSection{};
	};

	[[nodiscard]] bool Contains(const AddressRange& a_range, std::uintptr_t a_address) noexcept;

	[[nodiscard]] bool IsPlausibleVtablePointer(
		std::uintptr_t a_vtableAddress,
		const AddressRange& a_vtableSection) noexcept;

	[[nodiscard]] SlotVerdict VerifySlot(const SlotRequest& a_request) noexcept;

	[[nodiscard]] std::uintptr_t ReadSlot(const SlotRequest& a_request) noexcept;

	[[nodiscard]] bool WriteSucceeded(
		std::uintptr_t a_slotValueAfterWrite,
		std::uintptr_t a_replacement) noexcept;

	// A slot may be written only when it resolves to exactly the expected function relative address.
	[[nodiscard]] bool SlotMatchesExpectedRva(
		std::uintptr_t a_slotValue,
		std::uintptr_t a_moduleBase,
		std::uintptr_t a_expectedRva) noexcept;

	[[nodiscard]] std::string_view Describe(SlotVerdict a_verdict) noexcept;
}

#include "F4TVtableGuard.h"

#include <cstring>
#include <limits>

namespace F4T::VtableGuard
{
	bool Contains(const AddressRange& a_range, std::uintptr_t a_address) noexcept
	{
		if (a_range.begin == 0 || a_range.end <= a_range.begin) {
			return false;
		}
		return a_address >= a_range.begin && a_address < a_range.end;
	}

	bool IsPlausibleVtablePointer(
		std::uintptr_t a_vtableAddress,
		const AddressRange& a_vtableSection) noexcept
	{
		return a_vtableAddress != 0 && Contains(a_vtableSection, a_vtableAddress);
	}

	namespace
	{
		SlotVerdict ResolveEntry(const SlotRequest& a_request, std::uintptr_t& a_entryAddress) noexcept
		{
			if (a_request.vtableAddress == 0) {
				return SlotVerdict::kVtableUnresolved;
			}

			const auto sectionValid = [](const AddressRange& a_range) noexcept {
				return a_range.begin != 0 && a_range.end > a_range.begin;
			};
			if (!sectionValid(a_request.vtableSection) || !sectionValid(a_request.codeSection)) {
				return SlotVerdict::kModuleSectionsUnresolved;
			}

			constexpr auto entrySize = sizeof(std::uintptr_t);
			const auto maxIndex = (std::numeric_limits<std::uintptr_t>::max() - a_request.vtableAddress) / entrySize;
			if (a_request.slotIndex >= maxIndex) {
				return SlotVerdict::kSlotOutsideVtableSection;
			}

			a_entryAddress = a_request.vtableAddress + (a_request.slotIndex * entrySize);
			if (!Contains(a_request.vtableSection, a_entryAddress) ||
				!Contains(a_request.vtableSection, a_entryAddress + entrySize - 1)) {
				return SlotVerdict::kSlotOutsideVtableSection;
			}

			return SlotVerdict::kOk;
		}
	}

	SlotVerdict VerifySlot(const SlotRequest& a_request) noexcept
	{
		std::uintptr_t entryAddress = 0;
		if (const auto addressVerdict = ResolveEntry(a_request, entryAddress); addressVerdict != SlotVerdict::kOk) {
			return addressVerdict;
		}

		if (a_request.slotValue == 0) {
			return SlotVerdict::kSlotEmpty;
		}

		if (a_request.slotValue == a_request.replacement) {
			return SlotVerdict::kAlreadyInstalled;
		}

		if (!Contains(a_request.codeSection, a_request.slotValue)) {
			return SlotVerdict::kSlotNotExecutable;
		}

		return SlotVerdict::kOk;
	}

	std::uintptr_t ReadSlot(const SlotRequest& a_request) noexcept
	{
		std::uintptr_t entryAddress = 0;
		if (ResolveEntry(a_request, entryAddress) != SlotVerdict::kOk) {
			return 0;
		}

		std::uintptr_t value = 0;
		std::memcpy(&value, reinterpret_cast<const void*>(entryAddress), sizeof(value));
		return value;
	}

	bool WriteSucceeded(std::uintptr_t a_slotValueAfterWrite, std::uintptr_t a_replacement) noexcept
	{
		return a_replacement != 0 && a_slotValueAfterWrite == a_replacement;
	}

	bool SlotMatchesExpectedRva(
		std::uintptr_t a_slotValue,
		std::uintptr_t a_moduleBase,
		std::uintptr_t a_expectedRva) noexcept
	{
		if (a_slotValue == 0 || a_moduleBase == 0 || a_expectedRva == 0) {
			return false;
		}
		if (a_slotValue <= a_moduleBase) {
			return false;
		}
		return a_slotValue - a_moduleBase == a_expectedRva;
	}

	std::string_view Describe(SlotVerdict a_verdict) noexcept
	{
		switch (a_verdict) {
		case SlotVerdict::kOk:
			return "slot verified";
		case SlotVerdict::kVtableUnresolved:
			return "vtable address did not resolve";
		case SlotVerdict::kModuleSectionsUnresolved:
			return "game module .text/.rdata sections did not resolve";
		case SlotVerdict::kSlotOutsideVtableSection:
			return "slot index falls outside the module's vtable section";
		case SlotVerdict::kSlotEmpty:
			return "slot is empty";
		case SlotVerdict::kAlreadyInstalled:
			return "slot already holds this hook";
		case SlotVerdict::kSlotNotExecutable:
			return "slot does not point into the module's code section";
		default:
			return "unknown verdict";
		}
	}
}

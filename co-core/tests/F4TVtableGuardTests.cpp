#include "F4TVtableGuard.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <limits>

namespace
{
	constexpr F4T::VtableGuard::AddressRange kRData{ 0x1'4000'0000, 0x1'4001'0000 };
	constexpr F4T::VtableGuard::AddressRange kText{ 0x1'4100'0000, 0x1'4200'0000 };
	constexpr std::uintptr_t                 kVtable = 0x1'4000'1000;
	constexpr std::uintptr_t                 kGameFunc = 0x1'4100'5000;
	constexpr std::uintptr_t                 kOurHook = 0x7FF0'0000'1000;

	F4T::VtableGuard::SlotRequest MakeRequest()
	{
		F4T::VtableGuard::SlotRequest request{};
		request.vtableAddress = kVtable;
		request.slotIndex = 0xCF;
		request.slotValue = kGameFunc;
		request.replacement = kOurHook;
		request.vtableSection = kRData;
		request.codeSection = kText;
		return request;
	}
}

int main()
{
	using namespace F4T::VtableGuard;

	assert(Contains(kText, kGameFunc));
	assert(!Contains(kText, kVtable));
	assert(!Contains(AddressRange{}, kGameFunc));
	assert(!Contains(kText, kText.end));

	assert(VerifySlot(MakeRequest()) == SlotVerdict::kOk);

	{
		auto request = MakeRequest();
		request.vtableAddress = 0;
		assert(VerifySlot(request) == SlotVerdict::kVtableUnresolved);
	}

	{
		auto request = MakeRequest();
		request.vtableSection = AddressRange{};
		assert(VerifySlot(request) == SlotVerdict::kModuleSectionsUnresolved);
	}

	{
		auto request = MakeRequest();
		request.codeSection = AddressRange{ kText.begin, kText.begin };
		assert(VerifySlot(request) == SlotVerdict::kModuleSectionsUnresolved);
	}

	{
		auto request = MakeRequest();
		request.slotIndex = 0x100000;
		assert(VerifySlot(request) == SlotVerdict::kSlotOutsideVtableSection);
	}

	{
		auto request = MakeRequest();
		request.slotIndex = std::numeric_limits<std::size_t>::max();
		assert(VerifySlot(request) == SlotVerdict::kSlotOutsideVtableSection);
	}

	{
		auto request = MakeRequest();
		request.slotValue = 0;
		assert(VerifySlot(request) == SlotVerdict::kSlotEmpty);
	}

	{
		auto request = MakeRequest();
		request.slotValue = kOurHook;
		assert(VerifySlot(request) == SlotVerdict::kAlreadyInstalled);
	}

	{
		auto request = MakeRequest();
		request.slotValue = kRData.begin + 0x40;
		assert(VerifySlot(request) == SlotVerdict::kSlotNotExecutable);
	}

	{
		std::array<std::uintptr_t, 5> table{ 0x11, 0x22, 0x33, 0x44, 0x55 };
		const auto base = reinterpret_cast<std::uintptr_t>(table.data());
		const AddressRange section{ base, base + 4 * sizeof(std::uintptr_t) };

		SlotRequest request{};
		request.vtableAddress = base;
		request.vtableSection = section;
		request.codeSection = kText;

		request.slotIndex = 2;
		assert(ReadSlot(request) == 0x33);

		request.slotIndex = 3;
		assert(ReadSlot(request) == 0x44);
		request.vtableSection.end -= 1;
		assert(ReadSlot(request) == 0);
		request.vtableSection = section;

		request.slotIndex = 4;
		assert(ReadSlot(request) == 0);

		request.slotIndex = 2;
		request.vtableSection = AddressRange{};
		assert(ReadSlot(request) == 0);
		request.vtableSection = section;

		request.vtableAddress = 0;
		assert(ReadSlot(request) == 0);
		request.vtableAddress = base;

		request.slotIndex = std::numeric_limits<std::size_t>::max();
		assert(ReadSlot(request) == 0);

		request.slotIndex = (std::size_t{ 1 } << 61) + 2;
		assert(ReadSlot(request) == 0);
		request.slotValue = 0x33;
		request.replacement = 0x99;
		assert(VerifySlot(request) == SlotVerdict::kSlotOutsideVtableSection);

		request.slotIndex = 2;
		request.codeSection = AddressRange{};
		assert(ReadSlot(request) == 0);
	}

	assert(IsPlausibleVtablePointer(kVtable, kRData));
	assert(!IsPlausibleVtablePointer(0, kRData));
	assert(!IsPlausibleVtablePointer(kGameFunc, kRData));
	assert(!IsPlausibleVtablePointer(kRData.end, kRData));
	assert(!IsPlausibleVtablePointer(kVtable, AddressRange{}));

	assert(WriteSucceeded(kOurHook, kOurHook));
	assert(!WriteSucceeded(kGameFunc, kOurHook));
	assert(!WriteSucceeded(0, kOurHook));

	assert(SlotMatchesExpectedRva(kText.begin + 0x1234, kText.begin, 0x1234));
	assert(SlotMatchesExpectedRva(kText.end - 1, kText.begin, (kText.end - 1) - kText.begin));
	assert(!SlotMatchesExpectedRva(kText.begin + 0x1234, kText.begin, 0x1235));
	assert(!SlotMatchesExpectedRva(kGameFunc, kText.begin, 0x1234));
	assert(!SlotMatchesExpectedRva(kText.begin, kText.begin, 0x1234));
	assert(!SlotMatchesExpectedRva(kText.begin + 0x1234, kText.begin, 0));
	assert(!SlotMatchesExpectedRva(kText.begin + 0x1234, 0, 0x1234));
	assert(!SlotMatchesExpectedRva(0, kText.begin, 0x1234));

	assert(!Describe(SlotVerdict::kOk).empty());
	assert(!Describe(SlotVerdict::kSlotOutsideVtableSection).empty());
	assert(Describe(SlotVerdict::kOk) != Describe(SlotVerdict::kSlotEmpty));

	return 0;
}

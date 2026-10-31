#pragma once

#include <cstddef>
#include <cstdint>

namespace F4T::InventoryHookTable
{
	// Every value is an address relative to the image base 0x140000000 of that build.
	struct RuntimeSlots
	{
		std::uintptr_t playerCharacterVtableRva;
		std::size_t    dropObjectSlot;
		std::uintptr_t dropObjectRva;
		std::size_t    pickUpObjectSlot;
		std::uintptr_t pickUpObjectRva;
		std::uintptr_t containerMenuVtableRva;
		std::size_t    doItemTransferSlot;
		std::uintptr_t doItemTransferRva;
	};

	// OG 1.10.163 RVA row.
	inline constexpr RuntimeSlots kOg{
		0x2d438e8,  // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/findings.jsonl (rq1-dropobject-identity-og: PlayerCharacter primary vtable RVA, corpus exact)
		0xEB,       // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/README.md (section 2 RQ1: DropObject vtable slot 0xEB; expected RVA)
		0xe9eca0,   // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/findings.jsonl (rq1-dropobject-identity-og: DropObject RVA, VERIFIED direct chain)
		0xEC,       // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/README.md (section 2 RQ2: PickUpObject vtable slot 0xEC; expected RVA)
		0xea6bb0,   // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/findings.jsonl (rq2-pickupobject-identity-og: PickUpObject RVA)
		0x2d86418,  // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/findings.jsonl (rq3-domitemtransfer-identity-og: ContainerMenu primary vtable RVA, corpus exact)
		0x15,       // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/README.md (section 2 RQ3: DoItemTransfer vtable slot 0x15; expected RVA)
		0x1265810,  // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/findings.jsonl (rq3-domitemtransfer-identity-og: DoItemTransfer RVA)
	};

	// NG 1.10.984 RVA row.
	inline constexpr RuntimeSlots kNg{
		0x2369ad8,  // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/findings.jsonl (rq1-dropobject-identity-ng: PlayerCharacter primary vtable RVA, corpus exact)
		0xEB,       // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/README.md (section 2 RQ1: DropObject vtable slot 0xEB; expected RVA)
		0xcd4ea0,   // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/findings.jsonl (rq1-dropobject-identity-ng: DropObject RVA)
		0xEC,       // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/README.md (section 2 RQ2: PickUpObject vtable slot 0xEC; expected RVA)
		0xcdce20,   // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/findings.jsonl (rq2-pickupobject-identity-ng: PickUpObject RVA)
		0x23a98a8,  // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/findings.jsonl (rq3-domitemtransfer-identity-ng: ContainerMenu primary vtable RVA, corpus exact)
		0x15,       // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/README.md (section 2 RQ3: DoItemTransfer vtable slot 0x15; expected RVA)
		0xfb8e20,   // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/findings.jsonl (rq3-domitemtransfer-identity-ng: DoItemTransfer RVA)
	};

	// AE 1.11.240 RVA row.
	inline constexpr RuntimeSlots kAe{
		0x256c938,  // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/findings.jsonl (rq1-dropobject-identity-ae: PlayerCharacter primary vtable RVA, strong candidate, RTTI-confirmed in section 8)
		0xEB,       // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/README.md (section 2 RQ1: DropObject vtable slot 0xEB; expected RVA)
		0xd5aec0,   // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/findings.jsonl (rq1-dropobject-identity-ae: DropObject RVA)
		0xEC,       // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/README.md (section 2 RQ2: PickUpObject vtable slot 0xEC; expected RVA)
		0xd62e60,   // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/findings.jsonl (rq2-pickupobject-identity-ae: PickUpObject RVA)
		0x25ac6c8,  // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/findings.jsonl (rq3-domitemtransfer-identity-ae + commonlib-vtable-array-index0-primary: ContainerMenu primary vtable RVA)
		0x15,       // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/README.md (section 2 RQ3: DoItemTransfer vtable slot 0x15; expected RVA)
		0x103ee80,  // source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/findings.jsonl (rq3-domitemtransfer-identity-ae: DoItemTransfer RVA)
	};
}

#include "F4TVtableHookGuard.h"

#include "F4TVtableGuard.h"
#include "SessionCallbacks.h"

namespace F4T::VtableHookGuard
{
	std::uintptr_t TryWriteVfunc(
		REL::Relocation<std::uintptr_t>& a_vtable,
		std::size_t a_slotIndex,
		std::uintptr_t a_replacement,
		std::string_view a_hookName)
	{
		// TODO-verify: .text/.rdata split as used by CO-BETA/plugin/src/F4TProxyPuppet.cpp:Install
		const auto module = REX::FModule::GetExecutingModule();
		const auto text = module.GetSection(".text");
		const auto rdata = module.GetSection(".rdata");

		VtableGuard::SlotRequest request{};
		request.vtableAddress = a_vtable.address();
		request.slotIndex = a_slotIndex;
		request.replacement = a_replacement;
		request.codeSection = { text.GetAddress(), text.GetAddress() + text.GetSize() };
		request.vtableSection = { rdata.GetAddress(), rdata.GetAddress() + rdata.GetSize() };

		const auto slotAddress = request.vtableAddress + (a_slotIndex * sizeof(std::uintptr_t));
		request.slotValue = VtableGuard::ReadSlot(request);

		const auto verdict = VtableGuard::VerifySlot(request);
		if (verdict != VtableGuard::SlotVerdict::kOk) {
			REX::WARN(
				"{} {} hook NOT installed ({}).",
				COUI::Session::GetLogPrefix(),
				a_hookName,
				VtableGuard::Describe(verdict));
			return 0;
		}

		const auto original = a_vtable.write_vfunc(a_slotIndex, a_replacement);
		if (!VtableGuard::WriteSucceeded(*reinterpret_cast<std::uintptr_t*>(slotAddress), a_replacement)) {
			REX::WARN("{} {} hook FAILED: vtable slot did not take the write.", COUI::Session::GetLogPrefix(), a_hookName);
			return 0;
		}

		return original;
	}
}

#include "F4TIatPatch.h"

#include <cassert>

namespace
{
	ULONGLONG WINAPI FakeTickCount64()
	{
		return 4242;
	}
}

int main()
{
	auto* self = ::GetModuleHandleW(nullptr);
	const auto hook = reinterpret_cast<void*>(&FakeTickCount64);
	assert(self != nullptr);

	assert(F4T::IatPatch::PatchIATEntry(self, "KERNEL32.dll", "NoSuchExport", hook) == nullptr);
	assert(F4T::IatPatch::PatchIATEntry(self, "NOT-A-DLL.dll", "GetTickCount64", hook) == nullptr);

	const auto original = F4T::IatPatch::PatchIATEntry(self, "KERNEL32.dll", "GetTickCount64", hook);
	assert(original != nullptr);
	assert(::GetTickCount64() == 4242);

	assert(F4T::IatPatch::PatchIATEntry(self, "KERNEL32.dll", "GetTickCount64", hook) == nullptr);
	assert(::GetTickCount64() == 4242);

	return 0;
}

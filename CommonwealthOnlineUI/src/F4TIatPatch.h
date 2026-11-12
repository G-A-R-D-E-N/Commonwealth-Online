#pragma once

#include <Windows.h>

namespace F4T::IatPatch
{
	void* PatchIATEntry(HMODULE a_module, const char* a_dllName, const char* a_funcName, void* a_hook);
}

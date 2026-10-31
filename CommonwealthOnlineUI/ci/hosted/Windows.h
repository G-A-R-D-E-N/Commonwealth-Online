#pragma once

using HMODULE = void*;
using FARPROC = void (*)();

inline HMODULE GetModuleHandleW(const wchar_t*)
{
	return nullptr;
}

inline FARPROC GetProcAddress(HMODULE, const char*)
{
	return nullptr;
}

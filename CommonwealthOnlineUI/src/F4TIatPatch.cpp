#include "F4TIatPatch.h"

#include <cstdint>
#include <cstring>

namespace F4T::IatPatch
{
	void* PatchIATEntry(HMODULE a_module, const char* a_dllName, const char* a_funcName, void* a_hook)
	{
		auto* base = reinterpret_cast<std::uint8_t*>(a_module);
		auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
		if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) {
			return nullptr;
		}

		auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE) {
			return nullptr;
		}

		const auto importRVA = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
		if (importRVA == 0) {
			return nullptr;
		}

		auto* import = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + importRVA);
		for (; import->Name != 0; ++import) {
			const auto* modName = reinterpret_cast<const char*>(base + import->Name);
			if (_stricmp(modName, a_dllName) != 0) {
				continue;
			}

			auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + import->FirstThunk);
			const auto nameThunkRVA = import->OriginalFirstThunk != 0 ? import->OriginalFirstThunk : import->FirstThunk;
			auto* nameThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + nameThunkRVA);

			for (; nameThunk->u1.AddressOfData != 0; ++nameThunk, ++thunk) {
				if ((nameThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) != 0) {
					continue;
				}

				const auto* importByName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + nameThunk->u1.AddressOfData);
				if (std::strcmp(reinterpret_cast<const char*>(importByName->Name), a_funcName) != 0) {
					continue;
				}

				auto* entry = reinterpret_cast<void**>(&thunk->u1.Function);
				const auto dllModule = ::GetModuleHandleA(a_dllName);
				const auto expected = dllModule ? reinterpret_cast<void*>(::GetProcAddress(dllModule, a_funcName)) : nullptr;
				if (expected == nullptr || *entry != expected) {
					return nullptr;
				}

				DWORD oldProtect = 0;
				if (!::VirtualProtect(entry, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
					return nullptr;
				}

				void* original = *entry;
				*entry = a_hook;
				::VirtualProtect(entry, sizeof(void*), oldProtect, &oldProtect);

				return *entry == a_hook ? original : nullptr;
			}
		}

		return nullptr;
	}
}

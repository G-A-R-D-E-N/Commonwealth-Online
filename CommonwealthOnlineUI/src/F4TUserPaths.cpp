#include "pch.h"

#include "F4TUserPaths.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace F4T::UserPaths
{
	std::string EnvironmentValue(const char* a_name)
	{
		char* raw = nullptr;
		std::size_t length = 0;
		if (a_name == nullptr || _dupenv_s(&raw, &length, a_name) != 0 || raw == nullptr) {
			return {};
		}
		std::string value{ raw, length > 0 ? length - 1 : 0 };
		std::free(raw);
		return value;
	}

	std::filesystem::path CommonwealthOnlineDocumentsDirectory()
	{
		const auto profile = EnvironmentValue("USERPROFILE");
		if (profile.empty()) {
			return {};
		}
		return std::filesystem::path{ profile } / "Documents" / "My Games" / "Fallout4" / "CommonwealthOnline";
	}

	std::filesystem::path PackagedServerDirectory()
	{
		HMODULE module = nullptr;
		const auto address = reinterpret_cast<LPCWSTR>(reinterpret_cast<std::uintptr_t>(&PackagedServerDirectory));
		if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				address,
				std::addressof(module)) ||
			!module) {
			return {};
		}
		std::array<wchar_t, 32768> path{};
		const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
		if (length == 0 || length >= path.size()) {
			return {};
		}
		return std::filesystem::path{ std::wstring_view{ path.data(), length } }.parent_path().parent_path().parent_path() /
			L"server";
	}
}

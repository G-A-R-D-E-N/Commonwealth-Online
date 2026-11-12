#pragma once

#include "F4TClientCompatibility.h"
#include "F4TExecutingRuntime.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace F4T::RuntimeSelect
{
	[[nodiscard]] inline REL::Version VerifiedVersion() noexcept
	{
		switch (REX::FModule::GetRuntimeIndex()) {
		case REX::FModule::Runtime::kOG:
			return REL::Version{ 1, 10, 163 };
		case REX::FModule::Runtime::kNG:
			return REL::Version{ 1, 10, 984 };
		default:
			return REL::Version{ 1, 11, 240 };
		}
	}

	inline void RegisterClientTarget() noexcept
	{
		const auto target = ExecutingRuntime::Target();
		if (target) {
			ClientCompatibility::SetRuntimeTarget(*target);
		}
	}

	[[nodiscard]] inline bool IsOG() noexcept
	{
		return REX::FModule::GetRuntimeIndex() == REX::FModule::Runtime::kOG;
	}

	[[nodiscard]] inline std::vector<std::wstring> FindConflictingDlls(const std::filesystem::path& a_directory, std::wstring_view a_ownFileName)
	{
		const auto lower = [](std::wstring_view a_text) {
			std::wstring text(a_text);
			for (auto& c : text) {
				c = static_cast<wchar_t>(std::towlower(c));
			}
			return text;
		};
		const auto own = lower(a_ownFileName);
		std::vector<std::wstring> found;
		std::error_code ec;
		for (std::filesystem::directory_iterator it(a_directory, ec), end; !ec && it != end; it.increment(ec)) {
			std::error_code typeError;
			if (!it->is_regular_file(typeError)) {
				continue;
			}
			const auto name = it->path().filename().wstring();
			const auto key = lower(name);
			if (key.starts_with(L"commonwealthonline") && key.ends_with(L".dll") && key != own && key != L"commonwealthonline.dll" &&
				key != L"commonwealthonlineprobe.dll" && key != L"commonwealthonlinedebug.dll") {
				found.push_back(name);
			}
		}
		std::ranges::sort(found);
		return found;
	}

	[[nodiscard]] inline std::vector<std::wstring> ConflictingDlls() noexcept
	{
		try {
			HMODULE module = nullptr;
			const auto address = reinterpret_cast<LPCWSTR>(reinterpret_cast<std::uintptr_t>(&ConflictingDlls));
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
			const std::filesystem::path self(path.data());
			return FindConflictingDlls(self.parent_path(), self.filename().wstring());
		} catch (...) {
		}
		return {};
	}
}

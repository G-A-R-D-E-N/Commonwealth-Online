#pragma once

#include "F4TClientCompatibility.h"
#include "F4TExecutingRuntime.h"

#include <array>
#include <filesystem>

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

	[[nodiscard]] inline bool LegacyDllsPresent() noexcept
	{
		try {
			HMODULE module = nullptr;
			const auto address = reinterpret_cast<LPCWSTR>(reinterpret_cast<std::uintptr_t>(&LegacyDllsPresent));
			if (!GetModuleHandleExW(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					address,
					std::addressof(module)) ||
				!module) {
				return false;
			}
			std::array<wchar_t, 32768> path{};
			const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
			if (length == 0 || length >= path.size()) {
				return false;
			}
			const auto directory = std::filesystem::path(path.data()).parent_path();
			std::error_code ec;
			for (const auto* name : { L"CommonwealthOnline-NG.dll", L"CommonwealthOnline-OG.dll", L"CommonwealthOnlineUI.dll" }) {
				if (std::filesystem::exists(directory / name, ec)) {
					return true;
				}
			}
		} catch (...) {
		}
		return false;
	}
}

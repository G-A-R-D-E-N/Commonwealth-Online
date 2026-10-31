#include "pch.h"

#include "F4TCrashForensics.h"

#include "F4TGamePaths.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <span>
#include <string_view>

namespace
{
	constexpr std::string_view EnvironmentVariable = "COMMONWEALTH_ONLINE_CRASH_RING";
	constexpr std::size_t TrailerSize = 20;
	constexpr std::array<std::byte, 4> TrailerMagic{ std::byte{ 'C' }, std::byte{ 'R' }, std::byte{ 'F' }, std::byte{ 'T' } };

	std::atomic_bool g_inHandler{ false };
	std::filesystem::path g_dumpPath;
	std::array<F4T::MutationRing::Entry, F4T::MutationRing::Capacity> g_entries{};
	std::array<std::byte, F4T::MutationRing::MaxDumpSize + TrailerSize> g_buffer{};

	void PutUint64(std::size_t a_offset, std::uint64_t a_value)
	{
		for (std::size_t index = 0; index < 8; ++index) {
			g_buffer[a_offset + index] = static_cast<std::byte>((a_value >> (8 * index)) & 0xFFU);
		}
	}

	bool ResolveDumpPath()
	{
		std::filesystem::path folder;
		if (!F4T::GamePaths::ResolveF4SEFolder(folder)) {
			return false;
		}
		g_dumpPath = folder / std::format("CommonwealthOnline-fault-{}.corg", GetCurrentProcessId());
		return true;
	}

	void WriteDump(const EXCEPTION_POINTERS* a_info)
	{
		const auto count = F4T::MutationRing::Snapshot(g_entries);
		const auto size = F4T::MutationRing::Encode(std::span<const F4T::MutationRing::Entry>{ g_entries.data(), count }, g_buffer);
		if (size == 0) {
			return;
		}

		const auto rip = a_info->ContextRecord ? static_cast<std::uint64_t>(a_info->ContextRecord->Rip) : 0;
		const auto address = static_cast<std::uint64_t>(a_info->ExceptionRecord->ExceptionInformation[1]);
		std::ranges::copy(TrailerMagic, g_buffer.begin() + static_cast<std::ptrdiff_t>(size));
		PutUint64(size + 4, rip);
		PutUint64(size + 12, address);

		const auto file = CreateFileW(g_dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file != INVALID_HANDLE_VALUE) {
			DWORD written = 0;
			WriteFile(file, g_buffer.data(), static_cast<DWORD>(size + TrailerSize), &written, nullptr);
			CloseHandle(file);
		}
	}

	LONG CALLBACK OnVectoredException(EXCEPTION_POINTERS* a_info)
	{
		if (!a_info || !a_info->ExceptionRecord || a_info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
			return EXCEPTION_CONTINUE_SEARCH;
		}
		if (g_inHandler.exchange(true)) {
			return EXCEPTION_CONTINUE_SEARCH;
		}

		WriteDump(a_info);
		g_inHandler.store(false);
		return EXCEPTION_CONTINUE_SEARCH;
	}
}

namespace F4T::CrashForensics
{
	void Install()
	{
		const auto* value = std::getenv(EnvironmentVariable.data());
		if (!value || std::string_view{ value } != "1") {
			return;
		}
		if (!ResolveDumpPath()) {
			return;
		}

		MutationRing::SetEnabled(true);
		AddVectoredExceptionHandler(0, OnVectoredException);
		REX::INFO("Commonwealth Online crash ring armed; access-violation dump file {} in the F4SE folder", g_dumpPath.filename().string());
	}
}

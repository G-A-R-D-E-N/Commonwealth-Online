// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission.
#include "pch.h"

#include "F4TCrashReport.h"

#include "F4TF4SECapabilities.h"

#include "REX/W32/OLE32.h"
#include "REX/W32/SHELL32.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace
{
	constexpr std::size_t kPathCapacity = 1024;
	constexpr std::size_t kReportCapacity = 8192;
	constexpr std::size_t kMaxDisplacedFilters = 4;

	wchar_t g_logPath[kPathCapacity]{};
	char g_report[kReportCapacity]{};
	std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> g_displacedFilters[kMaxDisplacedFilters]{};
	std::atomic<std::size_t> g_displacedCount{ 0 };
	volatile LONG g_crashOwner{ 0 };
	thread_local bool t_inOnCrash{ false };
	bool g_installed{ false };
	DWORD g_mainThreadId{ 0 };
	constexpr std::size_t kFirstChanceLineCapacity = 256;
	constexpr int kMaxFirstChanceLines = 3;
	constexpr DWORD kStatusHeapCorruption = 0xC0000374UL;
	constexpr DWORD kStatusStackBufferOverrun = 0xC0000409UL;
	constexpr std::chrono::seconds kReassertInterval{ 30 };
	char g_firstChanceLine[kFirstChanceLineCapacity]{};
	volatile LONG g_firstChanceBusy{ 0 };
	std::atomic<int> g_firstChanceCount{ 0 };
	std::atomic<std::uint64_t> g_lastFirstChanceAddress{ ~std::uint64_t{ 0 } };
	std::chrono::steady_clock::time_point g_nextReassert{};

	void DescribeAddress(std::uint64_t a_address, F4T::CrashReport::Location& a_location) noexcept
	{
		a_location = F4T::CrashReport::Location{};
		if (a_address != 0) {
			HMODULE module{ nullptr };
			if (::GetModuleHandleExW(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(a_address)),
					std::addressof(module)) != 0 &&
				module != nullptr) {
				wchar_t path[1024]{};
				const auto length = ::GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
				if (length != 0 && length < std::size(path)) {
					const wchar_t* name = path;
					for (std::size_t index = 0; index < length; ++index) {
						if (path[index] == L'\\' || path[index] == L'/') {
							name = path + index + 1;
						}
					}
					std::size_t slot = 0;
					for (; name[slot] != L'\0' && slot + 1 < F4T::CrashReport::kMaxModuleName; ++slot) {
						const auto value = static_cast<std::uint32_t>(name[slot]);
						a_location.module[slot] = value >= 0x20 && value < 0x7F ? static_cast<char>(value) : '?';
					}
					a_location.offset = a_address - reinterpret_cast<std::uint64_t>(module);
					return;
				}
			}
		}
		a_location.address = a_address;
	}

	bool StackReadable(std::uint64_t a_address) noexcept
	{
		MEMORY_BASIC_INFORMATION info{};
		if (::VirtualQuery(reinterpret_cast<LPCVOID>(static_cast<ULONG_PTR>(a_address)), std::addressof(info), sizeof(info)) != sizeof(info)) {
			return false;
		}
		if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
			return false;
		}
		const auto regionEnd = reinterpret_cast<std::uint64_t>(info.BaseAddress) + info.RegionSize;
		return a_address + sizeof(std::uint64_t) <= regionEnd;
	}

	void CollectFrames(const CONTEXT& a_context, F4T::CrashReport::Data& a_data) noexcept
	{
		CONTEXT context = a_context;
		auto instruction = static_cast<std::uint64_t>(context.Rip);
		while (instruction != 0 && a_data.frameCount < F4T::CrashReport::kMaxFrames) {
			auto& frame = a_data.frames[a_data.frameCount++];
			DescribeAddress(instruction, frame);

			DWORD64 imageBase{ 0 };
			auto* function = ::RtlLookupFunctionEntry(instruction, std::addressof(imageBase), nullptr);
			if (function == nullptr) {
				if (!StackReadable(context.Rsp)) {
					return;
				}
				instruction = *reinterpret_cast<const std::uint64_t*>(static_cast<ULONG_PTR>(context.Rsp));
				context.Rsp += sizeof(std::uint64_t);
			} else {
				void* handlerData{ nullptr };
				DWORD64 establisherFrame{ 0 };
				::RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, instruction, function, std::addressof(context), std::addressof(handlerData), std::addressof(establisherFrame), nullptr);
				instruction = static_cast<std::uint64_t>(context.Rip);
			}
			context.Rip = instruction;
		}
	}

	void FillReport(const EXCEPTION_POINTERS& a_info, F4T::CrashReport::Data& a_data) noexcept
	{
		SYSTEMTIME time{};
		::GetLocalTime(std::addressof(time));
		a_data.year = time.wYear;
		a_data.month = static_cast<std::uint8_t>(time.wMonth);
		a_data.day = static_cast<std::uint8_t>(time.wDay);
		a_data.hour = static_cast<std::uint8_t>(time.wHour);
		a_data.minute = static_cast<std::uint8_t>(time.wMinute);
		a_data.second = static_cast<std::uint8_t>(time.wSecond);
		a_data.millisecond = time.wMilliseconds;
		a_data.threadId = ::GetCurrentThreadId();
		a_data.mainThread = a_data.threadId == g_mainThreadId;

		const auto* record = a_info.ExceptionRecord;
		if (record != nullptr) {
			a_data.exceptionCode = record->ExceptionCode;
			DescribeAddress(reinterpret_cast<std::uint64_t>(record->ExceptionAddress), a_data.faulting);
			if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
				a_data.accessViolation = true;
				a_data.accessType = static_cast<std::uint8_t>(record->ExceptionInformation[0]);
				a_data.accessAddress = record->ExceptionInformation[1];
			}
		}

		const auto* context = a_info.ContextRecord;
		if (context != nullptr) {
			a_data.registers[0] = context->Rax;
			a_data.registers[1] = context->Rbx;
			a_data.registers[2] = context->Rcx;
			a_data.registers[3] = context->Rdx;
			a_data.registers[4] = context->Rsi;
			a_data.registers[5] = context->Rdi;
			a_data.registers[6] = context->Rbp;
			a_data.registers[7] = context->Rsp;
			a_data.registers[8] = context->R8;
			a_data.registers[9] = context->R9;
			a_data.registers[10] = context->R10;
			a_data.registers[11] = context->R11;
			a_data.registers[12] = context->R12;
			a_data.registers[13] = context->R13;
			a_data.registers[14] = context->R14;
			a_data.registers[15] = context->R15;
			CollectFrames(*context, a_data);
		}
	}

	void AppendToLog(const char* a_text, std::size_t a_length) noexcept
	{
		const auto file = ::CreateFileW(
			g_logPath,
			FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			return;
		}

		DWORD written{ 0 };
		::WriteFile(file, a_text, static_cast<DWORD>(a_length), std::addressof(written), nullptr);
		::FlushFileBuffers(file);
		::CloseHandle(file);
	}

	// No allocation and no logger lock: the fault may be inside either, so the report uses static buffers and WriteFile only.
	void WriteReport(const EXCEPTION_POINTERS& a_info) noexcept
	{
		if (g_logPath[0] == L'\0') {
			return;
		}

		F4T::CrashReport::Data data{};
		FillReport(a_info, data);
		const auto length = F4T::CrashReport::Format(data, g_report, std::size(g_report));
		if (length == 0) {
			return;
		}
		AppendToLog(g_report, length);
	}

	LONG CALLBACK OnCrash(EXCEPTION_POINTERS* a_info) noexcept
	{
		if (t_inOnCrash) {
			return EXCEPTION_CONTINUE_SEARCH;
		}
		t_inOnCrash = true;
		const auto self = static_cast<LONG>(::GetCurrentThreadId());
		const auto owner = ::InterlockedCompareExchange(std::addressof(g_crashOwner), self, 0);
		if (owner == 0 && a_info != nullptr) {
			WriteReport(*a_info);
		}
		LONG result = EXCEPTION_CONTINUE_SEARCH;
		for (auto index = g_displacedCount.load(); index > 0 && result == EXCEPTION_CONTINUE_SEARCH; --index) {
			if (const auto filter = g_displacedFilters[index - 1].load()) {
				result = filter(a_info);
			}
		}
		if (owner == 0) {
			::InterlockedCompareExchange(std::addressof(g_crashOwner), 0, self);
		}
		t_inOnCrash = false;
		return result;
	}

	void RecordDisplaced(LPTOP_LEVEL_EXCEPTION_FILTER a_filter) noexcept
	{
		if (a_filter == nullptr || a_filter == OnCrash) {
			return;
		}
		const auto count = g_displacedCount.load();
		for (std::size_t index = 0; index < count; ++index) {
			if (g_displacedFilters[index].load() == a_filter) {
				return;
			}
		}
		if (count < kMaxDisplacedFilters) {
			g_displacedFilters[count].store(a_filter);
			g_displacedCount.store(count + 1);
		}
	}

	bool IsSeriousException(DWORD a_code) noexcept
	{
		switch (a_code) {
		case EXCEPTION_ACCESS_VIOLATION:
		case EXCEPTION_ILLEGAL_INSTRUCTION:
		case EXCEPTION_PRIV_INSTRUCTION:
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
		case EXCEPTION_IN_PAGE_ERROR:
		case kStatusHeapCorruption:
		case kStatusStackBufferOverrun:
			return true;
		default:
			return false;
		}
	}

	LONG CALLBACK OnFirstChance(EXCEPTION_POINTERS* a_info) noexcept
	{
		if (a_info == nullptr || a_info->ExceptionRecord == nullptr || !IsSeriousException(a_info->ExceptionRecord->ExceptionCode)) {
			return EXCEPTION_CONTINUE_SEARCH;
		}
		if (::InterlockedExchange(std::addressof(g_firstChanceBusy), 1) != 0) {
			return EXCEPTION_CONTINUE_SEARCH;
		}
		const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(a_info->ExceptionRecord->ExceptionAddress));
		if (address != g_lastFirstChanceAddress.load() && g_firstChanceCount.fetch_add(1) < kMaxFirstChanceLines) {
			g_lastFirstChanceAddress.store(address);
			F4T::CrashReport::Location location{};
			DescribeAddress(address, location);
			const auto length = F4T::CrashReport::FormatFirstChance(
				a_info->ExceptionRecord->ExceptionCode,
				location,
				g_firstChanceLine,
				std::size(g_firstChanceLine));
			AppendToLog(g_firstChanceLine, length);
		}
		::InterlockedExchange(std::addressof(g_firstChanceBusy), 0);
		return EXCEPTION_CONTINUE_SEARCH;
	}

	bool ResolveLogFolder(std::string_view a_saveFolderName, std::filesystem::path& a_folder)
	{
		wchar_t* knownBuffer{ nullptr };
		const auto knownResult =
			REX::W32::SHGetKnownFolderPath(REX::W32::FOLDERID_Documents, REX::W32::KF_FLAG_DEFAULT, nullptr, std::addressof(knownBuffer));
		std::unique_ptr<wchar_t[], decltype(&REX::W32::CoTaskMemFree)> knownPath(knownBuffer, REX::W32::CoTaskMemFree);
		if (!knownPath || knownResult != 0) {
			return false;
		}

		a_folder = knownPath.get();
		a_folder /= "My Games";
		a_folder /= a_saveFolderName;
		a_folder /= "F4SE";
		return true;
	}

	std::string PreInitPluginName()
	{
		if (const auto* data = F4SE::PluginVersionData::GetSingleton()) {
			return std::string{ data->GetPluginName() };
		}
		char path[MAX_PATH]{};
		const auto length = REX::W32::GetModuleFileNameA(REX::W32::GetCurrentModule(), path, MAX_PATH);
		return std::filesystem::path(path, path + length).stem().string();
	}

	bool ResolveLogPath() noexcept
	{
		std::filesystem::path path;
		const auto pluginName = F4SE::GetPluginName();
		if (pluginName.empty() || !ResolveLogFolder(F4T::F4SECapabilities::GetSaveFolderName(), path)) {
			return false;
		}
		path /= std::string{ pluginName } + ".log";

		const auto text = path.native();
		if (text.size() >= kPathCapacity) {
			return false;
		}
		std::copy(text.begin(), text.end(), g_logPath);
		g_logPath[text.size()] = L'\0';
		return true;
	}
}

namespace F4T::CrashReport
{
	void Install() noexcept
	{
		g_mainThreadId = ::GetCurrentThreadId();
		if (!ResolveLogPath()) {
			REX::WARN("Commonwealth Online crash report handler could not resolve the log path.");
			return;
		}

		const auto previous = ::SetUnhandledExceptionFilter(OnCrash);
		RecordDisplaced(previous);
		g_installed = true;
		::AddVectoredExceptionHandler(1, OnFirstChance);
		REX::INFO(
			"Commonwealth Online crash report handler installed into the plugin log (chained to an earlier handler: {}).",
			previous != nullptr ? "yes" : "no");
	}

	void KeepPreviousLog(const F4SE::LoadInterface* a_f4se) noexcept
	{
		if (a_f4se == nullptr) {
			return;
		}
		const auto version = a_f4se->F4SEVersion();
		const std::string saveFolder = F4T::F4SECapabilities::SupportsSaveFolderName(version.major(), version.minor(), version.patch())
			? std::string{ a_f4se->GetSaveFolderName() }
			: std::string{ "Fallout4" };
		std::filesystem::path folder;
		if (!ResolveLogFolder(saveFolder, folder)) {
			return;
		}
		const auto name = PreInitPluginName();
		const auto current = folder / (name + ".log");
		std::error_code error;
		const auto size = std::filesystem::file_size(current, error);
		if (error || size == 0) {
			return;
		}
		std::filesystem::rename(current, folder / (name + "-previous.log"), error);
	}

	void Reassert() noexcept
	{
		if (!g_installed) {
			return;
		}
		const auto current = ::SetUnhandledExceptionFilter(OnCrash);
		if (current == OnCrash) {
			return;
		}
		RecordDisplaced(current);
		REX::INFO("Commonwealth Online crash report handler put back in front (another handler had replaced it).");
	}

	void Tick() noexcept
	{
		if (!g_installed) {
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		if (now < g_nextReassert) {
			return;
		}
		g_nextReassert = now + kReassertInterval;
		Reassert();
	}
}

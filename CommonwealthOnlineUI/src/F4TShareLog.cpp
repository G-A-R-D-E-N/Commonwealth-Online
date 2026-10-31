// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission.
#include "pch.h"

#include "F4TShareLog.h"

#include "F4TLogRedaction.h"
#include "F4TServerBrowserBridgeInternal.h"

#include "REX/W32/OLE32.h"
#include "REX/W32/SHELL32.h"

#include <Windows.h>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::ShareLog
{
	namespace
	{
		constexpr const char* kLogPrefix = "[COUI] [ShareLog]";
		constexpr std::string_view kClientLogStems[] = { "CommonwealthOnline", "CommonwealthOnlineNG", "CommonwealthOnlineOG" };
		constexpr std::string_view kPreviousLogSuffix = "-previous.log";
		constexpr std::string_view kSharedLogName = "CommonwealthOnline-shared.log";
		constexpr std::size_t kMaxLogBytes = 4 * 1024 * 1024;
		constexpr int kClipboardAttempts = 10;
		constexpr DWORD kClipboardRetryDelayMs = 10;

		[[nodiscard]] bool ResolveLogDirectory(std::filesystem::path& a_directory)
		{
			wchar_t* knownBuffer{ nullptr };
			const auto knownResult =
				REX::W32::SHGetKnownFolderPath(REX::W32::FOLDERID_Documents, REX::W32::KF_FLAG_DEFAULT, nullptr, std::addressof(knownBuffer));
			std::unique_ptr<wchar_t[], decltype(&REX::W32::CoTaskMemFree)> knownPath(knownBuffer, REX::W32::CoTaskMemFree);
			if (!knownPath || knownResult != 0) {
				return false;
			}

			std::filesystem::path directory = knownPath.get();
			directory /= "My Games";
			directory /= std::string{ F4SE::GetSaveFolderName() };
			directory /= "F4SE";
			a_directory = std::move(directory);
			return true;
		}

		[[nodiscard]] bool ReadLogTail(const std::filesystem::path& a_path, std::string& a_text)
		{
			std::ifstream input(a_path, std::ios::binary);
			if (!input) {
				return false;
			}

			input.seekg(0, std::ios::end);
			const auto size = static_cast<std::uintmax_t>(input.tellg());

			if (size > kMaxLogBytes) {
				input.seekg(static_cast<std::streamoff>(size - kMaxLogBytes), std::ios::beg);
				a_text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
				const auto newline = a_text.find('\n');
				if (newline != std::string::npos) {
					a_text.erase(0, newline + 1);
				}
				return true;
			}

			input.seekg(0, std::ios::beg);
			a_text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
			return true;
		}

		[[nodiscard]] bool CopyToClipboard(std::string_view a_text)
		{
			if (a_text.empty()) {
				return false;
			}

			const auto length = static_cast<int>(a_text.size());
			const auto wideLength = ::MultiByteToWideChar(CP_UTF8, 0, a_text.data(), length, nullptr, 0);
			if (wideLength <= 0) {
				return false;
			}

			std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
			if (::MultiByteToWideChar(CP_UTF8, 0, a_text.data(), length, wide.data(), wideLength) != wideLength) {
				return false;
			}

			const auto bytes = (static_cast<std::size_t>(wideLength) + 1) * sizeof(wchar_t);
			HGLOBAL block = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
			if (!block) {
				return false;
			}

			if (auto* target = static_cast<wchar_t*>(::GlobalLock(block))) {
				std::memcpy(target, wide.c_str(), bytes);
				::GlobalUnlock(block);
			} else {
				::GlobalFree(block);
				return false;
			}

			bool opened = false;
			for (int attempt = 0; attempt < kClipboardAttempts && !opened; ++attempt) {
				opened = ::OpenClipboard(nullptr) != 0;
				if (!opened) {
					::Sleep(kClipboardRetryDelayMs);
				}
			}
			if (!opened) {
				::GlobalFree(block);
				return false;
			}

			bool stored = false;
			if (::EmptyClipboard()) {
				stored = ::SetClipboardData(CF_UNICODETEXT, block) != nullptr;
			}
			::CloseClipboard();

			if (!stored) {
				::GlobalFree(block);
			}
			return stored;
		}

		void DispatchResult(const Result& a_result)
		{
			std::string message;
			if (a_result.success) {
				message = a_result.clipboardCopied
					? std::format("Saved redacted log to {} and copied it to the clipboard.", a_result.savedPath)
					: std::format("Saved redacted log to {} (clipboard unavailable).", a_result.savedPath);
			} else {
				message = a_result.error.empty() ? std::string{ "Could not share the log." } : a_result.error;
			}

			F4T::ServerBrowser::detail::DispatchEvent(nlohmann::json{
				{ "type", "shareLogResult" },
				{ "success", a_result.success },
				{ "clipboardCopied", a_result.clipboardCopied },
				{ "savedPath", a_result.savedPath },
				{ "message", message }
			});
		}
	}

	Result Share()
	{
		Result result;

		const auto fail = [&result](std::string a_error) {
			result.success = false;
			result.clipboardCopied = false;
			result.error = std::move(a_error);
			REX::WARN("{} {}", kLogPrefix, result.error);
			DispatchResult(result);
			return result;
		};

		try {
			std::filesystem::path directory;
			if (!ResolveLogDirectory(directory)) {
				return fail("Could not resolve the Fallout 4 log directory.");
			}

			std::filesystem::path clientLog;
			std::filesystem::file_time_type newest{};
			for (const auto stem : kClientLogStems) {
				auto candidate = directory / (std::string{ stem } + ".log");
				std::error_code error;
				const auto written = std::filesystem::last_write_time(candidate, error);
				if (error) {
					continue;
				}
				if (clientLog.empty() || written > newest) {
					newest = written;
					clientLog = std::move(candidate);
				}
			}
			if (clientLog.empty()) {
				return fail(std::format("Log file not found in {}.", directory.string()));
			}

			std::string raw;
			if (!ReadLogTail(clientLog, raw)) {
				return fail(std::format("Could not read {}.", clientLog.string()));
			}
			if (raw.empty()) {
				return fail(std::format("Log file at {} is empty.", clientLog.string()));
			}

			std::string previousRaw;
			bool previousUnreadable = false;
			if (const auto previousLog = directory / (clientLog.stem().string() + std::string{ kPreviousLogSuffix });
				std::filesystem::exists(previousLog) && !ReadLogTail(previousLog, previousRaw)) {
				previousRaw.clear();
				previousUnreadable = true;
			}

			auto redacted = F4T::LogRedaction::ComposeSharedLog(previousRaw, raw);
			if (previousUnreadable) {
				if (!redacted.empty() && redacted.back() != '\n') {
					redacted.push_back('\n');
				}
				redacted += std::format("{} Could not read the previous session log, so only the current session is shared.\n", kLogPrefix);
			}

			const auto sharedLog = directory / std::string{ kSharedLogName };
			std::ofstream output(sharedLog, std::ios::binary | std::ios::trunc);
			if (!output) {
				return fail(std::format("Could not write {}.", sharedLog.string()));
			}
			output << redacted;
			output.close();
			if (!output) {
				return fail(std::format("Could not write {}.", sharedLog.string()));
			}

			result.success = true;
			result.savedPath = sharedLog.string();
			result.clipboardCopied = CopyToClipboard(redacted);

			REX::INFO(
				"{} Saved redacted log to {} ({} bytes, clipboard {})",
				kLogPrefix,
				result.savedPath,
				redacted.size(),
				result.clipboardCopied ? "ok" : "unavailable");
		} catch (const std::exception& exception) {
			result.success = false;
			result.clipboardCopied = false;
			result.error = std::format("Could not share the log: {}", exception.what());
			REX::WARN("{} {}", kLogPrefix, result.error);
		}

		DispatchResult(result);
		return result;
	}
}

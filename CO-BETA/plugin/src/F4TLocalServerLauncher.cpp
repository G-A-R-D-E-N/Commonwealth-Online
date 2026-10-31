#include "pch.h"

#include "F4TLocalServerLauncher.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include "F4TLocalServerLogLine.h"

#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace F4T::LocalServer
{
	namespace
	{
		constexpr auto kStartTimeout = 20s;

		std::mutex g_launchMutex;

		HANDLE ServerJob()
		{
			static const HANDLE job = [] {
				const HANDLE created = CreateJobObjectW(nullptr, nullptr);
				if (!created) {
					F4T::DebugLogging::Log("LocalServer", "CreateJobObjectW failed (Windows error {})", GetLastError());
					return HANDLE{};
				}
				JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
				limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
				if (!SetInformationJobObject(created, JobObjectExtendedLimitInformation, std::addressof(limits), sizeof(limits))) {
					F4T::DebugLogging::Log("LocalServer", "SetInformationJobObject failed (Windows error {})", GetLastError());
					CloseHandle(created);
					return HANDLE{};
				}
				return created;
			}();
			return job;
		}

		bool PortInUse(std::uint16_t a_port)
		{
			WSADATA winsockData{};
			if (WSAStartup(MAKEWORD(2, 2), std::addressof(winsockData)) != 0) {
				return false;
			}
			bool inUse = false;
			const auto probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (probe != INVALID_SOCKET) {
				sockaddr_in address{};
				address.sin_family = AF_INET;
				address.sin_addr.s_addr = htonl(INADDR_ANY);
				address.sin_port = htons(a_port);
				if (bind(probe, reinterpret_cast<const sockaddr*>(std::addressof(address)), sizeof(address)) == SOCKET_ERROR) {
					const auto error = WSAGetLastError();
					inUse = error == WSAEADDRINUSE || error == WSAEACCES;
				}
				closesocket(probe);
			}
			WSACleanup();
			return inUse;
		}

		std::filesystem::path ServerDirectory()
		{
			HMODULE module = nullptr;
			const auto moduleAddress = reinterpret_cast<LPCWSTR>(reinterpret_cast<std::uintptr_t>(&ServerDirectory));
			if (!GetModuleHandleExW(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					moduleAddress,
					std::addressof(module)) ||
				!module) {
				return {};
			}
			std::array<wchar_t, 32768> path{};
			const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
			if (length == 0 || length >= path.size()) {
				return {};
			}
			return std::filesystem::path{ std::wstring_view{ path.data(), length } }.parent_path().parent_path().parent_path() / L"server";
		}

		std::wstring Widen(std::string_view a_text)
		{
			if (a_text.empty()) {
				return {};
			}
			const auto length = MultiByteToWideChar(CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()), nullptr, 0);
			std::wstring wide(static_cast<std::size_t>(length > 0 ? length : 0), L'\0');
			if (length > 0) {
				MultiByteToWideChar(CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()), wide.data(), length);
			}
			return wide;
		}

		std::string LastLogLine(const std::filesystem::path& a_log)
		{
			std::ifstream file{ a_log, std::ios::binary };
			if (!file) {
				return {};
			}
			std::string text;
			file.seekg(0, std::ios::end);
			const auto size = file.tellg();
			if (size > 0) {
				text.resize(static_cast<std::size_t>(size));
				file.seekg(0, std::ios::beg);
				file.read(text.data(), size);
				const auto read = file.gcount();
				text.resize(read > 0 ? static_cast<std::size_t>(read) : 0);
			}
			return SelectLastLogLine(text);
		}

		void RotateConsoleLog(const std::filesystem::path& a_log)
		{
			std::error_code ec;
			if (!std::filesystem::exists(a_log, ec)) {
				return;
			}
			const auto previous = a_log.parent_path() / L"server-console.prev.log";
			if (!MoveFileExW(a_log.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING)) {
				F4T::DebugLogging::Log(
					"LocalServer",
					"could not rotate the previous server output to server-console.prev.log (Windows error {})",
					GetLastError());
			}
		}

		std::wstring Quote(std::wstring a_text)
		{
			std::erase(a_text, L'"');
			while (!a_text.empty() && a_text.back() == L'\\') {
				a_text.pop_back();
			}
			return L"\"" + a_text + L"\"";
		}
	}

	std::string EnsureRunning(std::string_view a_name, std::uint32_t a_maxPlayers, std::uint16_t a_port)
	{
		const std::scoped_lock lock(g_launchMutex);
		if (PortInUse(a_port)) {
			F4T::DebugLogging::Log("LocalServer", "port {} already in use; reusing the running server", a_port);
			return {};
		}

		const auto directory = ServerDirectory();
		const auto executable = directory / L"CommonwealthOnline.Server.exe";
		std::error_code ec;
		if (directory.empty() || !std::filesystem::exists(executable, ec)) {
			return "CommonwealthOnline.Server.exe was not found in Data\\server. Reinstall Commonwealth Online.";
		}

		auto commandLine = Quote(executable.wstring());
		const auto config = directory / L"commonwealth-server.json";
		if (std::filesystem::exists(config, ec)) {
			commandLine += L" --config " + Quote(config.wstring());
		}
		commandLine += std::format(L" --port {}", a_port);
		// Steam P2P hosting must never register with the public directory.
		commandLine += L" --no-public-listing";
		if (!a_name.empty()) {
			commandLine += L" --name " + Quote(Widen(a_name));
		}
		if (a_maxPlayers >= 1 && a_maxPlayers <= 16) {
			commandLine += std::format(L" --max-players {}", a_maxPlayers);
		}

		const auto consoleLog = directory / L"server-console.log";
		RotateConsoleLog(consoleLog);
		HANDLE logHandle = CreateFileW(consoleLog.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (logHandle == INVALID_HANDLE_VALUE) {
			F4T::DebugLogging::Log("LocalServer", "could not create server-console.log (Windows error {}); server output is not captured", GetLastError());
			logHandle = nullptr;
		}

		std::vector<std::byte> attributeStorage;
		LPPROC_THREAD_ATTRIBUTE_LIST attributes = nullptr;
		bool attributeListReady = false;
		bool inheritLog = false;
		if (logHandle) {
			SIZE_T attributeSize = 0;
			InitializeProcThreadAttributeList(nullptr, 1, 0, std::addressof(attributeSize));
			attributeStorage.resize(attributeSize);
			attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
			if (InitializeProcThreadAttributeList(attributes, 1, 0, std::addressof(attributeSize))) {
				attributeListReady = true;
				inheritLog = UpdateProcThreadAttribute(
					attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, std::addressof(logHandle), sizeof(logHandle), nullptr, nullptr) != FALSE;
			}
			if (!inheritLog) {
				F4T::DebugLogging::Log(
					"LocalServer",
					"could not restrict the inherited handles (Windows error {}); server output is not captured",
					GetLastError());
			}
		}

		if (inheritLog) {
			inheritLog = SetHandleInformation(logHandle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT) != FALSE;
			if (!inheritLog) {
				F4T::DebugLogging::Log(
					"LocalServer",
					"could not make the server-console.log handle inheritable (Windows error {}); server output is not captured",
					GetLastError());
			}
		}

		STARTUPINFOEXW startup{};
		startup.StartupInfo.cb = inheritLog ? sizeof(startup) : sizeof(STARTUPINFOW);
		startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
		startup.StartupInfo.wShowWindow = SW_SHOWMINNOACTIVE;
		if (inheritLog) {
			startup.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
			startup.StartupInfo.hStdOutput = logHandle;
			startup.StartupInfo.hStdError = logHandle;
			startup.lpAttributeList = attributes;
		}
		PROCESS_INFORMATION process{};
		const bool created = CreateProcessW(
			executable.c_str(),
			commandLine.data(),
			nullptr,
			nullptr,
			inheritLog,
			(inheritLog ? CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT : CREATE_NEW_CONSOLE) | CREATE_SUSPENDED,
			nullptr,
			directory.c_str(),
			reinterpret_cast<LPSTARTUPINFOW>(std::addressof(startup)),
			std::addressof(process));
		const auto createError = GetLastError();
		if (attributeListReady) {
			DeleteProcThreadAttributeList(attributes);
		}
		if (logHandle) {
			SetHandleInformation(logHandle, HANDLE_FLAG_INHERIT, 0);
			CloseHandle(logHandle);
		}
		if (!created) {
			return std::format("Could not start CommonwealthOnline.Server.exe (Windows error {}).", createError);
		}
		const auto job = ServerJob();
		if (!job) {
			F4T::DebugLogging::Log("LocalServer", "no job object; the server will keep running after the game exits");
		} else if (!AssignProcessToJobObject(job, process.hProcess)) {
			F4T::DebugLogging::Log("LocalServer", "could not tie the server to the game's lifetime (Windows error {}); it will keep running after the game exits", GetLastError());
		}
		if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
			const auto resumeError = GetLastError();
			TerminateProcess(process.hProcess, 1);
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
			return std::format("Could not start CommonwealthOnline.Server.exe (Windows error {}).", resumeError);
		}
		CloseHandle(process.hThread);
		F4T::DebugLogging::Log("LocalServer", "started CommonwealthOnline.Server.exe pid {}", process.dwProcessId);

		std::string error;
		const auto deadline = std::chrono::steady_clock::now() + kStartTimeout;
		while (true) {
			if (PortInUse(a_port)) {
				break;
			}
			if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) {
				DWORD exitCode = 0;
				GetExitCodeProcess(process.hProcess, std::addressof(exitCode));
				const auto reason = inheritLog ? LastLogLine(consoleLog) : std::string{};
				F4T::DebugLogging::Log("LocalServer", "server exited with code {}: {}", exitCode, reason.empty() ? std::string{ "<no output>" } : reason);
				if (!reason.empty()) {
					error = std::format(
						"The server closed while starting (exit code {}): \"{}\" Full output: Data\\server\\server-console.log.",
						exitCode,
						reason);
				} else if (inheritLog) {
					error = std::format("The server closed while starting (exit code {}). Check Data\\server\\commonwealth-server.json.", exitCode);
				} else {
					error = std::format("The server closed while starting (exit code {}). The server output was not captured.", exitCode);
				}
				break;
			}
			if (std::chrono::steady_clock::now() >= deadline) {
				error = inheritLog
					? "The server did not start listening in time. Check Data\\server\\server-console.log for errors."
					: "The server did not start listening in time.";
				break;
			}
			std::this_thread::sleep_for(250ms);
		}
		CloseHandle(process.hProcess);
		return error;
	}
}

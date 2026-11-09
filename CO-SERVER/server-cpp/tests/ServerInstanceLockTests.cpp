#include "ServerInstanceLock.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <Windows.h>
#else
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace
{
	int RunChild(const std::filesystem::path& a_executable, const std::filesystem::path& a_lock, bool a_abrupt = false)
	{
#ifdef _WIN32
		auto command = L"\"" + a_executable.wstring() + (a_abrupt ? L"\" --abrupt \"" : L"\" --child \"") + a_lock.wstring() + L"\"";
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		assert(CreateProcessW(a_executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process));
		assert(WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0);
		DWORD code = 0;
		assert(GetExitCodeProcess(process.hProcess, &code));
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		return static_cast<int>(code);
#else
		const auto pid = fork();
		assert(pid >= 0);
		if (pid == 0) {
			execl(a_executable.c_str(), a_executable.c_str(), a_abrupt ? "--abrupt" : "--child", a_lock.c_str(), static_cast<char*>(nullptr));
			_exit(127);
		}
		int status = 0;
		assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status));
		return WEXITSTATUS(status);
#endif
	}
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
	using CommonwealthOnline::Server::ServerInstanceLock;
#ifdef _WIN32
	const bool isChild = argc == 3 && (std::wstring_view{ argv[1] } == L"--child" || std::wstring_view{ argv[1] } == L"--abrupt");
	const bool abrupt = isChild && std::wstring_view{ argv[1] } == L"--abrupt";
#else
	const bool isChild = argc == 3 && (std::string_view{ argv[1] } == "--child" || std::string_view{ argv[1] } == "--abrupt");
	const bool abrupt = isChild && std::string_view{ argv[1] } == "--abrupt";
#endif
	if (isChild) {
		ServerInstanceLock child;
		std::string error;
		if (!child.Acquire(argv[2], error)) return 7;
		if (abrupt) std::_Exit(0);
		return 0;
	}
	const auto executable = std::filesystem::absolute(argv[0]);
	const auto root = std::filesystem::temp_directory_path() /
		("co-server-lock-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(root);
	const auto path = root / "server.json.lock";
	std::string error;
	{
		ServerInstanceLock first;
		ServerInstanceLock second;
		assert(first.Acquire(path, error) && error.empty());
		assert(!second.Acquire(path, error) && !error.empty());
		assert(!first.Acquire(path, error) && !error.empty());
		assert(RunChild(executable, path) == 7);
	}
	{
		ServerInstanceLock afterRelease;
		assert(afterRelease.Acquire(path, error) && error.empty());
	}
	assert(RunChild(executable, path) == 0);
	assert(RunChild(executable, path, true) == 0);
	{
		ServerInstanceLock afterAbruptExit;
		assert(afterAbruptExit.Acquire(path, error));
	}
	std::error_code ec;
	std::filesystem::create_symlink(path, root / "link", ec);
	if (!ec) {
		ServerInstanceLock linked;
		assert(!linked.Acquire(root / "link", error) && !error.empty());
	}
	ServerInstanceLock missingParent;
	assert(!missingParent.Acquire(root / "missing" / "server.lock", error) && !error.empty());
	std::filesystem::remove_all(root);
}

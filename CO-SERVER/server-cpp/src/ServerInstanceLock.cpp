#include "ServerInstanceLock.h"

#include <system_error>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <Windows.h>
#else
#  include <cerrno>
#  include <fcntl.h>
#  include <sys/file.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace CommonwealthOnline::Server
{
	ServerInstanceLock::~ServerInstanceLock()
	{
		if (handle_ == -1) return;
#ifdef _WIN32
		CloseHandle(reinterpret_cast<HANDLE>(handle_));
#else
		close(static_cast<int>(handle_));
#endif
	}

	bool ServerInstanceLock::Acquire(const std::filesystem::path& a_path, std::string& a_error)
	{
		if (handle_ != -1) { a_error = "Server instance lock is already held"; return false; }
#ifdef _WIN32
		const auto handle = CreateFileW(a_path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			a_error = std::system_category().message(static_cast<int>(GetLastError()));
			return false;
		}
		BY_HANDLE_FILE_INFORMATION information{};
		if (!GetFileInformationByHandle(handle, &information)) {
			a_error = std::system_category().message(static_cast<int>(GetLastError()));
			CloseHandle(handle);
			return false;
		}
		if ((information.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0 || GetFileType(handle) != FILE_TYPE_DISK) {
			a_error = "Server instance lock must be a regular file";
			CloseHandle(handle);
			return false;
		}
		OVERLAPPED overlap{};
		if (!LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &overlap)) {
			a_error = std::system_category().message(static_cast<int>(GetLastError()));
			CloseHandle(handle);
			return false;
		}
		handle_ = reinterpret_cast<std::intptr_t>(handle);
#else
		const auto handle = open(a_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
		if (handle < 0) { a_error = std::generic_category().message(errno); return false; }
		struct stat information{};
		if (fstat(handle, &information) != 0 || !S_ISREG(information.st_mode)) {
			a_error = "Server instance lock must be a regular file";
			close(handle);
			return false;
		}
		if (flock(handle, LOCK_EX | LOCK_NB) != 0) {
			a_error = std::generic_category().message(errno);
			close(handle);
			return false;
		}
		handle_ = handle;
#endif
		a_error.clear();
		return true;
	}
}

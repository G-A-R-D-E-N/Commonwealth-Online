#include "AtomicFileWrite.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <Windows.h>
#else
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

namespace CommonwealthOnline::Server
{
	namespace
	{
		std::string UniqueSuffix()
		{
			static std::atomic_uint64_t counter{ 1 };
			const auto now = static_cast<std::uint64_t>(
				std::chrono::steady_clock::now().time_since_epoch().count());
			return std::to_string(now) + "-" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
		}

		#ifdef _WIN32
		std::runtime_error WindowsFailure(std::string_view a_operation, const std::filesystem::path& a_path)
		{
			return std::runtime_error(
				std::string{ a_operation } + " failed for '" + a_path.string() + "' (code " +
				std::to_string(static_cast<unsigned long>(GetLastError())) + ")");
		}

		void WriteSynced(const std::filesystem::path& a_path, std::string_view a_contents)
		{
			const HANDLE handle = CreateFileW(
				a_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
			if (handle == INVALID_HANDLE_VALUE) throw WindowsFailure("CreateFileW", a_path);
			try {
				std::size_t written = 0;
				while (written < a_contents.size()) {
					const auto remaining = static_cast<DWORD>((std::min)(
						a_contents.size() - written,
						static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
					DWORD chunk = 0;
					if (!WriteFile(handle, a_contents.data() + written, remaining, &chunk, nullptr) || chunk == 0) {
						throw WindowsFailure("WriteFile", a_path);
					}
					written += chunk;
				}
				if (!FlushFileBuffers(handle)) throw WindowsFailure("FlushFileBuffers", a_path);
			} catch (...) {
				CloseHandle(handle);
				throw;
			}
			CloseHandle(handle);
		}

		void ReplaceSynced(
			const std::filesystem::path& a_source,
			const std::filesystem::path& a_destination,
			bool* a_replaced)
		{
			if (!MoveFileExW(
					a_source.c_str(), a_destination.c_str(),
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
				throw WindowsFailure("MoveFileExW", a_destination);
			}
			if (a_replaced != nullptr) *a_replaced = true;
		}
		#else
		std::runtime_error PosixFailure(std::string_view a_operation, const std::filesystem::path& a_path)
		{
			return std::runtime_error(
				std::string{ a_operation } + " failed for '" + a_path.string() + "' (errno " + std::to_string(errno) + ")");
		}

		void WriteSynced(const std::filesystem::path& a_path, std::string_view a_contents)
		{
			const int descriptor = open(a_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
			if (descriptor < 0) throw PosixFailure("open", a_path);
			try {
				std::size_t written = 0;
				while (written < a_contents.size()) {
					const auto count = write(descriptor, a_contents.data() + written, a_contents.size() - written);
					if (count < 0) {
						if (errno == EINTR) continue;
						throw PosixFailure("write", a_path);
					}
					if (count == 0) throw std::runtime_error("write made no progress for '" + a_path.string() + "'");
					written += static_cast<std::size_t>(count);
				}
				if (fsync(descriptor) != 0) throw PosixFailure("fsync", a_path);
			} catch (...) {
				close(descriptor);
				throw;
			}
			if (close(descriptor) != 0) throw PosixFailure("close", a_path);
		}

		void SyncDirectory(const std::filesystem::path& a_directory)
		{
			const int descriptor = open(a_directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
			if (descriptor < 0) throw PosixFailure("open directory", a_directory);
			const int result = fsync(descriptor);
			const int savedErrno = errno;
			close(descriptor);
			if (result != 0) {
				errno = savedErrno;
				throw PosixFailure("fsync directory", a_directory);
			}
		}

		void ReplaceSynced(
			const std::filesystem::path& a_source,
			const std::filesystem::path& a_destination,
			bool* a_replaced)
		{
			if (rename(a_source.c_str(), a_destination.c_str()) != 0) throw PosixFailure("rename", a_destination);
			if (a_replaced != nullptr) *a_replaced = true;
			if (chmod(a_destination.c_str(), 0600) != 0) throw PosixFailure("chmod", a_destination);
			SyncDirectory(a_destination.has_parent_path() ? a_destination.parent_path() : std::filesystem::path{ "." });
		}
		#endif
	}

	void WriteFileAtomically(
		const std::filesystem::path& a_destination,
		std::string_view a_contents,
		bool* a_committed)
	{
		const auto tempPath = a_destination.parent_path() /
			("." + a_destination.filename().string() + "." + UniqueSuffix() + ".tmp");
		try {
			WriteSynced(tempPath, a_contents);
			ReplaceSynced(tempPath, a_destination, a_committed);
		} catch (...) {
			std::error_code ignored;
			std::filesystem::remove(tempPath, ignored);
			throw;
		}
	}
}

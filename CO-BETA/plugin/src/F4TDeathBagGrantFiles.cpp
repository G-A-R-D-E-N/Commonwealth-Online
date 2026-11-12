#include "pch.h"

#include "F4TDeathBagGrantFiles.h"
#include "F4TServerProfileStore.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
	bool CommitFileToDisk(const std::filesystem::path& a_path)
	{
#ifdef _WIN32
		const auto fileDescriptor = _wopen(a_path.c_str(), _O_WRONLY | _O_BINARY);
		if (fileDescriptor < 0) {
			return false;
		}
		const auto committed = _commit(fileDescriptor) == 0;
		_close(fileDescriptor);
		return committed;
#else
		const auto fileDescriptor = open(a_path.c_str(), O_RDONLY);
		if (fileDescriptor < 0) {
			return false;
		}
		const auto committed = fsync(fileDescriptor) == 0;
		close(fileDescriptor);
		return committed;
#endif
	}
}

namespace F4T::DeathBagGrantFiles
{
	std::filesystem::path StorePath(const char* a_name)
	{
		auto directory = F4T::ServerProfileStore::GetStoreDirectory();
		if (directory.empty()) {
			return {};
		}
		return directory / a_name;
	}

	std::int64_t NowSeconds()
	{
		return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}

	std::optional<std::string> ReadBytes(const std::filesystem::path& a_path)
	{
		std::ifstream input(a_path, std::ios::binary);
		if (!input.is_open()) {
			return std::nullopt;
		}
		return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
	}

	bool WriteBytes(const std::filesystem::path& a_path, const std::string& a_bytes, std::ios::openmode a_mode)
	{
		std::error_code ec;
		std::filesystem::create_directories(a_path.parent_path(), ec);
		if (ec) {
			return false;
		}
		{
			std::ofstream output(a_path, std::ios::binary | a_mode);
			if (!output.is_open()) {
				return false;
			}
			output << a_bytes;
			output.close();
			if (output.fail()) {
				return false;
			}
		}
		return CommitFileToDisk(a_path);
	}

	bool ReplaceBytes(const std::filesystem::path& a_path, const std::string& a_bytes)
	{
		auto temporary = a_path;
		temporary += ".tmp";
		std::error_code ec;
		if (!WriteBytes(temporary, a_bytes, std::ios::trunc)) {
			std::filesystem::remove(temporary, ec);
			return false;
		}
		std::filesystem::rename(temporary, a_path, ec);
		if (ec) {
			std::filesystem::remove(temporary, ec);
			return false;
		}
		return true;
	}

	bool RetireTornTail(const std::filesystem::path& a_journal, const std::string& a_bytes, std::size_t a_committedBytes)
	{
		auto backup = a_journal;
		backup += ".torn-" + std::to_string(NowSeconds());
		if (!WriteBytes(backup, a_bytes, std::ios::trunc)) {
			return false;
		}
		std::error_code ec;
		std::filesystem::resize_file(a_journal, a_committedBytes, ec);
		return !ec && CommitFileToDisk(a_journal);
	}

	void RetireLegacy(const std::filesystem::path& a_legacy)
	{
		auto retired = a_legacy;
		retired += ".migrated";
		std::error_code ec;
		std::filesystem::rename(a_legacy, retired, ec);
		if (ec) {
			REX::WARN("Death bag legacy grant ledger could not be renamed after migration; it will be retried on the next load.");
		}
	}
}

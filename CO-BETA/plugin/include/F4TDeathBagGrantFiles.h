#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace F4T::DeathBagGrantFiles
{
	std::filesystem::path StorePath(const char* a_name);
	std::int64_t NowSeconds();
	std::optional<std::string> ReadBytes(const std::filesystem::path& a_path);
	bool WriteBytes(const std::filesystem::path& a_path, const std::string& a_bytes, std::ios::openmode a_mode);
	bool ReplaceBytes(const std::filesystem::path& a_path, const std::string& a_bytes);
	bool RetireTornTail(const std::filesystem::path& a_journal, const std::string& a_bytes, std::size_t a_committedBytes);
	void RetireLegacy(const std::filesystem::path& a_legacy);
}

#include "F4TModpackInstall.h"

#include "F4TModpackDownload.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <windows.h>
#endif

namespace F4T::ModpackInstall
{
	namespace
	{
		[[nodiscard]] std::uint64_t FileSizeOrMax(const std::filesystem::path& a_path)
		{
			std::error_code error;
			const auto size = std::filesystem::file_size(a_path, error);
			return error ? std::numeric_limits<std::uint64_t>::max() : size;
		}

#ifdef _WIN32
		[[nodiscard]] std::wstring Extended(const std::filesystem::path& a_path)
		{
			std::error_code error;
			auto absolute = std::filesystem::absolute(a_path, error);
			if (error) {
				absolute = a_path;
			}
			std::wstring text = absolute.lexically_normal().native();
			if (text.starts_with(L"\\\\?\\")) {
				return text;
			}
			if (text.starts_with(L"\\\\")) {
				return L"\\\\?\\UNC\\" + text.substr(2);
			}
			return L"\\\\?\\" + text;
		}

		[[nodiscard]] DWORD Attributes(const std::filesystem::path& a_path)
		{
			return GetFileAttributesW(Extended(a_path).c_str());
		}

		[[nodiscard]] bool HasReparseAncestor(const std::filesystem::path& a_path)
		{
			std::error_code error;
			auto current = std::filesystem::absolute(a_path, error).lexically_normal();
			while (!current.empty() && current != current.root_path()) {
				const auto attributes = Attributes(current);
				if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
					return true;
				}
				current = current.parent_path();
			}
			return false;
		}

		class NativeFileSystem final : public FileSystem
		{
		public:
			bool Exists(const std::filesystem::path& a_path) override
			{
				return Attributes(a_path) != INVALID_FILE_ATTRIBUTES;
			}

			bool IsPlainFile(const std::filesystem::path& a_path) override
			{
				const auto attributes = Attributes(a_path);
				return attributes != INVALID_FILE_ATTRIBUTES && (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
					!HasReparseAncestor(a_path.parent_path());
			}

			bool IsPlainDirectory(const std::filesystem::path& a_path) override
			{
				const auto attributes = Attributes(a_path);
				return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
					(attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 && !HasReparseAncestor(a_path);
			}

			bool CreateDirectories(const std::filesystem::path& a_path) override
			{
				std::error_code error;
				std::filesystem::create_directories(a_path, error);
				return IsPlainDirectory(a_path);
			}

			bool CreateNewDirectory(const std::filesystem::path& a_path) override
			{
				return !HasReparseAncestor(a_path.parent_path()) && CreateDirectoryW(Extended(a_path).c_str(), nullptr) != FALSE;
			}

			bool MoveNoReplace(const std::filesystem::path& a_from, const std::filesystem::path& a_to) override
			{
				if (HasReparseAncestor(a_from) || HasReparseAncestor(a_to.parent_path())) {
					return false;
				}
				return MoveFileExW(Extended(a_from).c_str(), Extended(a_to).c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
			}

			bool WriteDurably(const std::filesystem::path& a_path, std::string_view a_bytes) override
			{
				if (HasReparseAncestor(a_path.parent_path()) || a_bytes.size() > std::numeric_limits<DWORD>::max()) {
					return false;
				}
				auto temporary = a_path;
				temporary += L".co-tmp";
				const auto handle = CreateFileW(Extended(temporary).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (handle == INVALID_HANDLE_VALUE) {
					return false;
				}
				DWORD written = 0;
				const bool ok = WriteFile(handle, a_bytes.data(), static_cast<DWORD>(a_bytes.size()), &written, nullptr) != FALSE &&
					written == a_bytes.size() && FlushFileBuffers(handle) != FALSE;
				CloseHandle(handle);
				if (!ok) {
					DeleteFileW(Extended(temporary).c_str());
					return false;
				}
				return MoveFileExW(Extended(temporary).c_str(), Extended(a_path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
			}

			std::optional<std::string> Read(const std::filesystem::path& a_path, std::size_t a_maximumBytes) override
			{
				if (!IsPlainFile(a_path) || FileSizeOrMax(a_path) > a_maximumBytes) {
					return std::nullopt;
				}
				std::ifstream stream(a_path, std::ios::binary);
				if (!stream) {
					return std::nullopt;
				}
				return std::string{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
			}

			bool Remove(const std::filesystem::path& a_path) override
			{
				const auto attributes = Attributes(a_path);
				if (attributes == INVALID_FILE_ATTRIBUTES) {
					return true;
				}
				if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
					return RemoveDirectoryW(Extended(a_path).c_str()) != FALSE;
				}
				return DeleteFileW(Extended(a_path).c_str()) != FALSE;
			}

			bool Matches(const std::filesystem::path& a_path, std::string_view a_sha256Hex, std::uint64_t a_sizeBytes) override
			{
				return IsPlainFile(a_path) && ModpackDownload::StagedFileMatches(a_path, a_sha256Hex, a_sizeBytes);
			}

			std::optional<std::string> ActualName(const std::filesystem::path& a_path) override
			{
				WIN32_FIND_DATAW found{};
				const auto handle = FindFirstFileW(Extended(a_path).c_str(), &found);
				if (handle == INVALID_HANDLE_VALUE) {
					return std::nullopt;
				}
				FindClose(handle);
				return std::filesystem::path{ found.cFileName }.string();
			}

			bool SameVolume(const std::filesystem::path& a_left, const std::filesystem::path& a_right) override
			{
				const auto volume = [](const std::filesystem::path& a_path) -> std::optional<DWORD> {
					const auto handle = CreateFileW(Extended(a_path).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
					if (handle == INVALID_HANDLE_VALUE) {
						return std::nullopt;
					}
					BY_HANDLE_FILE_INFORMATION information{};
					const bool ok = GetFileInformationByHandle(handle, &information) != FALSE;
					CloseHandle(handle);
					return ok ? std::optional<DWORD>{ information.dwVolumeSerialNumber } : std::nullopt;
				};
				const auto left = volume(a_left);
				const auto right = volume(a_right);
				return left && right && *left == *right;
			}

			std::optional<std::uint64_t> FreeBytes(const std::filesystem::path& a_path) override
			{
				ULARGE_INTEGER available{};
				if (GetDiskFreeSpaceExW(Extended(a_path).c_str(), &available, nullptr, nullptr) == FALSE) {
					return std::nullopt;
				}
				return available.QuadPart;
			}

			std::vector<std::string> List(const std::filesystem::path& a_directory) override
			{
				std::vector<std::string> names;
				std::error_code error;
				for (std::filesystem::directory_iterator it(a_directory, error), end; !error && it != end; it.increment(error)) {
					if (it->is_regular_file(error)) {
						names.push_back(it->path().filename().string());
					}
				}
				return names;
			}
		};
#else
		[[nodiscard]] bool HasLinkAncestor(const std::filesystem::path& a_path)
		{
			std::error_code error;
			auto current = std::filesystem::absolute(a_path, error).lexically_normal();
			while (!current.empty() && current != current.root_path()) {
				if (std::filesystem::is_symlink(std::filesystem::symlink_status(current, error))) {
					return true;
				}
				current = current.parent_path();
			}
			return false;
		}

		class NativeFileSystem final : public FileSystem
		{
		public:
			bool Exists(const std::filesystem::path& a_path) override
			{
				std::error_code error;
				return std::filesystem::exists(std::filesystem::symlink_status(a_path, error));
			}

			bool IsPlainFile(const std::filesystem::path& a_path) override
			{
				std::error_code error;
				return std::filesystem::is_regular_file(std::filesystem::symlink_status(a_path, error)) && !HasLinkAncestor(a_path.parent_path());
			}

			bool IsPlainDirectory(const std::filesystem::path& a_path) override
			{
				std::error_code error;
				return std::filesystem::is_directory(std::filesystem::symlink_status(a_path, error)) && !HasLinkAncestor(a_path);
			}

			bool CreateDirectories(const std::filesystem::path& a_path) override
			{
				std::error_code error;
				std::filesystem::create_directories(a_path, error);
				return IsPlainDirectory(a_path);
			}

			bool CreateNewDirectory(const std::filesystem::path& a_path) override
			{
				std::error_code error;
				return !HasLinkAncestor(a_path.parent_path()) && std::filesystem::create_directory(a_path, error) && !error;
			}

			bool MoveNoReplace(const std::filesystem::path& a_from, const std::filesystem::path& a_to) override
			{
				std::error_code error;
				if (HasLinkAncestor(a_from) || HasLinkAncestor(a_to.parent_path()) || Exists(a_to)) {
					return false;
				}
				std::filesystem::rename(a_from, a_to, error);
				return !error;
			}

			bool WriteDurably(const std::filesystem::path& a_path, std::string_view a_bytes) override
			{
				if (HasLinkAncestor(a_path.parent_path())) {
					return false;
				}
				auto temporary = a_path;
				temporary += ".co-tmp";
				{
					std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
					stream.write(a_bytes.data(), static_cast<std::streamsize>(a_bytes.size()));
					stream.flush();
					if (!stream) {
						return false;
					}
				}
				std::error_code error;
				std::filesystem::rename(temporary, a_path, error);
				return !error;
			}

			std::optional<std::string> Read(const std::filesystem::path& a_path, std::size_t a_maximumBytes) override
			{
				if (!IsPlainFile(a_path) || FileSizeOrMax(a_path) > a_maximumBytes) {
					return std::nullopt;
				}
				std::ifstream stream(a_path, std::ios::binary);
				if (!stream) {
					return std::nullopt;
				}
				return std::string{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
			}

			bool Remove(const std::filesystem::path& a_path) override
			{
				std::error_code error;
				std::filesystem::remove(a_path, error);
				return !error;
			}

			bool Matches(const std::filesystem::path& a_path, std::string_view a_sha256Hex, std::uint64_t a_sizeBytes) override
			{
				return IsPlainFile(a_path) && ModpackDownload::StagedFileMatches(a_path, a_sha256Hex, a_sizeBytes);
			}

			std::optional<std::string> ActualName(const std::filesystem::path& a_path) override
			{
				return Exists(a_path) ? std::optional<std::string>{ a_path.filename().string() } : std::nullopt;
			}

			bool SameVolume(const std::filesystem::path&, const std::filesystem::path&) override
			{
				return true;
			}

			std::optional<std::uint64_t> FreeBytes(const std::filesystem::path& a_path) override
			{
				std::error_code error;
				const auto space = std::filesystem::space(a_path, error);
				return error ? std::nullopt : std::optional<std::uint64_t>{ space.available };
			}

			std::vector<std::string> List(const std::filesystem::path& a_directory) override
			{
				std::vector<std::string> names;
				std::error_code error;
				for (std::filesystem::directory_iterator it(a_directory, error), end; !error && it != end; it.increment(error)) {
					if (it->is_regular_file(error)) {
						names.push_back(it->path().filename().string());
					}
				}
				return names;
			}
		};
#endif
	}

	std::unique_ptr<FileSystem> MakeNativeFileSystem()
	{
		return std::make_unique<NativeFileSystem>();
	}
}

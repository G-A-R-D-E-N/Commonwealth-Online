#include "AdminTokenStore.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cerrno>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <aclapi.h>
#  include <sddl.h>
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
		std::string Trim(std::string_view a_value)
		{
			std::size_t begin = 0;
			std::size_t end = a_value.size();
			while (begin < end && std::isspace(static_cast<unsigned char>(a_value[begin])) != 0) {
				++begin;
			}
			while (end > begin && std::isspace(static_cast<unsigned char>(a_value[end - 1])) != 0) {
				--end;
			}
			return std::string{ a_value.substr(begin, end - begin) };
		}

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

		std::vector<std::byte> CurrentProcessUserSid(const std::filesystem::path& a_path)
		{
			HANDLE token = nullptr;
			if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
				throw WindowsFailure("OpenProcessToken", a_path);
			}
			try {
				DWORD size = 0;
				if (GetTokenInformation(token, TokenUser, nullptr, 0, &size) || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
					throw WindowsFailure("GetTokenInformation", a_path);
				}
				std::vector<std::byte> buffer(size);
				if (!GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
					throw WindowsFailure("GetTokenInformation", a_path);
				}
				const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
				if (!IsValidSid(user->User.Sid)) {
					throw std::runtime_error("Current process token user SID is invalid.");
				}
				const auto sidSize = GetLengthSid(user->User.Sid);
				std::vector<std::byte> sid(sidSize);
				if (!CopySid(sidSize, reinterpret_cast<PSID>(sid.data()), user->User.Sid)) {
					throw WindowsFailure("CopySid", a_path);
				}
				CloseHandle(token);
				return sid;
			} catch (...) {
				CloseHandle(token);
				throw;
			}
		}

		std::wstring CurrentProcessUserSidString(const std::filesystem::path& a_path)
		{
			const auto sid = CurrentProcessUserSid(a_path);
			LPWSTR sidStringRaw = nullptr;
			if (!ConvertSidToStringSidW(reinterpret_cast<PSID>(const_cast<std::byte*>(sid.data())), &sidStringRaw)) {
				throw WindowsFailure("ConvertSidToStringSidW", a_path);
			}
			std::unique_ptr<void, decltype(&LocalFree)> sidString{ sidStringRaw, &LocalFree };
			return static_cast<LPCWSTR>(sidString.get());
		}

		bool PathVolumeEnforcesAcls(const std::filesystem::path& a_path)
		{
			std::wstring root(MAX_PATH + 1, L'\0');
			DWORD flags = 0;
			if (!GetVolumePathNameW(a_path.c_str(), root.data(), static_cast<DWORD>(root.size())) ||
				!GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr, &flags, nullptr, 0)) {
				return true;
			}
			return AdminTokenStore::VolumeEnforcesAcls(flags);
		}

		void WarnUnprotectedSecretFile(const std::filesystem::path& a_path)
		{
			static std::mutex gate;
			static std::set<std::string> warned;
			const std::scoped_lock lock(gate);
			if (warned.insert(a_path.string()).second) {
				std::cerr << "WARNING: secret file cannot be access-protected on this drive type; any local user can read it: '" << a_path.string() << "'.\n";
			}
		}

		void WriteSync(const std::filesystem::path& a_path, std::string_view a_contents)
		{
			std::unique_ptr<void, decltype(&LocalFree)> descriptor{ nullptr, &LocalFree };
			SECURITY_ATTRIBUTES securityAttributes{
				static_cast<DWORD>(sizeof(SECURITY_ATTRIBUTES)),
				nullptr,
				FALSE
			};
			if (PathVolumeEnforcesAcls(a_path)) {
				const auto securityDescriptor = L"O:" + CurrentProcessUserSidString(a_path) + L"D:P(A;;FA;;;SY)(A;;FA;;;OW)";
				PSECURITY_DESCRIPTOR descriptorRaw = nullptr;
				if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
					securityDescriptor.c_str(),
					SDDL_REVISION_1,
					&descriptorRaw,
					nullptr)) {
					throw WindowsFailure("ConvertStringSecurityDescriptorToSecurityDescriptorW", a_path);
				}
				descriptor.reset(descriptorRaw);
				securityAttributes.lpSecurityDescriptor = descriptor.get();
			}
			const HANDLE handle = CreateFileW(
				a_path.c_str(),
				GENERIC_WRITE,
				0,
				&securityAttributes,
				CREATE_NEW,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
				nullptr);
			if (handle == INVALID_HANDLE_VALUE) {
				throw WindowsFailure("CreateFileW", a_path);
			}
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
				if (!FlushFileBuffers(handle)) {
					throw WindowsFailure("FlushFileBuffers", a_path);
				}
			} catch (...) {
				CloseHandle(handle);
				throw;
			}
			CloseHandle(handle);
		}

		void ReplaceFile(const std::filesystem::path& a_source, const std::filesystem::path& a_destination)
		{
			if (!MoveFileExW(
					a_source.c_str(),
					a_destination.c_str(),
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
				throw WindowsFailure("MoveFileExW", a_destination);
			}
		}

		bool IsCurrentProcessUser(PSID a_owner, const std::filesystem::path& a_path)
		{
			if (a_owner == nullptr || !IsValidSid(a_owner)) {
				return false;
			}
			const auto userSid = CurrentProcessUserSid(a_path);
			return EqualSid(a_owner, reinterpret_cast<PSID>(const_cast<std::byte*>(userSid.data()))) != FALSE;
		}
#else
		std::runtime_error PosixFailure(std::string_view a_operation, const std::filesystem::path& a_path)
		{
			return std::runtime_error(
				std::string{ a_operation } + " failed for '" + a_path.string() + "' (errno " + std::to_string(errno) + ")");
		}

		void WriteSync(const std::filesystem::path& a_path, std::string_view a_contents)
		{
			const int descriptor = open(a_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
			if (descriptor < 0) {
				throw PosixFailure("open", a_path);
			}
			try {
				std::size_t written = 0;
				while (written < a_contents.size()) {
					const auto count = write(descriptor, a_contents.data() + written, a_contents.size() - written);
					if (count < 0) {
						if (errno == EINTR) {
							continue;
						}
						throw PosixFailure("write", a_path);
					}
					if (count == 0) {
						throw std::runtime_error("write made no progress for admin token file");
					}
					written += static_cast<std::size_t>(count);
				}
				if (fsync(descriptor) != 0) {
					throw PosixFailure("fsync", a_path);
				}
			} catch (...) {
				close(descriptor);
				throw;
			}
			if (close(descriptor) != 0) {
				throw PosixFailure("close", a_path);
			}
		}

		void SyncDirectory(const std::filesystem::path& a_directory)
		{
			const int descriptor = open(a_directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
			if (descriptor < 0) {
				throw PosixFailure("open directory", a_directory);
			}
			const int result = fsync(descriptor);
			const int savedErrno = errno;
			close(descriptor);
			if (result != 0) {
				errno = savedErrno;
				throw PosixFailure("fsync directory", a_directory);
			}
		}

		void ReplaceFile(const std::filesystem::path& a_source, const std::filesystem::path& a_destination)
		{
			if (rename(a_source.c_str(), a_destination.c_str()) != 0) {
				throw PosixFailure("rename", a_destination);
			}
			if (chmod(a_destination.c_str(), 0600) != 0) {
				throw PosixFailure("chmod", a_destination);
			}
			SyncDirectory(a_destination.parent_path());
		}
#endif

		class TokenFileNotFound final : public std::runtime_error
		{
		public:
			explicit TokenFileNotFound(const std::filesystem::path& a_path) :
				std::runtime_error("Admin token file does not exist: '" + a_path.string() + "'.")
			{}
		};

		class TokenFile final
		{
		public:
			static std::unique_ptr<TokenFile> Open(const std::filesystem::path& a_path)
			{
				auto file = std::unique_ptr<TokenFile>(new TokenFile(a_path, OpenHandle(a_path)));
				file->Verify();
				return file;
			}

			~TokenFile()
			{
#ifdef _WIN32
				CloseHandle(_handle);
#else
				close(_descriptor);
#endif
			}

			std::string Read()
			{
				std::string contents;
				std::array<char, 4096> buffer{};
				for (;;) {
#ifdef _WIN32
					DWORD count = 0;
					if (!ReadFile(_handle, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr)) {
						throw WindowsFailure("ReadFile", _path);
					}
#else
					const auto count = read(_descriptor, buffer.data(), buffer.size());
					if (count < 0) {
						if (errno == EINTR) {
							continue;
						}
						throw PosixFailure("read", _path);
					}
#endif
					if (count == 0) {
						break;
					}
					contents.append(buffer.data(), static_cast<std::size_t>(count));
				}
				return Trim(contents);
			}

		private:
#ifdef _WIN32
			using NativeHandle = HANDLE;
#else
			using NativeHandle = int;
#endif

			TokenFile(std::filesystem::path a_path, NativeHandle a_handle) :
				_path(std::move(a_path))
#ifdef _WIN32
				, _handle(a_handle)
#else
				, _descriptor(a_handle)
#endif
			{}

			static NativeHandle OpenHandle(const std::filesystem::path& a_path)
			{
#ifdef _WIN32
				const auto handle = CreateFileW(
					a_path.c_str(), GENERIC_READ | READ_CONTROL,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
					FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
				if (handle == INVALID_HANDLE_VALUE) {
					const auto error = GetLastError();
					if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
						throw TokenFileNotFound(a_path);
					}
					SetLastError(error);
					throw WindowsFailure("CreateFileW", a_path);
				}
				return handle;
#else
				int flags = O_RDONLY | O_CLOEXEC | O_NONBLOCK;
#ifdef O_NOFOLLOW
				flags |= O_NOFOLLOW;
#endif
				const int descriptor = open(a_path.c_str(), flags);
				if (descriptor < 0) {
					if (errno == ENOENT) {
						throw TokenFileNotFound(a_path);
					}
					throw PosixFailure("open", a_path);
				}
				return descriptor;
#endif
			}

			void Verify()
			{
#ifdef _WIN32
				DWORD volumeFlags = 0;
				if (!GetVolumeInformationByHandleW(_handle, nullptr, 0, nullptr, nullptr, &volumeFlags, nullptr, 0)) {
					throw WindowsFailure("GetVolumeInformationByHandleW", _path);
				}
				const auto enforcesAcls = AdminTokenStore::VolumeEnforcesAcls(volumeFlags);
				FILE_ATTRIBUTE_TAG_INFO tagInfo{};
				BY_HANDLE_FILE_INFORMATION fileInfo{};
				DWORD attributes = 0;
				if (GetFileInformationByHandleEx(_handle, FileAttributeTagInfo, &tagInfo, sizeof(tagInfo))) {
					attributes = tagInfo.FileAttributes;
				} else if (GetFileInformationByHandle(_handle, &fileInfo)) {
					attributes = fileInfo.dwFileAttributes;
				} else {
					throw WindowsFailure("GetFileInformationByHandle", _path);
				}
				if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
					throw std::runtime_error("Admin token path is a directory: '" + _path.string() + "'.");
				}
				if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
					throw std::runtime_error("Admin token file is a reparse point: '" + _path.string() + "'.");
				}
				if (!enforcesAcls) {
					WarnUnprotectedSecretFile(_path);
					return;
				}
				PSECURITY_DESCRIPTOR descriptorRaw = nullptr;
				PSID owner = nullptr;
				PACL dacl = nullptr;
				const auto securityResult = GetSecurityInfo(
					_handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
					&owner, nullptr, &dacl, nullptr, &descriptorRaw);
				if (securityResult != ERROR_SUCCESS) {
					SetLastError(securityResult);
					throw WindowsFailure("GetSecurityInfo", _path);
				}
				std::unique_ptr<void, decltype(&LocalFree)> descriptor{ descriptorRaw, &LocalFree };
				if (!IsWellKnownSid(owner, WinLocalSystemSid) && !IsCurrentProcessUser(owner, _path)) {
					throw std::runtime_error("Admin token file has an unexpected owner: '" + _path.string() + "'.");
				}
				BOOL present = FALSE;
				BOOL defaulted = FALSE;
				if (!GetSecurityDescriptorDacl(descriptor.get(), &present, &dacl, &defaulted) ||
					present == FALSE || dacl == nullptr) {
					throw std::runtime_error("Admin token file has no explicit DACL: '" + _path.string() + "'.");
				}
				SECURITY_DESCRIPTOR_CONTROL control{};
				DWORD revision = 0;
				if (!GetSecurityDescriptorControl(descriptor.get(), &control, &revision) ||
					(control & SE_DACL_PROTECTED) == 0) {
					throw std::runtime_error("Admin token file DACL is not protected: '" + _path.string() + "'.");
				}
				PSID ownerRightsRaw = nullptr;
				if (!ConvertStringSidToSidW(L"S-1-3-4", &ownerRightsRaw)) {
					throw WindowsFailure("ConvertStringSidToSidW", _path);
				}
				std::unique_ptr<void, decltype(&LocalFree)> ownerRights{ ownerRightsRaw, &LocalFree };
				bool system = false;
				bool ownerAce = false;
				for (DWORD index = 0; index < dacl->AceCount; ++index) {
					void* rawAce = nullptr;
					if (!GetAce(dacl, index, &rawAce)) {
						throw WindowsFailure("GetAce", _path);
					}
					const auto* header = static_cast<const ACE_HEADER*>(rawAce);
					if (header->AceType != ACCESS_ALLOWED_ACE_TYPE || header->AceFlags != 0) {
						throw std::runtime_error("Admin token file DACL contains an unsupported ACE: '" + _path.string() + "'.");
					}
					const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(rawAce);
					const auto sid = reinterpret_cast<PSID>(const_cast<DWORD*>(&ace->SidStart));
					if (ace->Mask != FILE_ALL_ACCESS) {
						throw std::runtime_error("Admin token file DACL grants unexpected access: '" + _path.string() + "'.");
					}
					if (IsWellKnownSid(sid, WinLocalSystemSid)) {
						system = true;
					} else if (EqualSid(sid, ownerRights.get())) {
						ownerAce = true;
					} else {
						throw std::runtime_error("Admin token file DACL grants an unauthorized principal: '" + _path.string() + "'.");
					}
				}
				if (!system || !ownerAce || dacl->AceCount != 2) {
					throw std::runtime_error("Admin token file DACL is not restricted: '" + _path.string() + "'.");
				}
#else
				struct stat info{};
				if (fstat(_descriptor, std::addressof(info)) != 0) {
					throw PosixFailure("fstat", _path);
				}
				if (!S_ISREG(info.st_mode)) {
					throw std::runtime_error("Admin token file is not a regular file: '" + _path.string() + "'.");
				}
				const auto owner = geteuid();
				if (info.st_uid != owner && info.st_uid != 0) {
					throw std::runtime_error("Admin token file is owned by another user: '" + _path.string() + "'.");
				}
				if ((info.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
					if (fchmod(_descriptor, 0600) != 0) throw PosixFailure("fchmod", _path);
				}
#endif
			}

			std::filesystem::path _path;
#ifdef _WIN32
			HANDLE _handle;
#else
			int _descriptor;
#endif
		};

		std::string ReadVerifiedTokenFile(const std::filesystem::path& a_path)
		{
			return TokenFile::Open(a_path)->Read();
		}

		std::optional<std::string> TryReadVerifiedTokenFile(const std::filesystem::path& a_path)
		{
			try {
				return ReadVerifiedTokenFile(a_path);
			} catch (const TokenFileNotFound&) {
				return std::nullopt;
			}
		}
	}

#ifdef _WIN32
	bool AdminTokenStore::VolumeEnforcesAcls(std::uint32_t a_fileSystemFlags) noexcept
	{
		return (a_fileSystemFlags & FILE_PERSISTENT_ACLS) != 0;
	}
#endif

	std::string AdminTokenStore::LoadOrCreate(const std::filesystem::path& a_path, Generator a_generator)
	{
		if (!a_generator) {
			throw std::invalid_argument("Admin token store requires a token generator.");
		}
		const auto fullPath = std::filesystem::absolute(a_path);
		std::filesystem::create_directories(fullPath.parent_path());
		if (const auto existing = TryReadVerifiedTokenFile(fullPath); existing && !existing->empty()) {
			return *existing;
		}

		const auto token = Trim(a_generator());
		if (token.empty()) {
			throw std::runtime_error("Admin token generator returned an empty token.");
		}
		const auto tempPath = fullPath.parent_path() /
			("." + fullPath.filename().string() + "." + UniqueSuffix() + ".tmp");
		try {
			WriteSync(tempPath, token + "\n");
			ReplaceFile(tempPath, fullPath);
			return ReadVerifiedTokenFile(fullPath);
		} catch (...) {
			std::error_code ignored;
			std::filesystem::remove(tempPath, ignored);
			if (const auto existing = TryReadVerifiedTokenFile(fullPath); existing && !existing->empty()) {
				return *existing;
			}
			throw;
		}
	}

	std::string AdminTokenStore::LoadOrCreateHexKey(const std::filesystem::path& a_path, Generator a_generator)
	{
		const auto fullPath = std::filesystem::absolute(a_path);
		const auto existing = TryReadVerifiedTokenFile(fullPath);
		const auto key = existing ? *existing : LoadOrCreate(fullPath, std::move(a_generator));
		std::string fault;
		if (key.empty()) {
			fault = "is empty";
		} else if (key.size() != 64) {
			fault = "has " + std::to_string(key.size()) + " characters, expected 64";
		} else if (key.find_first_not_of("0123456789abcdef") != std::string::npos) {
			fault = "contains characters other than lowercase hex";
		}
		if (!fault.empty()) {
			throw std::runtime_error(
				"Key file '" + fullPath.string() + "' " + fault +
				". Restore it from a backup, or delete it deliberately and restart to generate a new key.");
		}
		return key;
	}

	std::string AdminTokenStore::Load(const std::filesystem::path& a_path)
	{
		const auto fullPath = std::filesystem::absolute(a_path);
		const auto token = ReadVerifiedTokenFile(fullPath);
		if (token.empty()) {
			throw std::runtime_error("Admin token file is empty: " + fullPath.string());
		}
		return token;
	}

	std::optional<std::string> AdminTokenStore::TryLoad(const std::filesystem::path& a_path)
	{
		auto value = TryReadVerifiedTokenFile(std::filesystem::absolute(a_path));
		if (value && value->empty()) {
			return std::nullopt;
		}
		return value;
	}

	void AdminTokenStore::Replace(const std::filesystem::path& a_path, const std::string& a_value)
	{
		const auto value = Trim(a_value);
		if (value.empty()) {
			throw std::invalid_argument("Refusing to store an empty value.");
		}
		const auto fullPath = std::filesystem::absolute(a_path);
		std::filesystem::create_directories(fullPath.parent_path());
		const auto tempPath = fullPath.parent_path() /
			("." + fullPath.filename().string() + "." + UniqueSuffix() + ".tmp");
		try {
			WriteSync(tempPath, value + "\n");
			ReplaceFile(tempPath, fullPath);
		} catch (...) {
			std::error_code ignored;
			std::filesystem::remove(tempPath, ignored);
			throw;
		}
		(void)ReadVerifiedTokenFile(fullPath);
	}
}

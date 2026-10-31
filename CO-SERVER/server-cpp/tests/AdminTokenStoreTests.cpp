#include "AdminTokenStore.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#  include <csignal>
#  include <sys/stat.h>
#  include <sys/wait.h>
#  include <unistd.h>
#else
#  define WIN32_LEAN_AND_MEAN
#  include <aclapi.h>
#  include <sddl.h>
#  include <Windows.h>
#endif

#ifndef _WIN32
void AssertFifoDoesNotBlockLoad(const std::filesystem::path& a_path)
{
	assert(mkfifo(a_path.c_str(), 0600) == 0);
	const auto child = fork();
	assert(child >= 0);
	if (child == 0) {
		try {
			(void)CommonwealthOnline::Server::AdminTokenStore::Load(a_path);
			std::_Exit(2);
		} catch (...) {
			std::_Exit(0);
		}
	}

	int status = 0;
	bool completed = false;
	for (int attempt = 0; attempt < 100; ++attempt) {
		const auto waited = waitpid(child, &status, WNOHANG);
		assert(waited >= 0);
		if (waited == child) {
			completed = true;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	if (!completed) {
		assert(kill(child, SIGKILL) == 0);
		assert(waitpid(child, &status, 0) == child);
	}
	std::filesystem::remove(a_path);
	assert(completed);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
#endif

namespace
{
	std::filesystem::path ScratchDirectory()
	{
		const auto suffix = std::to_string(
			static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
		return std::filesystem::temp_directory_path() / ("co-admin-token-tests-" + suffix);
	}

#ifdef _WIN32
	void AssertOwnerMatchesContract(const std::filesystem::path& a_path)
	{
		PSECURITY_DESCRIPTOR descriptor = nullptr;
		PSID owner = nullptr;
		assert(GetNamedSecurityInfoW(
			const_cast<wchar_t*>(a_path.c_str()),
			SE_FILE_OBJECT,
			OWNER_SECURITY_INFORMATION,
			&owner,
			nullptr,
			nullptr,
			nullptr,
			&descriptor) == ERROR_SUCCESS);
		HANDLE token = nullptr;
		assert(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
		DWORD size = 0;
		assert(!GetTokenInformation(token, TokenUser, nullptr, 0, &size));
		assert(GetLastError() == ERROR_INSUFFICIENT_BUFFER);
		std::vector<std::byte> buffer(size);
		assert(GetTokenInformation(token, TokenUser, buffer.data(), size, &size));
		const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
		assert(IsWellKnownSid(owner, WinLocalSystemSid) || EqualSid(owner, user->User.Sid));
		CloseHandle(token);
		LocalFree(descriptor);
	}

	void AssertRestrictedAcl(const std::filesystem::path& a_path)
	{
		PSECURITY_DESCRIPTOR descriptor = nullptr;
		PACL dacl = nullptr;
		assert(GetNamedSecurityInfoW(
			const_cast<wchar_t*>(a_path.c_str()),
			SE_FILE_OBJECT,
			DACL_SECURITY_INFORMATION,
			nullptr,
			nullptr,
			&dacl,
			nullptr,
			&descriptor) == ERROR_SUCCESS);
		BOOL present = FALSE;
		BOOL defaulted = FALSE;
		assert(GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted));
		assert(present == TRUE && dacl != nullptr);
		SECURITY_DESCRIPTOR_CONTROL control{};
		DWORD revision = 0;
		assert(GetSecurityDescriptorControl(descriptor, &control, &revision));
		assert((control & SE_DACL_PROTECTED) != 0);
		PSID ownerRights = nullptr;
		assert(ConvertStringSidToSidW(L"S-1-3-4", &ownerRights));
		bool system = false;
		bool owner = false;
		for (DWORD index = 0; index < dacl->AceCount; ++index) {
			void* rawAce = nullptr;
			assert(GetAce(dacl, index, &rawAce));
			const auto* header = static_cast<const ACE_HEADER*>(rawAce);
			assert(header->AceType == ACCESS_ALLOWED_ACE_TYPE);
			const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(rawAce);
			const auto sid = reinterpret_cast<PSID>(const_cast<DWORD*>(&ace->SidStart));
			assert(ace->Mask == FILE_ALL_ACCESS);
			if (IsWellKnownSid(sid, WinLocalSystemSid)) {
				system = true;
			} else if (EqualSid(sid, ownerRights)) {
				owner = true;
			} else {
				assert(false);
			}
		}
		assert(system && owner && dacl->AceCount == 2);
		LocalFree(ownerRights);
		LocalFree(descriptor);
	}

	void SetDacl(const std::filesystem::path& a_path, LPCWSTR a_sddl)
	{
		PSECURITY_DESCRIPTOR descriptor = nullptr;
		assert(ConvertStringSecurityDescriptorToSecurityDescriptorW(
			a_sddl,
			SDDL_REVISION_1,
			&descriptor,
			nullptr));
		PACL dacl = nullptr;
		BOOL present = FALSE;
		BOOL defaulted = FALSE;
		assert(GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted));
		assert(SetNamedSecurityInfoW(
			const_cast<wchar_t*>(a_path.c_str()),
			SE_FILE_OBJECT,
			DACL_SECURITY_INFORMATION,
			nullptr,
			nullptr,
			dacl,
			nullptr) == ERROR_SUCCESS);
		LocalFree(descriptor);
	}
#endif
}

int main()
{
	using CommonwealthOnline::Server::AdminTokenStore;

	const auto scratch = ScratchDirectory();
	std::filesystem::create_directories(scratch);
	const auto tokenPath = scratch / ".admin-token";

	int generated = 0;
	const auto first = AdminTokenStore::LoadOrCreate(tokenPath, [&] {
		++generated;
		return std::string{ "first-token" };
	});
	assert(first == "first-token");
	assert(generated == 1);
	assert(AdminTokenStore::Load(tokenPath) == "first-token");

#ifdef _WIN32
	assert(!AdminTokenStore::VolumeEnforcesAcls(0));
	assert(AdminTokenStore::VolumeEnforcesAcls(FILE_PERSISTENT_ACLS));
	AssertOwnerMatchesContract(tokenPath);
	AssertRestrictedAcl(tokenPath);
	const auto insecurePath = scratch / "insecure-token";
	std::filesystem::copy_file(tokenPath, insecurePath);
	SetDacl(insecurePath, L"D:P(A;;FR;;;WD)");
	bool refusedInsecureAcl = false;
	try {
		(void)AdminTokenStore::Load(insecurePath);
	} catch (...) {
		refusedInsecureAcl = true;
	}
	assert(refusedInsecureAcl);
	std::filesystem::remove(insecurePath);
#endif

	const auto reused = AdminTokenStore::LoadOrCreate(tokenPath, [&] {
		++generated;
		return std::string{ "should-not-be-used" };
	});
	assert(reused == "first-token");
	assert(generated == 1);

#ifndef _WIN32
	struct stat metadata{};
	assert(stat(tokenPath.c_str(), &metadata) == 0);
	assert((metadata.st_mode & 0777) == 0600);
#endif

	{
		std::ofstream empty(tokenPath, std::ios::binary | std::ios::trunc);
		assert(empty.good());
	}
	const auto regenerated = AdminTokenStore::LoadOrCreate(tokenPath, [&] {
		++generated;
		return std::string{ "replacement-token" };
	});
	assert(regenerated == "replacement-token");
	assert(generated == 2);
	assert(AdminTokenStore::Load(tokenPath) == "replacement-token");

	bool threw = false;
	try {
		(void)AdminTokenStore::LoadOrCreate(scratch / "empty-generated", [] { return std::string{}; });
	} catch (...) {
		threw = true;
	}
	assert(threw);

#ifndef _WIN32
	AssertFifoDoesNotBlockLoad(scratch / "fifo-token");

	const auto target = scratch / "symlink-target";
	{
		std::ofstream output(target, std::ios::binary | std::ios::trunc);
		output << "linked-token\n";
	}
	const auto link = scratch / "symlinked-token";
	std::filesystem::create_symlink(target, link);
	bool refusedSymlink = false;
	try {
		(void)AdminTokenStore::Load(link);
	} catch (const std::exception&) {
		refusedSymlink = true;
	}
	assert(refusedSymlink);

	const auto loose = scratch / "loose-token";
	{
		std::ofstream output(loose, std::ios::binary | std::ios::trunc);
		output << "loose-token\n";
	}
	std::filesystem::permissions(loose, std::filesystem::perms::owner_all | std::filesystem::perms::others_read);
	assert(AdminTokenStore::Load(loose) == "loose-token");
	const auto tightened = std::filesystem::status(loose).permissions();
	assert((tightened & std::filesystem::perms::others_read) == std::filesystem::perms::none);
	assert((tightened & std::filesystem::perms::group_read) == std::filesystem::perms::none);
#endif

	const std::string hexKey = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
	const auto writeFile = [](const std::filesystem::path& a_path, const std::string& a_content) {
		std::ofstream output(a_path, std::ios::binary | std::ios::trunc);
		output << a_content;
	};
	const auto keyPath = scratch / "save-attestation-key";
	int keyGenerated = 0;
	const auto generateKey = [&] {
		++keyGenerated;
		return hexKey;
	};

	const auto createdKey = AdminTokenStore::LoadOrCreateHexKey(keyPath, generateKey);
	assert(createdKey == hexKey);
	assert(keyGenerated == 1);
	assert(AdminTokenStore::Load(keyPath) == hexKey);

	writeFile(keyPath, hexKey + "\n");
	const auto loadedKey = AdminTokenStore::LoadOrCreateHexKey(keyPath, generateKey);
	assert(loadedKey == hexKey);
	assert(keyGenerated == 1);

	const auto rejectsKey = [&](const std::string& a_content) {
		writeFile(keyPath, a_content);
		bool threw = false;
		try {
			(void)AdminTokenStore::LoadOrCreateHexKey(keyPath, generateKey);
		} catch (const std::runtime_error& error) {
			threw = std::string{ error.what() }.find("Restore it from a backup") != std::string::npos;
		}
		assert(threw);
		assert(std::filesystem::file_size(keyPath) == a_content.size());
		assert(keyGenerated == 1);
	};
	rejectsKey("");
	rejectsKey(std::string(63, 'a') + "\n");
	rejectsKey(std::string(65, 'a'));
	rejectsKey(std::string(63, 'a') + "g");
	rejectsKey(std::string(64, 'A'));

	std::filesystem::remove_all(scratch);
	return 0;
}

#include "F4TServerProfileStoreInternal.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace F4T::ServerProfileStore::Internal
{
	namespace
	{
		int g_lockTimeoutMs = 2000;
		int g_transactionDepth = 0;

		int TryOpenExclusive(const std::filesystem::path& a_lockPath)
		{
#ifdef _WIN32
			int descriptor = -1;
			if (_wsopen_s(&descriptor, a_lockPath.c_str(), _O_RDWR | _O_CREAT | _O_BINARY, _SH_DENYRW, _S_IREAD | _S_IWRITE) != 0) {
				return -1;
			}
			return descriptor;
#else
			const int descriptor = open(a_lockPath.c_str(), O_RDWR | O_CREAT, 0600);
			if (descriptor < 0) {
				return -1;
			}
			if (flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
				close(descriptor);
				return -1;
			}
			return descriptor;
#endif
		}

		void ReloadFromDisk(const std::filesystem::path& a_path)
		{
			nlohmann::json fresh;
			if (TryLoadProfilesFile(a_path, fresh)) {
				g_profiles = std::move(fresh);
				return;
			}
			std::error_code ec;
			if (!std::filesystem::exists(a_path, ec) && !ec && TryLoadProfilesFile(GetBackupPath(a_path), fresh)) {
				g_profiles = std::move(fresh);
			}
		}
	}

	ProfileFileLock::ProfileFileLock(const std::filesystem::path& a_profilesPath)
	{
		auto lockPath = a_profilesPath;
		lockPath += ".lock";
		std::error_code ec;
		std::filesystem::create_directories(lockPath.parent_path(), ec);
		if (ec || !std::filesystem::is_directory(lockPath.parent_path(), ec)) {
			LogStoreWarning(std::format("Could not create the server profile directory for {}", lockPath.string()));
			return;
		}

		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(g_lockTimeoutMs);
		while ((m_handle = TryOpenExclusive(lockPath)) < 0 && std::chrono::steady_clock::now() < deadline) {
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		if (m_handle < 0) {
			LogStoreWarning(std::format("Could not lock the server profile store {} within {} ms; another game process may be using it.", lockPath.string(), g_lockTimeoutMs));
		}
	}

	ProfileFileLock::~ProfileFileLock()
	{
		if (m_handle >= 0) {
#ifdef _WIN32
			_close(m_handle);
#else
			close(m_handle);
#endif
		}
	}

	StoreTransaction::StoreTransaction()
	{
		EnsureLoaded();
		if (g_transactionDepth == 0) {
			const auto path = GetProfilesPath();
			if (!path.empty()) {
				m_lock.emplace(path);
				if (!m_lock->Held()) {
					m_lock.reset();
					return;
				}
				ReloadFromDisk(path);
			}
		}
		++g_transactionDepth;
		m_ok = true;
	}

	StoreTransaction::~StoreTransaction()
	{
		if (m_ok) {
			--g_transactionDepth;
		}
	}

	bool AcquireSaveLock(std::optional<ProfileFileLock>& a_out, const std::filesystem::path& a_profilesPath)
	{
		if (g_transactionDepth > 0) {
			return true;
		}
		a_out.emplace(a_profilesPath);
		if (a_out->Held()) {
			return true;
		}
		a_out.reset();
		return false;
	}

	bool QuarantineFile(const std::filesystem::path& a_path)
	{
		const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		auto target = a_path;
		target += std::format(".corrupt-{}", stamp);
		std::error_code ec;
		if (!std::filesystem::is_regular_file(a_path, ec)) {
			LogStoreWarning(std::format("Refusing to quarantine {}: not a regular file.", a_path.string()));
			return false;
		}
		std::filesystem::rename(a_path, target, ec);
		if (ec) {
			LogStoreWarning(std::format("Failed to quarantine corrupt server profile copy {}: {}", a_path.string(), ec.message()));
			return false;
		}
		LogStoreWarning(std::format("Quarantined corrupt server profile copy: {}", target.string()));
		return true;
	}

#ifdef F4T_SERVER_PROFILE_STORE_TESTING
	void SetLockTimeoutForTesting(int a_milliseconds)
	{
		g_lockTimeoutMs = a_milliseconds;
	}
#endif
}

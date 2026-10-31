#ifndef F4T_CLIENT_IDENTITY_TESTING
#include "pch.h"
#endif

#include "F4TClientIdentity.h"

#include "F4TContentManifest.h"
#include "F4TInventorySync.h"
#include "F4TObjectSync.h"
#include "F4TQuestSync.h"
#include "F4TSaveLineageStore.h"
#include "F4TServerProfileStore.h"
#include "F4TModpackChunks.h"
#include "F4TPlayerAnimEventSync.h"
#include "F4TShotReplay.h"
#include "F4TWorldPolicy.h"

#ifndef F4T_CLIENT_IDENTITY_TESTING
#include <Windows.h>
#endif

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace
{
#ifndef F4T_CLIENT_IDENTITY_TESTING
	constexpr wchar_t kPackagedManifestName[] = L"CommonwealthOnlineClient.json";
#endif
	constexpr char kManifestClientVersionField[] = "clientVersion";
	constexpr char kManifestFingerprintField[] = "contentManifestFingerprint";
	constexpr std::size_t kMaxServerPasswordBytes = 128;

	std::mutex g_heldIdentityMutex;
	std::optional<F4T::ClientIdentity::ConnectionIdentity> g_heldIdentity;
	std::atomic<bool> g_connectedServerAdmin{ false };
	std::mutex g_connectedProfileMutex;
	std::string g_connectedProfileId;

#ifdef F4T_CLIENT_IDENTITY_TESTING
	std::optional<std::filesystem::path> g_testManifestPath;
#endif

#ifndef F4T_CLIENT_IDENTITY_TESTING
	std::filesystem::path GetModuleDirectory()
	{
		HMODULE module = nullptr;
		const auto moduleAddress = reinterpret_cast<LPCWSTR>(reinterpret_cast<std::uintptr_t>(&GetModuleDirectory));
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
		return std::filesystem::path{ std::wstring_view{ path.data(), length } }.parent_path();
	}
#endif

	std::optional<nlohmann::json> ReadPackagedManifest()
	{
#ifdef F4T_CLIENT_IDENTITY_TESTING
		if (g_testManifestPath) {
			std::ifstream input(*g_testManifestPath);
			if (!input.is_open()) {
				return std::nullopt;
			}

			try {
				nlohmann::json manifest;
				input >> manifest;
				if (!manifest.is_object()) {
					return std::nullopt;
				}
				return manifest;
			} catch (const std::exception&) {
				return std::nullopt;
			}
		}
		return std::nullopt;
#else
		const auto directory = GetModuleDirectory();
		if (directory.empty()) {
			return std::nullopt;
		}

		std::ifstream input(directory / kPackagedManifestName);
		if (!input.is_open()) {
			return std::nullopt;
		}

		try {
			nlohmann::json manifest;
			input >> manifest;
			if (!manifest.is_object()) {
				return std::nullopt;
			}
			return manifest;
		} catch (const std::exception&) {
			return std::nullopt;
		}
#endif
	}

	std::optional<F4T::SaveProfileLogic::AttestedSave> ResolveSaveAttestation(const std::string& a_serverKey)
	{
		const auto boundSave = F4T::ServerProfileStore::LookupSaveFile(a_serverKey);
		if (!boundSave) {
			return std::nullopt;
		}
		for (const auto& attested : F4T::SaveLineageStore::LookupAttestedSaves(a_serverKey)) {
			if (F4T::SaveProfileLogic::EqualsIgnoreCase(attested.saveFile, *boundSave)) {
				return attested;
			}
		}
		return std::nullopt;
	}

	std::optional<std::string> ResolveProfileSecret(const std::string& a_serverKey)
	{
		return F4T::SaveLineageStore::LookupServerSecret(a_serverKey);
	}
}

namespace F4T::ClientIdentity
{
#ifdef F4T_CLIENT_IDENTITY_TESTING
	void SetManifestPathForTesting(const std::filesystem::path& a_path)
	{
		g_testManifestPath = a_path;
	}

	void ResetManifestPathForTesting()
	{
		g_testManifestPath.reset();
	}
#endif

	std::optional<ClientCompatibility::Metadata> LoadPackagedMetadata()
	{
		const auto manifest = ReadPackagedManifest();
		if (!manifest) {
			return std::nullopt;
		}

		const auto clientVersion = manifest->find(kManifestClientVersionField);
		if (clientVersion == manifest->end() || !clientVersion->is_string() ||
			clientVersion->get_ref<const std::string&>() != ClientCompatibility::kClientVersion) {
			return std::nullopt;
		}

		const auto fingerprint = manifest->find(kManifestFingerprintField);
		if (fingerprint == manifest->end() || !fingerprint->is_string() ||
			!F4T::ContentManifest::IsValidFingerprint(fingerprint->get_ref<const std::string&>())) {
			return std::nullopt;
		}

		const auto target = ClientCompatibility::ResolveCompileTimeTarget();
		const auto* canonical = target ? ClientCompatibility::Find(*target) : nullptr;
		if (!canonical) {
			return std::nullopt;
		}

		auto metadata = *canonical;
		metadata.contentManifestFingerprint = fingerprint->get<std::string>();
		F4T::InventorySync::AddSessionCapabilities(metadata);
		F4T::ObjectSync::AddSessionCapabilities(metadata);
		metadata.capabilities.emplace_back("session-lifecycle-v1");
		metadata.capabilities.emplace_back("voice-v1");
		metadata.capabilities.emplace_back(std::string{ F4T::WorldPolicy::kCapabilityName });
		F4T::QuestSync::AddSessionCapabilities(metadata);
		metadata.capabilities.emplace_back(std::string{ F4T::ModpackChunks::kCapabilityName });
		if (F4T::PlayerAnimEventSync::IsInstalled()) {
			metadata.capabilities.emplace_back(std::string{ F4T::ShotReplay::kCapabilityName });
		}
		return metadata;
	}

	std::optional<ConnectionIdentity> PrepareConnectionIdentity(
		const std::string& a_host,
		std::uint16_t a_port,
		std::string& a_error,
		const std::optional<std::string>& a_serverPassword,
		const std::string& a_joinId)
	{
		a_error.clear();
		g_connectedServerAdmin.store(false, std::memory_order_release);
		if (a_serverPassword && a_serverPassword->size() > kMaxServerPasswordBytes) {
			a_error = "Server password must be 128 characters or fewer.";
			return std::nullopt;
		}

		const auto metadata = LoadPackagedMetadata();
		if (!metadata) {
			a_error = "Packaged client identity manifest is missing or invalid.";
			return std::nullopt;
		}

		const auto serverKey = ServerProfileStore::ProfileServerKey(a_host, a_port, a_joinId);
		const auto credential = ServerProfileStore::LookupResumeCredential(serverKey);
		const auto profileId = ServerProfileStore::EnsureProfileId(serverKey);
		if (profileId.empty()) {
			a_error = "Failed to persist client profile identity.";
			return std::nullopt;
		}

		return ConnectionIdentity{
			.serverKey = serverKey,
			.profileId = profileId,
			.resumeToken = credential ? std::optional<std::string>{ credential->token } : std::nullopt,
			.metadata = metadata,
			.serverPassword = a_serverPassword,
			.saveAttestation = ResolveSaveAttestation(serverKey),
			.profileSecret = ResolveProfileSecret(serverKey)
		};
	}

	void HoldConnectionIdentity(ConnectionIdentity a_identity)
	{
		const std::scoped_lock lock(g_heldIdentityMutex);
		g_heldIdentity = std::move(a_identity);
	}

	void ClearConnectionIdentity()
	{
		F4T::ServerProfileStore::SetConnectedWorldId({});
		ClearConnectedProfileId();
		const std::scoped_lock lock(g_heldIdentityMutex);
		g_heldIdentity.reset();
	}

	std::optional<ConnectionIdentity> TakeConnectionIdentity()
	{
		const std::scoped_lock lock(g_heldIdentityMutex);
		auto identity = std::move(g_heldIdentity);
		g_heldIdentity.reset();
		return identity;
	}

	void SetConnectedServerAdmin(bool a_isAdmin)
	{
		g_connectedServerAdmin.store(a_isAdmin, std::memory_order_release);
	}

	bool IsConnectedServerAdmin()
	{
		return g_connectedServerAdmin.load(std::memory_order_acquire);
	}

	std::string ConnectedProfileId()
	{
		const std::scoped_lock lock(g_connectedProfileMutex);
		return g_connectedProfileId;
	}

	void ClearConnectedProfileId()
	{
		const std::scoped_lock lock(g_connectedProfileMutex);
		g_connectedProfileId.clear();
	}

	bool CommitReadyIdentity(
		const std::string& a_serverKey,
		const F4T::GnsProtocolAdapter::ReadyIdentity& a_identity,
		std::string& a_error)
	{
		SetConnectedServerAdmin(a_identity.multiplayerSaveAdmin);
		if (!ServerProfileStore::StoreResumeCredential(a_serverKey, a_identity.resumeToken, a_identity.resumeExpiresAt)) {
			a_error = "Failed to persist the rotated resume credential.";
			return false;
		}
		if (a_identity.profileSecret &&
			!SaveLineageStore::StoreServerSecret(a_serverKey, *a_identity.profileSecret)) {
			a_error = "Failed to persist the multiplayer save secret this server issued.";
			return false;
		}
		const auto profileId = a_identity.profileId.empty()
			? ServerProfileStore::LookupProfileId(a_serverKey).value_or(std::string{})
			: a_identity.profileId;
		{
			const std::scoped_lock lock(g_connectedProfileMutex);
			g_connectedProfileId = profileId;
		}
		return true;
	}
}

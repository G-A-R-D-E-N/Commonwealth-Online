#include "pch.h"

#include "F4TSaveLineageSession.h"

#include "F4TContentManifest.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TSaveLineageStore.h"
#include "F4TSaveProfileLogic.h"
#include "F4TServerProfileStore.h"
#include "F4TUIClient.h"

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>

namespace
{
	constexpr auto kPendingRecordTimeout = std::chrono::seconds{ 30 };
	constexpr std::string_view NotMultiplayerReason =
		"Only Commonwealth Online multiplayer saves can join this server. Start a new multiplayer save for this "
		"server or load a multiplayer save that was created here.";

	struct PendingRecord
	{
		std::string serverKey;
		std::string characterId;
		std::string saveName;
		std::uint64_t baselineSaveTime{ 0 };
		std::string baselineSaveFile;
		std::chrono::steady_clock::time_point startedAt{};
	};

	std::optional<PendingRecord> g_pendingRecord;

	std::string GetLogPrefix()
	{
		return F4T::Networking::GetLocalPlayerLogPrefix() + " [SessionLaunch]";
	}

	void LogInfo(std::string_view a_message)
	{
		REX::INFO("{} {}", GetLogPrefix(), a_message);
	}

	void LogWarning(std::string_view a_message)
	{
		REX::WARN("{} {}", GetLogPrefix(), a_message);
	}

	constexpr int kSaveSearchMaxDepth = 3;

	std::filesystem::path g_saveDirectory;
	std::unordered_set<std::string> g_searchedSaveFiles;

	std::filesystem::path EngineSaveDirectory(const std::filesystem::path& a_saveFolder)
	{
		const auto* setting = RE::GetINISetting("sLocalSavePath:General");
		if (!setting || setting->GetType() != RE::Setting::SETTING_TYPE::kString || setting->GetString().empty()) {
			return {};
		}
		return a_saveFolder / std::filesystem::path{ setting->GetString() };
	}

	std::filesystem::path FindSaveDirectory(const std::filesystem::path& a_root, const std::filesystem::path& a_fileName)
	{
		std::error_code ec;
		std::filesystem::recursive_directory_iterator it{ a_root, std::filesystem::directory_options::skip_permission_denied, ec };
		for (; !ec && it != std::filesystem::recursive_directory_iterator{}; it.increment(ec)) {
			if (it->path().filename() == a_fileName) {
				return it->path().parent_path();
			}
			if (it.depth() >= kSaveSearchMaxDepth) {
				it.disable_recursion_pending();
			}
		}
		return {};
	}

	std::optional<std::filesystem::path> SaveFilePath(std::string_view a_saveFile, bool a_searchIfMissing)
	{
		const auto saveFolder = F4T::ServerProfileStore::GetSaveFolderDirectory();
		if (saveFolder.empty() || a_saveFile.empty()) {
			return std::nullopt;
		}

		const std::filesystem::path fileName{ std::string{ a_saveFile } + ".fos" };
		std::error_code ec;
		const auto engineDirectory = EngineSaveDirectory(saveFolder);
		if (!engineDirectory.empty() && std::filesystem::exists(engineDirectory / fileName, ec)) {
			return engineDirectory / fileName;
		}
		if (!g_saveDirectory.empty() && std::filesystem::exists(g_saveDirectory / fileName, ec)) {
			return g_saveDirectory / fileName;
		}

		if (a_searchIfMissing && g_searchedSaveFiles.emplace(a_saveFile).second) {
			if (const auto found = FindSaveDirectory(saveFolder, fileName); !found.empty()) {
				LogInfo(std::format("Found save '{}' outside the engine save folder; using that folder.", a_saveFile));
				g_saveDirectory = found;
				return found / fileName;
			}
		}

		const auto& fallback = engineDirectory.empty() ? g_saveDirectory : engineDirectory;
		if (fallback.empty()) {
			return std::nullopt;
		}
		return fallback / fileName;
	}

	std::optional<std::string> HashSaveFile(std::string_view a_saveFile)
	{
		const auto saveFilePath = SaveFilePath(a_saveFile, true);
		if (!saveFilePath) {
			return std::nullopt;
		}
		return F4T::ContentManifest::Sha256HexFile(*saveFilePath);
	}

	bool IsSaveFileSettled(const std::filesystem::path& a_saveFilePath)
	{
		auto tmpFilePath = a_saveFilePath;
		tmpFilePath += ".tmp";
		std::error_code ec;
		if (std::filesystem::exists(tmpFilePath, ec) || ec) {
			return false;
		}

		const HANDLE handle = CreateFileW(
			a_saveFilePath.c_str(),
			GENERIC_READ,
			0,
			nullptr,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			return false;
		}
		CloseHandle(handle);
		return true;
	}

	void AnnounceSaveAttestation(const std::string& a_serverKey, const F4T::SaveProfileLogic::AttestedSave& a_save)
	{
		if (!F4T::Networking::IsConnectedToServer()) {
			return;
		}

		const auto secret = F4T::SaveLineageStore::LookupServerSecret(a_serverKey);
		if (!secret) {
			LogWarning(std::format(
				"Cannot announce save attestation for '{}' to server {}: this profile has no multiplayer save secret yet.",
				a_save.saveFile,
				a_serverKey));
			return;
		}

		const nlohmann::json packet{
			{ "type", "saveAttested" },
			{ "saveFile", a_save.saveFile },
			{ "sha256", a_save.sha256Hex },
			{ "profileSecret", *secret }
		};
		if (!F4T::Networking::Detail::SendPacketBytes(packet.dump())) {
			LogWarning(std::format("Failed to announce save attestation for '{}' to the server.", a_save.saveFile));
		}
	}

	void RecordActiveSave(const std::string& a_serverKey, const std::string& a_saveFile)
	{
		const auto sha256Hex = HashSaveFile(a_saveFile);
		if (!sha256Hex) {
			LogWarning(std::format(
				"Could not hash save '{}' for server {}; it will not be attested.",
				a_saveFile,
				a_serverKey));
			return;
		}

		const auto attestedAt =
			std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		const F4T::SaveProfileLogic::AttestedSave save{ a_saveFile, *sha256Hex, static_cast<std::uint64_t>(attestedAt) };
		if (!F4T::SaveLineageStore::RecordAttestedSave(a_serverKey, save)) {
			LogWarning(std::format("Failed to persist save attestation for '{}' on server {}.", a_saveFile, a_serverKey));
		}
		AnnounceSaveAttestation(a_serverKey, save);
	}

	void AnnounceRecordedAttestations(
		const std::string& a_serverKey,
		std::span<const F4T::SaveProfileLogic::AttestedSave> a_attested)
	{
		for (const auto& save : a_attested) {
			AnnounceSaveAttestation(a_serverKey, save);
		}
	}

	void BeginPendingRecord(const std::string& a_serverKey, const std::string& a_characterId, const std::string& a_saveName)
	{
		const auto* saveLoadManager = RE::BGSSaveLoadManager::GetSingleton();
		g_pendingRecord = PendingRecord{
			a_serverKey,
			a_characterId,
			a_saveName,
			saveLoadManager ? saveLoadManager->lastSaveTime : 0,
			saveLoadManager && saveLoadManager->mostRecentSaveGame ? saveLoadManager->mostRecentSaveGame : "",
			std::chrono::steady_clock::now()
		};
	}

	void PollPendingRecord()
	{
		if (!g_pendingRecord) {
			return;
		}

		try {
			const auto saveFilePath = SaveFilePath(g_pendingRecord->saveName, false);
			if (!saveFilePath || !IsSaveFileSettled(*saveFilePath)) {
				if (std::chrono::steady_clock::now() - g_pendingRecord->startedAt < kPendingRecordTimeout) {
					return;
				}
				LogWarning(std::format(
					"Dropped save attestation for server {}: save '{}' did not settle within {} s (baseline '{}' at {}).",
					g_pendingRecord->serverKey,
					g_pendingRecord->saveName,
					kPendingRecordTimeout.count(),
					g_pendingRecord->baselineSaveFile,
					g_pendingRecord->baselineSaveTime));
				g_pendingRecord.reset();
				return;
			}

			const auto serverKey = g_pendingRecord->serverKey;
			const auto characterId = g_pendingRecord->characterId;
			const auto saveName = g_pendingRecord->saveName;
			g_pendingRecord.reset();
			if (!F4T::Networking::IsConnectedToServer()) {
				LogWarning(std::format(
					"Dropped save attestation for '{}' on server {}: the connection closed before the save settled.",
					saveName,
					serverKey));
				return;
			}
			if (!F4T::SaveProfileLogic::CanRebindProfileSave(characterId, saveName)) {
				LogWarning(std::format(
					"Dropped save attestation for '{}' on server {}: the save does not belong to character {}.",
					saveName,
					serverKey,
					characterId));
				return;
			}
			RecordActiveSave(serverKey, saveName);
		} catch (...) {
			g_pendingRecord.reset();
			LogWarning("Save attestation failed with an exception; the pending record was dropped.");
		}
	}
}

namespace F4T::SaveLineageSession
{
	void TickPendingRecord()
	{
		PollPendingRecord();
	}

	bool HasPendingRecord()
	{
		return g_pendingRecord.has_value();
	}

	std::string ReadPostSaveGameName(const F4SE::MessagingInterface::Message& a_msg)
	{
		constexpr std::size_t kSaveNameFieldBytes = 0x104;
		if (!a_msg.data) {
			return {};
		}
		const auto* name = static_cast<const char*>(a_msg.data);
		return std::string{ name, strnlen(name, kSaveNameFieldBytes) };
	}

	void TryUpdateActiveProfileBinding(
		const std::string& a_serverKey,
		const std::string& a_characterId,
		bool a_pendingProfileBind,
		const std::optional<std::string>& a_bindableSaveFile,
		const std::string& a_savedName)
	{
		try {
			if (a_serverKey.empty() ||
				a_characterId.empty() ||
				a_pendingProfileBind ||
				!F4T::Networking::IsConnectedToServer()) {
				return;
			}

			if (!a_savedName.empty()) {
				BeginPendingRecord(a_serverKey, a_characterId, a_savedName);
			}

			const auto& saveFileName = a_bindableSaveFile;
			if (!saveFileName || !F4T::SaveProfileLogic::CanRebindProfileSave(a_characterId, *saveFileName)) {
				return;
			}

			const auto currentSaveFile = F4T::ServerProfileStore::LookupSaveFile(a_serverKey);
			if (!currentSaveFile || *currentSaveFile != *saveFileName) {
				if (!F4T::ServerProfileStore::BindSaveFile(a_serverKey, *saveFileName)) {
					LogWarning(std::format(
						"Failed to persist updated save binding for server {}; will retry on the next poll.",
						a_serverKey));
				} else {
					LogInfo(std::format(
						"Updated active server profile save to '{}' for server {}.",
						*saveFileName,
						a_serverKey));
				}
			}
		} catch (...) {
			LogWarning("Save binding update failed with an exception.");
		}
	}

	JoinSaveDecision RefuseJoin(JoinSaveDecision a_decision, std::string_view a_reason)
	{
		a_decision.allowed = false;
		a_decision.code = std::string{ F4T::SaveProfileLogic::SaveNotMultiplayerCode };
		a_decision.reason = std::string{ a_reason };
		return a_decision;
	}

	JoinSaveDecision ResolveJoinSave(
		const std::string& a_serverKey,
		std::span<const std::string> a_saveFiles,
		const std::optional<std::string>& a_latestSave,
		bool a_isAdmin)
	{
		JoinSaveDecision decision;
		decision.saveFile = a_latestSave;
		if (!a_latestSave) {
			return decision;
		}

		try {
			const auto characterId = F4T::SaveProfileLogic::ExtractCharacterId(*a_latestSave);
			if (!characterId) {
				return RefuseJoin(std::move(decision), NotMultiplayerReason);
			}

			auto attested = F4T::SaveLineageStore::LookupAttestedSaves(a_serverKey);
			if (attested.empty()) {
				const auto boundSave = F4T::ServerProfileStore::LookupSaveFile(a_serverKey);
				if (!boundSave) {
					return decision;
				}
				RecordActiveSave(a_serverKey, *boundSave);
				attested = F4T::SaveLineageStore::LookupAttestedSaves(a_serverKey);
				if (attested.empty()) {
					return RefuseJoin(std::move(decision), NotMultiplayerReason);
				}
			}

			const auto resolution =
				F4T::SaveProfileLogic::ResolveLatestAttestedSave(*characterId, a_saveFiles, attested, HashSaveFile);
			const auto admission = F4T::SaveProfileLogic::AdmitJoinSave(resolution, a_isAdmin);
			if (!admission.allowed) {
				LogWarning(std::format(
					"Refusing join for server {}: save '{}' is not an attested multiplayer save.",
					a_serverKey,
					*a_latestSave));
				decision.allowed = false;
				decision.code = admission.code;
				decision.reason = admission.reason;
				return decision;
			}

			AnnounceRecordedAttestations(a_serverKey, attested);
			if (!resolution.saveFile || resolution.verdict == F4T::SaveProfileLogic::LineageVerdict::Attested) {
				return decision;
			}

			LogInfo(std::format(
				"Ignoring newer save '{}' for server {}; loading attested save '{}'.",
				*a_latestSave,
				a_serverKey,
				*resolution.saveFile));
			F4T::UIClient::ShowOfflineSaveIgnoredNotification();
			decision.saveFile = resolution.saveFile;
			return decision;
		} catch (...) {
			LogWarning(std::format(
				"Save lineage check failed for server {}; refusing to load save '{}'.",
				a_serverKey,
				*a_latestSave));
			return RefuseJoin(std::move(decision), NotMultiplayerReason);
		}
	}
}

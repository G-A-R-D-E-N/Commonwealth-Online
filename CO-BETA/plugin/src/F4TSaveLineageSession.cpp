#include "pch.h"

#include "F4TSaveLineageSession.h"
#include "F4TInventoryGrant.h"
#include "F4TDeathBagGrantLedger.h"
#include "F4TSessionSaveGuard.h"

#include "F4TContentManifest.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TSaveLineageStore.h"
#include "F4TSaveProfileLogic.h"
#include "F4TSaveRecoveryOffer.h"
#include "F4TServerProfileStore.h"
#include "F4TUIClient.h"

#include <Windows.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>

namespace
{
	constexpr auto kPendingRecordTimeout = std::chrono::seconds{ 30 };
	constexpr auto kSaveProbeInterval = std::chrono::seconds{ 2 };
	constexpr auto kRecoveryConsumeTimeout = std::chrono::seconds{ 15 };
	constexpr std::string_view NotMultiplayerReason =
		"Only Commonwealth Online multiplayer saves can join this server. Start a new multiplayer save for this "
		"server or load a multiplayer save that was created here.";

	struct PendingRecord
	{
		std::uint64_t connectionGeneration{};
		std::string serverKey;
		std::string characterId;
		std::string saveName;
		std::uint64_t baselineSaveTime{ 0 };
		std::string baselineSaveFile;
		std::chrono::steady_clock::time_point startedAt{};
		std::chrono::steady_clock::time_point nextProbe{};
	};

	std::optional<PendingRecord> g_pendingRecord;
	struct RecoveryRequest { std::string serverKey, worldId, selectedProfile; std::uint64_t connection{}; bool offerStaged{}; bool consumeSent{}; std::chrono::steady_clock::time_point consumeSentAt{}; };
	std::mutex g_recoveryMutex;
	std::optional<RecoveryRequest> g_recoveryRequest;
	struct FirstJoinAttestation { std::string saveFile, sha256Hex; F4T::SaveLineageSession::AttestationOutcome outcome{}; };
	std::mutex g_firstJoinAttestationMutex;
	std::optional<FirstJoinAttestation> g_firstJoinAttestation;

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

	std::mutex g_saveDirectoryMutex;
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
		const std::scoped_lock lock(g_saveDirectoryMutex);
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

	bool AnnounceSaveAttestation(const std::string& a_serverKey, const F4T::SaveProfileLogic::AttestedSave& a_save, std::optional<std::uint64_t> a_generation = std::nullopt)
	{
		if (!F4T::Networking::IsConnectedToServer()) {
			return false;
		}

		const auto secret = F4T::SaveLineageStore::LookupServerSecret(a_serverKey);
		if (!secret) {
			LogWarning(std::format(
				"Cannot announce save attestation for '{}' to server {}: this profile has no multiplayer save secret yet.",
				a_save.saveFile,
				a_serverKey));
			return false;
		}

		const nlohmann::json packet{
			{ "type", "saveAttested" },
			{ "saveFile", a_save.saveFile },
			{ "sha256", a_save.sha256Hex },
			{ "profileSecret", *secret }
		};
		const auto encoded = packet.dump();
		const bool sent = a_generation ? F4T::Networking::Detail::SendPacketBytesForGeneration(encoded, *a_generation) : F4T::Networking::Detail::SendPacketBytes(encoded);
		if (!sent) {
			LogWarning(std::format("Failed to announce save attestation for '{}' to the server.", a_save.saveFile));
			return false;
		}
		return true;
	}

	bool RecordActiveSave(const std::string& a_serverKey, const std::string& a_saveFile, const std::string& a_sha256Hex, bool a_announce = true, std::optional<std::uint64_t> a_generation = std::nullopt)
	{
		const auto attestedAt =
			std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		const F4T::SaveProfileLogic::AttestedSave save{ a_saveFile, a_sha256Hex, static_cast<std::uint64_t>(attestedAt) };
		const bool persisted = F4T::SaveLineageStore::RecordAttestedSave(a_serverKey, save);
		if (!persisted) {
			LogWarning(std::format("Failed to persist save attestation for '{}' on server {}.", a_saveFile, a_serverKey));
			return false;
		}
		return !a_announce || AnnounceSaveAttestation(a_serverKey, save, a_generation);
	}

	bool RecordActiveSave(const std::string& a_serverKey, const std::string& a_saveFile)
	{
		const auto sha256Hex = HashSaveFile(a_saveFile);
		if (!sha256Hex) {
			LogWarning(std::format(
				"Could not hash save '{}' for server {}; it will not be attested.",
				a_saveFile,
				a_serverKey));
			return false;
		}

		return RecordActiveSave(a_serverKey, a_saveFile, *sha256Hex);
	}

	void AnnounceRecordedAttestations(
		const std::string& a_serverKey,
		std::string_view a_characterId,
		std::span<const F4T::SaveProfileLogic::AttestedSave> a_attested)
	{
		for (const auto& save : a_attested) {
			if (!F4T::SaveProfileLogic::CanRebindProfileSave(a_characterId, save.saveFile)) continue;
			AnnounceSaveAttestation(a_serverKey, save);
		}
	}

	void BeginPendingRecord(const std::string& a_serverKey, const std::string& a_characterId, const std::string& a_saveName)
	{
		const auto* saveLoadManager = RE::BGSSaveLoadManager::GetSingleton();
		g_pendingRecord = PendingRecord{
			F4T::Networking::GetConnectionGeneration(),
			a_serverKey,
			a_characterId,
			a_saveName,
			saveLoadManager ? saveLoadManager->lastSaveTime : 0,
			saveLoadManager && saveLoadManager->mostRecentSaveGame ? saveLoadManager->mostRecentSaveGame : "",
			std::chrono::steady_clock::now()
		};
	}

	void RecordSettledSave(const std::string& a_serverKey, const std::string& a_saveFile)
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
	}

	bool SettleDeathBagSave(const std::string& a_saveName)
	{
		const auto saveAttempt = F4T::DeathBagGrantLedger::PendingSaveAttempt(a_saveName);
		const auto barrierAttempt = F4T::DeathBagGrantLedger::PendingBarrierAttempt(a_saveName);
		if (!saveAttempt && !barrierAttempt) return true;
		if (std::chrono::steady_clock::now() < g_pendingRecord->nextProbe) return false;
		const auto observed = F4T::SaveLineageSession::CaptureSaveBaseline(a_saveName);
		g_pendingRecord->nextProbe = std::chrono::steady_clock::now() + kSaveProbeInterval;
		const auto candidateReady = F4T::DeathBagGrantLedger::SaveCandidateReady(a_saveName, observed, saveAttempt);
		const bool unverified = observed.state != F4T::DeathBagReceipts::SaveBaseline::State::Present || (candidateReady && !*candidateReady);
		if (unverified && std::chrono::steady_clock::now() - g_pendingRecord->startedAt < kPendingRecordTimeout) return false;
		if (unverified) {
			g_pendingRecord.reset();
			LogWarning(std::format("Dropped save attestation for '{}': the saved file did not change or could not be verified.", a_saveName));
			return false;
		}
		if (saveAttempt) F4T::DeathBagGrantLedger::ConfirmSaved(a_saveName, observed, *saveAttempt);
		if (barrierAttempt) F4T::DeathBagGrantLedger::ObserveSaveBarrier(a_saveName, observed, *barrierAttempt);
		return true;
	}

	void PollPendingRecord()
	{
		if (!g_pendingRecord) {
			return;
		}

		try {
			if (F4T::SessionSaveGuard::IsSessionExitPending() && !F4T::SessionSaveGuard::IsSessionExitLanded()) {
				if (std::chrono::steady_clock::now() - g_pendingRecord->startedAt >= kPendingRecordTimeout) {
					LogWarning(std::format("Dropped save attestation for '{}': session exit did not settle in time.", g_pendingRecord->saveName));
					g_pendingRecord.reset();
				}
				return;
			}
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
			const auto generation = g_pendingRecord->connectionGeneration;
			if (!F4T::SaveProfileLogic::CanRebindProfileSave(characterId, saveName)) {
				g_pendingRecord.reset();
				LogWarning(std::format(
					"Dropped save attestation for '{}' on server {}: the save does not belong to character {}.",
					saveName,
					serverKey,
					characterId));
				return;
			}
			if (!SettleDeathBagSave(saveName)) return;
			g_pendingRecord.reset();
			if (F4T::Networking::GetConnectionGeneration() != generation) {
				LogInfo(std::format(
					"Save '{}' settled after the connection changed; recorded it for server {} without announcing it.",
					saveName,
					serverKey));
				RecordSettledSave(serverKey, saveName);
				return;
			}
			RecordActiveSave(serverKey, saveName);
		} catch (...) {
			g_pendingRecord.reset();
			LogWarning("Save attestation failed with an exception; the pending record was dropped.");
		}
	}

	void ShowFirstJoinManualReconciliation(std::string_view a_saveFile, const std::optional<std::string>& a_localSha256, std::string_view a_serverSha256)
	{
		const auto message = std::format(
			"Your save '{}' could not be recovered automatically. Local SHA-256: {}. Server SHA-256: {}. Ask a server administrator to run save-recovery-resolve for this save.",
			a_saveFile, a_localSha256.value_or("unavailable"), a_serverSha256);
		F4T::UIClient::ShowDisconnectScreen("disconnected", message.c_str());
	}

	void ShowFirstJoinConnectionLost()
	{
		F4T::UIClient::ShowDisconnectScreen(
			"disconnected",
			"Connection lost while finishing recovery. Reconnect to finish; if it keeps failing ask a server administrator to run save-recovery-resolve.");
	}

	void ResetRecoveryRequest(std::uint64_t a_connection)
	{
		const std::scoped_lock lock(g_recoveryMutex);
		if (g_recoveryRequest && g_recoveryRequest->connection == a_connection) g_recoveryRequest.reset();
	}

	bool SendFirstJoinRecoveryConsume(RecoveryRequest& a_request)
	{
		const auto staged = F4T::ServerProfileStore::LookupFirstJoinRecovery(a_request.serverKey);
		const auto secret = F4T::SaveLineageStore::LookupServerSecret(a_request.serverKey);
		if (!staged || !secret) return false;
		const nlohmann::json approval{ { "profileId", staged->profileId }, { "characterId", staged->characterId }, { "saveFile", staged->saveFile }, { "sha256", staged->sha256 }, { "generation", staged->generation } };
		const nlohmann::json request{ { "type", "saveRecoveryConsume" }, { "approval", approval }, { "profileSecret", *secret } };
		if (!F4T::Networking::Detail::SendPacketBytesForGeneration(request.dump(), a_request.connection)) return false;
		a_request.consumeSent = true;
		a_request.consumeSentAt = std::chrono::steady_clock::now();
		return true;
	}
}

namespace F4T::SaveLineageSession
{
	F4T::DeathBagReceipts::SaveBaseline CaptureSaveBaseline(std::string_view a_saveFile)
	{
		try {
			if (a_saveFile.find_first_of("/\\") != std::string_view::npos) return {};
			const auto path = SaveFilePath(a_saveFile, true);
			if (!path) return {};
			const auto state = F4T::DeathBagReceipts::InspectSaveTarget(*path);
			if (state == F4T::DeathBagReceipts::SaveBaseline::State::Unknown) return {};
			if (state == F4T::DeathBagReceipts::SaveBaseline::State::Absent) {
				if (F4T::DeathBagReceipts::InspectSaveTarget(*path) != state) return {};
				return { state, {}, *path };
			}
			const auto hash = HashSaveForRecovery(a_saveFile);
			if (!hash) return {};
			const auto resolved = SaveFilePath(a_saveFile, false);
			if (!resolved || *resolved != *path || F4T::DeathBagReceipts::InspectSaveTarget(*path) != state) return {};
			const auto verifiedHash = HashSaveForRecovery(a_saveFile);
			const auto verifiedPath = SaveFilePath(a_saveFile, false);
			if (!verifiedHash || *verifiedHash != *hash || !verifiedPath || *verifiedPath != *path || F4T::DeathBagReceipts::InspectSaveTarget(*path) != state) return {};
			return { F4T::DeathBagReceipts::SaveBaseline::State::Present, *hash, *path };
		} catch (...) { return {}; }
	}

	std::optional<std::string> HashSaveForRecovery(std::string_view a_saveFile)
	{
		try {
			if (a_saveFile.find_first_of("/\\") != std::string_view::npos) return std::nullopt;
			const auto path = SaveFilePath(a_saveFile, true);
			if (!path || !IsSaveFileSettled(*path)) return std::nullopt;
			const auto raw = CreateFileW(path->c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (raw == INVALID_HANDLE_VALUE) return std::nullopt;
			const std::unique_ptr<void, decltype(&CloseHandle)> handle(raw, &CloseHandle);
			F4T::ContentManifest::Sha256Stream hash;
			std::array<char, 65536> buffer{};
			DWORD count{};
			for (;;) {
				if (!ReadFile(handle.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr)) return std::nullopt;
				if (count == 0) break;
				hash.Update(std::string_view{ buffer.data(), count });
			}
			return hash.FinishHex();
		} catch (const std::exception& error) {
			LogWarning(std::format("Cannot hash recovery save: {}", error.what()));
			return std::nullopt;
		}
	}

	bool RequestFirstJoinRecovery(const std::string& a_serverKey, const std::string& a_worldId, const std::string& a_selectedProfile, std::uint64_t a_connection)
	{
		{
			const std::scoped_lock lock(g_recoveryMutex);
			g_recoveryRequest.reset();
		}
		if (a_serverKey.empty() || a_worldId.empty() || a_selectedProfile.empty() || !a_connection || !F4T::ServerProfileStore::HasFirstJoinHold(a_serverKey)) return false;
		const auto secret = F4T::SaveLineageStore::LookupServerSecret(a_serverKey);
		if (!secret) return false;
		{
			const std::scoped_lock lock(g_recoveryMutex);
			g_recoveryRequest = RecoveryRequest{ a_serverKey, a_worldId, a_selectedProfile, a_connection };
		}
		const nlohmann::json request{ { "type", "saveRecoveryOfferRequest" }, { "profileId", a_selectedProfile }, { "profileSecret", *secret } };
		if (F4T::Networking::Detail::SendPacketBytesForGeneration(request.dump(), a_connection)) return true;
		const std::scoped_lock lock(g_recoveryMutex);
		if (g_recoveryRequest && g_recoveryRequest->connection == a_connection) g_recoveryRequest.reset();
		return false;
	}

	bool StageApprovedFirstJoin(std::uint64_t a_connection, const std::string& a_serverKey,
		const std::string& a_worldId, const std::string& a_selectedProfile, const std::string& a_approvalProfile,
		const std::string& a_characterId, const std::string& a_saveFile, const std::string& a_sha256, std::uint64_t a_generation)
	{
		if (!a_connection || a_selectedProfile.empty() || a_selectedProfile != a_approvalProfile ||
			!F4T::Networking::IsConnectedToServer() || F4T::Networking::GetConnectionGeneration() != a_connection) return false;
		const auto hash = HashSaveForRecovery(a_saveFile);
		if (!hash || !F4T::SaveProfileLogic::EqualsIgnoreCase(*hash, a_sha256)) return false;
		bool staged = false;
		return F4T::Networking::TryRunForConnectionGeneration(a_connection, [&] {
			staged = F4T::ServerProfileStore::StageFirstJoinRecovery(a_serverKey, a_worldId, a_approvalProfile,
				a_characterId, a_saveFile, a_sha256, a_generation);
		}) && staged;
	}

	bool HandleFirstJoinRecoveryOffer(const nlohmann::json& a_packet)
	{
		try {
			const auto offer = F4T::SaveRecovery::ParseOffer(a_packet);
			if (!offer) return false;
			std::optional<RecoveryRequest> request;
			{
				const std::scoped_lock lock(g_recoveryMutex);
				if (g_recoveryRequest && !g_recoveryRequest->offerStaged && g_recoveryRequest->connection == F4T::Networking::GetConnectionGeneration()) request = g_recoveryRequest;
			}
			if (!request) return false;
			const bool staged = StageApprovedFirstJoin(request->connection, request->serverKey, request->worldId,
				request->selectedProfile, offer->profile, offer->character, offer->save, offer->sha256, offer->generation);
			if (!staged) {
				if (!F4T::Networking::IsConnectedToServer() || F4T::Networking::GetConnectionGeneration() != request->connection) {
					ShowFirstJoinConnectionLost();
				} else {
					ShowFirstJoinManualReconciliation(offer->save, HashSaveForRecovery(offer->save), offer->sha256);
				}
				ResetRecoveryRequest(request->connection);
				return false;
			}
			const std::scoped_lock lock(g_recoveryMutex);
			if (!g_recoveryRequest || g_recoveryRequest->offerStaged || g_recoveryRequest->connection != request->connection) return false;
			g_recoveryRequest->offerStaged = true;
			if (!SendFirstJoinRecoveryConsume(*g_recoveryRequest)) g_recoveryRequest.reset();
			return true;
		} catch (const std::exception&) {
			return false;
		}
	}

	bool HandleFirstJoinRecoveryConsumed(const nlohmann::json& a_packet)
	{
		try {
			const auto approval = a_packet.find("approval");
			if (approval == a_packet.end() || !approval->is_object()) return false;
			std::optional<RecoveryRequest> request;
			{
				const std::scoped_lock lock(g_recoveryMutex);
				if (g_recoveryRequest && g_recoveryRequest->consumeSent && g_recoveryRequest->connection == F4T::Networking::GetConnectionGeneration()) request = g_recoveryRequest;
			}
			if (!request) return false;
			const auto staged = F4T::ServerProfileStore::LookupFirstJoinRecovery(request->serverKey);
			if (!staged) {
				ResetRecoveryRequest(request->connection);
				return false;
			}
			const auto localHash = HashSaveForRecovery(staged->saveFile);
			const bool applies = staged->profileId == request->selectedProfile &&
				F4T::ServerProfileStore::FirstJoinRecoveryApplies(*staged, request->worldId, *approval, localHash);
			bool applied = false;
			bool connectionHeld = false;
			if (applies) {
				connectionHeld = F4T::Networking::TryRunForConnectionGeneration(request->connection, [&] {
					applied = F4T::ServerProfileStore::ApplyFirstJoinRecovery(request->serverKey, *staged);
				});
			}
			if (applies && !connectionHeld) ShowFirstJoinConnectionLost();
			else if (!applied) ShowFirstJoinManualReconciliation(staged->saveFile, localHash, staged->sha256);
			ResetRecoveryRequest(request->connection);
			return applied;
		} catch (const std::exception&) {
			return false;
		}
	}

	bool HandleFirstJoinRecoveryRejected()
	{
		try {
			std::optional<RecoveryRequest> request;
			{
				const std::scoped_lock lock(g_recoveryMutex);
				if (g_recoveryRequest && g_recoveryRequest->connection == F4T::Networking::GetConnectionGeneration()) request = g_recoveryRequest;
			}
			if (!request) return false;
			const auto staged = F4T::ServerProfileStore::LookupFirstJoinRecovery(request->serverKey);
			if (staged) {
				const std::scoped_lock lock(g_recoveryMutex);
				if (g_recoveryRequest && g_recoveryRequest->connection == request->connection && !g_recoveryRequest->consumeSent &&
					SendFirstJoinRecoveryConsume(*g_recoveryRequest)) {
					return true;
				}
			}
			ResetRecoveryRequest(request->connection);
			if (staged) ShowFirstJoinManualReconciliation(staged->saveFile, HashSaveForRecovery(staged->saveFile), staged->sha256);
			return true;
		} catch (const std::exception&) {
			return false;
		}
	}

	void TickFirstJoinRecovery()
	{
		std::string serverKey;
		{
			const std::scoped_lock lock(g_recoveryMutex);
			if (!g_recoveryRequest || !g_recoveryRequest->consumeSent) return;
			if (std::chrono::steady_clock::now() - g_recoveryRequest->consumeSentAt < kRecoveryConsumeTimeout) return;
			LogWarning("Save recovery consume timed out; the staged record is kept and recovery resumes on reconnect.");
			serverKey = g_recoveryRequest->serverKey;
			g_recoveryRequest.reset();
		}
		if (F4T::ServerProfileStore::LookupFirstJoinRecovery(serverKey)) {
			ShowFirstJoinConnectionLost();
		}
	}

	bool AttestFirstJoinSave(const std::string& a_serverKey, const std::string& a_saveFile, const std::string& a_sha256Hex)
	{
		return RecordActiveSave(a_serverKey, a_saveFile, a_sha256Hex);
	}

	void ExpectFirstJoinAttestation(const std::string& a_saveFile, const std::string& a_sha256Hex)
	{
		const std::scoped_lock lock(g_firstJoinAttestationMutex);
		g_firstJoinAttestation = FirstJoinAttestation{ a_saveFile, a_sha256Hex, AttestationOutcome::Pending };
	}

	AttestationOutcome FirstJoinAttestationOutcome()
	{
		const std::scoped_lock lock(g_firstJoinAttestationMutex);
		return g_firstJoinAttestation ? g_firstJoinAttestation->outcome : AttestationOutcome::Rejected;
	}

	void NoteFirstJoinAttestationResult(std::string_view a_saveFile, std::string_view a_sha256Hex, bool a_acknowledged)
	{
		const std::scoped_lock lock(g_firstJoinAttestationMutex);
		if (!g_firstJoinAttestation || g_firstJoinAttestation->outcome != AttestationOutcome::Pending) return;
		if (!a_acknowledged) {
			g_firstJoinAttestation->outcome = AttestationOutcome::Rejected;
			return;
		}
		if (a_saveFile != g_firstJoinAttestation->saveFile) return;
		const bool matches = F4T::SaveProfileLogic::EqualsIgnoreCase(a_sha256Hex, g_firstJoinAttestation->sha256Hex);
		g_firstJoinAttestation->outcome = matches ? AttestationOutcome::Accepted : AttestationOutcome::Rejected;
	}

	void TickPendingRecord()
	{
		PollPendingRecord();
	}

	bool HasPendingRecord()
	{
		return g_pendingRecord.has_value();
	}

	bool HasPendingRecordFor(std::string_view a_saveName)
	{
		return g_pendingRecord && g_pendingRecord->saveName == a_saveName;
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
		const std::string& a_savedName,
		bool a_lateExitSave)
	{
		try {
			if (a_serverKey.empty() ||
				a_characterId.empty() ||
				a_pendingProfileBind ||
				(!F4T::Networking::IsConnectedToServer() && !a_lateExitSave)) {
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
			return RefuseJoin(std::move(decision), "The current character save could not be found. Contact the server administrator for recovery.");
		}

		bool approved = false;
		if (!F4T::InventoryGrant::CheckLaunchSave(a_latestSave, approved)) {
			return RefuseJoin(std::move(decision), "Starting inventory needs recovery. Contact the server administrator before loading this character.");
		}
		if (approved) return decision;
		try {
			const auto characterId = F4T::SaveProfileLogic::ExtractCharacterId(*a_latestSave);
			if (!characterId) {
				return RefuseJoin(std::move(decision), NotMultiplayerReason);
			}

			const auto attested = F4T::SaveLineageStore::LookupAttestedSaves(a_serverKey);
			if (attested.empty()) {
				return RefuseJoin(std::move(decision), "This character save has no verified lineage. Keep your saves and contact the server administrator for recovery.");
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

			AnnounceRecordedAttestations(a_serverKey, *characterId, attested);
			if (!resolution.saveFile || resolution.verdict != F4T::SaveProfileLogic::LineageVerdict::Attested ||
				!F4T::SaveProfileLogic::EqualsIgnoreCase(*resolution.saveFile, *a_latestSave)) {
				return RefuseJoin(std::move(decision), "The newest character save cannot be verified. No older save will be loaded; contact the server administrator for recovery.");
			}
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

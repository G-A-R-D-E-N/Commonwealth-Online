#pragma once

#include <F4SE/F4SE.h>
#include "F4TDeathBagReceipts.h"
#include <nlohmann/json_fwd.hpp>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace F4T::SaveLineageSession
{
	struct JoinSaveDecision
	{
		std::optional<std::string> saveFile;
		bool allowed{ true };
		std::string code;
		std::string reason;
	};

	void TickPendingRecord();
	bool SettlePendingRecordForExit();
	bool HasPendingRecord();
	bool HasPendingRecordFor(std::string_view a_saveName);
	std::optional<std::string> HashSaveForRecovery(std::string_view a_saveFile);
	F4T::DeathBagReceipts::SaveBaseline CaptureSaveBaseline(std::string_view a_saveFile);
	bool RequestFirstJoinRecovery(const std::string& a_serverKey, const std::string& a_worldId, const std::string& a_selectedProfile, std::uint64_t a_connection);
	bool HandleFirstJoinRecoveryOffer(const nlohmann::json& a_packet);
	bool HandleFirstJoinRecoveryConsumed(const nlohmann::json& a_packet);
	bool HandleFirstJoinRecoveryRejected();
	void TickFirstJoinRecovery();
	bool StageApprovedFirstJoin(std::uint64_t a_connection, const std::string& a_serverKey,
		const std::string& a_worldId, const std::string& a_selectedProfile, const std::string& a_approvalProfile,
		const std::string& a_characterId, const std::string& a_saveFile, const std::string& a_sha256, std::uint64_t a_generation);
	bool AttestFirstJoinSave(const std::string& a_serverKey, const std::string& a_saveFile, const std::string& a_sha256Hex);
	enum class AttestationOutcome { Pending, Accepted, Rejected };
	void ExpectFirstJoinAttestation(const std::string& a_saveFile, const std::string& a_sha256Hex);
	AttestationOutcome FirstJoinAttestationOutcome();
	void NoteFirstJoinAttestationResult(std::string_view a_saveFile, std::string_view a_sha256Hex, bool a_acknowledged);

	std::string ReadPostSaveGameName(const F4SE::MessagingInterface::Message& a_msg);

	void TryUpdateActiveProfileBinding(
		const std::string& a_serverKey,
		const std::string& a_characterId,
		bool a_pendingProfileBind,
		const std::optional<std::string>& a_bindableSaveFile,
		const std::string& a_savedName,
		bool a_lateExitSave);

	JoinSaveDecision ResolveJoinSave(
		const std::string& a_serverKey,
		std::span<const std::string> a_saveFiles,
		const std::optional<std::string>& a_latestSave,
		bool a_isAdmin);
}

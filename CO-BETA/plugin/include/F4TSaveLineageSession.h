#pragma once

#include <F4SE/F4SE.h>
#include <optional>
#include <span>
#include <string>

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
	bool HasPendingRecord();

	std::string ReadPostSaveGameName(const F4SE::MessagingInterface::Message& a_msg);

	void TryUpdateActiveProfileBinding(
		const std::string& a_serverKey,
		const std::string& a_characterId,
		bool a_pendingProfileBind,
		const std::optional<std::string>& a_bindableSaveFile,
		const std::string& a_savedName);

	JoinSaveDecision ResolveJoinSave(
		const std::string& a_serverKey,
		std::span<const std::string> a_saveFiles,
		const std::optional<std::string>& a_latestSave,
		bool a_isAdmin);
}

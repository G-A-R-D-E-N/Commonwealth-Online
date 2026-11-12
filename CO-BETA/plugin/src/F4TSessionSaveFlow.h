#pragma once

#include "F4TSaveLineageSession.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace RE
{
	class BGSSaveLoadFileEntry;
}

namespace F4T::SessionSaveFlow
{
	struct JoinSaveResolution
	{
		std::optional<std::string> saveFile;
		bool allowed{ true };
		std::string code;
		std::string reason;
		std::optional<F4T::SaveLineageSession::ContinuationPlan> continuation;
		std::optional<std::string> verifiedHead;
	};

	[[nodiscard]] RE::BGSSaveLoadFileEntry* FindSaveEntryByFileName(const std::string& a_saveFile);
	[[nodiscard]] bool LoadSaveFromMainMenu(const std::string& a_saveFile, std::string& a_error);
	[[nodiscard]] bool LoadSaveInGame(const std::string& a_saveFile, std::string& a_error);
	[[nodiscard]] bool IsMissingAssociatedSaveError(std::string_view a_error);
	[[nodiscard]] std::optional<std::string> ResolveBindablePostSaveName(std::string_view a_captured);
	[[nodiscard]] JoinSaveResolution ResolveJoinSave(
		const std::string& a_serverKey,
		const std::string& a_associatedSaveFile,
		std::string_view a_characterId,
		bool a_isAdmin);
}

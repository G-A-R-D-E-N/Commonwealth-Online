#include "pch.h"

#include "F4TSessionSaveFlow.h"

#include "F4TSaveLineageSession.h"
#include "F4TSavePluginPreflight.h"
#include "F4TSaveProfileLogic.h"
#include "F4TSessionWorldState.h"

#include "RE/B/BGSSaveLoadManager.h"
#include "RE/M/MainMenu.h"

#include <cstring>
#include <format>
#include <utility>
#include <vector>

namespace F4T::SessionSaveFlow
{
	RE::BGSSaveLoadFileEntry* FindSaveEntryByFileName(const std::string& a_saveFile)
	{
		auto* saveLoadManager = RE::BGSSaveLoadManager::GetSingleton();
		if (!saveLoadManager) {
			return nullptr;
		}

		saveLoadManager->BuildSaveGameList(0);

		for (std::uint32_t i = 0; i < saveLoadManager->saveGameCount; ++i) {
			if (i >= saveLoadManager->saveGameList.size()) {
				break;
			}

			auto* entry = saveLoadManager->saveGameList[i];
			if (!entry || !entry->fileName) {
				continue;
			}

			if (_stricmp(entry->fileName, a_saveFile.c_str()) == 0) {
				return entry;
			}
		}

		return nullptr;
	}

	bool LoadSaveFromMainMenu(const std::string& a_saveFile, std::string& a_error)
	{
		if (!SessionWorldState::GetMainMenu()) {
			a_error = "Main menu is not available to load the associated save.";
			return false;
		}
		return LoadSaveInGame(a_saveFile, a_error);
	}

	bool LoadSaveInGame(const std::string& a_saveFile, std::string& a_error)
	{
		auto* saveEntry = FindSaveEntryByFileName(a_saveFile);
		if (!saveEntry) {
			a_error = std::format("Associated save '{}' was not found.", a_saveFile);
			return false;
		}

		auto* saveLoadManager = RE::BGSSaveLoadManager::GetSingleton();
		if (!saveLoadManager) {
			a_error = "Save/load manager is unavailable.";
			return false;
		}

		if (!SavePluginPreflight::AllowSaveLoad(a_saveFile, a_error)) {
			return false;
		}

		saveLoadManager->queuedEntryToLoad = saveEntry;
		saveLoadManager->QueueSaveLoadTask(RE::BGSSaveLoadManager::QUEUED_TASK::kLoadGame);
		return true;
	}

	bool IsMissingAssociatedSaveError(std::string_view a_error)
	{
		return a_error.find("was not found") != std::string_view::npos;
	}

	std::optional<std::string> ResolveBindablePostSaveName(std::string_view a_captured)
	{
		if (a_captured.empty() || SessionWorldState::IsFirstJoinSetupSave(a_captured)) { return std::nullopt; }
		return std::string{ a_captured };
	}

	JoinSaveResolution ResolveJoinSave(
		const std::string& a_serverKey,
		const std::string& a_associatedSaveFile,
		std::string_view a_characterId,
		bool a_isAdmin)
	{
		JoinSaveResolution result;

		auto* saveLoadManager = RE::BGSSaveLoadManager::GetSingleton();
		if (!saveLoadManager) {
			return result;
		}

		saveLoadManager->BuildSaveGameList(0);
		if (saveLoadManager->saveGameCount == 0 || saveLoadManager->saveGameList.empty()) {
			return result;
		}

		std::vector<std::string> saveFiles;
		saveFiles.reserve(saveLoadManager->saveGameCount);
		for (std::uint32_t i = 0; i < saveLoadManager->saveGameCount; ++i) {
			if (i >= saveLoadManager->saveGameList.size()) {
				break;
			}

			const auto* entry = saveLoadManager->saveGameList[i];
			if (!entry || !entry->fileName || entry->fileName[0] == '\0') {
				continue;
			}

			if (SessionWorldState::IsFirstJoinSetupSave(entry->fileName)) {
				continue;
			}
			saveFiles.emplace_back(entry->fileName);
		}

		const auto latestSave =
			F4T::SaveProfileLogic::ResolveLatestProfileSave(a_associatedSaveFile, a_characterId, saveFiles);
		const auto decision = F4T::SaveLineageSession::ResolveJoinSave(a_serverKey, saveFiles, latestSave, a_isAdmin);
		result.saveFile = decision.saveFile;
		result.allowed = decision.allowed;
		result.code = decision.code;
		result.reason = decision.reason;
		return result;
	}
}

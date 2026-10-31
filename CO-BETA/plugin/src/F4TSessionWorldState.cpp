#include "pch.h"

#include "F4TSessionWorldState.h"

#include "F4TSaveProfileLogic.h"
#include "F4TSoloCell.h"

#include "RE/L/LoadingMenu.h"
#include "RE/M/MainMenu.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/U/UI.h"

#include <cmath>
#include <string>
#include <vector>

namespace F4T::SessionWorldState
{
	bool IsMainMenuOpen()
	{
		const auto* ui = RE::UI::GetSingleton();
		return ui && ui->GetMenuOpen<RE::MainMenu>();
	}

	bool IsLoadingScreenActive()
	{
		const auto* ui = RE::UI::GetSingleton();
		return ui && ui->GetMenuOpen<RE::LoadingMenu>();
	}

	bool IsPlaceholderSpawnPosition(const RE::PlayerCharacter& a_player)
	{
		const auto pos = a_player.GetPosition();
		return std::abs(pos.x - 2048.0F) < 1.0F && std::abs(pos.y - 2048.0F) < 1.0F && std::abs(pos.z) < 1.0F;
	}

	bool IsWorldReadyForCharGen(const RE::PlayerCharacter& a_player)
	{
		if (IsLoadingScreenActive() || IsMainMenuOpen() || IsPlaceholderSpawnPosition(a_player)) {
			return false;
		}
		return a_player.GetParentCell() != nullptr;
	}

	bool IsGameplayWorldReady(const RE::PlayerCharacter& a_player)
	{
		if (!IsWorldReadyForCharGen(a_player)) {
			return false;
		}
		const auto* cell = a_player.GetParentCell();
		return cell && !F4T::SoloCell::IsSoloCell(*cell);
	}

	RE::MainMenu* GetMainMenu()
	{
		const auto* ui = RE::UI::GetSingleton();
		return ui ? ui->GetMenu<RE::MainMenu>().get() : nullptr;
	}

	bool IsMenuOpen(const char* a_menuName)
	{
		const auto* ui = RE::UI::GetSingleton();
		return ui && ui->GetMenuOpen(a_menuName);
	}

	bool IsFirstJoinSetupSave(std::string_view a_saveFile)
	{
		const auto timestamp = F4T::SaveProfileLogic::ExtractSaveTimestamp(a_saveFile);
		if (!timestamp) {
			return false;
		}

		const auto timestampText = std::to_string(*timestamp);
		std::vector<std::string_view> tokens;
		std::size_t tokenStart = 0;
		while (tokenStart <= a_saveFile.size()) {
			auto tokenEnd = a_saveFile.find('_', tokenStart);
			if (tokenEnd == std::string_view::npos) {
				tokenEnd = a_saveFile.size();
			}
			tokens.push_back(a_saveFile.substr(tokenStart, tokenEnd - tokenStart));
			if (tokenEnd == a_saveFile.size()) {
				break;
			}
			tokenStart = tokenEnd + 1;
		}

		for (std::size_t index = 2; index < tokens.size(); ++index) {
			if (tokens[index] == timestampText && tokens[index - 2] == kFirstJoinStartCellEditorId) {
				return true;
			}
		}
		return false;
	}
}

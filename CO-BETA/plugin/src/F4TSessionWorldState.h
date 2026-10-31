#pragma once

#include <string_view>

namespace RE
{
	class MainMenu;
	class PlayerCharacter;
}

namespace F4T::SessionWorldState
{
	inline constexpr std::string_view kFirstJoinStartCellEditorId = "Vault111Cryo";

	[[nodiscard]] bool IsMainMenuOpen();
	[[nodiscard]] bool IsLoadingScreenActive();
	[[nodiscard]] bool IsPlaceholderSpawnPosition(const RE::PlayerCharacter& a_player);
	[[nodiscard]] bool IsWorldReadyForCharGen(const RE::PlayerCharacter& a_player);
	[[nodiscard]] bool IsGameplayWorldReady(const RE::PlayerCharacter& a_player);
	[[nodiscard]] RE::MainMenu* GetMainMenu();
	[[nodiscard]] bool IsMenuOpen(const char* a_menuName);
	[[nodiscard]] bool IsFirstJoinSetupSave(std::string_view a_saveFile);
}

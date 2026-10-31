#pragma once

#include "pch.h"
#include "Scaleform/G/GFx_Movie.h"
#include "Scaleform/G/GFx_Value.h"

namespace F4T::MainMenuInject::Internal
{
	inline constexpr auto kMultiplayerIndex = 15;

	void LogInfo(std::string_view a_message);
	void LogWarning(std::string_view a_message);
	std::optional<std::int32_t> GetEntryLogicalIndex(const Scaleform::GFx::Value& a_entry);
	bool IsPauseMode(Scaleform::GFx::Movie* a_movie);
	bool EntryListHasIndex(const Scaleform::GFx::Value& a_entryList, std::int32_t a_index);
	bool ResolveMainList(
		RE::MainMenu* a_mainMenu,
		Scaleform::GFx::Value& a_outListWidget,
		Scaleform::GFx::Value& a_outEntryList);
	bool IsMainStateForMenu(RE::MainMenu* a_mainMenu);
	bool InjectMultiplayerRow(RE::MainMenu* a_mainMenu);
	bool TryInjectForStartMenu(RE::StartMenuBase* a_menu);
	void MarkMainMenuInjectionComplete(std::string_view a_reason);

	bool InstallInitMainListHook();
	bool InstallMainMenuInputHooks();
	bool AreMainMenuInputHooksInstalled();
	bool TryHandleMultiplayerSelection(RE::MainMenu* a_mainMenu);

	void RegisterItemPressListener(RE::MainMenu* a_mainMenu);
	void ResetItemPressListener();
	void PollMultiplayerActivation();
	void ResyncMultiplayerRow();
}

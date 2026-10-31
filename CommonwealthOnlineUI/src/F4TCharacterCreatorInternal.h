#pragma once

#include "pch.h"

#include "F4TCharacterCreator.h"
#include "F4TCharacterCreatorCommands.h"
#include "F4TCharacterCreatorLifecycle.h"
#include "F4TPrismaUIInternal.h"

#include <atomic>
#include <cstdint>
#include <string_view>

namespace F4T::CharacterCreator
{
	namespace Internal = F4T::PrismaUI::Internal;

	constexpr const char* kHtmlPath = "CommonwealthOnline/character-creator/index.html";
	constexpr const char* kMenuName = "LooksMenu";
	constexpr bool        kSuppressVanillaInput = true;

	struct CreatorState
	{
		F4T::PrismaViewState::Controller controller{ "CharacterCreator" };
		Lifecycle::Gate                 suppressionGate;
		bool                            installed{ false };
		bool                            sinkRegistered{ false };
		bool                            menuOpen{ false };
		std::atomic<std::uint64_t>      generation{ 0 };
	};

	inline CreatorState g_state;

	void LogInfo(std::string_view a_message);
	void LogWarning(std::string_view a_message);

	void PushEvent(const nlohmann::json& a_event);
	void PushState();

	void HandleCreatorAction(const char* a_json);
}

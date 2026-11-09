#include "pch.h"

#include "F4TSessionLaunch.h"

#include "F4TSessionLaunchDiagnostics.h"
#include "F4TSessionLaunchInternal.h"
#include "F4TSessionWorldState.h"
#include "F4TSoloCell.h"

#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESWorldSpace.h"

#include <format>
#include <string_view>

	using namespace F4T::SessionWorldState;
	using namespace F4T::SessionLaunchDiagnostics;

namespace F4T::SessionLaunch::Internal
{
	void LogPlayerLocation(std::string_view a_label, const RE::PlayerCharacter& a_player)
	{
		const auto* cell = a_player.GetParentCell();
		const auto cellId = cell ? cell->GetFormID() : 0U;
		const auto* cellEditorId = cell ? cell->GetFormEditorID() : nullptr;
		const auto pos = a_player.GetPosition();
		const auto* worldspace = cell && cell->worldSpace ? cell->worldSpace : a_player.cachedWorldspace;
		const auto worldId = worldspace ? worldspace->GetFormID() : 0U;

		LogInfo(std::format(
			"{} phase={} cell={:08X} editorId={} world={:08X} exterior={} pos=({:.1f},{:.1f},{:.1f}) loading={} mainMenu={} specialMenu={}",
			a_label,
			PhaseName(g_charGenPhase),
			cellId,
			cellEditorId ? cellEditorId : "<none>",
			worldId,
			cell && cell->IsExterior(),
			pos.x,
			pos.y,
			pos.z,
			IsLoadingScreenActive(),
			IsMainMenuOpen(),
			IsMenuOpen("SPECIALMenu")));
	}

	void LogGameplayReadyPending(const RE::PlayerCharacter* a_player)
	{
		if (g_spawnWaitAttempts != 1 && g_spawnWaitAttempts % kPendingLogIntervalPolls != 0) {
			return;
		}

		if (!a_player) {
			LogInfo("Gameplay-ready gate pending: local player is unavailable.");
			return;
		}
		if (IsLoadingScreenActive()) {
			LogPlayerLocation("Gameplay-ready gate pending: loading screen active", *a_player);
			return;
		}
		if (IsMainMenuOpen()) {
			LogPlayerLocation("Gameplay-ready gate pending: main menu active", *a_player);
			return;
		}
		if (IsPlaceholderSpawnPosition(*a_player)) {
			LogPlayerLocation("Gameplay-ready gate pending: placeholder position", *a_player);
			return;
		}
		const auto* cell = a_player->GetParentCell();
		if (!cell) {
			LogPlayerLocation("Gameplay-ready gate pending: no parent cell", *a_player);
			return;
		}
		if (F4T::SoloCell::IsSoloCell(*cell)) {
			LogPlayerLocation("Gameplay-ready gate pending: player remains in solo onboarding cell", *a_player);
			return;
		}
		LogPlayerLocation("Gameplay-ready gate stabilizing", *a_player);
	}

}

#pragma once

#include "F4TReplicationRuntime.h"

#include "RE/P/PlayerCharacter.h"

#include <string>
#include <string_view>
#include <vector>

namespace F4T::LocalPlayerState
{
	std::string GetLocalPlayerLogMessage(std::string_view a_message);
	void LogInfoWithLocalPlayerPrefix(std::string_view a_message);
	void LogWarningWithLocalPlayerPrefix(std::string_view a_message);

	RE::PlayerCharacter* TryGetLocalPlayer();
	F4T::ReplicationRuntime::PlayerTransform GetPlayerTransform(const RE::PlayerCharacter& a_player);
	F4T::ReplicationRuntime::PlayerLocation GetPlayerLocation(const RE::PlayerCharacter& a_player);
	[[nodiscard]] bool GetLocalPlayerCrouching(const RE::PlayerCharacter& a_player);
	std::vector<F4T::RemotePlayerState::RemoteEquippedItem> CaptureLocalEquippedItems(
		RE::PlayerCharacter& a_player,
		std::vector<F4T::RemotePlayerState::RemoteEquippedItem>& a_lastLoggedItems);
	std::string CaptureLocalCharacterName(RE::PlayerCharacter& a_player);
	F4T::RemotePlayerState::RemoteAppearance CaptureLocalAppearance(const RE::PlayerCharacter& a_player);
	bool AreEquipmentStatesEqual(
		const std::vector<F4T::RemotePlayerState::RemoteEquippedItem>& a_left,
		const std::vector<F4T::RemotePlayerState::RemoteEquippedItem>& a_right);
	bool AreAppearanceStatesEqual(
		const F4T::RemotePlayerState::RemoteAppearance& a_left,
		const F4T::RemotePlayerState::RemoteAppearance& a_right);
}

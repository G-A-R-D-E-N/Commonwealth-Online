#pragma once

#include "F4TNpcSync.h"
#include "F4TPlayerVitals.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace F4T::RemotePlayerState
{
	struct RemoteActionEvent
	{
		std::uint32_t sequence{};
		std::uint32_t type{};
		std::string eventName;
		std::vector<float> animationVariablesFloat;
		std::vector<bool> animationVariablesBool;
		std::vector<std::uint32_t> animationVariablesInt;
		std::chrono::steady_clock::time_point captureTime{};
	};

	struct RemoteItemMod
	{
		// Wire recipes must retain index and rank; source:co-core/src/validation/F4TPlayerStateValidation.cpp.
		std::uint32_t formId{};
		std::uint8_t index{};
		std::uint8_t rank{};
		bool operator==(const RemoteItemMod&) const = default;
	};

	struct RemoteEquippedItem
	{
		std::string slot;
		std::uint32_t formId{};
		std::optional<std::vector<RemoteItemMod>> mods;
		bool operator==(const RemoteEquippedItem&) const = default;
	};

	struct RemoteAppearanceMorph
	{
		std::uint32_t id{};
		float value{};
	};

	struct RemoteAppearanceTintColor
	{
		std::uint8_t r{};
		std::uint8_t g{};
		std::uint8_t b{};
		std::uint8_t a{};
	};

	struct RemoteAppearanceMorphWeight
	{
		float thin{};
		float muscular{};
		float large{};
	};

	struct RemoteAppearanceFacialBoneMorph
	{
		std::uint32_t id{};
		float position[3]{};
		float rotation[3]{};
		float scale[3]{ 1.0F, 1.0F, 1.0F };
	};

	struct RemoteAppearanceTint
	{
		std::uint16_t id{};
		std::uint32_t type{};
		std::uint8_t value{};
		std::uint32_t color{};
		std::uint16_t swatch{};
		bool hasColor{ false };

		bool operator==(const RemoteAppearanceTint&) const = default;
	};

	struct RemoteAppearance
	{
		std::uint32_t version{ 4 };
		std::uint32_t raceFormId{};
		float height{ 1.0F };
		RemoteAppearanceMorphWeight morphWeight;
		RemoteAppearanceTintColor bodyTintColor;
		std::uint32_t hairColorFormId{};
		std::uint32_t facialHairColorFormId{};
		std::uint32_t complexionFormId{};
		std::optional<bool> isFemale;
		std::vector<std::uint32_t> headPartFormIds;
		std::vector<RemoteAppearanceMorph> morphSliders;
		std::vector<float> morphRegionSliders;
		std::vector<RemoteAppearanceFacialBoneMorph> facialBoneMorphs;
		std::vector<RemoteAppearanceTint> tints;
	};

	struct RemotePlayerState
	{
		std::uint32_t playerId{};
		float x{};
		float y{};
		float z{};
		float angleZ{};
		std::string movementType;
		std::string cellId;
		std::string worldspaceId;
		std::optional<double> clientTime;
		std::optional<double> serverTime;
		bool isMoving{ false };
		bool isSprinting{ false };
		bool isSneaking{ false };
		bool isJumping{ false };
		bool isCrouching{ false };
		bool weaponDrawn{ false };
		float movementSpeed{ 0.0F };
		float animationGraphSpeed{ -1.0F };
		bool hasEquipmentUpdate{ false };
		std::vector<RemoteEquippedItem> equippedItems;
		bool hasAppearanceUpdate{ false };
		RemoteAppearance appearance;
		std::chrono::steady_clock::time_point lastReceivedLocalTime{};

		std::vector<RemoteActionEvent> actionEvents;

		std::string characterName;

		std::optional<F4T::PlayerVitals::Vitals> vitals;
		float animationDirection{ 0.0F };
		bool hasAnimationDirection{ false };
		float aimPitch{ 0.0F };
		float turnDelta{ 0.0F };
		bool hasAimState{ false };

		bool hasPowerArmorUpdate{ false };
		bool powerArmorInFrame{ false };
		std::uint32_t powerArmorFrameBaseFormId{ 0 };
		std::vector<RemoteEquippedItem> powerArmorPieces;
	};

	void SetAssignedPlayerId(std::uint32_t a_playerId);
	std::optional<std::uint32_t> GetAssignedPlayerId();
	bool IsAssignedPlayerId(std::uint32_t a_playerId);
	void ClearAssignedPlayerId();
	float BoundClaimedDamage(std::uint32_t a_senderId, float a_claimedDamage);

	struct IncomingCombatHit
	{
		std::uint32_t senderId{};
		std::uint32_t sequence{};
		float damage{};
	};

	void UpdateRemotePlayer(RemotePlayerState a_state);
	bool HasRemotePlayer(std::uint32_t a_playerId);
	bool RemoveRemotePlayer(std::uint32_t a_playerId);
	void ClearRemotePlayers();
	std::vector<RemotePlayerState> GetRemotePlayerSnapshot();
	bool SetRemotePlayerVitals(std::uint32_t a_playerId, const F4T::PlayerVitals::Vitals& a_vitals);
	bool SetRemotePlayerDownedState(std::uint32_t a_playerId, bool a_downed, bool a_isDead);

	bool QueueIncomingCombatHit(std::uint32_t a_senderId, std::uint32_t a_sequence, float a_damage);
	bool QueueIncomingNpcDamage(std::uint32_t a_authorityPlayerId, std::uint32_t a_sequence, float a_damage, std::uint32_t a_npcId);

	// Applies every queued network hit on the game thread and clears the queue.
	// Damage is applied here, in one place, so no caller can reach for a base
	// Health write as a fallback.
	void ApplyPendingCombatHits();
}

#include "F4TLocalPlayerState.h"
#include "F4TUIClient.h"

#include "F4TDebugLogging.h"
#include "F4TEquipmentInstance.h"
#include "F4TNetworking.h"
#include "F4TSoloCell.h"
#include "F4TThrowableSync.h"

#include "RE/A/ACTOR_STANCE.h"
#include "RE/B/BIPED_OBJECT.h"
#include "RE/B/BipedAnim.h"
#include "RE/T/TESObjectWEAP.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::LocalPlayerState
{
	using F4T::ReplicationRuntime::PlayerTransform;
	using F4T::ReplicationRuntime::PlayerLocation;

	using RemoteEquippedItem = F4T::RemotePlayerState::RemoteEquippedItem;
	using RemoteAppearance = F4T::RemotePlayerState::RemoteAppearance;
	using RemoteAppearanceMorph = F4T::RemotePlayerState::RemoteAppearanceMorph;
	using RemoteAppearanceFacialBoneMorph = F4T::RemotePlayerState::RemoteAppearanceFacialBoneMorph;
	using RemoteAppearanceTint = F4T::RemotePlayerState::RemoteAppearanceTint;

	namespace
	{
	struct SyncedEquipmentSlot
	{
		std::string_view name;
		RE::BIPED_OBJECT bipedObject;
	};

	constexpr std::array<SyncedEquipmentSlot, 16> kSyncedEquipmentSlots{
		{
			{ "hairTop", RE::BIPED_OBJECT::kHairTop },
			{ "hairLong", RE::BIPED_OBJECT::kHairLong },
			{ "body", RE::BIPED_OBJECT::kBody },
			{ "underTorso", RE::BIPED_OBJECT::kUnderTorso },
			{ "underLeftArm", RE::BIPED_OBJECT::kUnderLeftArm },
			{ "underRightArm", RE::BIPED_OBJECT::kUnderRightArm },
			{ "underLeftLeg", RE::BIPED_OBJECT::kUnderLeftLeg },
			{ "underRightLeg", RE::BIPED_OBJECT::kUnderRightLeg },
			{ "aboveTorso", RE::BIPED_OBJECT::kAboveTorso },
			{ "aboveLeftArm", RE::BIPED_OBJECT::kAboveLeftArm },
			{ "aboveRightArm", RE::BIPED_OBJECT::kAboveRightArm },
			{ "aboveLeftLeg", RE::BIPED_OBJECT::kAboveLeftLeg },
			{ "aboveRightLeg", RE::BIPED_OBJECT::kAboveRightLeg },
			{ "headband", RE::BIPED_OBJECT::kHeadband },
			{ "eyes", RE::BIPED_OBJECT::kEyes },
			{ "scalp", RE::BIPED_OBJECT::kScalp }
		}
	};

	std::uint32_t GetEquippedArmorFormId(const RE::Actor& a_actor, RE::BIPED_OBJECT a_bipedObject)
	{
		if (!a_actor.biped) {
			return 0;
		}

		const auto* bipedObject = a_actor.biped->GetBipObject(a_bipedObject);
		if (!bipedObject || !bipedObject->parent.object) {
			return 0;
		}

		if (!bipedObject->parent.object->Is(RE::ENUM_FORM_ID::kARMO)) {
			return 0;
		}

		return bipedObject->parent.object->GetFormID();
	}

	std::uint32_t GetEquippedWeaponFormId(RE::Actor& a_actor, RE::BGSEquipIndex a_equipIndex)
	{
		RE::BGSObjectInstance objectInstance{ nullptr, nullptr };
		const auto* item = a_actor.GetEquippedItem(&objectInstance, a_equipIndex);

		if (!item || !item->object) {
			return 0;
		}

		if (!item->object->Is(RE::ENUM_FORM_ID::kWEAP)) {
			return 0;
		}

		const auto* weapon = static_cast<const RE::TESObjectWEAP*>(item->object);
		return F4T::ThrowableSync::RightHandEntryFormId(item->object->GetFormID(), weapon->IsThrownWeapon());
	}
	}

	std::string GetLocalPlayerLogMessage(std::string_view a_message)
	{
		return F4T::Networking::GetLocalPlayerLogPrefix() + " " + std::string(a_message);
	}

	void LogInfoWithLocalPlayerPrefix(std::string_view a_message)
	{
		F4T::DebugLogging::Write("Plugin", GetLocalPlayerLogMessage(a_message));
	}

	void LogWarningWithLocalPlayerPrefix(std::string_view a_message)
	{
		const auto message = GetLocalPlayerLogMessage(a_message);
		REX::WARN(std::string_view{ message });
	}

	RE::PlayerCharacter* TryGetLocalPlayer()
	{
		return RE::PlayerCharacter::GetSingleton();
	}

	PlayerTransform GetPlayerTransform(const RE::PlayerCharacter& a_player)
	{
		const auto position = a_player.GetPosition();

		return {
			position.x,
			position.y,
			position.z,
			a_player.data.angle.z
		};
	}

	PlayerLocation GetPlayerLocation(const RE::PlayerCharacter& a_player)
	{
		PlayerLocation location{
			false,
			0,
			false,
			0,
			false
		};

		const auto* parentCell = a_player.GetParentCell();
		if (parentCell) {
			location.hasCellId = true;
			location.cellId = parentCell->GetFormID();
			location.isSoloCell = F4T::SoloCell::IsSoloCell(*parentCell);
		}

		if (a_player.cachedWorldspace) {
			location.hasWorldspaceId = true;
			location.worldspaceId = a_player.cachedWorldspace->GetFormID();
		} else if (parentCell && parentCell->IsExterior() && parentCell->worldSpace) {
			location.hasWorldspaceId = true;
			location.worldspaceId = parentCell->worldSpace->GetFormID();
		}

		return location;
	}

	bool GetLocalPlayerCrouching(const RE::PlayerCharacter& a_player)
	{
		return a_player.stance == static_cast<std::uint32_t>(RE::ACTOR_STANCE::kSneaking);
	}

	std::vector<RemoteEquippedItem> CaptureLocalEquippedItems(RE::PlayerCharacter& a_player, std::vector<RemoteEquippedItem>& a_lastLoggedItems)
	{
		std::vector<RemoteEquippedItem> equippedItems;
		equippedItems.reserve(kSyncedEquipmentSlots.size() + 2);

		// Constraint: an armor recipe is captured through the equipped stack, never the biped modExtra, in one inventory pass per publish; source:docs/code-notes/equipment-state.md (armor recipe capture).
		std::vector<std::uint32_t> armorFormIds;
		armorFormIds.reserve(kSyncedEquipmentSlots.size());
		for (const auto& slot : kSyncedEquipmentSlots) {
			armorFormIds.push_back(GetEquippedArmorFormId(a_player, slot.bipedObject));
		}
		const auto armorRecipes = F4T::EquipmentInstance::CaptureEquippedItemMods(a_player, armorFormIds);
		for (std::size_t i = 0; i < kSyncedEquipmentSlots.size(); ++i) {
			RemoteEquippedItem item{ std::string{ kSyncedEquipmentSlots[i].name }, armorFormIds[i], std::nullopt };
			if (armorFormIds[i] != 0) {
				item.mods = armorRecipes[i];
			}
			equippedItems.push_back(std::move(item));
		}

		RE::BGSEquipIndex rightHandEquipIndex;
		rightHandEquipIndex.index = 0;
		const auto rightHandWeaponFormId = GetEquippedWeaponFormId(a_player, rightHandEquipIndex);
		equippedItems.push_back({
			"rightHand",
			rightHandWeaponFormId,
			{}
		});
		if (rightHandWeaponFormId != 0) {
			equippedItems.back().mods = F4T::EquipmentInstance::CaptureEquippedItemMods(a_player, rightHandWeaponFormId);
		}

		if (const auto throwable = F4T::ThrowableSync::CaptureEquippedThrowable(a_player)) {
			equippedItems.push_back(*throwable);
		}

		// TODO-verify(#407)
		if (F4T::DebugLogging::IsEnabled() && equippedItems != a_lastLoggedItems) {
			a_lastLoggedItems = equippedItems;
			F4T::DebugLogging::Log(
				"Equipment",
				"[RT-VERIFY 361-J] capture local equippedItems=[{}] count={}",
				F4T::EquipmentState::DescribeItems(equippedItems),
				equippedItems.size());
		}
		return equippedItems;
	}

	std::string CaptureLocalCharacterName(RE::PlayerCharacter& a_player)
	{
		if (auto name = F4T::UIClient::GetMultiplayerDisplayName(); !name.empty()) return name;
		if (const char* displayName = a_player.GetDisplayFullName()) {
			if (displayName[0] != '\0') {
				return displayName;
			}
		}

		if (const auto* npc = a_player.GetNPC()) {
			const auto fullName = RE::TESFullName::GetFullName(*npc);
			if (!fullName.empty()) {
				return std::string{ fullName };
			}
		}

		const auto fullName = RE::TESFullName::GetFullName(a_player);
		if (!fullName.empty()) {
			return std::string{ fullName };
		}

		return {};
	}

	RemoteAppearance CaptureLocalAppearance(const RE::PlayerCharacter& a_player)
	{
		RemoteAppearance appearance{};

		auto* npc = a_player.GetNPC();
		if (!npc) {
			return appearance;
		}

		if (const auto* race = npc->GetFormRace()) {
			appearance.raceFormId = race->GetFormID();
		}

		appearance.isFemale = npc->GetSex() == RE::SEX::kFemale;
		appearance.height = npc->height;
		appearance.morphWeight.thin = npc->morphWeight.x;
		appearance.morphWeight.muscular = npc->morphWeight.y;
		appearance.morphWeight.large = npc->morphWeight.z;
		appearance.bodyTintColor.r = static_cast<std::uint8_t>(npc->bodyTintColorR);
		appearance.bodyTintColor.g = static_cast<std::uint8_t>(npc->bodyTintColorG);
		appearance.bodyTintColor.b = static_cast<std::uint8_t>(npc->bodyTintColorB);
		appearance.bodyTintColor.a = static_cast<std::uint8_t>(npc->bodyTintColorA);

	if (npc->headRelatedData) {
		if (npc->headRelatedData->hairColor) {
			appearance.hairColorFormId = npc->headRelatedData->hairColor->GetFormID();
		}
		if (npc->headRelatedData->facialHairColor) {
			appearance.facialHairColorFormId = npc->headRelatedData->facialHairColor->GetFormID();
		}
	}

	if (a_player.complexion) {
		appearance.complexionFormId = a_player.complexion->GetFormID();
	}

		for (const auto* headPart : npc->GetHeadParts()) {
			if (headPart) {
				appearance.headPartFormIds.push_back(headPart->GetFormID());
			}
		}

		if (npc->morphSliderValues) {
			for (const auto& morph : *npc->morphSliderValues) {
				if (std::isfinite(morph.second)) {
					appearance.morphSliders.push_back(RemoteAppearanceMorph{ morph.first, morph.second });
				}
			}
		}

		if (npc->morphRegionSliderValues) {
			for (const auto& value : *npc->morphRegionSliderValues) {
				appearance.morphRegionSliders.push_back(std::isfinite(value) ? value : 0.0F);
			}
		}

		if (npc->facialBoneRegionSliderValues) {
			for (const auto& region : *npc->facialBoneRegionSliderValues) {
				const auto& transform = region.second;
				RemoteAppearanceFacialBoneMorph morph{};
				morph.id = region.first;
				morph.position[0] = transform.position.x;
				morph.position[1] = transform.position.y;
				morph.position[2] = transform.position.z;
				morph.rotation[0] = transform.rotation.x;
				morph.rotation[1] = transform.rotation.y;
				morph.rotation[2] = transform.rotation.z;
				morph.scale[0] = transform.scale.x;
				morph.scale[1] = transform.scale.y;
				morph.scale[2] = transform.scale.z;
				appearance.facialBoneMorphs.push_back(morph);
			}
		}

		auto* tintEntries = a_player.tintingData ? a_player.tintingData : npc->tintingData;
		if (tintEntries) {
			for (auto* entry : tintEntries->entriesA) {
				if (!entry) {
					continue;
				}

				RemoteAppearanceTint tint{};
				tint.id = entry->idLink;
				tint.value = static_cast<std::uint8_t>(entry->tintingValue);
				const auto type = entry->GetType();
				tint.type = static_cast<std::uint32_t>(type);
				if (type == RE::BGSCharacterTint::EntryType::kPalette) {
					const auto* palette = static_cast<const RE::BGSCharacterTint::PaletteEntry*>(entry);
					tint.color = palette->tintingColor;
					tint.swatch = palette->swatchID;
					tint.hasColor = true;
				}
				appearance.tints.push_back(tint);
			}
		}

		return appearance;
	}

	// source:CO-BETA/plugin/include/F4TRemotePlayerState.h RemoteEquippedItem is CO wire state, not an engine layout
	bool AreEquipmentStatesEqual(
		const std::vector<RemoteEquippedItem>& a_left,
		const std::vector<RemoteEquippedItem>& a_right)
	{
		return a_left == a_right;
	}

	bool AreAppearanceStatesEqual(const RemoteAppearance& a_left, const RemoteAppearance& a_right)
	{
		if (a_left.version != a_right.version ||
			a_left.raceFormId != a_right.raceFormId ||
			a_left.isFemale != a_right.isFemale ||
			a_left.height != a_right.height ||
			a_left.morphWeight.thin != a_right.morphWeight.thin ||
			a_left.morphWeight.muscular != a_right.morphWeight.muscular ||
			a_left.morphWeight.large != a_right.morphWeight.large ||
			a_left.bodyTintColor.r != a_right.bodyTintColor.r ||
			a_left.bodyTintColor.g != a_right.bodyTintColor.g ||
			a_left.bodyTintColor.b != a_right.bodyTintColor.b ||
			a_left.bodyTintColor.a != a_right.bodyTintColor.a ||
		a_left.hairColorFormId != a_right.hairColorFormId ||
		a_left.facialHairColorFormId != a_right.facialHairColorFormId ||
		a_left.complexionFormId != a_right.complexionFormId ||
		a_left.headPartFormIds != a_right.headPartFormIds ||
			a_left.morphRegionSliders != a_right.morphRegionSliders ||
			a_left.tints != a_right.tints ||
			a_left.morphSliders.size() != a_right.morphSliders.size() ||
			a_left.facialBoneMorphs.size() != a_right.facialBoneMorphs.size()) {
			return false;
		}

		for (std::size_t index = 0; index < a_left.morphSliders.size(); ++index) {
			if (a_left.morphSliders[index].id != a_right.morphSliders[index].id ||
				a_left.morphSliders[index].value != a_right.morphSliders[index].value) {
				return false;
			}
		}

		for (std::size_t index = 0; index < a_left.facialBoneMorphs.size(); ++index) {
			const auto& left = a_left.facialBoneMorphs[index];
			const auto& right = a_right.facialBoneMorphs[index];
			if (left.id != right.id ||
				!std::equal(std::begin(left.position), std::end(left.position), std::begin(right.position)) ||
				!std::equal(std::begin(left.rotation), std::end(left.rotation), std::begin(right.rotation)) ||
				!std::equal(std::begin(left.scale), std::end(left.scale), std::begin(right.scale))) {
				return false;
			}
		}

		return true;
	}
}

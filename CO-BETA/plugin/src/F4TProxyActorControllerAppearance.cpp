// source:CO-BETA/plugin/src/F4TProxyActorController.cpp moved verbatim (RM-10)
#include "pch.h"

#include "F4TProxyActorControllerInternal.h"

using namespace F4T::ProxyActorController::detail;

namespace
{
	bool g_allowSharedProxyBaseAppearanceWrites = false;

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

	void RefreshProxyAfterAppearanceChange(ProxyActorSlot& a_slot, RE::Actor& a_proxy)
	{
		QueueProxyUpdate3D(a_proxy, GetProxyVisibilityUpdate3DFlags());
		a_slot.headMeshRefreshAttempted = false;
		a_slot.headMeshPendingStable = true;
		F4T::ProxyAnimationSync::ResetAppliedAnimationState(a_slot.appliedAnimationState);
		a_slot.hasLoggedInitialAnimationState = false;
	}

	RE::BGSCharacterTint::Template::Entry* ResolveTintTemplateEntry(RE::TESNPC& a_npc, std::uint16_t a_uniqueId)
	{
		const auto* race = a_npc.GetFormRace();
		if (!race) {
			return nullptr;
		}

		const auto sexIndex = a_npc.GetSex() == RE::SEX::kFemale ? 1 : 0;
		const auto* faceData = race->faceRelatedData[sexIndex];
		if (!faceData || !faceData->tintingTemplate) {
			return nullptr;
		}

		for (auto* group : faceData->tintingTemplate->groups) {
			if (!group) {
				continue;
			}
			for (auto* entry : group->entries) {
				if (entry && entry->uniqueID == a_uniqueId) {
					return entry;
				}
			}
		}

		return nullptr;
	}

	template <class T>
	RE::BGSCharacterTint::Entry* MakeTintEntryOfType(const F4T::VtableGuard::AddressRange& a_vtableSection)
	{
		static_assert(std::is_base_of_v<RE::BGSCharacterTint::Entry, T>);

		const auto vtable = T::VTABLE[0].address();
		if (!F4T::VtableGuard::IsPlausibleVtablePointer(vtable, a_vtableSection)) {
			LogThrottledWarning(
				"proxy_tint_vtable_implausible",
				"Refusing to build a character tint entry: its vtable address did not resolve inside the game module. Proxy tinting is disabled this session.");
			return nullptr;
		}

		auto* memory = RE::malloc(sizeof(T));
		if (!memory) {
			return nullptr;
		}

		auto* entry = static_cast<T*>(memory);
		REX::MEM_WRITE_ZERO(entry);
		REX::EMPLACE_VTABLE(entry);
		return entry;
	}

	RE::BGSCharacterTint::Entry* MakeTintEntry(RE::BGSCharacterTint::EntryType a_type)
	{
		const auto module = REX::FModule::GetExecutingModule();
		const auto rdata = module.GetSection(".rdata");
		const F4T::VtableGuard::AddressRange vtableSection{
			rdata.GetAddress(),
			rdata.GetAddress() + rdata.GetSize()
		};

		switch (a_type) {
		case RE::BGSCharacterTint::EntryType::kPalette:
			return MakeTintEntryOfType<RE::BGSCharacterTint::PaletteEntry>(vtableSection);
		case RE::BGSCharacterTint::EntryType::kTexture:
			return MakeTintEntryOfType<RE::BGSCharacterTint::TextureSetEntry>(vtableSection);
		case RE::BGSCharacterTint::EntryType::kMask:
		default:
			return MakeTintEntryOfType<RE::BGSCharacterTint::MaskEntry>(vtableSection);
		}
	}
}

namespace F4T::ProxyActorController::detail
{
	void ResetSlotAppearanceTracking(ProxyActorSlot& a_slot)
	{
		a_slot.hasAppliedAppearanceState = false;
		a_slot.lastAppliedAppearance = {};
	}

	void ResetSlotDisplayNameTracking(ProxyActorSlot& a_slot)
	{
		a_slot.lastAppliedCharacterName.reset();
	}

	void ApplyProxyDisplayNameFromRemoteState(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer)
	{
		if (a_remotePlayer.characterName.empty()) {
			return;
		}

		if (a_slot.lastAppliedCharacterName &&
			*a_slot.lastAppliedCharacterName == a_remotePlayer.characterName) {
			return;
		}

		if (!a_proxy.extraList) {
			LogThrottledWarning(
				"proxy_display_name_extra_list_missing_" + std::to_string(a_remotePlayer.playerId),
				std::format(
					"Could not set display name for remote player {}; proxy extraList unavailable.",
					a_remotePlayer.playerId));
			return;
		}

		a_proxy.extraList->SetOverrideName(a_remotePlayer.characterName.c_str());
		a_slot.lastAppliedCharacterName = a_remotePlayer.characterName;

		LogInfoWithLocalPlayerPrefix(std::format(
			"Set runtime proxy display name for remote player {}: actor={:08X}, characterName={}.",
			a_remotePlayer.playerId,
			a_proxy.GetFormID(),
			a_remotePlayer.characterName));
	}

	void ApplyProxyAppearanceFromRemoteState(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer)
	{
		if (!a_remotePlayer.hasAppearanceUpdate) {
			return;
		}

		if (a_slot.hasAppliedAppearanceState &&
			AreAppearanceStatesEqual(a_slot.lastAppliedAppearance, a_remotePlayer.appearance)) {
			return;
		}

		if (!g_allowSharedProxyBaseAppearanceWrites) {
			a_slot.hasAppliedAppearanceState = true;
			a_slot.lastAppliedAppearance = a_remotePlayer.appearance;
			LogThrottledInfo(
				"proxy_appearance_shared_base_blocked_" + std::to_string(a_remotePlayer.playerId),
				std::format(
					"Detailed appearance apply is skipped for remote player {} because runtime proxies share immutable base forms; sex-specific proxy selection remains active.",
					a_remotePlayer.playerId),
				kProxyNeutralizationInterval);
			return;
		}

		auto* npc = a_proxy.GetNPC();
		if (!npc) {
			LogThrottledWarning(
				"proxy_appearance_npc_missing_" + std::to_string(a_remotePlayer.playerId),
				std::format("Could not apply appearance for remote player {}; proxy has no NPC base.", a_remotePlayer.playerId));
			return;
		}

		const auto& appearance = a_remotePlayer.appearance;
		bool changed = false;

		if (appearance.raceFormId != 0) {
			const auto currentRaceFormId = npc->GetFormRace() ? npc->GetFormRace()->GetFormID() : 0U;
			if (currentRaceFormId != appearance.raceFormId) {
				LogThrottledWarning(
					"proxy_appearance_race_deferred_" + std::to_string(a_remotePlayer.playerId),
					std::format(
						"Appearance for remote player {} requested race {:08X}, but proxy race switching is deferred until multi-race proxy bases are supported.",
						a_remotePlayer.playerId,
						appearance.raceFormId));
			}
		}

		if (std::isfinite(appearance.height) && appearance.height > 0.0F && npc->height != appearance.height) {
			npc->height = appearance.height;
			changed = true;
		}

		if (npc->morphWeight.x != appearance.morphWeight.thin ||
			npc->morphWeight.y != appearance.morphWeight.muscular ||
			npc->morphWeight.z != appearance.morphWeight.large) {
			npc->morphWeight.x = appearance.morphWeight.thin;
			npc->morphWeight.y = appearance.morphWeight.muscular;
			npc->morphWeight.z = appearance.morphWeight.large;
			changed = true;
		}

		const auto tintR = static_cast<std::int8_t>(appearance.bodyTintColor.r);
		const auto tintG = static_cast<std::int8_t>(appearance.bodyTintColor.g);
		const auto tintB = static_cast<std::int8_t>(appearance.bodyTintColor.b);
		const auto tintA = static_cast<std::int8_t>(appearance.bodyTintColor.a);
		if (npc->bodyTintColorR != tintR ||
			npc->bodyTintColorG != tintG ||
			npc->bodyTintColorB != tintB ||
			npc->bodyTintColorA != tintA) {
			npc->bodyTintColorR = tintR;
			npc->bodyTintColorG = tintG;
			npc->bodyTintColorB = tintB;
			npc->bodyTintColorA = tintA;
			changed = true;
		}

		if (appearance.hairColorFormId != 0) {
			if (auto* hairColor = RE::TESForm::GetFormByID<RE::BGSColorForm>(appearance.hairColorFormId)) {
				if (!npc->headRelatedData || npc->headRelatedData->hairColor != hairColor) {
					npc->SetHairColor(hairColor);
					changed = true;
				}
			} else {
				LogThrottledWarning(
					"proxy_appearance_hair_color_missing_" + std::to_string(a_remotePlayer.playerId),
					std::format(
						"Could not apply hair color {:08X} for remote player {}; no local CLFM form resolved.",
						appearance.hairColorFormId,
						a_remotePlayer.playerId));
			}
		}

		if (npc->headRelatedData && appearance.facialHairColorFormId != 0) {
			if (auto* facialHairColor = RE::TESForm::GetFormByID<RE::BGSColorForm>(appearance.facialHairColorFormId)) {
				if (npc->headRelatedData->facialHairColor != facialHairColor) {
					npc->headRelatedData->facialHairColor = facialHairColor;
					changed = true;
				}
			} else {
				LogThrottledWarning(
					"proxy_appearance_facial_hair_color_missing_" + std::to_string(a_remotePlayer.playerId),
					std::format(
						"Could not apply facial hair color {:08X} for remote player {}; no local CLFM form resolved.",
						appearance.facialHairColorFormId,
						a_remotePlayer.playerId));
			}
		}

		std::size_t appliedHeadPartCount = 0;
		if (!appearance.headPartFormIds.empty()) {
			std::vector<RE::BGSHeadPart*> resolvedHeadParts;
			resolvedHeadParts.reserve(appearance.headPartFormIds.size());
			for (std::size_t index = 0; index < appearance.headPartFormIds.size(); ++index) {
				auto* headPart = RE::TESForm::GetFormByID<RE::BGSHeadPart>(appearance.headPartFormIds[index]);
				if (!headPart) {
					LogThrottledWarning(
						"proxy_appearance_headpart_missing_" + std::to_string(a_remotePlayer.playerId) + "_" + std::to_string(index),
						std::format(
							"Could not apply head part {:08X} for remote player {}; no local HDPT form resolved.",
							appearance.headPartFormIds[index],
							a_remotePlayer.playerId));
					continue;
				}
				resolvedHeadParts.push_back(headPart);
			}

			if (!resolvedHeadParts.empty()) {
				const auto desiredCount = resolvedHeadParts.size();
				const bool countMatches =
					npc->headParts && static_cast<std::size_t>(npc->numHeadParts) == desiredCount;

				if (countMatches) {
					for (std::size_t index = 0; index < desiredCount; ++index) {
						if (npc->headParts[index] != resolvedHeadParts[index]) {
							npc->headParts[index] = resolvedHeadParts[index];
							changed = true;
						}
					}
					appliedHeadPartCount = desiredCount;
				} else {
					auto* newHeadParts = RE::calloc<RE::BGSHeadPart*>(desiredCount);
					if (newHeadParts) {
						for (std::size_t index = 0; index < desiredCount; ++index) {
							newHeadParts[index] = resolvedHeadParts[index];
						}
						npc->headParts = newHeadParts;
						npc->numHeadParts = static_cast<std::int8_t>(desiredCount);
						appliedHeadPartCount = desiredCount;
						changed = true;
					} else {
						LogThrottledWarning(
							"proxy_appearance_headpart_alloc_failed_" + std::to_string(a_remotePlayer.playerId),
							std::format(
								"Could not resize head-part array for remote player {}; allocation of {} entries failed.",
								a_remotePlayer.playerId,
								desiredCount));
					}
				}
			}
		}

		if (!appearance.morphSliders.empty()) {
			if (!npc->morphSliderValues) {
				npc->morphSliderValues = new RE::BSTHashMap<std::uint32_t, float>();
			}
			npc->morphSliderValues->clear();
			for (const auto& morph : appearance.morphSliders) {
				if (std::isfinite(morph.value)) {
					npc->morphSliderValues->insert({ morph.id, morph.value });
				}
			}
			changed = true;
		}

		if (!appearance.morphRegionSliders.empty()) {
			if (!npc->morphRegionSliderValues) {
				npc->morphRegionSliderValues = new RE::BSTArray<float>();
			}
			npc->morphRegionSliderValues->clear();
			for (const auto value : appearance.morphRegionSliders) {
				npc->morphRegionSliderValues->push_back(std::isfinite(value) ? value : 0.0F);
			}
			changed = true;
		}

		if (!appearance.facialBoneMorphs.empty()) {
			if (!npc->facialBoneRegionSliderValues) {
				npc->facialBoneRegionSliderValues = new RE::BSTHashMap<std::uint32_t, RE::BGSCharacterMorph::Transform>();
			}
			npc->facialBoneRegionSliderValues->clear();
			for (const auto& morph : appearance.facialBoneMorphs) {
				RE::BGSCharacterMorph::Transform transform{};
				transform.position = { morph.position[0], morph.position[1], morph.position[2] };
				transform.rotation = { morph.rotation[0], morph.rotation[1], morph.rotation[2] };
				transform.scale = { morph.scale[0], morph.scale[1], morph.scale[2] };
				npc->facialBoneRegionSliderValues->insert({ morph.id, transform });
			}
			changed = true;
		}

		std::size_t appliedTintCount = 0;
		if (!appearance.tints.empty()) {
			if (!npc->tintingData) {
				npc->tintingData = new RE::BGSCharacterTint::Entries();
			}

			auto& tintEntries = npc->tintingData->entriesA;
			for (const auto& tint : appearance.tints) {
				const auto entryType = static_cast<RE::BGSCharacterTint::EntryType>(tint.type);

				RE::BGSCharacterTint::Entry* existing = nullptr;
				for (auto* candidate : tintEntries) {
					if (candidate && candidate->idLink == tint.id) {
						existing = candidate;
						break;
					}
				}

				if (existing) {
					if (existing->tintingValue != static_cast<std::int8_t>(tint.value)) {
						existing->tintingValue = static_cast<std::int8_t>(tint.value);
						changed = true;
					}
					if (tint.hasColor && existing->GetType() == RE::BGSCharacterTint::EntryType::kPalette) {
						auto* palette = static_cast<RE::BGSCharacterTint::PaletteEntry*>(existing);
						if (palette->tintingColor != tint.color || palette->swatchID != tint.swatch) {
							palette->tintingColor = tint.color;
							palette->swatchID = tint.swatch;
							changed = true;
						}
					}
					++appliedTintCount;
					continue;
				}

				auto* templateEntry = ResolveTintTemplateEntry(*npc, tint.id);
				if (!templateEntry) {
					LogThrottledWarning(
						"proxy_appearance_tint_template_missing_" + std::to_string(a_remotePlayer.playerId) + "_" + std::to_string(tint.id),
						std::format(
							"Skipped tint {} for remote player {}; no matching race tint template resolved.",
							tint.id,
							a_remotePlayer.playerId));
					continue;
				}

				auto* entry = MakeTintEntry(entryType);
				if (!entry) {
					LogThrottledWarning(
						"proxy_appearance_tint_alloc_failed_" + std::to_string(a_remotePlayer.playerId),
						std::format("Could not allocate tint entry for remote player {}.", a_remotePlayer.playerId));
					continue;
				}

				entry->templateEntry = templateEntry;
				entry->idLink = tint.id;
				entry->tintingValue = static_cast<std::int8_t>(tint.value);
				if (entryType == RE::BGSCharacterTint::EntryType::kPalette) {
					auto* palette = static_cast<RE::BGSCharacterTint::PaletteEntry*>(entry);
					palette->tintingColor = tint.color;
					palette->swatchID = tint.swatch;
				}

				tintEntries.push_back(entry);
				++appliedTintCount;
				changed = true;
			}
		}

		a_slot.hasAppliedAppearanceState = true;
		a_slot.lastAppliedAppearance = appearance;

		if (changed) {
			RefreshProxyAfterAppearanceChange(a_slot, a_proxy);
			LogInfoWithLocalPlayerPrefix(std::format(
				"Applied synced appearance to runtime proxy actor {:08X} for remote player {}: headParts={}/{}, morphs={}, tints={}/{}.",
				a_proxy.GetFormID(),
				a_remotePlayer.playerId,
				appliedHeadPartCount,
				appearance.headPartFormIds.size(),
				appearance.morphSliders.size(),
				appliedTintCount,
				appearance.tints.size()));
		}
	}
}

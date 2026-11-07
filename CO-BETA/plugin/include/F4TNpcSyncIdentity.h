#pragma once

#include "F4TNpcAliasScan.h"
#include "F4TNpcSync.h"
#include "F4TRemoteNpcState.h"

#include <RE/Fallout.h>

#include <cstdint>
#include <chrono>
#include <unordered_set>
#include <utility>

namespace F4T::NpcSync
{
	// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/README.md (W2 review R1, w2r-getnpc-templated-temp-id-og; R2 and w2r-extra-leveled-accessors-og for originalBase at +0x18, NG and AE PARTIAL)
	inline RE::TESActorBase* ResolveStableBase(const RE::Actor& a_actor)
	{
		const auto* reference = a_actor.GetObjectReference();
		if (!reference || reference->GetFormType() != RE::ENUM_FORM_ID::kNPC_) {
			return nullptr;
		}
		auto* npc = a_actor.GetNPC();
		if (!npc) {
			return nullptr;
		}
		if (!RemoteNpcState::IsRuntimeFormId(npc->GetFormID())) {
			return npc;
		}
		if (const auto& extraList = a_actor.extraList) {
			if (const auto* extra = extraList->GetByType<RE::ExtraLeveledCreature>()) {
				auto* original = extra->originalBase;
				if (original && original->GetFormType() == RE::ENUM_FORM_ID::kNPC_ &&
					!RemoteNpcState::IsRuntimeFormId(original->GetFormID())) {
					return original;
				}
			}
		}
		return nullptr;
	}

	inline std::uint32_t StableBaseFormId(const RE::Actor& a_actor)
	{
		const auto* base = ResolveStableBase(a_actor);
		return base ? base->GetFormID() : 0U;
	}

	inline std::uint32_t WireSourceFormId(std::uint32_t a_formId)
	{
		return RemoteNpcState::IsRuntimeFormId(a_formId) ? 0U : a_formId;
	}

	inline bool SourceMatchesActor(std::uint32_t a_sourceFormId, std::uint32_t a_actorFormId)
	{
		return a_sourceFormId == WireSourceFormId(a_actorFormId);
	}

	inline constexpr std::uint32_t kCodsworthPlacedRefFormId = 0x0001CA7D;
	inline constexpr std::size_t kMaxAliasedRefScanPerRefresh = 512;
	inline constexpr auto kAliasedRefScanInterval = std::chrono::seconds(2);

	namespace detail
	{
		inline bool& AliasedRefScanComplete()
		{
			static bool complete{ false };
			return complete;
		}

		inline const std::unordered_set<std::uint32_t>& AliasedRefFormIds()
		{
			static std::unordered_set<std::uint32_t> published;
			static std::unordered_set<std::uint32_t> building;
			static AliasScan::Cursor cursor;
			static std::chrono::steady_clock::time_point refreshed{};

			const auto now = std::chrono::steady_clock::now();
			if (refreshed != std::chrono::steady_clock::time_point{} && now - refreshed < kAliasedRefScanInterval) {
				return published;
			}
			refreshed = now;

			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			if (!dataHandler) {
				return published;
			}

			const bool passComplete = AliasScan::Advance(
				dataHandler->GetFormArray<RE::TESQuest>(),
				cursor,
				kMaxAliasedRefScanPerRefresh,
				[](RE::TESQuest* a_quest) { return a_quest && a_quest->GetActive(); },
				[](RE::TESQuest* a_quest) { return a_quest->aliases.size(); },
				[&](RE::TESQuest* a_quest, std::size_t a_aliasIndex) {
					auto* alias = a_quest->aliases[a_aliasIndex];
					if (!alias) {
						return false;
					}
					RE::ObjectRefHandle handle;
					a_quest->GetAliasedRef(&handle, alias->aliasID);
					if (auto ref = handle.get()) {
						building.insert(ref->GetFormID());
					}
					return true;
				});
			if (passComplete) {
				published = std::move(building);
				building.clear();
				AliasedRefScanComplete() = true;
			}
			return published;
		}
	}

	inline bool IsAliasedRefScanReady()
	{
		detail::AliasedRefFormIds();
		return detail::AliasedRefScanComplete();
	}

	inline bool IsUniqueOrAliasedPlacedRef(std::uint32_t a_formId)
	{
		if (a_formId == 0) {
			return false;
		}
		if (a_formId == kCodsworthPlacedRefFormId) {
			return true;
		}
		return detail::AliasedRefFormIds().contains(a_formId);
	}
}

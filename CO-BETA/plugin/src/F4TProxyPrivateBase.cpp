#include "pch.h"

#include <algorithm>

#include "F4TCrashBreadcrumbs.h"
#include "F4TProxyActorControllerInternal.h"
#include "F4TProxyPrivateBasePolicy.h"
#include "F4TProxySaveSafety.h"

using namespace F4T::ProxyActorController::detail;

namespace
{
	struct PrivateBaseGraveyardEntry
	{
		RE::TESNPC* base;
		RE::ActorHandle proxyHandle;
		RE::ActorHandle pendingSpawnHandle;
	};

	std::vector<PrivateBaseGraveyardEntry> g_privateBaseGraveyard;
	std::vector<RE::TESNPC*> g_parkedPrivateBases;
	std::vector<RE::TESNPC*> g_loadRetainedPrivateBases;

	bool IsBaseOfSex(RE::TESNPC* a_base, bool a_wantFemale)
	{
		return a_base && (a_base->GetSex() == RE::SEX::kFemale) == a_wantFemale;
	}

	std::size_t CountParkedForSex(bool a_wantFemale)
	{
		return static_cast<std::size_t>(std::count_if(g_parkedPrivateBases.begin(), g_parkedPrivateBases.end(), [a_wantFemale](RE::TESNPC* a_base) {
			return IsBaseOfSex(a_base, a_wantFemale);
		}));
	}

	std::size_t CountLivePrivateBasesForSex(bool a_wantFemale)
	{
		auto count = CountParkedForSex(a_wantFemale);
		for (const auto& entry : g_privateBaseGraveyard) {
			if (IsBaseOfSex(entry.base, a_wantFemale)) {
				++count;
			}
		}
		for (const auto& entry : g_proxySlots) {
			if (IsBaseOfSex(entry.second.privateProxyBase, a_wantFemale)) {
				++count;
			}
		}
		return count;
	}

	RE::TESNPC* TakeParkedPrivateBase(bool a_wantFemale)
	{
		const auto found = std::find_if(g_parkedPrivateBases.begin(), g_parkedPrivateBases.end(), [a_wantFemale](RE::TESNPC* a_base) {
			return a_base && (a_base->GetSex() == RE::SEX::kFemale) == a_wantFemale;
		});
		if (found == g_parkedPrivateBases.end()) {
			return nullptr;
		}

		auto* parked = *found;
		g_parkedPrivateBases.erase(found);
		return parked;
	}
}

namespace F4T::ProxyActorController::detail
{
	bool IsPrivateProxyBase(const RE::TESNPC& a_base)
	{
		for (const auto& entry : g_proxySlots) {
			if (entry.second.privateProxyBase == std::addressof(a_base)) {
				return true;
			}
		}
		for (const auto& entry : g_privateBaseGraveyard) {
			if (entry.base == std::addressof(a_base)) {
				return true;
			}
		}
		return std::ranges::contains(g_parkedPrivateBases, std::addressof(a_base));
	}

	RE::TESNPC* DuplicateSharedProxyBase(RE::TESNPC& a_sharedBase)
	{
		auto* duplicate = a_sharedBase.CreateDuplicateForm(false, nullptr);
		auto* base = duplicate ? duplicate->As<RE::TESNPC>() : nullptr;
		if (!base) {
			if (duplicate) {
				LogThrottledWarning(
					"proxy_private_base_not_npc_" + std::to_string(duplicate->GetFormID()),
					std::format("Duplicated proxy base {:08X} is not an NPC; the duplicate stays engine-owned.", duplicate->GetFormID()));
			}
			return nullptr;
		}
		base->faceNPC = a_sharedBase.GetRootFaceNPC();
		return base;
	}

	void ReleaseSlotPrivateProxyBase(ProxyActorSlot& a_slot)
	{
		if (!a_slot.privateProxyBase) {
			return;
		}
		LogInfoWithLocalPlayerPrefix(std::format(
			"Queued a private proxy base of remote player {} for release once its proxy actor is gone.",
			a_slot.remotePlayerId));
		F4T::CrashBreadcrumbs::Record(F4T::CrashBreadcrumbs::Code::kPrivateBaseQueued, 0, a_slot.remotePlayerId);
		g_privateBaseGraveyard.push_back({ a_slot.privateProxyBase, a_slot.proxyHandle, a_slot.pendingSpawnHandle });
		a_slot.privateProxyBase = nullptr;
	}

	void ParkPrivateBase(RE::TESNPC& a_base)
	{
		const bool wantFemale = a_base.GetSex() == RE::SEX::kFemale;
		const auto parkedForSex = CountParkedForSex(wantFemale);
		if (!F4T::ProxyPrivateBasePolicy::MayParkPrivateBase(parkedForSex)) {
			F4T::CrashBreadcrumbs::Record(F4T::CrashBreadcrumbs::Code::kPrivateBaseLeaked, a_base.GetFormID(), static_cast<std::uint32_t>(parkedForSex));
			LogThrottledWarning(
				"proxy_private_base_park_full",
				std::format("Parked private proxy base list is full for one sex ({} parked); the copy stays allocated and is not reused.", parkedForSex));
			return;
		}
		g_parkedPrivateBases.push_back(std::addressof(a_base));
		F4T::CrashBreadcrumbs::Record(F4T::CrashBreadcrumbs::Code::kPrivateBaseParked, a_base.GetFormID(), static_cast<std::uint32_t>(parkedForSex + 1));
	}

	void SweepPrivateProxyBaseGraveyard()
	{
		std::erase_if(g_privateBaseGraveyard, [](PrivateBaseGraveyardEntry& a_entry) {
			if (a_entry.proxyHandle.get() || a_entry.pendingSpawnHandle.get()) {
				return false;
			}
			if (a_entry.base) {
				ParkPrivateBase(*a_entry.base);
			}
			return true;
		});
	}

	RE::TESNPC* AttachParkedPrivateBase(ProxyActorSlot& a_slot, RE::TESNPC& a_parked)
	{
		a_slot.privateProxyBase = std::addressof(a_parked);
		a_slot.privateProxyBaseBuildFailed = false;
		a_slot.hasAppliedAppearanceState = false;
		F4T::CrashBreadcrumbs::Record(F4T::CrashBreadcrumbs::Code::kPrivateBaseReused, a_parked.GetFormID(), a_slot.remotePlayerId);
		LogInfoWithLocalPlayerPrefix(std::format(
			"Reused parked private proxy base {:08X} for remote player {}.",
			a_parked.GetFormID(),
			a_slot.remotePlayerId));
		return std::addressof(a_parked);
	}

	RE::TESNPC* EnsureSlotPrivateProxyBase(ProxyActorSlot& a_slot, bool a_wantFemale)
	{
		if (auto* existing = a_slot.privateProxyBase) {
			if ((existing->GetSex() == RE::SEX::kFemale) == a_wantFemale) {
				return existing;
			}
			ReleaseSlotPrivateProxyBase(a_slot);
		}

		if (a_slot.privateProxyBaseBuildFailed) {
			return nullptr;
		}

		if (auto* parked = TakeParkedPrivateBase(a_wantFemale)) {
			return AttachParkedPrivateBase(a_slot, *parked);
		}

		if (!F4T::ProxyPrivateBasePolicy::MayBuildPrivateBase(CountLivePrivateBasesForSex(a_wantFemale))) {
			SweepPrivateProxyBaseGraveyard();
			if (auto* reclaimed = TakeParkedPrivateBase(a_wantFemale)) {
				return AttachParkedPrivateBase(a_slot, *reclaimed);
			}
			a_slot.privateProxyBaseBuildFailed = true;
			F4T::CrashBreadcrumbs::Record(F4T::CrashBreadcrumbs::Code::kPrivateBaseCapped, 0, a_wantFemale ? 1U : 0U);
			LogThrottledWarning(
				"proxy_private_base_capped",
				std::format("Private proxy base cap reached for one sex; remote player {} falls back to the shared base until its slot resets.", a_slot.remotePlayerId));
			return nullptr;
		}

		auto* sharedBase = ResolveProxyActorBaseForSex(a_wantFemale);
		if (!sharedBase) {
			a_slot.privateProxyBaseBuildFailed = true;
			return nullptr;
		}

		auto* base = DuplicateSharedProxyBase(*sharedBase);
		if (!base) {
			a_slot.privateProxyBaseBuildFailed = true;
			LogThrottledWarning(
				"proxy_private_base_duplicate_failed_" + std::to_string(a_slot.remotePlayerId),
				std::format(
					"Could not duplicate proxy base {:08X} for remote player {}; the proxy keeps the shared base.",
					sharedBase->GetFormID(),
					a_slot.remotePlayerId));
			return nullptr;
		}

		a_slot.privateProxyBase = base;
		F4T::CrashBreadcrumbs::Record(F4T::CrashBreadcrumbs::Code::kPrivateBaseBuilt, base->GetFormID(), a_slot.remotePlayerId);
		LogInfoWithLocalPlayerPrefix(std::format(
			"Built private proxy base {:08X} for remote player {} from shared base {:08X}.",
			base->GetFormID(),
			a_slot.remotePlayerId,
			sharedBase->GetFormID()));
		return base;
	}
}

namespace F4T::ProxySaveSafety
{
	void DropPrivateProxyBasesWithoutFreeing()
	{
		for (auto& [_, slot] : g_proxySlots) {
			slot.privateProxyBase = nullptr;
		}
		F4T::CrashBreadcrumbs::Record(
			F4T::CrashBreadcrumbs::Code::kLoadDrop,
			0,
			static_cast<std::uint32_t>(g_privateBaseGraveyard.size() + g_parkedPrivateBases.size()));
		for (const auto& entry : g_privateBaseGraveyard) {
			if (entry.base) {
				g_loadRetainedPrivateBases.push_back(entry.base);
			}
		}
		g_loadRetainedPrivateBases.insert(g_loadRetainedPrivateBases.end(), g_parkedPrivateBases.begin(), g_parkedPrivateBases.end());
		g_privateBaseGraveyard.clear();
		g_parkedPrivateBases.clear();
	}
}

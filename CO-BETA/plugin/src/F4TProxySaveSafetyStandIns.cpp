#include "pch.h"

#include <mutex>
#include <unordered_set>

#include "F4TDebugLogging.h"
#include "F4TProxySaveSafety.h"

namespace
{
	constexpr std::size_t kMaxSeenStandInFormIds = 16384;
	constexpr auto kProxySaveSafetyComponent = "ProxySaveSafety";

	std::mutex g_standInMutex;
	std::vector<std::uint32_t> g_standInFormIds;
	std::unordered_set<std::uint32_t> g_standInFormIdsSeenThisSession;
}

namespace F4T::ProxySaveSafety
{
	bool IsStandIn(std::uint32_t a_formId)
	{
		std::scoped_lock lock{ g_standInMutex };
		return std::ranges::contains(g_standInFormIds, a_formId);
	}

	void PruneStandInFormIds()
	{
		std::scoped_lock lock{ g_standInMutex };
		const auto before = g_standInFormIds.size();
		const auto dropped = std::erase_if(g_standInFormIds, [](std::uint32_t a_formId) {
			auto* ref = RE::TESForm::GetFormByID<RE::TESObjectREFR>(a_formId);
			const bool resolved = ref != nullptr;
			const bool created = ref && ref->IsCreated();
			const bool deleted = ref && ref->IsDeleted();
			const bool keep = F4T::ProxySaveSafety::ShouldKeepStandIn(resolved, created, deleted);
			if (!keep) {
				F4T::DebugLogging::Log(
					kProxySaveSafetyComponent,
					"stand-in prune drop actor={:08X} resolved={} created={} deleted={}",
					a_formId,
					resolved,
					created,
					deleted);
			}
			return !keep;
		});
		F4T::DebugLogging::Log(
			kProxySaveSafetyComponent,
			"stand-in prune before={} after={} dropped={} cap={}",
			before,
			g_standInFormIds.size(),
			dropped,
			F4T::ProxySaveSafety::kMaximumStandInFormIds);
	}

	void ForgetStandIn(std::uint32_t a_formId)
	{
		std::scoped_lock lock{ g_standInMutex };
		if (std::erase(g_standInFormIds, a_formId) != 0) {
			F4T::DebugLogging::Log(
				kProxySaveSafetyComponent,
				"stand-in forgotten actor={:08X} count={}",
				a_formId,
				g_standInFormIds.size());
		}
	}

	void RecordStandIn(std::uint32_t a_formId)
	{
		std::scoped_lock lock{ g_standInMutex };
		const auto threadId = REX::W32::GetCurrentThreadId();
		F4T::DebugLogging::Log(
			kProxySaveSafetyComponent,
			"[RT-VERIFY 408-E/F/G] RecordStandIn actor={:08X} thread={} count={}",
			a_formId,
			threadId,
			g_standInFormIds.size());
		if (std::ranges::contains(g_standInFormIds, a_formId)) {
			return;
		}
		if (!F4T::ProxySaveSafety::CanRecordStandIn(g_standInFormIds.size())) {
			F4T::DebugLogging::Log(
				kProxySaveSafetyComponent,
				"stand-in list is full at {} ids; actor={:08X} is not recorded, so it is kept as a leftover unless its base is a CO custom proxy base",
				kMaximumStandInFormIds,
				a_formId);
			return;
		}
		if (g_standInFormIdsSeenThisSession.size() >= kMaxSeenStandInFormIds) {
			F4T::DebugLogging::Log(
				kProxySaveSafetyComponent,
				"reuse-check set reached {} ids; cleared",
				kMaxSeenStandInFormIds);
			g_standInFormIdsSeenThisSession.clear();
		}
		const auto reused = g_standInFormIdsSeenThisSession.contains(a_formId);
		g_standInFormIdsSeenThisSession.insert(a_formId);
		g_standInFormIds.push_back(a_formId);
		if (reused) {
			// TODO-verify(#407): a forgotten FormID is reused by a new created ref source:rt-briefs/thread-408.md (comment 20722)
			F4T::DebugLogging::Log(
				kProxySaveSafetyComponent,
				"runtime-unknown [RT-VERIFY 408-D] reused id actor={:08X} thread={}",
				a_formId,
				threadId);
		}
		F4T::DebugLogging::Log(
			kProxySaveSafetyComponent,
			"stand-in recorded actor={:08X} thread={} count={}",
			a_formId,
			threadId,
			g_standInFormIds.size());
	}

	std::vector<std::uint32_t> StandInFormIdsForSave()
	{
		std::scoped_lock lock{ g_standInMutex };
		F4T::DebugLogging::Log(
			kProxySaveSafetyComponent,
			"stand-in save count={}",
			g_standInFormIds.size());
		return g_standInFormIds;
	}

	void LoadStandInFormIds(std::vector<std::uint32_t> a_formIds)
	{
		std::scoped_lock lock{ g_standInMutex };
		g_standInFormIdsSeenThisSession.insert(a_formIds.begin(), a_formIds.end());
		g_standInFormIds = std::move(a_formIds);
	}

	void ClearStandInFormIds()
	{
		std::scoped_lock lock{ g_standInMutex };
		g_standInFormIds.clear();
		g_standInFormIdsSeenThisSession.clear();
	}

	std::size_t StandInFormIdCount()
	{
		std::scoped_lock lock{ g_standInMutex };
		return g_standInFormIds.size();
	}
}

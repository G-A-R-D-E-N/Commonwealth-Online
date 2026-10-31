#include "pch.h"

#include "F4TNpcSync.h"

#include "F4TNpcSyncInternal.h"
#include "F4TNpcSyncRegistry.h"

#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace F4T::NpcSync
{
	namespace
	{
		struct RegistrySnapshot
		{
			std::unordered_map<std::uint32_t, PuppetIdentity> puppetsByActorFormId;
			std::unordered_map<std::uint32_t, std::uint32_t> actorFormIdsByNpcId;
			std::unordered_map<std::uint32_t, HostNpcIdentity> hostNpcsByActorFormId;
			std::unordered_map<std::uint32_t, std::uint32_t> hostActorFormIdsByNpcId;
			std::unordered_set<std::uint32_t> suppressedFormIds;
		};

		std::shared_mutex g_registryMutex;
		RegistrySnapshot g_registrySnapshot;
		std::uint64_t g_registryGeneration{ 1 };
		std::uint64_t g_publishedGeneration{ 0 };
	}

	std::optional<PuppetIdentity> TryGetPuppetByActorFormId(std::uint32_t a_actorFormId)
	{
		const std::shared_lock lock(g_registryMutex);
		const auto found = g_registrySnapshot.puppetsByActorFormId.find(a_actorFormId);
		if (found == g_registrySnapshot.puppetsByActorFormId.end()) {
			return std::nullopt;
		}
		return found->second;
	}

	std::optional<std::uint32_t> TryGetPuppetActorFormIdByNpcId(std::uint32_t a_npcId)
	{
		const std::shared_lock lock(g_registryMutex);
		const auto found = g_registrySnapshot.actorFormIdsByNpcId.find(a_npcId);
		if (found == g_registrySnapshot.actorFormIdsByNpcId.end()) {
			return std::nullopt;
		}
		return found->second;
	}

	std::optional<HostNpcIdentity> TryGetHostNpcByActorFormId(std::uint32_t a_actorFormId)
	{
		const std::shared_lock lock(g_registryMutex);
		const auto found = g_registrySnapshot.hostNpcsByActorFormId.find(a_actorFormId);
		if (found == g_registrySnapshot.hostNpcsByActorFormId.end()) {
			return std::nullopt;
		}
		return found->second;
	}

	std::optional<std::uint32_t> TryGetHostActorFormIdByNpcId(std::uint32_t a_npcId)
	{
		const std::shared_lock lock(g_registryMutex);
		const auto found = g_registrySnapshot.hostActorFormIdsByNpcId.find(a_npcId);
		if (found == g_registrySnapshot.hostActorFormIdsByNpcId.end()) {
			return std::nullopt;
		}
		return found->second;
	}

	bool IsPuppetFormId(std::uint32_t a_formId)
	{
		const std::shared_lock lock(g_registryMutex);
		return g_registrySnapshot.puppetsByActorFormId.contains(a_formId);
	}

	bool IsSuppressedFormId(std::uint32_t a_formId)
	{
		const std::shared_lock lock(g_registryMutex);
		return g_registrySnapshot.suppressedFormIds.contains(a_formId);
	}

	namespace Detail
	{
		void MarkRegistryDirty()
		{
			++g_registryGeneration;
		}

		void PublishRegistrySnapshot()
		{
			if (g_publishedGeneration == g_registryGeneration) {
				return;
			}
			g_publishedGeneration = g_registryGeneration;

			RegistrySnapshot snapshot;
			for (const auto& [_, slot] : g_puppetSlots) {
				if (const auto actor = slot.handle.get()) {
					snapshot.puppetsByActorFormId.emplace(
						actor->GetFormID(),
						PuppetIdentity{ slot.npcId, slot.sourceFormId, slot.baseFormId, slot.cellId, slot.worldspaceId });
					snapshot.actorFormIdsByNpcId.emplace(slot.npcId, actor->GetFormID());
				}
			}
			for (const auto& [actorFormId, npcId] : g_hostNpcIdsByActorFormId) {
				snapshot.hostNpcsByActorFormId.emplace(actorFormId, HostNpcIdentity{ npcId, actorFormId });
				snapshot.hostActorFormIdsByNpcId.emplace(npcId, actorFormId);
			}
			for (const auto& [actorFormId, _] : g_suppressedActors) {
				snapshot.suppressedFormIds.insert(actorFormId);
			}

			const std::unique_lock lock(g_registryMutex);
			g_registrySnapshot = std::move(snapshot);
		}
	}
}

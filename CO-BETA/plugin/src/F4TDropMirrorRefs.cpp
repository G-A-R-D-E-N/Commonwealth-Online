#include "pch.h"

#include "F4TDropMirrorRefs.h"
#include "F4TInventorySync.h"
#include "F4TNetworkingInternal.h"
#include "RE/N/NEW_REFR_DATA.h"
#include "RE/T/TESDataHandler.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <map>

namespace F4T::DropMirrorRefs
{
	namespace
	{
		using EntityId = F4T::InventorySync::EntityId;
		constexpr std::size_t kWorkPerTick = 8;
		constexpr std::size_t kMaximumRetries = 256;
		constexpr bool kMirrorDropForcePersist = true;

		struct Work
		{
			F4T::Drops::MirrorDiffEntry entry;
			bool removed{};
			bool retry{};
		};

		std::map<EntityId, Work> g_work;
		std::map<EntityId, RE::ObjectRefHandle> g_refs;
		std::uint64_t g_generation{};
		EntityId g_cursor{};

		void Warn(const char* a_key, const char* a_message)
		{
			F4T::Networking::Detail::LogThrottledWarning(a_key, a_message);
		}

		std::uint32_t Hex(const std::string& a_text)
		{
			std::uint32_t result{};
			const auto parsed = std::from_chars(a_text.data(), a_text.data() + a_text.size(), result, 16);
			return parsed.ec == std::errc{} && parsed.ptr == a_text.data() + a_text.size() ? result : 0;
		}

		RE::TESBoundObject* ResolveItem(const nlohmann::json& a_item, RE::TESDataHandler& a_handler)
		{
			const auto plugin = a_item.at("plugin").get<std::string>();
			const auto local = Hex(a_item.at("localFormId").get<std::string>());
			std::uint32_t fullId{};
			if (const auto* file = a_handler.LookupLoadedModByName(plugin)) {
				if (file->compileIndex == 0xFF || local > 0xFFFFFF) {
					return nullptr;
				}
				fullId = (static_cast<std::uint32_t>(file->compileIndex) << 24) | local;
			} else if (const auto* file = a_handler.LookupLoadedLightModByName(plugin)) {
				if (local > 0xFFF) {
					return nullptr;
				}
				fullId = 0xFE000000 | (static_cast<std::uint32_t>(file->smallFileCompileIndex) << 12) | local;
			} else {
				return nullptr;
			}
			return RE::TESForm::GetFormByID<RE::TESBoundObject>(fullId);
		}

		void Despawn(EntityId a_entityId)
		{
			const auto found = g_refs.find(a_entityId);
			if (found != g_refs.end()) {
				if (const auto ref = found->second.get()) {
					ref->Disable();
					ref->MarkAsDeleted();
				}
				g_refs.erase(found);
			}
			F4T::InventorySync::UnbindMirrorEntity(a_entityId);
		}

		enum class SpawnResult { Done, Deferred };

		SpawnResult Materialize(const F4T::Drops::MirrorDiffEntry& a_entry)
		{
			auto* handler = RE::TESDataHandler::GetSingleton();
			auto* object = handler ? ResolveItem(a_entry.object.item, *handler) : nullptr;
			if (!object) {
				Warn("drop_mirror_missing_item", "Mirrored drop skipped: plugin not loaded or base form missing.");
				return SpawnResult::Done;
			}
			const auto& position = a_entry.object.position;
			auto* cell = RE::TESForm::GetFormByID<RE::TESObjectCELL>(Hex(position.cellId));
			if (!cell || !cell->IsAttached()) {
				return SpawnResult::Deferred;
			}
			if (cell->IsExterior() && (!cell->worldSpace || cell->worldSpace->GetFormID() != Hex(position.worldspaceId))) {
				Warn("drop_mirror_world_mismatch", "Mirrored drop skipped: cell and worldspace do not match.");
				return SpawnResult::Done;
			}
			RE::NEW_REFR_DATA data;
			data.object = object;
			data.location = { position.x, position.y, position.z };
			data.direction = { 0.0F, 0.0F, 0.0F };
			data.reference = nullptr;
			data.interior = cell->IsExterior() ? nullptr : cell;
			data.world = cell->IsExterior() ? cell->worldSpace : nullptr;
			data.extra = nullptr;
			data.maxLevel = 0;
			data.forcePersist = kMirrorDropForcePersist;
			data.clearStillLoadingFlag = true;
			data.initializeScripts = true;
			data.initiallyDisabled = false;
			auto handle = handler->CreateReferenceAtLocation(data);
			if (const auto ref = handle.get()) {
				const auto count = a_entry.object.item.at("count").get<std::uint32_t>();
				if (count > 1) {
					constexpr auto maximum = static_cast<std::uint32_t>(std::numeric_limits<std::int16_t>::max());
					if (count > maximum) {
						Warn("drop_mirror_count_clamped", "Mirrored drop count clamped to INT16_MAX for ExtraDataList::SetCount.");
					}
					auto* extraList = ref->extraList.get();
					if (!extraList) {
						ref->extraList = RE::BSTSmartPointer<RE::ExtraDataList>{ new RE::ExtraDataList() };
						extraList = ref->extraList.get();
					}
					if (extraList) {
						extraList->SetCount(static_cast<std::int16_t>(std::min(count, maximum)));
						ref->AddChange(0x400);
					} else {
						Warn("drop_mirror_extra_missing", "Mirrored drop reference has no extra data list; stack count left unset.");
					}
				}
				if (g_refs.contains(a_entry.entityId)) {
					Despawn(a_entry.entityId);
				}
				g_refs[a_entry.entityId] = handle;
				F4T::InventorySync::BindMirrorEntity(a_entry.entityId, ref->GetFormID());
			} else {
				Warn("drop_mirror_create_failed", "Mirrored drop reference creation failed.");
				return SpawnResult::Deferred;
			}
			return SpawnResult::Done;
		}
	}

	void Tick()
	{
		std::uint64_t generation{};
		auto diff = F4T::InventorySync::TakeDropMirrorDiff(generation);
		if (generation != g_generation) {
			g_work.clear();
			g_generation = generation;
		}
		for (const auto entityId : diff.removed) {
			g_work[entityId] = Work{ { entityId, {}, false }, true };
		}
		for (const auto* entries : { &diff.added, &diff.changed }) {
			for (const auto& entry : *entries) {
				if (!entry.materialize) {
					if (g_refs.contains(entry.entityId)) {
						Despawn(entry.entityId);
					}
					g_work.erase(entry.entityId);
					continue;
				}
				g_work[entry.entityId] = Work{ entry };
			}
		}
		for (const auto entityId : diff.removed) {
			const auto found = g_work.find(entityId);
			if (found != g_work.end() && !found->second.removed) {
				Despawn(entityId);
			}
		}
		std::size_t retries = std::count_if(g_work.begin(), g_work.end(), [](const auto& pair) { return pair.second.retry; });
		const auto budget = std::min(kWorkPerTick, g_work.size());
		for (std::size_t processed = 0; processed < budget && !g_work.empty(); ++processed) {
			auto it = g_work.upper_bound(g_cursor);
			if (it == g_work.end()) {
				it = g_work.begin();
			}
			g_cursor = it->first;
			auto& work = it->second;
			if (work.removed) {
				Despawn(it->first);
			} else if (Materialize(work.entry) == SpawnResult::Deferred) {
				if (work.retry) {
					continue;
				}
				if (retries < kMaximumRetries) {
					work.retry = true;
					++retries;
					continue;
				}
				Warn("drop_mirror_retry_full", "Mirrored drop deferred queue is full; the drop stays queued until a retry slot opens.");
				continue;
			}
			if (work.retry) {
				retries -= 1;
			}
			g_work.erase(it);
		}
	}
}

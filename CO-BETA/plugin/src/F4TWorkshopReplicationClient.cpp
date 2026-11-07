#include "pch.h"

#include "F4TWorkshopReplicationClient.h"

#include "F4TCampRegistration.h"
#include "F4TClientIdentity.h"
#include "F4TFormIdentity.h"
#include "F4TInventoryPickupGate.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TQuestFormKey.h"
#include "F4TQuestLogic.h"
#include "F4TRemotePlayerState.h"
#include "F4TSettlementAccessClient.h"
#include "F4TWorkshopDoorLock.h"
#include "F4TWorkshopProtocol.h"
#include "F4TWorkshopRaids.h"
#include "F4TWorkshopReplicationInternal.h"
#include "F4TWorkshopWorldRefs.h"
#include "RE/N/NEW_REFR_DATA.h"
#include "RE/S/SendHUDMessage.h"
#include "RE/T/TESDataHandler.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace F4T::WorkshopReplicationClient
{
	using namespace F4T::Networking::Detail;
	using Json = nlohmann::json;

	namespace
	{
		constexpr std::chrono::seconds kResendAfter{ 5 };
		constexpr std::size_t kMaximumPending = 64;
		constexpr std::size_t kMaximumMappedObjects = 8192;
		constexpr std::size_t kMaximumInbound = 4096;
		constexpr bool kRemoteForcePersist = false;
		constexpr std::string_view kCampKitPlugin = "commonwealthonline.esp";
		constexpr std::uint32_t kCampKitLocalFormId = 0x00997F;

		struct PendingFade
		{
			Workshop::BuildZoneId buildZoneId{};
			Workshop::ObjectId objectId{};
			std::chrono::steady_clock::time_point despawnAt{};
		};

		std::vector<PendingFade> g_pendingFades;
		std::optional<Workshop::BuildZoneId> g_activeZone;
		constexpr std::chrono::milliseconds kDestroyFadeMs{ 1500 };

		std::deque<std::pair<std::string, Json>> g_inbound;

		[[nodiscard]] std::optional<Workshop::FormKey> FormKeyFromFormId(std::uint32_t a_formId)
		{
			const auto loadOrder = QuestFormKey::GetLoadOrder();
			const auto key = QuestKey::FromFormId(a_formId, loadOrder);
			if (!key) {
				return std::nullopt;
			}
			return Workshop::FormKey{ key->plugin, key->localId };
		}

		[[nodiscard]] std::optional<std::uint32_t> FormIdFromKey(const Workshop::FormKey& a_key)
		{
			const auto loadOrder = QuestFormKey::GetLoadOrder();
			// TODO-verify: 0xFE light-plugin load-order lookup unverified
			return QuestKey::ToFormId(QuestKey::Key{ a_key.plugin, a_key.localFormId }, loadOrder);
		}

		[[nodiscard]] RE::TESBoundObject* ResolveBoundObject(const Workshop::FormKey& a_form)
		{
			const auto formId = FormIdFromKey(a_form);
			if (!formId) {
				return nullptr;
			}
			return RE::TESForm::GetFormByID<RE::TESBoundObject>(*formId);
		}

		bool MoveRef(RE::TESObjectREFR& a_ref, const Workshop::Transform& a_transform)
		{
			a_ref.data.location = RE::NiPoint3A{ a_transform.x, a_transform.y, a_transform.z };
			a_ref.data.angle.x = a_transform.pitch;
			a_ref.data.angle.y = a_transform.roll;
			a_ref.data.angle.z = a_transform.yaw;
			a_ref.SetScale(a_transform.scale);
			a_ref.Update3DPosition(true);
			return true;
		}

		std::optional<RE::ObjectRefHandle> SpawnRemote(const Workshop::ObjectState& a_object)
		{
			auto* handler = RE::TESDataHandler::GetSingleton();
			auto* object = handler ? ResolveBoundObject(a_object.form) : nullptr;
			if (!handler || !object) {
				Warn("workshop_spawn_missing_form", "Workshop remote spawn skipped: base form unresolved.");
				return std::nullopt;
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cell = player ? player->GetParentCell() : nullptr;
			if (!cell || !cell->IsAttached()) {
				Warn("workshop_spawn_no_cell", "Workshop remote spawn skipped: no attached player cell.");
				return std::nullopt;
			}
			RE::NEW_REFR_DATA data;
			data.object = object;
			data.location = { a_object.transform.x, a_object.transform.y, a_object.transform.z };
			data.direction = { a_object.transform.pitch, a_object.transform.roll, a_object.transform.yaw };
			data.reference = nullptr;
			data.interior = cell->IsExterior() ? nullptr : cell;
			data.world = cell->IsExterior() ? cell->worldSpace : nullptr;
			data.extra = nullptr;
			data.maxLevel = 0;
			data.forcePersist = kRemoteForcePersist;
			data.clearStillLoadingFlag = true;
			data.initializeScripts = a_object.form == kCampWorkbenchForm;
			data.initiallyDisabled = false;
			auto handle = handler->CreateReferenceAtLocation(data);
			if (!handle.get()) {
				Warn("workshop_spawn_refused", "Workshop remote spawn skipped: engine refused the reference.");
				return std::nullopt;
			}
			if (auto ref = handle.get()) {
				MoveRef(*ref, a_object.transform);
			}
			return handle;
		}

		void NoteDamaged(ZoneState& a_zone, Workshop::ObjectId a_objectId, float a_health, float a_maxHealth)
		{
			if (a_health < a_maxHealth) {
				a_zone.damagedObjects.insert(a_objectId);
			} else {
				a_zone.damagedObjects.erase(a_objectId);
			}
		}

		void ApplyObject(ZoneState& a_zone, const Workshop::ObjectState& a_object, bool a_isLocalCommit)
		{
			if (a_isLocalCommit) {
				return;
			}
			NoteDamaged(a_zone, a_object.objectId, a_object.health, a_object.maxHealth);
			if (a_zone.objects.size() >= kMaximumMappedObjects && !a_zone.objects.contains(a_object.objectId)) {
				Warn("workshop_map_full", "Workshop object map full; remote spawn skipped.");
				return;
			}
			auto found = a_zone.objects.find(a_object.objectId);
			if (found != a_zone.objects.end()) {
				if (auto ref = found->second.get()) {
					MoveRef(*ref, a_object.transform);
				}
				return;
			}
			if (auto handle = SpawnRemote(a_object)) {
				a_zone.objects.emplace(a_object.objectId, std::move(*handle));
			}
		}

		void SpawnCampWorkbench(ZoneState& a_zone)
		{
			auto& handle = a_zone.objects[F4T::WorkshopRaids::WorkbenchObjectId];
			const auto* player = RE::PlayerCharacter::GetSingleton();
			if (!handle.get() && player && SpaceOf(*player) == a_zone.campWorkbench->space) { handle = SpawnRemote(*a_zone.campWorkbench).value_or(RE::ObjectRefHandle{}); a_zone.campBenchOwned = false; a_zone.benchClaimTries = 0; }
		}

		void RemoveObject(ZoneState& a_zone, Workshop::ObjectId a_objectId)
		{
			const auto found = a_zone.objects.find(a_objectId);
			if (found == a_zone.objects.end()) {
				return;
			}
			Despawn(found->second);
			a_zone.objects.erase(found);
			a_zone.doors.erase(a_objectId);
			a_zone.damagedObjects.erase(a_objectId);
		}

		void FinishSnapshot(ZoneState& a_zone)
		{
			std::map<Workshop::ObjectId, RE::ObjectRefHandle> keep;
			for (const auto& [objectId, state] : a_zone.pendingSnapshot) {
				auto existing = a_zone.objects.find(objectId);
				if (existing != a_zone.objects.end()) {
					if (auto ref = existing->second.get()) { MoveRef(*ref, state.transform); }
					keep.emplace(objectId, std::move(existing->second));
					a_zone.objects.erase(existing);
				} else if (auto handle = SpawnRemote(state)) {
					keep.emplace(objectId, std::move(*handle));
				}
			}
			if (a_zone.campWorkbench) {
				if (auto found = a_zone.objects.find(F4T::WorkshopRaids::WorkbenchObjectId); found != a_zone.objects.end()) {
					keep.emplace(found->first, std::move(found->second));
					a_zone.objects.erase(found);
				}
			}
			for (auto& [objectId, handle] : a_zone.objects) { Despawn(handle); }
			a_zone.objects = std::move(keep);
			for (const auto& [objectId, state] : a_zone.pendingTombstones) {
				RemoveObject(a_zone, objectId);
				// TODO-verify: snapshot tombstone hide unverified in game
				if (const auto formId = FormIdFromKey(state.form)) { WorkshopWorldRefs::Hide(*formId, state.transform); }
			}
			std::erase_if(a_zone.doors, [&a_zone](const auto& a_door) { return !a_zone.objects.contains(a_door.first); });
			a_zone.pendingSnapshot.clear();
			a_zone.pendingTombstones.clear();
			a_zone.assemblingSnapshot = false;
		}

		bool HandleCommit(const Json& a_packet)
		{
			const auto commit = F4T::WorkshopProtocol::NormalizeCommit(a_packet);
			if (!commit) {
				return false;
			}
			std::scoped_lock lock(g_mutex);
			g_activeZone = commit->buildZoneId;
			auto& zone = g_zones[commit->buildZoneId];
			zone.revision = commit->revision;
			const auto localPlayer = F4T::RemotePlayerState::GetAssignedPlayerId();
			const bool localCommit = localPlayer && *localPlayer == commit->playerId;
			if (const auto pending = g_pending.find(commit->operationId); localCommit && pending != g_pending.end()) {
				if (pending->second.kind == PendingKind::Place && commit->object) {
					zone.objects[commit->objectId] = pending->second.localHandle;
				} else if (pending->second.kind == PendingKind::Scrap) {
					RemoveObject(zone, commit->objectId);
					Despawn(pending->second.localHandle);
				} else if (pending->second.kind == PendingKind::Move && commit->object) {
					zone.objects[commit->objectId] = pending->second.localHandle;
					if (auto ref = pending->second.localHandle.get()) {
						MoveRef(*ref, commit->object->transform);
					}
				}
				g_pending.erase(pending);
			} else if (commit->kind == Workshop::OperationKind::Scrap) {
				RemoveObject(zone, commit->objectId);
			} else if (commit->kind == Workshop::OperationKind::ScrapWorld) {
				RemoveObject(zone, commit->objectId);
				// TODO-verify: scrapWorld placed-ref hide unverified in game
				if (commit->object) {
					if (const auto formId = FormIdFromKey(commit->object->form)) { WorkshopWorldRefs::Hide(*formId, commit->object->transform); }
				}
			} else if (commit->object) {
				ApplyObject(zone, *commit->object, localCommit);
			}
			return true;
		}

		bool HandleSnapshot(const Json& a_packet)
		{
			const auto snapshot = F4T::WorkshopProtocol::NormalizeSnapshot(a_packet);
			if (!snapshot) {
				return false;
			}
			std::scoped_lock lock(g_mutex);
			g_activeZone = snapshot->buildZoneId;
			if (snapshot->unchanged) {
				return true;
			}
			auto& zone = g_zones[snapshot->buildZoneId];
			if (!zone.assemblingSnapshot) {
				zone.pendingSnapshot.clear();
				zone.pendingTombstones.clear();
				zone.assemblingSnapshot = true;
			}
			for (const auto& object : snapshot->objects) {
				zone.pendingSnapshot[object.objectId] = object;
			}
			for (const auto& tombstone : snapshot->tombstones) {
				zone.pendingTombstones[tombstone.objectId] = tombstone;
			}
			zone.revision = snapshot->revision;
			if (snapshot->complete) {
				FinishSnapshot(zone);
			}
			return true;
		}

		bool HandleStructureDestroyed(const Json& a_packet)
		{
			const auto zoneId = a_packet.find("buildZoneId");
			const auto objectId = a_packet.find("objectId");
			const auto revision = a_packet.find("revision");
			if (zoneId == a_packet.end() || objectId == a_packet.end() || !zoneId->is_number_unsigned() ||
				!objectId->is_number_unsigned()) {
				return false;
			}
			std::scoped_lock lock(g_mutex);
			auto& zone = g_zones[zoneId->get<Workshop::BuildZoneId>()];
			if (revision != a_packet.end() && revision->is_number_unsigned()) {
				zone.revision = revision->get<Workshop::Revision>();
			}
			const auto id = objectId->get<Workshop::ObjectId>();
			PushStructureDestroyedHud(zoneId->get<Workshop::BuildZoneId>(), id);
			zone.crackStages[id] = 2;
			g_pendingFades.push_back(PendingFade{ zoneId->get<Workshop::BuildZoneId>(), id, std::chrono::steady_clock::now() + kDestroyFadeMs });
			return true;
		}

		bool HandleStructureDamage(const Json& a_packet)
		{
			const auto zoneId = a_packet.find("buildZoneId");
			const auto objectId = a_packet.find("objectId");
			if (zoneId == a_packet.end() || objectId == a_packet.end() || !zoneId->is_number_unsigned() ||
				!objectId->is_number_unsigned()) {
				return false;
			}
			std::uint8_t stage = 0;
			if (const auto damageStage = a_packet.find("damageStage"); damageStage != a_packet.end() && damageStage->is_number_unsigned()) {
				stage = static_cast<std::uint8_t>(damageStage->get<std::uint32_t>());
			} else {
				const auto health = a_packet.find("health");
				const auto maxHealth = a_packet.find("maxHealth");
				if (health != a_packet.end() && maxHealth != a_packet.end() && health->is_number() && maxHealth->is_number()) {
					const float hp = health->get<float>();
					const float maxHp = maxHealth->get<float>();
					if (maxHp > 0.0F) {
						const float ratio = hp / maxHp;
						stage = ratio <= 0.25F ? 2 : (ratio <= 0.50F ? 1 : 0);
					}
				}
			}
			PushStructureDamageHud(a_packet);
			std::scoped_lock lock(g_mutex);
			auto& zone = g_zones[zoneId->get<Workshop::BuildZoneId>()];
			zone.crackStages[objectId->get<Workshop::ObjectId>()] = stage;
			const auto healthFound = a_packet.find("health");
			const auto maxHealthFound = a_packet.find("maxHealth");
			if (healthFound != a_packet.end() && maxHealthFound != a_packet.end() && healthFound->is_number() && maxHealthFound->is_number()) {
				NoteDamaged(zone, objectId->get<Workshop::ObjectId>(), healthFound->get<float>(), maxHealthFound->get<float>());
			}
			return true;
		}
	}

	bool EnqueuePending(Pending a_pending)
	{
		std::scoped_lock lock(g_mutex);
		if (g_pending.size() >= kMaximumPending) {
			Warn("workshop_pending_full", "Workshop pending queue full; op not sent.");
			return false;
		}
		const auto packet = F4T::WorkshopProtocol::MakeOperation(a_pending.request);
		if (!packet) {
			F4T::DebugLogging::Log("Workshop", "operation {} not sent: packet could not be built", a_pending.request.operationId);
			return false;
		}
		const auto now = std::chrono::steady_clock::now();
		a_pending.sentAt = now;
		a_pending.lastSendAt = now;
		g_pending[a_pending.request.operationId] = std::move(a_pending);
		QueuePacket(*packet);
		return true;
	}

	void Warn(const char* a_key, std::string_view a_message)
	{
		LogThrottledWarning(a_key, a_message);
	}

	[[nodiscard]] Workshop::Transform TransformOf(const RE::TESObjectREFR& a_ref)
	{
		const auto& angle = a_ref.data.angle;
		const auto position = a_ref.GetPosition();
		return Workshop::Transform{
			position.x,
			position.y,
			position.z,
			angle.x,
			angle.z,
			angle.y,
			static_cast<float>(a_ref.refScale) / 100.0F
		};
	}

	[[nodiscard]] std::optional<Workshop::FormKey> SpaceOf(const RE::TESObjectREFR& a_ref)
	{
		if (const auto* cell = a_ref.GetParentCell()) {
			if (cell->IsExterior()) {
				if (cell->worldSpace) {
					return FormKeyFromFormId(cell->worldSpace->GetFormID());
				}
			}
			return FormKeyFromFormId(cell->GetFormID());
		}
		return std::nullopt;
	}

	[[nodiscard]] RE::ObjectRefHandle HandleOf(const RE::TESObjectREFR& a_ref)
	{
		return const_cast<RE::TESObjectREFR&>(a_ref).GetHandle();
	}

	void Despawn(RE::ObjectRefHandle& a_handle)
	{
		if (const auto ref = a_handle.get()) {
			ref->Disable();
			ref->MarkAsDeleted();
		}
		a_handle = {};
	}

	void RollbackPending(Pending& a_pending)
	{
		switch (a_pending.kind) {
		case PendingKind::Place:
			Despawn(a_pending.localHandle);
			break;
		case PendingKind::Move:
			if (a_pending.hasPriorTransform) {
				if (auto ref = a_pending.localHandle.get()) {
					MoveRef(*ref, a_pending.priorTransform);
				}
			}
			break;
		case PendingKind::Scrap:
			break;
		}
	}

	bool IsCampKitForm(const Workshop::FormKey& a_form) noexcept
	{
		return a_form.plugin == kCampKitPlugin && a_form.localFormId == kCampKitLocalFormId;
	}

	bool IsCampKitRef(const RE::TESObjectREFR& a_ref)
	{
		const auto form = a_ref.GetObjectReference() ? FormKeyFromFormId(a_ref.GetObjectReference()->GetFormID()) : std::nullopt;
		return form && IsCampKitForm(*form);
	}

	void QueuePacket(Json a_packet)
	{
		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			return;
		}
		taskInterface->AddTask([packet = std::move(a_packet)]() {
			if (!F4T::Networking::IsConnectedToServer()) {
				Warn("workshop_op_offline", "Dropped workshopOp: not connected.");
				return;
			}
			if (!SendPacketBytes(packet.dump())) {
				Warn("workshop_op_send_failed", "Could not send workshopOp.");
			}
		});
	}

	void ApplyCampWorkbench(const Workshop::ObjectState& a_workbench, bool a_own)
	{
		std::scoped_lock lock(g_mutex);
		auto& zoneState = g_zones[a_workbench.buildZoneId];
		if (auto ref = zoneState.objects[F4T::WorkshopRaids::WorkbenchObjectId].get(); ref && zoneState.campWorkbench && zoneState.campWorkbench->space == a_workbench.space) { MoveRef(*ref, a_workbench.transform); }
		else if (ref) { Despawn(zoneState.objects[F4T::WorkshopRaids::WorkbenchObjectId]); }
		zoneState.campWorkbench = a_workbench;
		if (a_own) { g_ownCamps.insert(a_workbench.buildZoneId); } else { g_ownCamps.erase(a_workbench.buildZoneId); }
		SpawnCampWorkbench(zoneState);
		F4T::CampRegistration::RequestRegistration(a_workbench.buildZoneId, a_workbench.transform, a_own);
	}

	static bool IsInboundType(std::string_view a_type)
	{
		return a_type == "workshopCommit" || a_type == "workshopReject" || a_type == "workshopSnapshot" ||
			a_type == "structureDestroyed" || a_type == "structureDamage" || a_type == "doorState" ||
			a_type == "campCreated" || a_type == "campMoved" || a_type == "campRejected" || a_type == "campStored" ||
			a_type == "campRemoved" || a_type == "campList" || a_type == "campRefund" || a_type == "campGrant";
	}

	static bool DispatchInbound(std::string_view a_type, const Json& a_packet)
	{
		if (a_type == "workshopCommit") {
			return HandleCommit(a_packet);
		}
		if (a_type == "workshopReject") {
			return HandleReject(a_packet);
		}
		if (a_type == "workshopSnapshot") {
			return HandleSnapshot(a_packet);
		}
		if (a_type == "structureDestroyed") {
			return HandleStructureDestroyed(a_packet);
		}
		if (a_type == "structureDamage") {
			return HandleStructureDamage(a_packet);
		}
		if (a_type == "doorState") {
			return HandleDoorState(a_packet);
		}
		if (a_type == "campCreated" || a_type == "campMoved") { return HandleCampPlacement(a_packet); }
		if (a_type == "campRejected") { return HandleCampRejected(a_packet); }
		if (a_type == "campStored" || a_type == "campRemoved") {
			const auto zone = a_packet.find("buildZoneId");
			if (zone == a_packet.end() || !zone->is_number_unsigned()) {
				return false;
			}
			const auto buildZoneId = zone->get<Workshop::BuildZoneId>();
			if (const auto removed = a_packet.find("operationId"); removed != a_packet.end() && removed->is_number_unsigned()) {
				ResolvePendingCampRemove(removed->get<std::uint64_t>());
			}
			if (const auto operation = a_packet.find("operationId"); operation != a_packet.end() && operation->is_number_unsigned() && ResolvePendingCampStore(operation->get<std::uint64_t>(), buildZoneId)) {
				RE::SendHUDMessage::ShowHUDMessage("Camp packed up.", nullptr, true, true);
				RE::SendHUDMessage::ShowHUDMessage("The camp kit was dropped at your feet.", nullptr, true, true);
			}
			std::scoped_lock lock(g_mutex);
			if (auto found = g_zones.find(buildZoneId); found != g_zones.end()) {
				for (auto& entry : found->second.objects) { Despawn(entry.second); }
				g_zones.erase(found);
			}
			F4T::CampRegistration::Forget(buildZoneId);
			g_ownCamps.erase(buildZoneId);
			if (g_activeZone && *g_activeZone == buildZoneId) {
				g_activeZone.reset();
			}
			return true;
		}
		if (a_type == "campList") { return HandleCampList(a_packet); }
		if (a_type == "campRefund") { return HandleCampRefund(a_packet); }
		if (a_type == "campGrant") { return HandleCampGrant(a_packet); }
		return false;
	}

	static void DrainInbound()
	{
		std::deque<std::pair<std::string, Json>> inbound;
		{
			std::scoped_lock lock(g_mutex);
			inbound.swap(g_inbound);
		}
		for (const auto& [type, packet] : inbound) {
			DispatchInbound(type, packet);
		}
	}

	bool TryHandlePacket(std::string_view a_type, const Json& a_packet)
	{
		if (!IsInboundType(a_type)) { return false; }
		std::scoped_lock lock(g_mutex);
		if (g_inbound.size() >= kMaximumInbound) {
			Warn("workshop_inbound_full", "Workshop inbound queue full; packet dropped.");
			return true;
		}
		g_inbound.emplace_back(std::string{ a_type }, a_packet);
		return true;
	}

	void Reset()
	{
		ForgetCampStores();
		ForgetCampGrants();
		std::scoped_lock lock(g_mutex);
		for (auto& [zoneId, zone] : g_zones) {
			(void)zoneId;
			for (auto& [objectId, handle] : zone.objects) {
				(void)objectId;
				Despawn(handle);
			}
		}
		WorkshopWorldRefs::Restore();
		g_zones.clear();
		g_pending.clear();
		g_pendingCamps.clear();
		g_ownCamps.clear();
		g_pendingFades.clear();
		g_inbound.clear();
		g_nextDoorPoll = {};
		g_activeZone.reset();
	}

	void Tick()
	{
		WorkshopDoorLock::EnsureActivateSink();
		DrainInbound();
		std::vector<Pending> resend;
		std::vector<Pending> expired;
		std::vector<PendingFade> faded;
		std::vector<Json> doorOps;
		{
			std::scoped_lock lock(g_mutex);
			const auto now = std::chrono::steady_clock::now();
			for (auto it = g_pending.begin(); it != g_pending.end();) {
				if (now - it->second.sentAt >= kPendingTimeout) {
					expired.push_back(std::move(it->second));
					it = g_pending.erase(it);
					continue;
				}
				if (now - it->second.lastSendAt >= kResendAfter) {
					it->second.lastSendAt = now;
					resend.push_back(it->second);
				}
				++it;
			}
			std::erase_if(g_pendingCamps, [now](const auto& a_entry) {
				if (now - a_entry.second.sentAt < kPendingTimeout) { return false; }
				F4T::InventoryPickupGate::RefuseCamp(a_entry.second.dropEntityId);
				Warn("camp_placement_timeout", "Camp placement timed out; camp kit refused.");
				RE::SendHUDMessage::ShowHUDMessage("Camp setup timed out. Try again.", nullptr, true, true);
				return true;
			});
			for (auto& [zoneId, zone] : g_zones) { (void)zoneId; if (zone.campWorkbench) { SpawnCampWorkbench(zone); } }
			for (auto it = g_pendingFades.begin(); it != g_pendingFades.end();) {
				if (now >= it->despawnAt) {
					faded.push_back(*it);
					it = g_pendingFades.erase(it);
					continue;
				}
				++it;
			}
			for (const auto& fade : faded) {
				if (auto zone = g_zones.find(fade.buildZoneId); zone != g_zones.end()) {
					zone->second.crackStages.erase(fade.objectId);
					RemoveObject(zone->second, fade.objectId);
				}
			}
			CollectDoorOps(doorOps, now);
		}
		TickCampStores();
		TickCampGrants();
		for (auto& packet : doorOps) {
			QueuePacket(std::move(packet));
		}
		for (auto& pending : expired) {
			RollbackPending(pending);
			Warn("workshop_op_timeout", "Workshop op timed out; rolled back locally.");
		}
		for (const auto& pending : resend) {
			if (const auto packet = F4T::WorkshopProtocol::MakeOperation(pending.request)) {
				QueuePacket(*packet);
			}
		}
	}

	void NoteActiveBuildZone(Workshop::BuildZoneId a_buildZoneId)
	{
		if (a_buildZoneId == 0) {
			return;
		}
		std::scoped_lock lock(g_mutex);
		g_activeZone = a_buildZoneId;
	}

	std::optional<Workshop::BuildZoneId> ActiveBuildZoneId()
	{
		std::scoped_lock lock(g_mutex);
		return g_activeZone;
	}

	std::optional<Workshop::Revision> ZoneRevision(Workshop::BuildZoneId a_buildZoneId)
	{
		std::scoped_lock lock(g_mutex);
		const auto found = g_zones.find(a_buildZoneId);
		if (found == g_zones.end()) {
			return std::nullopt;
		}
		return found->second.revision;
	}

	std::vector<Workshop::ObjectId> DamagedObjectIds(Workshop::BuildZoneId a_buildZoneId)
	{
		std::scoped_lock lock(g_mutex);
		const auto found = g_zones.find(a_buildZoneId);
		if (found == g_zones.end()) {
			return {};
		}
		return std::vector<Workshop::ObjectId>{ found->second.damagedObjects.begin(), found->second.damagedObjects.end() };
	}

	bool SubmitPlace(const RE::TESObjectREFR& a_ref)
	{
		const auto zoneId = ActiveBuildZoneId();
		const auto form = a_ref.GetObjectReference() ? FormKeyFromFormId(a_ref.GetObjectReference()->GetFormID()) : std::nullopt;
		const auto space = SpaceOf(a_ref);
		if (!zoneId || !form || !space) {
			F4T::DebugLogging::Log("Workshop", "place skipped: no active zone, form or space, form {}", form ? std::format("{}:{:08X}", form->plugin, form->localFormId) : std::string{ "unavailable" });
			return false;
		}
		const auto revision = ZoneRevision(*zoneId).value_or(0);
		Workshop::Request request;
		request.buildZoneId = *zoneId;
		request.expectedRevision = revision;
		request.kind = Workshop::OperationKind::Place;
		request.form = *form;
		request.space = *space;
		request.transform = TransformOf(a_ref);
		{
			std::scoped_lock lock(g_mutex);
			request.operationId = g_nextOperationId++;
		}
		Pending pending;
		pending.kind = PendingKind::Place;
		pending.request = request;
		pending.localHandle = HandleOf(a_ref);
		return EnqueuePending(std::move(pending));
	}

	std::optional<Workshop::FormKey> SpaceKeyOf(const RE::TESObjectREFR& a_ref)
	{
		return SpaceOf(a_ref);
	}

	std::optional<std::uint32_t> FormIdOfKey(const Workshop::FormKey& a_key)
	{
		return FormIdFromKey(a_key);
	}
}

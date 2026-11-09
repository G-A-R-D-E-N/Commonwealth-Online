#include "pch.h"

#include "F4TParkedSuitSync.h"

#include "F4TParkedSuit.h"
#include "F4TParkedSuitInternal.h"
#include "F4TParkedSuitMirror.h"
#include "F4TParkedSuitSyncInternal.h"
#include "F4TParkedSuitUi.h"
#include "F4TPowerArmorScript.h"

#include "F4TDebugLogging.h"
#include "F4TNetworking.h"

#include "RE/T/TESForm.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESObjectREFR.h"
#include "RE/T/TESWorldSpace.h"

#include "RE/P/PlayerCharacter.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace F4T::ParkedSuitSync
{
	using namespace detail;

	namespace
	{
		constexpr std::uint32_t kFurnitureTypePowerArmorKeywordFormId = 0x0003430B;
		constexpr std::size_t kMaximumSpawnedParks = 4;
		// Constraint: the server rejects a park beyond kMaximumObjectReach (512) of the sender's last position; spawned frames stay within 400.
		constexpr float kSpawnedFrameReachSquared = 400.0F * 400.0F;
		constexpr auto kGoneFrameConfirmation = std::chrono::seconds{ 30 };

		struct Occupied
		{
			F4T::PowerArmorSync::OccupiedFrame frame;
			F4T::PowerArmorSync::PowerArmorState powerArmor;
		};

		// Constraint: g_occupied, g_inFrame and g_missingOwnFrames are touched only from PublishLocal and OnWorldReset on the game thread.
		std::optional<Occupied> g_occupied;
		bool g_inFrame{ false };
		std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> g_missingOwnFrames;
		std::atomic<bool> g_resync{ true };

		nlohmann::json PositionJson(const F4T::PowerArmorSync::OccupiedFrame& a_frame)
		{
			return nlohmann::json{
				{ "cellId", Hex(a_frame.cellId) },
				{ "worldspaceId", a_frame.worldspaceId == 0 ? std::string{} : Hex(a_frame.worldspaceId) },
				{ "x", a_frame.x },
				{ "y", a_frame.y },
				{ "z", a_frame.z }
			};
		}

		bool SendPark(const Occupied& a_occupied, bool a_consoleSpawned)
		{
			const auto powerArmor = F4T::PowerArmorSync::Encode(a_occupied.powerArmor);
			if (!powerArmor) {
				return false;
			}
			auto packet = WithPieceKeys(nlohmann::json{
				{ "type", "parkedSuit" },
				{ "action", "park" },
				{ "localId", Hex(a_occupied.frame.refFormId) },
				{ "powerArmor", *powerArmor },
				{ "position", PositionJson(a_occupied.frame) },
				{ "angleZ", a_occupied.frame.angleZ } }, a_occupied.powerArmor.pieces);
			if (a_consoleSpawned) {
				packet["console"] = true;
			}
			Enqueue(PendingTransition{
				std::move(packet),
				"park",
				a_occupied.frame.refFormId,
				a_consoleSpawned });
			FlushPending();
			return true;
		}

		void SendRemove(std::uint32_t a_localId, std::uint32_t a_frameBaseFormId)
		{
			{
				const std::scoped_lock lock(g_mutex);
				const auto stale = std::any_of(g_owners.begin(), g_owners.end(), [&](const auto& a_owner) {
					return a_frameBaseFormId != 0 && a_owner.second.mine && a_owner.second.localId == a_localId && a_owner.second.frameBaseFormId != 0 && a_owner.second.frameBaseFormId != a_frameBaseFormId;
				});
				if (stale) {
					return;
				}
				for (auto owner = g_owners.begin(); owner != g_owners.end();) {
					if (owner->second.mine && owner->second.localId == a_localId) {
						g_hackResults.erase(owner->first);
						owner = g_owners.erase(owner);
					} else {
						++owner;
					}
				}
			}
			F4T::ParkedSuitUi::NotifyStateChanged();
			Enqueue(PendingTransition{
				nlohmann::json{
					{ "type", "parkedSuit" },
					{ "action", "remove" },
					{ "localId", Hex(a_localId) } },
				"remove",
				a_localId });
			FlushPending();
		}

		std::optional<std::uint32_t> GraduatedSuit(std::uint32_t a_localId)
		{
			const std::scoped_lock lock(g_mutex);
			const auto found = g_graduated.find(a_localId);
			if (found != g_graduated.end()) {
				return found->second;
			}
			const auto carried = g_graduatedBeforeReconnect.find(a_localId);
			if (carried != g_graduatedBeforeReconnect.end()) {
				return carried->second;
			}
			return std::nullopt;
		}

		bool AdoptMirror(std::uint32_t a_localId)
		{
			const auto mirrored = F4T::ParkedSuitMirror::SuitForFrame(a_localId);
			const auto suitId = mirrored ? mirrored : GraduatedSuit(a_localId);
			if (!suitId) {
				return false;
			}
			{
				const std::scoped_lock lock(g_mutex);
				const auto owner = g_owners.find(*suitId);
				if (owner == g_owners.end() || !owner->second.mine) {
					return false;
				}
				if (g_adopting.contains(*suitId)) {
					return true;
				}
				g_graduated[a_localId] = *suitId;
				g_adopting.emplace(*suitId, false);
			}
			if (mirrored) {
				F4T::ParkedSuitMirror::Graduate(*suitId);
			}
			Enqueue(PendingTransition{
				nlohmann::json{
					{ "type", "parkedSuit" },
					{ "action", "adopt" },
					{ "suitId", Hex(*suitId) },
					{ "localId", Hex(a_localId) } },
				"adopt",
				a_localId });
			FlushPending();
			return true;
		}

		std::vector<RE::TESObjectREFR*> PowerArmorFramesInCell()
		{
			std::vector<RE::TESObjectREFR*> frames;
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cell = player ? player->GetParentCell() : nullptr;
			auto* keyword = RE::TESForm::GetFormByID<RE::BGSKeyword>(kFurnitureTypePowerArmorKeywordFormId);
			if (!cell || !keyword) {
				return frames;
			}
			cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
				if (!a_ref || a_ref->IsDeleted()) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				auto* base = a_ref->GetObjectReference();
				auto* furniture = base && base->GetFormType() == RE::ENUM_FORM_ID::kFURN ? base->As<RE::TESFurniture>() : nullptr;
				if (furniture && furniture->HasKeyword(keyword)) {
					frames.push_back(a_ref);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			return frames;
		}

		// Constraint: an activated frame opens the breach menu only when it is the mirror of a suit that is not ours.
		void OnFrameActivated(RE::TESObjectREFR& a_ref)
		{
			const auto suitId = F4T::ParkedSuitMirror::SuitForFrame(a_ref.GetFormID());
			if (!suitId) {
				return;
			}
			{
				const std::scoped_lock lock(g_mutex);
				const auto owner = g_owners.find(*suitId);
				if (owner == g_owners.end() || owner->second.mine) {
					return;
				}
				g_target = *suitId;
				++g_targetSequence;
			}
			F4T::ParkedSuitUi::Open();
		}

		class ActivateSink final : public RE::BSTEventSink<RE::TESActivateEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent& a_event, RE::BSTEventSource<RE::TESActivateEvent>*) override
			{
				auto* ref = a_event.objectActivated.get();
				if (ref && a_event.actionRef.get() == RE::PlayerCharacter::GetSingleton() && IsPowerArmorFrame(*ref)) {
					OnFrameActivated(*ref);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		} g_activateSink;

		void RegisterActivateSink()
		{
			static bool registered = false;
			if (registered) {
				return;
			}
			if (auto* source = RE::TESActivateEvent::GetEventSource()) {
				source->RegisterSink(&g_activateSink);
				registered = true;
			}
		}

		bool ApplyLock(std::uint32_t a_suitId, std::uint32_t a_localId, bool a_locked, RE::TESNPC* a_ownerBase, std::optional<std::uint32_t> a_expectedBase)
		{
			{
				const std::scoped_lock lock(g_mutex);
				const auto applied = g_appliedLocked.find(a_suitId);
				if (applied != g_appliedLocked.end() && applied->second.locked == a_locked && applied->second.localId == a_localId) {
					return true;
				}
			}
			auto* frame = RE::TESForm::GetFormByID<RE::TESObjectREFR>(a_localId);
			if (!frame) {
				const std::scoped_lock lock(g_mutex);
				g_appliedLocked.erase(a_suitId);
				return false;
			}
			if (a_expectedBase) {
				const auto* base = frame->GetObjectReference();
				if (!base || base->GetFormID() != *a_expectedBase) {
					return false;
				}
			}
			if (!F4T::PowerArmorScript::SetFrameLocked(*frame, a_locked)) {
				return false;
			}
			if (a_ownerBase && !F4T::PowerArmorScript::SetFrameOwner(*frame, a_ownerBase)) {
				return false;
			}
			const std::scoped_lock lock(g_mutex);
			g_appliedLocked[a_suitId] = AppliedLock{ a_localId, a_locked };
			return true;
		}

		void ReconcileFrames(RE::TESNPC* a_playerBase)
		{
			const auto owners = [] {
				const std::scoped_lock lock(g_mutex);
				return g_owners;
			}();
			for (const auto& [suitId, placed] : owners) {
				if (!placed.localId) {
					continue;
				}
				if (!placed.mine) {
					ApplyLock(suitId, *placed.localId, true, nullptr, placed.frameBaseFormId);
				} else if (a_playerBase) {
					ApplyLock(suitId, *placed.localId, false, a_playerBase, placed.frameBaseFormId);
				}
			}
			std::vector<std::pair<std::uint32_t, std::uint32_t>> stale;
			{
				const std::scoped_lock lock(g_mutex);
				for (const auto& [suitId, applied] : g_appliedLocked) {
					if (!g_owners.contains(suitId)) {
						stale.emplace_back(suitId, applied.localId);
					}
				}
			}
			for (const auto& [suitId, localId] : stale) {
				if (ApplyLock(suitId, localId, false, nullptr, std::nullopt)) {
					const std::scoped_lock lock(g_mutex);
					g_appliedLocked.erase(suitId);
				}
			}
		}

		void ReconcileMirrors(RE::PlayerCharacter& a_player)
		{
			std::vector<F4T::ParkedSuitMirror::MirrorRequest> requests;
			{
				const std::scoped_lock lock(g_mutex);
				for (const auto& [suitId, placed] : g_owners) {
					if (g_adopting.contains(suitId)) {
						continue;
					}
					if (placed.localId && FrameResolvesToBase(*placed.localId, placed.frameBaseFormId)) {
						continue;
					}
					// Constraint: a suit of ours whose own frame is gone gets no mirror; RemoveGoneOwnParks removes it instead.
					if (placed.mine) {
						continue;
					}
					requests.push_back(F4T::ParkedSuitMirror::MirrorRequest{ suitId, placed.mine, placed.frameBaseFormId, placed.cellId, placed.worldspaceId, placed.x, placed.y, placed.z, placed.angleZ, placed.powerArmor });
				}
			}
			F4T::ParkedSuitMirror::Reconcile(a_player, requests);
		}

		void RetryDroppedParks()
		{
			std::unordered_map<std::uint32_t, nlohmann::json> dropped;
			{
				const std::scoped_lock lock(g_mutex);
				if (g_droppedParks.empty() || g_pending.size() >= kMaximumPendingTransitions) {
					return;
				}
				dropped = g_droppedParks;
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			const auto origin = player->GetPosition();
			const auto occupied = F4T::PowerArmorSync::CaptureOccupiedFrame(*player);
			std::unordered_set<std::uint32_t> present;
			std::unordered_set<std::uint32_t> settled;
			for (auto* ref : PowerArmorFramesInCell()) {
				const auto refId = ref->GetFormID();
				present.insert(refId);
				const auto found = dropped.find(refId);
				if (found == dropped.end()) {
					continue;
				}
				const auto position = ref->GetPosition();
				const auto dx = position.x - origin.x;
				const auto dy = position.y - origin.y;
				const auto dz = position.z - origin.z;
				const auto* base = ref->GetObjectReference();
				bool known{};
				bool room{};
				{
					const std::scoped_lock lock(g_mutex);
					known = std::any_of(g_owners.begin(), g_owners.end(), [&](const auto& a_owner) { return a_owner.second.mine && a_owner.second.localId == refId; });
					room = g_pending.size() < kMaximumPendingTransitions;
				}
				if (known || !base || (occupied && occupied->refFormId == refId) || dx * dx + dy * dy + dz * dz > kSpawnedFrameReachSquared) {
					settled.insert(refId);
					continue;
				}
				if (!room) {
					continue;
				}
				const auto frame = F4T::PowerArmorSync::CaptureFrameFromReference(*ref);
				auto packet = found->second;
				packet["position"] = PositionJson(frame);
				packet["angleZ"] = frame.angleZ;
				Enqueue(PendingTransition{ std::move(packet), "park", refId, false });
				FlushPending();
			}
			const std::scoped_lock lock(g_mutex);
			std::erase_if(g_droppedParks, [&](const auto& a_entry) {
				return settled.contains(a_entry.first) || !present.contains(a_entry.first);
			});
		}

		// Constraint: a remove for a frame needs a local record of it, either an owner entry or a queued park.
		bool HasLocalParkRecord(std::uint32_t a_localId)
		{
			const std::scoped_lock lock(g_mutex);
			const auto owned = std::any_of(g_owners.begin(), g_owners.end(), [&](const auto& a_owner) { return a_owner.second.mine && a_owner.second.localId == a_localId; });
			const auto queued = std::any_of(g_pending.begin(), g_pending.end(), [&](const PendingTransition& a_transition) { return a_transition.op == "park" && a_transition.localId == a_localId; });
			return owned || queued;
		}

		bool OwnFrameLive(std::uint32_t a_localId, std::uint32_t a_frameBaseFormId)
		{
			auto* frame = RE::TESForm::GetFormByID<RE::TESObjectREFR>(a_localId);
			return frame && !frame->IsDeleted() && FrameResolvesToBase(a_localId, a_frameBaseFormId);
		}

		// Constraint: an owner's parked suit is removed only after its frame has been missing for kGoneFrameConfirmation while the player stands in the frame's cell, and one removal goes out per tick.
		void RemoveGoneOwnParks(RE::PlayerCharacter& a_player)
		{
			const auto* cell = a_player.GetParentCell();
			std::vector<std::pair<std::uint32_t, std::uint32_t>> candidates;
			if (cell) {
				const std::scoped_lock lock(g_mutex);
				for (const auto& [suitId, placed] : g_owners) {
					if (placed.mine && placed.localId && placed.cellId == cell->GetFormID()) {
						candidates.emplace_back(*placed.localId, placed.frameBaseFormId);
					}
				}
			}
			std::erase_if(g_missingOwnFrames, [&](const auto& a_entry) {
				return std::none_of(candidates.begin(), candidates.end(), [&](const auto& a_candidate) { return a_candidate.first == a_entry.first; });
			});
			if (!cell) {
				return;
			}
			const auto occupied = F4T::PowerArmorSync::CaptureOccupiedFrame(a_player);
			const auto now = std::chrono::steady_clock::now();
			for (const auto& [localId, frameBaseFormId] : candidates) {
				if ((occupied && occupied->refFormId == localId) || OwnFrameLive(localId, frameBaseFormId)) {
					g_missingOwnFrames.erase(localId);
					continue;
				}
				const auto since = g_missingOwnFrames.try_emplace(localId, now).first->second;
				if (now - since < kGoneFrameConfirmation) {
					continue;
				}
				g_missingOwnFrames.erase(localId);
				SendRemove(localId, 0);
				return;
			}
		}
	}

	void RemoveOwnSuit(std::uint32_t a_suitId)
	{
		std::optional<std::uint32_t> localId;
		{
			const std::scoped_lock lock(g_mutex);
			const auto owner = g_owners.find(a_suitId);
			if (owner != g_owners.end() && owner->second.mine) {
				localId = owner->second.localId;
			}
		}
		if (localId) {
			SendRemove(*localId, 0);
		}
	}

	void OnWorldReset()
	{
		g_occupied.reset();
		g_inFrame = false;
		g_missingOwnFrames.clear();
		const std::scoped_lock lock(g_mutex);
		DiscardParksLocked();
		g_droppedParks.clear();
	}

	void PublishLocal(RE::PlayerCharacter& a_player, const std::optional<F4T::PowerArmorSync::PowerArmorState>& a_captured)
	{
		RegisterActivateSink();
		FlushPending();
		const bool connected = F4T::Networking::IsConnectedToServer();
		ReconcileFrames(a_player.GetNPC());
		ReconcileMirrors(a_player);
		if (connected) {
			RemoveGoneOwnParks(a_player);
			RetryDroppedParks();
		}
		if (!a_captured) {
			return;
		}
		if (!a_captured->inFrame) {
			if (g_inFrame && g_occupied) {
				SendPark(*g_occupied, false);
			}
			g_occupied.reset();
			g_inFrame = false;
			return;
		}

		const auto frame = F4T::PowerArmorSync::CaptureOccupiedFrame(a_player);
		if (!frame) {
			return;
		}
		bool resync{};
		{
			const std::scoped_lock lock(g_mutex);
			resync = connected && !g_owners.empty() && g_resync.exchange(false);
		}
		if ((!g_inFrame || resync) && connected && !AdoptMirror(frame->refFormId) && HasLocalParkRecord(frame->refFormId)) {
			SendRemove(frame->refFormId, a_captured->frameBaseFormId);
		}
		g_inFrame = true;
		g_occupied = Occupied{ *frame, *a_captured };
	}

	std::unordered_set<std::uint32_t> SnapshotFrameRefs()
	{
		std::unordered_set<std::uint32_t> refs;
		for (auto* ref : PowerArmorFramesInCell()) {
			refs.insert(ref->GetFormID());
		}
		return refs;
	}

	void ParkSpawnedFrames(const std::unordered_set<std::uint32_t>& a_before)
	{
		if (!F4T::Networking::IsConnectedToServer()) {
			return;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		const auto origin = player->GetPosition();
		const auto occupied = F4T::PowerArmorSync::CaptureOccupiedFrame(*player);
		std::size_t parked{};
		for (auto* ref : PowerArmorFramesInCell()) {
			if (parked >= kMaximumSpawnedParks) {
				break;
			}
			const auto refId = ref->GetFormID();
			if (occupied && occupied->refFormId == refId) {
				continue;
			}
			{
				const std::scoped_lock lock(g_mutex);
				if (g_consoleParked.contains(refId) || (a_before.contains(refId) && !g_consoleUnsent.contains(refId))) {
					continue;
				}
			}
			const auto position = ref->GetPosition();
			const auto dx = position.x - origin.x;
			const auto dy = position.y - origin.y;
			const auto dz = position.z - origin.z;
			if (dx * dx + dy * dy + dz * dz > kSpawnedFrameReachSquared) {
				continue;
			}
			F4T::PowerArmorSync::PowerArmorState powerArmor;
			powerArmor.inFrame = true;
			powerArmor.frameBaseFormId = ref->GetObjectReference()->GetFormID();
			const auto sent = SendPark(Occupied{ F4T::PowerArmorSync::CaptureFrameFromReference(*ref), std::move(powerArmor) }, true);
			const std::scoped_lock lock(g_mutex);
			if (!sent) {
				g_consoleUnsent.insert(refId);
				continue;
			}
			++parked;
		}
	}

	void OnDisconnected()
	{
		std::vector<std::pair<std::uint32_t, std::uint32_t>> unlock;
		{
			const std::scoped_lock lock(g_mutex);
			for (const auto& [suitId, applied] : g_appliedLocked) {
				if (applied.locked) {
					unlock.emplace_back(suitId, applied.localId);
				}
			}
			g_owners.clear();
			g_hackResults.clear();
			g_appliedLocked.clear();
			RetainParksLocked();
			g_consoleParked.clear();
			g_consoleUnsent.clear();
			g_droppedParks.clear();
			for (const auto& [localId, suitId] : g_graduated) {
				g_graduatedBeforeReconnect.insert_or_assign(localId, suitId);
			}
			g_graduated.clear();
			g_target.reset();
			g_challenge.reset();
			g_adopting.clear();
			g_resync.store(true);
		}
		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			F4T::DebugLogging::Log("ParkedSuitSync", "task interface unavailable; parked suit frames stay locked");
			return;
		}
		taskInterface->AddTask([suits = std::move(unlock)]() {
			for (const auto& [suitId, localId] : suits) {
				{
					const std::scoped_lock lock(g_mutex);
					if (g_owners.contains(suitId)) {
						continue;
					}
				}
				if (auto* frame = RE::TESForm::GetFormByID<RE::TESObjectREFR>(localId)) {
					(void)F4T::PowerArmorScript::SetFrameLocked(*frame, false);
				}
			}
			{
				const std::scoped_lock lock(g_mutex);
				g_adopting.clear();
			}
			(void)F4T::ParkedSuitMirror::DestroyAll();
		});
	}
}

namespace F4T::ParkedSuitSync::detail
{
	std::unordered_map<std::uint32_t, AppliedLock> g_appliedLocked;
	std::unordered_map<std::uint32_t, std::uint32_t> g_graduated;
	std::unordered_map<std::uint32_t, std::uint32_t> g_graduatedBeforeReconnect;
	std::deque<PendingTransition> g_pending;
	std::unordered_set<std::uint32_t> g_consoleParked;
	std::unordered_set<std::uint32_t> g_consoleUnsent;

	std::optional<std::uint32_t> ParseHexId(const nlohmann::json& a_object, std::string_view a_key)
	{
		const auto found = a_object.find(a_key);
		if (found == a_object.end() || !found->is_string()) {
			return std::nullopt;
		}
		const auto& text = found->get_ref<const std::string&>();
		if (text.size() != 8) {
			return std::nullopt;
		}
		std::uint32_t value{};
		const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 16);
		if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
			return std::nullopt;
		}
		return value;
	}

	bool IsPowerArmorFrame(RE::TESObjectREFR& a_ref)
	{
		auto* keyword = RE::TESForm::GetFormByID<RE::BGSKeyword>(kFurnitureTypePowerArmorKeywordFormId);
		auto* base = a_ref.GetObjectReference();
		auto* furniture = base && base->GetFormType() == RE::ENUM_FORM_ID::kFURN ? base->As<RE::TESFurniture>() : nullptr;
		return keyword && furniture && furniture->HasKeyword(keyword);
	}

	bool FrameResolvesToBase(std::uint32_t a_localId, std::uint32_t a_frameBaseFormId)
	{
		auto* frame = RE::TESForm::GetFormByID<RE::TESObjectREFR>(a_localId);
		const auto* base = frame ? frame->GetObjectReference() : nullptr;
		return base && base->GetFormID() == a_frameBaseFormId;
	}
}

#include "pch.h"
#include "F4TObjectSync.h"
#include "F4TFormIdentity.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <format>
#include <map>
#include <mutex>
#include <vector>

namespace F4T::ObjectSync
{
	using State = F4T::ObjectWire::ObjectState;
	using Kind = F4T::ObjectWire::Kind;
	using Json = nlohmann::json;

	namespace
	{
		constexpr bool kEnableDoorOpenState = true;
		struct Capture
		{
			std::uint32_t refFormID{};
			bool localActor{};
		};
		struct Tracked
		{
			std::uint32_t refFormID{};
			State observed;
		};
		struct Pending
		{
			State expected;
			bool liveOpen{};
			bool liveHasLock{};
			bool liveLocked{};
			std::uint32_t activations{};
			std::uint32_t attempts{};
			std::uint32_t priorRevision{};
		};
		struct Mutation
		{
			std::uint32_t refId{};
			bool activation{};
			bool locked{};
		};

		void CanonicalizeState(State& a_state)
		{
			if (a_state.kind != Kind::Door) {
				a_state.open = false;
			}
			if (!a_state.locked) {
				a_state.lockLevel = 0;
			}
		}
		std::mutex g_mutex;
		std::deque<Capture> g_captures;
		std::deque<Json> g_inbound;
		std::deque<Tracked> g_tracked;
		std::map<std::uint32_t, Pending> g_retries;
		std::map<F4T::ObjectWire::ObjectKey, std::uint32_t> g_revisions;
		F4T::ObjectState::ApplyGuard g_applyGuard;
		std::atomic_bool g_applying{};
		std::uint64_t g_dropped{};
		std::uint64_t g_operation{};
		std::uint64_t g_session{};
		std::uint64_t g_nextPoll{};
		bool g_active{};
		std::atomic_bool g_registered{};
		std::atomic_bool g_sharedDoors{ true };

		class ActivateSink final : public RE::BSTEventSink<RE::TESActivateEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent& a_event, RE::BSTEventSource<RE::TESActivateEvent>*) override
			{
				if (g_applying.load() || !a_event.objectActivated) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const Capture capture{ a_event.objectActivated->GetFormID(),
					a_event.actionRef.get() == RE::PlayerCharacter::GetSingleton() };
				const std::scoped_lock lock(g_mutex);
				if (!g_active) {
					return RE::BSEventNotifyControl::kContinue;
				}
				if (g_captures.size() >= kMaximumQueue) {
					g_dropped += 1;
					return RE::BSEventNotifyControl::kContinue;
				}
				g_captures.push_back(capture);
				return RE::BSEventNotifyControl::kContinue;
			}
		} g_sink;

		bool Observe(RE::TESObjectREFR* a_ref, State& a_state)
		{
			if (!a_ref || !a_ref->GetObjectReference()) {
				return false;
			}
			const auto type = a_ref->GetObjectReference()->GetFormType();
			if (type != RE::ENUM_FORM_ID::kDOOR && type != RE::ENUM_FORM_ID::kACTI) {
				return false;
			}
			a_state.kind = type == RE::ENUM_FORM_ID::kDOOR ? Kind::Door : Kind::Activator;
			auto* extras = a_ref->extraList.get();
			if (a_state.kind == Kind::Door && extras && extras->GetByType(RE::EXTRA_DATA_TYPE::kTeleport)) {
				return false;
			}
			const auto split = F4T::FormIdentity::SplitFormId(a_ref->GetFormID());
			if (split.kind == F4T::FormIdentity::Kind::Runtime) {
				return false;
			}
			auto* data = RE::TESDataHandler::GetSingleton();
			if (!data) {
				return false;
			}
			const auto* file = split.kind == F4T::FormIdentity::Kind::Light ?
				data->LookupLoadedLightModByIndex(static_cast<std::uint16_t>(split.fileIndex)) :
				data->LookupLoadedModByIndex(static_cast<std::uint8_t>(split.fileIndex));
			if (!file || !F4T::FormIdentity::IsValidRecordIdentity({ std::string(file->GetFilename()), split.localId }, split.kind)) {
				return false;
			}
			a_state.object = { std::string(file->GetFilename()), split.localId };
			if (kEnableDoorOpenState && a_state.kind == Kind::Door) {
				const auto open = RE::BGSOpenCloseForm::GetOpenState(a_ref);
				a_state.open = open == RE::BGSOpenCloseForm::OPEN_STATE::kOpen || open == RE::BGSOpenCloseForm::OPEN_STATE::kOpening;
			}
			if (auto* refLock = extras ? a_ref->GetLock() : nullptr) {
				a_state.hasLock = true;
				a_state.locked = (refLock->flags & static_cast<std::uint8_t>(RE::REFR_LOCK::Flags::kLocked)) != 0;
				const auto level = static_cast<std::int32_t>(refLock->GetLockLevel(a_ref));
				a_state.lockLevel = static_cast<std::uint32_t>(std::clamp(level, 0, static_cast<int>(F4T::ObjectWire::kMaximumLockLevel)));
			}
			const auto position = a_ref->GetPosition();
			const auto* cell = a_ref->GetParentCell();
			a_state.position = { std::format("{:08X}", cell ? cell->GetFormID() : 0), "", position.x, position.y, position.z };
			if (cell && cell->worldSpace) {
				a_state.position.worldspaceId = std::format("{:08X}", cell->worldSpace->GetFormID());
			}
			return true;
		}

		Json Request(const State& a_state, bool a_lock)
		{
			const auto state = F4T::ObjectWire::BuildObjectState(a_state);
			Json request{ { "type", a_lock ? "objectLockRequest" : "objectActivateRequest" },
				{ "operationId", std::format("obj{}-{}", g_session, ++g_operation) }, { "object", state.at("object") },
				{ "position", { { "cellId", a_state.position.cellId }, { "worldspaceId", a_state.position.worldspaceId },
					{ "x", a_state.position.x }, { "y", a_state.position.y }, { "z", a_state.position.z } } } };
			if (a_lock) {
				request["locked"] = a_state.locked;
				if (a_state.locked) {
					request["lockLevel"] = a_state.lockLevel;
				}
			} else {
				request["kind"] = F4T::ObjectWire::KindName(a_state.kind);
				request["action"] = a_state.kind == Kind::Activator ? "activate" : (a_state.open ? "open" : "close");
			}
			return request;
		}

		void Apply(const Json& a_packet, double a_nowSeconds, RE::PlayerCharacter* a_player, std::map<std::uint32_t, Pending>& a_pending, bool a_live)
		{
			const auto key = F4T::ObjectWire::ParseObjectKey(a_packet.at("object"));
			auto* data = RE::TESDataHandler::GetSingleton();
			if (!key || !data) {
				return;
			}
			const auto revision = a_packet.at("revision").get<std::uint32_t>();
			const auto stored = g_revisions.find(*key);
			if (stored != g_revisions.end() && revision <= stored->second) {
				return;
			}
			std::uint32_t priorRevision = stored == g_revisions.end() ? 0u : stored->second;
			auto* ref = data->LookupForm<RE::TESObjectREFR>(key->localId, key->plugin);
			State observed;
			if (!Observe(ref, observed) || F4T::ObjectWire::KindName(observed.kind) != a_packet.at("kind").get<std::string>()) {
				return;
			}
			State expected = observed;
			expected.revision = revision;
			expected.open = a_packet.value("open", false);
			expected.hasLock = a_packet.contains("locked");
			expected.locked = a_packet.value("locked", false);
			expected.lockLevel = a_packet.value("lockLevel", 0u);
			CanonicalizeState(expected);
			if (g_revisions.size() >= F4T::ObjectState::kMaximumObjects && !g_revisions.contains(*key)) {
				return;
			}
			const bool activation = expected.kind == Kind::Activator ||
				(kEnableDoorOpenState && expected.kind == Kind::Door && observed.open != expected.open);
			if (!a_player && activation && (a_live || expected.kind == Kind::Door)) {
				return;
			}
			if (expected.kind == Kind::Door && !g_sharedDoors.load()) {
				return;
			}
			g_revisions[*key] = revision;
			if (const auto retry = g_retries.find(ref->GetFormID()); retry != g_retries.end()) {
				priorRevision = retry->second.priorRevision;
				g_retries.erase(retry);
			}
			if (g_applyGuard.Consume(expected, a_nowSeconds)) {
				if (auto tracked = std::find_if(g_tracked.begin(), g_tracked.end(), [&](const Tracked& entry) { return entry.refFormID == ref->GetFormID(); }); tracked != g_tracked.end()) {
					tracked->observed = observed;
				}
				return;
			}
			if (auto queued = a_pending.find(ref->GetFormID()); queued != a_pending.end()) {
				queued->second.expected = expected;
				if (a_live && expected.kind == Kind::Activator) {
					queued->second.activations += 1;
				}
				return;
			}
			Pending entry{ expected, observed.open, observed.hasLock, observed.locked,
				a_live && expected.kind == Kind::Activator ? 1u : 0u };
			entry.priorRevision = priorRevision;
			a_pending.emplace(ref->GetFormID(), entry);
		}
	}

	void NoteWelcome(const Json& a_packet)
	{
		const std::scoped_lock lock(g_mutex);
		g_active = false;
		g_retries.clear();
		if (const auto capabilities = a_packet.find("capabilities"); capabilities != a_packet.end() && capabilities->is_array()) {
			g_active = std::find(capabilities->begin(), capabilities->end(), Json("object-state-v1")) != capabilities->end();
		}
		++g_session;
	}

	void NoteCapabilities(const Json& a_packet)
	{
		const std::scoped_lock lock(g_mutex);
		g_active = false;
		if (const auto capabilities = a_packet.find("capabilities"); capabilities != a_packet.end() && capabilities->is_array()) {
			g_active = std::find(capabilities->begin(), capabilities->end(), Json("object-state-v1")) != capabilities->end();
		}
	}

	void EnqueuePacket(const Json& a_packet)
	{
		const std::scoped_lock lock(g_mutex);
		if (!g_active) {
			return;
		}
		if (g_inbound.size() >= kMaximumQueue) {
			g_dropped += 1;
			return;
		}
		g_inbound.push_back(a_packet);
	}

	void Tick(std::uint64_t a_nowMs)
	{
		if (!g_registered.load()) {
			if (auto* source = RE::TESActivateEvent::GetEventSource()) {
				source->RegisterSink(&g_sink);
				g_registered.store(true);
			}
		}
		std::vector<Json> outbound;
		std::vector<Mutation> mutations;
		std::map<std::uint32_t, Pending> pending;
		std::map<std::uint32_t, Pending> retries;
		bool retryDropped = false;
		auto* player = RE::PlayerCharacter::GetSingleton();
		{
			const std::scoped_lock lock(g_mutex);
			if (!g_active) {
				return;
			}
			const auto now = static_cast<double>(a_nowMs) / 1000.0;
			g_applyGuard.Expire(now);
			while (!g_inbound.empty()) {
				const auto packet = std::move(g_inbound.front());
				g_inbound.pop_front();
				if (packet.at("type") == "objectState") {
					Apply(packet, now, player, pending, true);
				} else if (packet.at("type") == "objectStateSnapshot") {
					for (const auto& state : packet.at("objects")) {
						Apply(state, now, player, pending, false);
					}
				}
			}
			for (const auto& [refId, retry] : g_retries) {
				pending.emplace(refId, retry);
			}
			for (const auto& [refId, entry] : pending) {
				const auto& expected = entry.expected;
				const bool door = expected.kind == Kind::Door;
				if (door && !g_sharedDoors.load()) {
					continue;
				}
				const std::uint32_t activations = door ?
					(kEnableDoorOpenState && entry.liveOpen != expected.open ? 1u : 0u) :
					entry.activations;
				const bool lockWrite = expected.hasLock && entry.liveHasLock && entry.liveLocked != expected.locked;
				if (activations == 0 && !lockWrite) {
					continue;
				}
				if (!RE::TESForm::GetFormByID<RE::TESObjectREFR>(refId) || (activations != 0 && !player)) {
					if (entry.attempts >= kMaximumWriteRetries) {
						if (entry.priorRevision == 0) {
							g_revisions.erase(expected.object);
						} else {
							g_revisions[expected.object] = entry.priorRevision;
						}
						retryDropped = true;
						continue;
					}
					auto next = entry;
					next.attempts += 1;
					retries.insert_or_assign(refId, next);
					continue;
				}
				if ((lockWrite || expected.kind != Kind::Activator) && !g_applyGuard.Record(expected, now)) {
					continue;
				}
				if (lockWrite && !expected.locked) {
					mutations.push_back({ refId, false, false });
				}
				for (std::uint32_t index = 0; index < activations; index += 1) {
					mutations.push_back({ refId, true, false });
				}
				if (lockWrite && expected.locked) {
					mutations.push_back({ refId, false, true });
				}
			}
			g_retries = std::move(retries);
			while (!g_captures.empty()) {
				const auto capture = g_captures.front();
				g_captures.pop_front();
				if (!capture.localActor) {
					continue;
				}
				State state;
				if (!Observe(RE::TESForm::GetFormByID<RE::TESObjectREFR>(capture.refFormID), state)) {
					continue;
				}
				CanonicalizeState(state);
				if (g_applyGuard.Consume(state, now)) {
					continue;
				}
				const bool published = state.kind != Kind::Door || (kEnableDoorOpenState && g_sharedDoors.load());
				if (published) {
					outbound.push_back(Request(state, false));
				}
				const auto existing = std::find_if(g_tracked.begin(), g_tracked.end(), [&](const Tracked& entry) { return entry.refFormID == capture.refFormID; });
				const bool known = existing != g_tracked.end();
				if (known) {
					g_tracked.erase(existing);
				}
				if (published) {
					if (g_tracked.size() >= kMaximumTrackedRefs) {
						g_tracked.pop_front();
					}
					g_tracked.push_back({ capture.refFormID, state });
					[[maybe_unused]] const bool guarded = g_applyGuard.Record(state, now);
					if (state.hasLock) {
						outbound.push_back(Request(state, true));
					}
				}
			}
			if (a_nowMs >= g_nextPoll) {
				g_nextPoll = a_nowMs + 1000;
				for (auto& tracked : g_tracked) {
					State state;
					if (Observe(RE::TESForm::GetFormByID<RE::TESObjectREFR>(tracked.refFormID), state)) {
						CanonicalizeState(state);
						if (state.hasLock && (state.locked != tracked.observed.locked || state.lockLevel != tracked.observed.lockLevel) && !g_applyGuard.Consume(state, now)) {
							outbound.push_back(Request(state, true));
						}
						tracked.observed = state;
					}
				}
			}
		}
		if (retryDropped) {
			F4T::Networking::Detail::LogThrottledWarning(
				"object_sync_write_unresolved",
				"Dropped a queued object sync write: the reference never resolved.");
		}
		if (!mutations.empty()) {
			g_applying.store(true);
			for (const auto& mutation : mutations) {
				auto* ref = RE::TESForm::GetFormByID<RE::TESObjectREFR>(mutation.refId);
				if (!ref) {
					continue;
				}
				if (!mutation.activation) {
					if (auto* refLock = ref->GetLock()) {
						refLock->SetLocked(mutation.locked);
					}
					continue;
				}
				if (player) {
					ref->ActivateRef(player, nullptr, 1, false, false, false);
				}
			}
			g_applying.store(false);
		}
		for (const auto& request : outbound) {
			if (request.at("type") == "objectActivateRequest") {
				F4T::Networking::SendObjectActivateRequest(request);
			} else {
				F4T::Networking::SendObjectLockRequest(request);
			}
		}
	}

	void SetSharedDoors(bool a_sharedDoors)
	{
		g_sharedDoors.store(a_sharedDoors);
	}

	void RefreshTrackedLocks()
	{
		const std::scoped_lock lock(g_mutex);
		std::deque<Tracked> kept;
		for (auto& tracked : g_tracked) {
			if (tracked.observed.kind == Kind::Door && !g_sharedDoors.load()) {
				continue;
			}
			State state;
			if (!Observe(RE::TESForm::GetFormByID<RE::TESObjectREFR>(tracked.refFormID), state)) {
				continue;
			}
			CanonicalizeState(state);
			tracked.observed = state;
			if (kept.size() < kMaximumTrackedRefs) {
				kept.push_back(tracked);
			}
		}
		g_tracked = std::move(kept);
		g_nextPoll = 0;
	}

	void Reset()
	{
		const std::scoped_lock lock(g_mutex);
		g_active = false;
		g_captures.clear();
		g_inbound.clear();
		g_tracked.clear();
		g_retries.clear();
		g_revisions.clear();
		g_applyGuard = {};
		g_nextPoll = 0;
		g_dropped = 0;
	}
}

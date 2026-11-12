#include "pch.h"

#include "F4TExplosionSync.h"

#include "F4TExplosionSyncPolicy.h"
#include "F4TNetworking.h"
#include "F4TProxyActorController.h"
#include "F4TProxyLiveFireGuard.h"
#include "F4TProxyPuppet.h"
#include "F4TQuestFormKey.h"
#include "F4TRemotePlayerState.h"
#include "F4TReplicationRuntime.h"
#include "F4TShotReplay.h"
#include "F4TSurvivalPolicyClient.h"

#include "RE/B/BGSExplosion.h"
#include "RE/E/Explosion.h"
#include "RE/N/NEW_REFR_DATA.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TES.h"
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESObjectCELL.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace F4T::ExplosionSync
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		namespace Policy = F4T::ExplosionSyncPolicy;

		constexpr float kScanRadius = 6000.0F;
		constexpr auto kScanInterval = std::chrono::milliseconds{ 100 };
		constexpr auto kProxyBlastKeep = std::chrono::seconds{ 3 };
		constexpr auto kReplayedKeep = std::chrono::seconds{ 10 };
		constexpr std::size_t kMaximumQueued = 32;
		constexpr std::uintptr_t kChainChildHandleOffset = 0x634; // source:E:/RE-work/co-explosion/882-chain-offsets.md

		struct OutboundSpawn
		{
			std::string formKey;
			double x{};
			double y{};
			double z{};
			std::uint64_t connection{};
			std::uint64_t session{};
			std::uint64_t revision{};
		};

		struct LocalBlast
		{
			std::uintptr_t address{};
			std::uint32_t localId{};
			std::uint32_t baseId{};
			std::uint32_t ownerId{};
			RE::NiPoint3 position{};
			std::uint32_t nativeHandle{};
		};

		struct InboundSpawn
		{
			std::uint32_t playerId{};
			QuestProtocol::FormKey formKey;
			RE::NiPoint3 position{};
			Clock::time_point arrival{};
		};

		struct PendingReplay
		{
			std::uint32_t playerId{};
			std::uint32_t baseId{};
			RE::NiPoint3 position{};
			Clock::time_point arrival{};
		};

		struct ProxyBlast
		{
			std::uint32_t playerId{};
			RE::NiPoint3 position{};
			Clock::time_point seenAt{};
		};

		struct ReplayedRef
		{
			RE::ObjectRefHandle handle;
			Clock::time_point placedAt{};
		};

		std::atomic<bool> g_serverSupports{ false };
		std::mutex g_inboundMutex;
		std::deque<InboundSpawn> g_inbound;
		std::mutex g_outboundMutex;
		std::deque<OutboundSpawn> g_outbound;

		bool g_primed = false;
		Clock::time_point g_nextScan{};
		std::vector<Policy::SeenRef> g_seen;
		std::vector<ProxyBlast> g_proxyBlasts;
		std::vector<PendingReplay> g_pending;
		std::mutex g_replayedMutex;
		std::vector<ReplayedRef> g_replayed;
		std::vector<Policy::ChainChildRecord> g_chainChildren;

		std::uint32_t ParseHex(const std::string& a_text)
		{
			std::uint32_t result{};
			const auto parsed = std::from_chars(a_text.data(), a_text.data() + a_text.size(), result, 16);
			return parsed.ec == std::errc{} && parsed.ptr == a_text.data() + a_text.size() ? result : 0;
		}

		Policy::Point ToPoint(const RE::NiPoint3& a_position)
		{
			return Policy::Point{ a_position.x, a_position.y, a_position.z };
		}

		bool SameSpace(const F4T::RemotePlayerState::RemotePlayerState& a_remote, RE::TESObjectCELL& a_cell)
		{
			const auto worldspaceId = a_cell.worldSpace ? a_cell.worldSpace->GetFormID() : 0U;
			return Policy::SameSpace(a_cell.IsExterior(), a_cell.GetFormID(), worldspaceId, ParseHex(a_remote.cellId), ParseHex(a_remote.worldspaceId));
		}

		bool IsExplosionRef(const RE::TESObjectREFR* a_ref)
		{
			const REL::Relocation<std::uintptr_t> explosionTable{ RE::VTABLE::Explosion[0] /* E:/RE-work/F4RR-co/RE-Work/subsystems/commonwealth-online-re/co245-ranged/README.md */ };
			const REL::Relocation<std::uintptr_t> chainTable{ RE::VTABLE::ChainExplosion[0] /* E:/RE-work/F4RR-co/RE-Work/subsystems/commonwealth-online-re/explosion-sync-chain/README.md */ };
			const auto table = *reinterpret_cast<const std::uintptr_t*>(a_ref);
			return table == explosionTable.address() || table == chainTable.address();
		}

		bool IsChainExplosionRef(const RE::TESObjectREFR* a_ref)
		{
			const REL::Relocation<std::uintptr_t> chainTable{ RE::VTABLE::ChainExplosion[0] /* E:/RE-work/F4RR-co/RE-Work/subsystems/commonwealth-online-re/explosion-sync-chain/README.md */ };
			return *reinterpret_cast<const std::uintptr_t*>(a_ref) == chainTable.address();
		}

		std::uint32_t ChainChildHandle(const RE::TESObjectREFR* a_ref)
		{
			return *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uint8_t*>(a_ref) + kChainChildHandleOffset);
		}

		bool TracksChainChildren()
		{
			static const bool supported = [] {
				const auto version = REX::FModule::GetExecutingModule().GetFileVersion();
				return version == REL::Version{ 1, 10, 163 } || version == REL::Version{ 1, 10, 984 } || version == REL::Version{ 1, 11, 240 };
			}();
			return supported;
		}

		bool Matches(const ProxyBlast& a_blast, const PendingReplay& a_entry)
		{
			const auto gap = a_blast.seenAt > a_entry.arrival ? a_blast.seenAt - a_entry.arrival : a_entry.arrival - a_blast.seenAt;
			return Policy::ProxyBlastMatches(a_blast.playerId, a_entry.playerId, ToPoint(a_blast.position), ToPoint(a_entry.position), std::chrono::duration_cast<std::chrono::milliseconds>(gap));
		}

		void QueueOutbound(const LocalBlast& a_blast)
		{
			const auto key = F4T::QuestFormKey::ToKey(a_blast.baseId, F4T::QuestFormKey::GetLoadOrder());
			const auto connection = F4T::Networking::GetConnectionGeneration();
			const auto session = F4T::ReplicationRuntime::CurrentGeneration();
			const auto revision = F4T::SurvivalPolicy::g_clientState.ReadyRevision(connection, session);
			if (!key || !revision || !F4T::Networking::IsConnectedToServer()) {
				return;
			}
			std::lock_guard lock{ g_outboundMutex };
			if (g_outbound.size() < kMaximumQueued) {
				g_outbound.push_back(OutboundSpawn{ *key, a_blast.position.x, a_blast.position.y, a_blast.position.z, connection, session, *revision });
			}
		}

		void Scan(RE::PlayerCharacter& a_player, Clock::time_point a_now)
		{
			auto* tes = RE::TES::GetSingleton();
			if (!tes) {
				return;
			}
			std::vector<LocalBlast> found;
			const bool tracksChildren = TracksChainChildren();
			tes->ForEachReferenceInRange(a_player.GetPosition(), kScanRadius, [&](RE::TESObjectREFR* a_ref) { // source:E:/GardenEngine/re-answers/2026-10-27-fs-p2-explosions-framelimiter.md E4a
				const auto* base = a_ref ? a_ref->GetObjectReference() : nullptr;
				if (!base || a_ref->IsDeleted() || !base->As<RE::BGSExplosion>() || !IsExplosionRef(a_ref)) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				if (tracksChildren && IsChainExplosionRef(a_ref)) {
					Policy::RecordChainChild(g_chainChildren, ChainChildHandle(a_ref), a_now);
				}
				auto* explosion = static_cast<RE::Explosion*>(a_ref);
				const auto ownerId = explosion->owner.get() ? explosion->owner.get()->GetFormID() : 0U;
				found.push_back(LocalBlast{ reinterpret_cast<std::uintptr_t>(a_ref), a_ref->GetFormID(), base->GetFormID(), ownerId, a_ref->GetPosition(), a_ref->GetHandle().get_handle() });
				return RE::BSContainer::ForEachResult::kContinue;
			});

			const bool learning = !g_primed;
			g_primed = true;
			const auto playerLocalId = a_player.GetFormID();
			std::vector<Policy::SeenRef> seen;
			for (const auto& blast : found) {
				const Policy::SeenRef ref{ blast.address, blast.localId, blast.nativeHandle };
				seen.push_back(ref);
				if (!Policy::IsNewRef(g_seen, ref) || learning || Policy::IsChainChild(g_chainChildren, blast.nativeHandle)) {
					continue;
				}
				if (blast.ownerId == playerLocalId) { // source:E:/GardenEngine/re-answers/2026-10-27-fs-p2-explosions-framelimiter.md E2a
					QueueOutbound(blast);
				} else if (const auto proxyPlayerId = F4T::ProxyActorController::TryGetRemotePlayerIdForProxyFormId(blast.ownerId)) { // source:E:/GardenEngine/re-answers/2026-10-27-fs-p2-explosions-framelimiter.md E2a
					g_proxyBlasts.push_back(ProxyBlast{ *proxyPlayerId, blast.position, a_now });
				}
			}
			g_seen = std::move(seen);
			Policy::PruneChainChildren(g_chainChildren, a_now, kReplayedKeep);
		}

		Policy::Decision Decide(const PendingReplay& a_entry, Clock::time_point a_now, RE::PlayerCharacter& a_player)
		{
			auto* cell = a_player.GetParentCell();
			const auto remote = F4T::RemotePlayerState::GetRemotePlayer(a_entry.playerId);
			const bool sameSpace = cell && remote && SameSpace(*remote, *cell);
			const bool inRange = Policy::InReplayRange(ToPoint(a_entry.position), ToPoint(a_player.GetPosition()));
			const bool hasProxy = static_cast<bool>(F4T::ProxyActorController::TryGetProxyActorForRemotePlayerId(a_entry.playerId));
			const auto blast = sameSpace && inRange && hasProxy ?
				std::find_if(g_proxyBlasts.begin(), g_proxyBlasts.end(), [&](const ProxyBlast& a_blast) { return Matches(a_blast, a_entry); }) :
				g_proxyBlasts.end();
			const auto decision = Policy::Decide(Policy::DecisionInput{
				sameSpace,
				inRange,
				hasProxy,
				blast != g_proxyBlasts.end(),
				std::chrono::duration_cast<std::chrono::milliseconds>(a_now - a_entry.arrival) });
			if (decision == Policy::Decision::Drop && blast != g_proxyBlasts.end()) {
				g_proxyBlasts.erase(blast);
			}
			return decision;
		}

		void Replay(const PendingReplay& a_entry, RE::PlayerCharacter& a_player, Clock::time_point a_now)
		{
			auto* base = RE::TESForm::GetFormByID<RE::BGSExplosion>(a_entry.baseId);
			auto* handler = RE::TESDataHandler::GetSingleton();
			auto* cell = a_player.GetParentCell();
			if (!base || !handler || !cell || !cell->IsAttached()) {
				return;
			}
			RE::NEW_REFR_DATA data;
			data.object = base;
			data.location = a_entry.position;
			data.direction = { 0.0F, 0.0F, 0.0F };
			data.reference = nullptr;
			data.interior = cell->IsExterior() ? nullptr : cell;
			data.world = cell->IsExterior() ? cell->worldSpace : nullptr;
			data.extra = nullptr;
			data.maxLevel = 0;
			data.forcePersist = false;
			data.clearStillLoadingFlag = true;
			data.initializeScripts = true;
			data.initiallyDisabled = false;
			const auto created = handler->CreateReferenceAtLocation(data); // source:E:/GardenEngine/re-answers/2026-10-27-fs-p2-explosions-framelimiter.md E2a
			if (created) {
				{
					std::lock_guard lock{ g_replayedMutex };
					g_replayed.push_back(ReplayedRef{ created, a_now });
				}
				F4T::ProxyLiveFire::RegisterGiveBackSink();
			}
		}

		void ProcessPending(RE::PlayerCharacter& a_player, Clock::time_point a_now)
		{
			std::deque<InboundSpawn> inbound;
			{
				std::lock_guard lock{ g_inboundMutex };
				inbound.swap(g_inbound);
			}
			for (const auto& spawn : inbound) {
				const auto baseId = F4T::QuestFormKey::ToFormId(F4T::QuestProtocol::FormatFormKey(spawn.formKey), F4T::QuestFormKey::GetLoadOrder());
				if (baseId) {
					g_pending.push_back(PendingReplay{ spawn.playerId, *baseId, spawn.position, spawn.arrival });
				}
			}
			std::erase_if(g_proxyBlasts, [&](const ProxyBlast& a_blast) { return a_now - a_blast.seenAt > kProxyBlastKeep; });
			{
				std::lock_guard lock{ g_replayedMutex };
				std::erase_if(g_replayed, [&](const ReplayedRef& a_entry) { return a_now - a_entry.placedAt > kReplayedKeep; });
			}
			for (auto entry = g_pending.begin(); entry != g_pending.end();) {
				const auto decision = Decide(*entry, a_now, a_player);
				if (decision == Policy::Decision::Wait) {
					++entry;
					continue;
				}
				if (decision == Policy::Decision::Replay) {
					Replay(*entry, a_player, a_now);
				}
				entry = g_pending.erase(entry);
			}
		}
	}

	bool IsReplayedExplosion(const RE::ObjectRefHandle& a_ref)
	{
		std::lock_guard lock{ g_replayedMutex };
		return std::any_of(g_replayed.begin(), g_replayed.end(), [&](const ReplayedRef& a_entry) { return a_entry.handle == a_ref; });
	}

	void NoteWelcome(const nlohmann::json& a_welcome)
	{
		const auto capabilities = a_welcome.find("capabilities");
		const bool supported = capabilities != a_welcome.end() && capabilities->is_array() &&
			std::any_of(capabilities->begin(), capabilities->end(), [](const nlohmann::json& a_capability) {
				return a_capability.is_string() && a_capability.get<std::string>() == F4T::ShotReplay::kExplosionCapabilityName;
			});
		g_serverSupports.store(supported, std::memory_order_release);
	}

	void Tick(bool a_connected)
	{
		const auto now = Clock::now();
		if (!a_connected) {
			g_primed = false;
			g_seen.clear();
			g_chainChildren.clear();
			g_proxyBlasts.clear();
			g_pending.clear();
			{
				std::lock_guard lock{ g_replayedMutex };
				g_replayed.clear();
			}
			std::lock_guard lock{ g_inboundMutex };
			g_inbound.clear();
			return;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->GetParentCell()) {
			return;
		}
		if (now >= g_nextScan) {
			g_nextScan = now + kScanInterval;
			if (g_serverSupports.load(std::memory_order_acquire)) {
				Scan(*player, now);
			}
		}
		ProcessPending(*player, now);
	}

	void FlushOutbound()
	{
		std::deque<OutboundSpawn> outbound;
		{
			std::lock_guard lock{ g_outboundMutex };
			outbound.swap(g_outbound);
		}
		for (const auto& spawn : outbound) {
			if (spawn.connection == F4T::Networking::GetConnectionGeneration() &&
				spawn.session == F4T::ReplicationRuntime::CurrentGeneration() &&
				F4T::SurvivalPolicy::g_clientState.Revision(spawn.connection) == spawn.revision &&
				F4T::Networking::IsConnectedToServer()) {
				F4T::Networking::SendExplosionSpawnPacket(spawn.formKey, spawn.x, spawn.y, spawn.z);
			}
		}
	}

	void DiscardOutbound()
	{
		std::lock_guard lock{ g_outboundMutex };
		g_outbound.clear();
	}

	void HandleRelayedPacket(const nlohmann::json& a_packet)
	{
		const auto playerId = F4T::ShotReplay::ParseRelayedPlayerId(a_packet);
		const auto spawn = F4T::ShotReplay::ParseExplosionSpawn(a_packet);
		if (!playerId || !spawn) {
			return;
		}
		const RE::NiPoint3 position{ static_cast<float>(spawn->x), static_cast<float>(spawn->y), static_cast<float>(spawn->z) };
		std::lock_guard lock{ g_inboundMutex };
		if (g_inbound.size() < kMaximumQueued) {
			g_inbound.push_back(InboundSpawn{ *playerId, spawn->formKey, position, Clock::now() });
		}
	}

	bool TryHandlePacket(std::string_view a_type, const nlohmann::json& a_packet)
	{
		if (a_type == "welcome" || a_type == "capabilities") {
			NoteWelcome(a_packet);
			return false;
		}
		if (a_type != "explosionSpawn") {
			return false;
		}
		HandleRelayedPacket(a_packet);
		return true;
	}
}

#include "pch.h"

#include "F4TSleepProxy.h"
#include "F4TDebugLogging.h"
#include "F4TProxyActorController.h"
#include "F4TProxySaveSafety.h"
#include "F4TSleepRemote.h"
#include "F4TSleepMarker.h"

#include "RE/A/Actor.h"
#include "RE/A/AIProcess.h"
#include "RE/B/BGSKeyword.h"
#include "RE/B/BSScriptUtil.h"
#include "RE/G/GameScript.h"
#include "RE/M/MiddleHighProcessData.h"
#include "RE/S/SIT_SLEEP_STATE.h"
#include "RE/T/TESDataHandler.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace F4T::SleepProxy
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		enum class State { Entering, Sleeping, Exiting, Rejected };
		struct Track
		{
			RE::ObjectRefHandle proxy;
			RE::ObjectRefHandle bed;
			std::uint32_t formId{};
			std::uint32_t marker{};
			State state{ State::Entering };
			Clock::time_point deadline;
			SleepMarker::Token markerToken;
		};
		std::map<std::uint32_t, Track> g_tracks;
		std::mutex g_ownedMutex;
		std::unordered_set<std::uint32_t> g_ownedActors;

		void SetOwned(std::uint32_t a_formId, bool a_owned)
		{
			std::scoped_lock lock(g_ownedMutex);
			if (a_owned) g_ownedActors.insert(a_formId);
			else g_ownedActors.erase(a_formId);
		}

		RE::BGSKeyword* SleepKeyword()
		{
			auto* data = RE::TESDataHandler::GetSingleton();
			return data ? data->LookupForm<RE::BGSKeyword>(0x9983, "CommonwealthOnline.esp") : nullptr;
		}

		bool Evaluate(RE::Actor& a_actor)
		{
			const auto game = RE::GameVM::GetSingleton();
			const auto vm = game ? game->GetVM() : nullptr;
			if (!vm) return false;
			const auto& handles = vm->GetObjectHandlePolicy();
			const auto handle = handles.GetHandleForObject(65, std::addressof(a_actor));
			if (handle == handles.EmptyHandle()) return false;
			RE::BSTSmartPointer<RE::BSScript::Object> object;
			if (!vm->FindBoundObject(handle, "Actor", false, object, false) &&
				vm->CreateObject("Actor", object) && object) {
				vm->GetObjectBindPolicy().BindObject(object, handle);
			}
			if (!object) return false;
			const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{};
			return vm->DispatchMethodCall(static_cast<std::uint64_t>(handle), "Actor", "EvaluatePackage", callback);
		}

		void Release(Track& a_track, bool a_quick)
		{
			const auto ref = a_track.proxy.get();
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			SleepMarker::Remove(a_track.markerToken);
			a_track.markerToken.reset();
			if (actor && !actor->IsDeleted()) {
				if (auto* keyword = SleepKeyword()) actor->SetLinkedRef(nullptr, keyword);
				Evaluate(*actor);
				if (a_quick) actor->StopInteractingQuick(true, true, true);
			}
			if (a_quick) SetOwned(a_track.formId, false);
		}

		void Reject(Track& a_track, std::uint32_t a_playerId, const char* a_reason)
		{
			DebugLogging::Log("SleepProxy", "Remote player {} sleep rejected: {} (marker {})", a_playerId, a_reason, a_track.marker);
			Release(a_track, true);
			a_track.state = State::Rejected;
		}
	}

	bool OwnsMovement(std::uint32_t a_actorFormId)
	{
		std::scoped_lock lock(g_ownedMutex);
		return g_ownedActors.contains(a_actorFormId);
	}

	void Tick()
	{
		if (ProxySaveSafety::IsProxyTickBlocked() || ProxySaveSafety::IsSessionResetPending()) return;
		const auto proxies = ProxyActorController::GetActiveProxyActors();
		std::vector<std::uint32_t> removed;
		for (const auto& [playerId, track] : g_tracks) {
			const auto proxy = std::ranges::find(proxies, playerId, &ProxyActorController::ActiveProxyActor::remotePlayerId);
			if (proxy == proxies.end() || proxy->actor->GetHandle() != track.proxy) removed.push_back(playerId);
		}
		for (const auto playerId : removed) OnProxyRemoved(playerId);

		for (const auto& proxy : proxies) {
			const auto playerId = proxy.remotePlayerId;
			const auto lookup = SleepRemote::Find(playerId);
			auto found = g_tracks.find(playerId);
			if (found != g_tracks.end()) {
				auto& track = found->second;
				if (lookup.status == SleepRemote::LookupStatus::Absent) {
					if (track.state == State::Rejected) { g_tracks.erase(found); continue; }
					if (track.state != State::Exiting) {
						Release(track, false);
						track.state = State::Exiting;
						track.deadline = Clock::now() + std::chrono::seconds(10);
					}
					if (proxy.actor->DoGetSitSleepState() == RE::SIT_SLEEP_STATE::kNormal || Clock::now() >= track.deadline) {
						OnProxyRemoved(playerId);
					}
					continue;
				}
				if (lookup.status != SleepRemote::LookupStatus::Ready || track.bed != lookup.bed || track.marker != lookup.marker || track.state == State::Exiting) {
					OnProxyRemoved(playerId);
					continue;
				}
				if (track.state == State::Rejected) continue;
				auto* process = proxy.actor->currentProcess;
				if (proxy.actor->DoGetSitSleepState() == RE::SIT_SLEEP_STATE::kIsSleeping) {
					if (!process || !process->middleHigh || process->GetOccupiedFurniture() != track.bed ||
						process->middleHigh->occupiedFurnitureMarkerID != track.marker) {
						Reject(track, playerId, "engine selected a different bed or marker");
					} else track.state = State::Sleeping;
				} else if (track.state == State::Sleeping || Clock::now() >= track.deadline) {
					Reject(track, playerId, "entry timed out or sleep was interrupted");
				}
				continue;
			}
			if (lookup.status != SleepRemote::LookupStatus::Ready) continue;
			const auto bed = lookup.bed.get();
			auto* keyword = SleepKeyword();
			if (!bed || !keyword || !proxy.actor->Get3D()) continue;
			auto markerToken = SleepMarker::Register(*proxy.actor, *bed, lookup.marker);
			if (!markerToken) continue;
			auto& track = g_tracks[playerId];
			track = Track{ proxy.actor->GetHandle(), lookup.bed, proxy.actor->GetFormID(), lookup.marker,
				State::Entering, Clock::now() + std::chrono::seconds(15), std::move(markerToken) };
			SetOwned(track.formId, true);
			proxy.actor->SetLinkedRef(bed.get(), keyword);
			if (proxy.actor->GetLinkedRef(keyword) != bed.get() || !Evaluate(*proxy.actor)) {
				Reject(track, playerId, "bed link or package evaluation dispatch failed");
			}
		}
	}

	void OnProxyRemoved(std::uint32_t a_playerId)
	{
		const auto found = g_tracks.find(a_playerId);
		if (found == g_tracks.end()) return;
		Release(found->second, true);
		g_tracks.erase(found);
	}
}

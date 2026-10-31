#include "pch.h"

#include "F4TProxyPuppet.h"
#include "F4THookThreadTable.h"

#include "F4TNetworking.h"
#include "F4TDebugLogging.h"
#include "F4TExecutingRuntime.h"
#include "F4TNpcSync.h"
#include "F4TProxyProtection.h"
#include "F4TProxyPuppetLocomotionState.h"
#include "F4TProxyRemoteAnimationState.h"
#include "F4TVtableGuard.h"

#include "F4TNpcActorGuard.h"
#include "F4TNpcSyncRegistry.h"

#include "RE/A/AIProcess.h"
#include "RE/B/bhkCharacterController.h"
#include "RE/H/HighProcessData.h"
#include "RE/I/IAnimationGraphManagerHolder.h"
#include "RE/M/MiddleHighProcessData.h"
#include "RE/N/NiPoint3.h"

#include <atomic>
#include <cmath>
#include <format>
#include <iterator>
#include <numbers>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace F4T::ProxyPuppet
{
	namespace
	{
		std::shared_mutex g_proxyFormIdMutex;
		std::unordered_set<std::uint32_t> g_proxyFormIds;
		std::unordered_map<std::uint32_t, ProxyLocomotionState> g_proxyLocomotion;
		std::unordered_map<std::uint32_t, float> g_proxyWrittenAimPitch;

		std::atomic<bool> g_hookInstalled{ false };

		constexpr std::size_t kUpdateVtableIndex = 0xCF;

		// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/aim-pitch-co316/README.md
		constexpr const char* kAimPitchVariable = "Pitch";
		constexpr const char* kTurnDeltaVariable = "TurnDelta";
		constexpr auto kRadiansToDegrees = 180.0F / std::numbers::pi_v<float>;

		using UpdateFn = void (*)(RE::Actor*, float);

		UpdateFn g_originalUpdate = nullptr;

		std::atomic<std::uint64_t> g_hookCallCount{ 0 };
		std::atomic<std::uint64_t> g_proxyReassertCount{ 0 };

		ProxyLocomotionState GetProxyLocomotionState(std::uint32_t a_formID)
		{
			const std::shared_lock lock{ g_proxyFormIdMutex };
			const auto it = g_proxyLocomotion.find(a_formID);
			return it != g_proxyLocomotion.end() ? it->second : ProxyLocomotionState{};
		}

		void WriteProxyGraphSpeed(RE::Actor* a_this, float a_speed)
		{
			if (a_speed < 0.0F || !std::isfinite(a_speed)) {
				return;
			}
			auto* graphHolder = static_cast<RE::IAnimationGraphManagerHolder*>(a_this);
			graphHolder->SetGraphVariableFloat(RE::BSFixedString{ "Speed" }, a_speed);
			graphHolder->SetGraphVariableFloat(RE::BSFixedString{ "SpeedSmoothed" }, a_speed);
		}

		bool TryWriteGraphFloat(
			RE::IAnimationGraphManagerHolder& a_holder,
			const char* a_variableName,
			float a_value,
			std::uint32_t a_formID)
		{
			const RE::BSFixedString variableName{ a_variableName };
			if (a_holder.SetGraphVariableFloat(variableName, a_value)) {
				return true;
			}

			static std::unordered_map<std::string, std::uint64_t> s_failureCounts;
			const auto failureCount = ++s_failureCounts[a_variableName];
			if (failureCount <= 5 || failureCount % 300 == 0) {
				REX::WARN(std::string_view{
					F4T::Networking::GetLocalPlayerLogPrefix() +
					std::format(
						" Proxy aim graph write rejected form={:08X} variable={}; skipping that write only.",
						a_formID,
						a_variableName) });
			}
			return false;
		}

		void WriteProxyAimGraphVariables(RE::Actor* a_this, float a_deltaSeconds, const ProxyLocomotionState& a_locomotion)
		{
			if (!a_this || !a_locomotion.hasAimState) {
				return;
			}

			auto* graphHolder = static_cast<RE::IAnimationGraphManagerHolder*>(a_this);
			if (!graphHolder) {
				return;
			}

			float writtenPitch = 0.0F;
			{
				const std::unique_lock lock{ g_proxyFormIdMutex };
				auto& storedPitch = g_proxyWrittenAimPitch[a_this->GetFormID()];
				storedPitch = F4T::ProxyRemoteAnimationState::EaseAimPitchDegrees(
					storedPitch,
					a_locomotion.aimPitchDegrees,
					a_deltaSeconds);
				writtenPitch = storedPitch;
			}

			const auto pitchAccepted = TryWriteGraphFloat(
				*graphHolder,
				kAimPitchVariable,
				writtenPitch,
				a_this->GetFormID());
			if (auto* process = a_this->currentProcess) {
				if (auto* middleHigh = process->middleHigh; middleHigh && std::isfinite(writtenPitch)) {
					// Constraint: must run before UpdateNoAI, which lets the engine's Pitch channel read this rotation.x.
					middleHigh->rotation.x = writtenPitch / kRadiansToDegrees;
				}
			}
			const auto turnDelta = F4T::ProxyRemoteAnimationState::ClampTurnDeltaDegreesPerSecond(a_locomotion.turnDelta);
			const auto turnAccepted = TryWriteGraphFloat(
				*graphHolder,
				kTurnDeltaVariable,
				turnDelta,
				a_this->GetFormID());

			static std::atomic<std::uint64_t> s_aimWriteCount{ 0 };
			const auto aimWriteCount = s_aimWriteCount.fetch_add(1, std::memory_order_relaxed) + 1;
			if (aimWriteCount <= 5 || aimWriteCount % 300 == 0) {
				F4T::DebugLogging::Log(
					"ProxyAim",
					"[RT-VERIFY 364-A] proxy aim apply #{} form={:08X} proxy={:p} writtenPitchDegrees={:.2f} sourcePitchDegrees={:.2f} pitchAccepted={} turnDelta={:.2f} turnAccepted={}",
					aimWriteCount,
					a_this->GetFormID(),
					static_cast<const void*>(a_this),
					writtenPitch,
					a_locomotion.aimPitchDegrees,
					pitchAccepted,
					turnDelta,
					turnAccepted);
			}
		}

		void SyncControllerMotionFields(RE::Actor& a_actor, const RE::NiPoint3& a_velocity, float a_speed)
		{
			auto* process = a_actor.currentProcess;
			if (!process) {
				return;
			}

			if (auto* middleHigh = process->middleHigh) {
				middleHigh->desiredSpeed = a_speed;
				middleHigh->animationSpeed = a_speed;

				if (auto* controller = middleHigh->charController.get()) {
					controller->cachedLinearVelocity = a_velocity;
					controller->outVelocity = RE::hkVector4f{ a_velocity.x, a_velocity.y, a_velocity.z, 0.0F };
				}
			}

			if (auto* high = process->high) {
				high->movementOutput.handlerData.movementSpeed = a_speed;
				high->movementOutput.tweenerData.velocity = a_velocity;
				high->pathingCurrentMovementSpeed = a_velocity;
				high->pathingDesiredMovementSpeed = a_velocity;
			}
		}

		// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/README.md (NPC puppet actor FormIDs come from the NpcSync registry)
		bool IsNoAiActorFormID(std::uint32_t a_formID)
		{
			if (IsProxyFormID(a_formID)) {
				return true;
			}
			if (NpcSync::IsPuppetFormId(a_formID) || NpcSync::IsSuppressedFormId(a_formID)) {
				return true;
			}
			return false;
		}

		void UpdateHook(RE::Actor* a_this, float a_delta)
		{
			F4T::HookThread::Note(F4T::HookThread::Id::ProxyUpdate);
			g_hookCallCount.fetch_add(1, std::memory_order_relaxed);

			const auto isProxy = a_this && IsNoAiActorFormID(a_this->GetFormID());
			if (!isProxy) {
				g_originalUpdate(a_this, a_delta);
				return;
			}

			F4T::ProxyProtection::ApplyToProxyActor(*a_this);

		const bool meshReadyForPuppet =
			a_this->GetFullyLoaded3D() != nullptr || a_this->Get3D() != nullptr;
		if (!meshReadyForPuppet) {
			return;
		}

			try {
				const auto locomotion = GetProxyLocomotionState(a_this->GetFormID());

				WriteProxyGraphSpeed(a_this, locomotion.speed);
				WriteProxyAimGraphVariables(a_this, a_delta, locomotion);
				a_this->UpdateNoAI(a_delta);

				if (locomotion.speed >= 0.0F) {
					const RE::NiPoint3 velocity{
						locomotion.velocityX,
						locomotion.velocityY,
						locomotion.velocityZ
					};
					SyncControllerMotionFields(*a_this, velocity, locomotion.speed);
					WriteProxyGraphSpeed(a_this, locomotion.speed);
				}

				g_proxyReassertCount.fetch_add(1, std::memory_order_relaxed);
			} catch (...) {
			}
		}
	}

	void GetHookStats(std::uint64_t& a_outTotalCalls, std::uint64_t& a_outProxySkips)
	{
		a_outTotalCalls = g_hookCallCount.load(std::memory_order_relaxed);
		a_outProxySkips = g_proxyReassertCount.load(std::memory_order_relaxed);
	}

	bool Install()
	{
		if (g_hookInstalled) {
			return true;
		}

		const auto target = ExecutingRuntime::Target();
		if (!target) {
			REX::WARN("Proxy puppet Update hook NOT installed: the game runtime is not one of the supported OG, NG or AE builds.");
			return false;
		}

		REL::Relocation<std::uintptr_t> actorVtable{ RE::Actor::VTABLE[0] };

		const auto module = REX::FModule::GetExecutingModule();
		const auto text = module.GetSection(".text");
		const auto rdata = module.GetSection(".rdata");

		VtableGuard::SlotRequest request{};
		request.vtableAddress = actorVtable.address();
		request.slotIndex = kUpdateVtableIndex;
		request.replacement = REX::UNRESTRICTED_CAST<std::uintptr_t>(&UpdateHook);
		request.codeSection = { text.GetAddress(), text.GetAddress() + text.GetSize() };
		request.vtableSection = { rdata.GetAddress(), rdata.GetAddress() + rdata.GetSize() };
		request.slotValue = VtableGuard::ReadSlot(request);

		const auto verdict = VtableGuard::VerifySlot(request);
		if (verdict != VtableGuard::SlotVerdict::kOk) {
			const auto refusal =
				F4T::Networking::GetLocalPlayerLogPrefix() +
				" Proxy puppet Update hook NOT installed (" + std::string{ VtableGuard::Describe(verdict) } +
				"). Remote players will not be puppeted this session.";
			REX::WARN(std::string_view{ refusal });
			return false;
		}

		// source:RE-Work/subsystems/commonwealth-online-re/npc-coop-co255/findings-hooks.jsonl (hooks-slot-0xCF-{og,ng,ae}: expected RVA)
		const auto updateRva = ExecutingRuntime::RvaFor(*target, 0xD7B020, 0xBDE8D0, 0xC648A0);
		if (!VtableGuard::SlotMatchesExpectedRva(request.slotValue, module.GetBaseAddress(), updateRva)) {
			const auto refusal =
				F4T::Networking::GetLocalPlayerLogPrefix() +
				" Proxy puppet Update hook NOT installed: the Update vtable slot does not hold this runtime's expected engine function. Remote players will not be puppeted this session.";
			REX::WARN(std::string_view{ refusal });
			return false;
		}

		g_originalUpdate = reinterpret_cast<UpdateFn>(request.slotValue);
		actorVtable.write_vfunc(kUpdateVtableIndex, &UpdateHook);

		const auto slotAfterWrite = *reinterpret_cast<std::uintptr_t*>(
			request.vtableAddress + (kUpdateVtableIndex * sizeof(std::uintptr_t)));
		g_hookInstalled =
			g_originalUpdate != nullptr &&
			VtableGuard::WriteSucceeded(slotAfterWrite, request.replacement);

		const auto message =
			F4T::Networking::GetLocalPlayerLogPrefix() +
			(g_hookInstalled
					? " Proxy puppet Update hook installed: proxies use UpdateNoAI + manual motion feedback for cadence."
					: " Proxy puppet Update hook FAILED to install: vtable slot did not take the write.");
		if (g_hookInstalled) {
			F4T::DebugLogging::Write("ProxyPuppet", message);
		} else {
			REX::WARN(std::string_view{ message });
		}

		return g_hookInstalled;
	}

	void SetActiveProxyFormIDs(std::vector<std::uint32_t> a_formIDs)
	{
		std::unordered_set<std::uint32_t> updated;
		updated.reserve(a_formIDs.size());
		for (const auto formId : a_formIDs) {
			if (formId != 0) {
				updated.insert(formId);
			}
		}

		const std::unique_lock lock{ g_proxyFormIdMutex };
		const auto previousCount = g_proxyFormIds.size();
		g_proxyFormIds = std::move(updated);
		if (previousCount != g_proxyFormIds.size()) {
			F4T::DebugLogging::Log("ProxyPuppet", "active proxy form count {} -> {}", previousCount, g_proxyFormIds.size());
		}

		for (auto it = g_proxyLocomotion.begin(); it != g_proxyLocomotion.end();) {
			it = g_proxyFormIds.contains(it->first) ? std::next(it) : g_proxyLocomotion.erase(it);
		}

		for (auto it = g_proxyWrittenAimPitch.begin(); it != g_proxyWrittenAimPitch.end();) {
			it = g_proxyFormIds.contains(it->first) ? std::next(it) : g_proxyWrittenAimPitch.erase(it);
		}
	}

	void SetProxyLocomotionState(std::uint32_t a_formID, float a_speed, float a_direction)
	{
		if (a_formID == 0) {
			return;
		}

		const std::unique_lock lock{ g_proxyFormIdMutex };
		auto& state = g_proxyLocomotion[a_formID];
		state.speed = a_speed;
		state.direction = a_direction;
	}

	void SetProxyAimState(std::uint32_t a_formID, float a_aimPitchDegrees, float a_turnDelta)
	{
		if (a_formID == 0) {
			return;
		}

		const std::unique_lock lock{ g_proxyFormIdMutex };
		auto& state = g_proxyLocomotion[a_formID];
		state.aimPitchDegrees = a_aimPitchDegrees;
		state.turnDelta = a_turnDelta;
		state.hasAimState = true;
	}

	void SetProxyDesiredSpeed(std::uint32_t a_formID, float a_speed)
	{
		const auto existing = GetProxyLocomotionState(a_formID);
		SetProxyLocomotionState(a_formID, a_speed, existing.direction);
	}

	void ApplyProxyLocomotionFrame(
		RE::Actor& a_actor,
		const RE::NiPoint3& a_worldDelta,
		float a_deltaSeconds,
		float a_graphSpeed,
		float a_direction)
	{
		if (!IsProxyFormID(a_actor.GetFormID())) {
			return;
		}

		const bool meshReadyForPuppet =
			a_actor.GetFullyLoaded3D() != nullptr || a_actor.Get3D() != nullptr;
		if (!meshReadyForPuppet) {
			return;
		}

		const float safeDelta = std::clamp(a_deltaSeconds, 1.0F / 120.0F, 0.1F);
		RE::NiPoint3 velocity{};
		if (safeDelta > 0.0F) {
			velocity.x = a_worldDelta.x / safeDelta;
			velocity.y = a_worldDelta.y / safeDelta;
			velocity.z = a_worldDelta.z / safeDelta;
		}

		if (a_graphSpeed < 0.0F || !std::isfinite(a_graphSpeed)) {
			const std::unique_lock lock{ g_proxyFormIdMutex };
			ApplyLocomotionMotionFields(
				g_proxyLocomotion[a_actor.GetFormID()],
				-1.0F,
				a_direction,
				0.0F,
				0.0F,
				0.0F);
			SyncControllerMotionFields(a_actor, {}, 0.0F);
			return;
		}

		if (a_graphSpeed < 8.0F) {
			const std::unique_lock lock{ g_proxyFormIdMutex };
			ApplyLocomotionMotionFields(
				g_proxyLocomotion[a_actor.GetFormID()],
				-1.0F,
				a_direction,
				0.0F,
				0.0F,
				0.0F);
			SyncControllerMotionFields(a_actor, {}, 0.0F);
			return;
		}

		{
			const std::unique_lock lock{ g_proxyFormIdMutex };
			ApplyLocomotionMotionFields(
				g_proxyLocomotion[a_actor.GetFormID()],
				a_graphSpeed,
				a_direction,
				velocity.x,
				velocity.y,
				velocity.z);
		}
		SyncControllerMotionFields(a_actor, velocity, a_graphSpeed);
		WriteProxyGraphSpeed(&a_actor, a_graphSpeed);
	}

	bool IsProxyFormID(std::uint32_t a_formID)
	{
		if (a_formID == 0) {
			return false;
		}

		const std::shared_lock lock{ g_proxyFormIdMutex };
		return g_proxyFormIds.find(a_formID) != g_proxyFormIds.end();
	}

	RE::NiPoint3 GetProxyLocomotionVelocity(std::uint32_t a_formID)
	{
		const auto state = GetProxyLocomotionState(a_formID);
		return RE::NiPoint3{ state.velocityX, state.velocityY, state.velocityZ };
	}
}

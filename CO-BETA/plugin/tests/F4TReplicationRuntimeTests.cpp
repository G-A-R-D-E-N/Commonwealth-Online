#include "F4TReplicationRuntime.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <vector>

namespace
{
	using F4T::ReplicationRuntime::Coordinator;
	using F4T::ReplicationRuntime::Generation;
	using F4T::ReplicationRuntime::kNoSession;
	using F4T::ReplicationRuntime::LocalPlayerPublicationState;
	using Clock = std::chrono::steady_clock;

	void Dirty(LocalPlayerPublicationState& a_state)
	{
		const auto now = Clock::time_point{} + std::chrono::seconds{ 5 };
		a_state.hasLastTransform = true;
		a_state.lastSentTransform = { 1.0F, 2.0F, 3.0F, 0.5F };
		a_state.lastDebugSampleTransform = { 1.0F, 2.0F, 3.0F, 0.5F };
		a_state.lastKnownLocation = { true, 0x1234U, true, 0x3CU, true };
		a_state.lastSentMovementState = {
			.isMoving = true,
			.isSprinting = true,
			.isSneaking = false,
			.isJumping = false,
			.isCrouching = false,
			.weaponDrawn = true,
			.movementSpeed = 200.0F
		};
		a_state.lastSentEquipmentState.push_back({ "Body", 0x0001EED7U, std::nullopt });
		a_state.lastSentAppearanceState.raceFormId = 0x00013746U;
		a_state.lastSentAppearanceState.headPartFormIds.push_back(0x1U);
		a_state.lastSentPowerArmorState.inFrame = true;
		a_state.lastSentPowerArmorState.frameBaseFormId = 0x0010A0FFU;
		a_state.lastSentPowerArmorState.pieces.push_back({ "body", 0x00011111U, std::nullopt });
		a_state.lastSendTime = now;
		a_state.lastLogTime = now;
		a_state.lastDebugSampleTime = now;
		a_state.debugMovingHoldUntil = now;
		a_state.networkMovingHoldUntil = now;
		a_state.soloCellTransformSuppressed = true;
	}

	void AssertFresh(const LocalPlayerPublicationState& a_state)
	{
		const LocalPlayerPublicationState fresh{};
		assert(!a_state.hasLastTransform);
		assert(a_state.lastSentTransform.x == fresh.lastSentTransform.x);
		assert(a_state.lastSentTransform.angleZ == fresh.lastSentTransform.angleZ);
		assert(a_state.lastDebugSampleTransform.z == fresh.lastDebugSampleTransform.z);
		assert(!a_state.lastKnownLocation.hasCellId);
		assert(!a_state.lastKnownLocation.isSoloCell);
		assert(!a_state.lastSentMovementState.isMoving);
		assert(!a_state.lastSentMovementState.weaponDrawn);
		assert(a_state.lastSentEquipmentState.empty());
		assert(a_state.lastSentAppearanceState.raceFormId == 0);
		assert(a_state.lastSentAppearanceState.headPartFormIds.empty());
		assert(!a_state.lastSentPowerArmorState.inFrame);
		assert(a_state.lastSentPowerArmorState.frameBaseFormId == 0);
		assert(a_state.lastSentPowerArmorState.pieces.empty());
		assert(a_state.lastSendTime == Clock::time_point{});
		assert(a_state.lastLogTime == Clock::time_point{});
		assert(a_state.lastDebugSampleTime == Clock::time_point{});
		assert(a_state.debugMovingHoldUntil == Clock::time_point{});
		assert(a_state.networkMovingHoldUntil == Clock::time_point{});
		assert(!a_state.soloCellTransformSuppressed);
	}

	void VerifyNoSessionByDefault()
	{
		Coordinator runtime;
		assert(!runtime.IsActive());
		assert(runtime.CurrentGeneration() == kNoSession);
		assert(runtime.Session().generation == kNoSession);
		assert(!runtime.Admits(kNoSession));
		AssertFresh(runtime.Session().localPlayer);
		assert(runtime.Capture().nextActionSequence == 1);
		assert(runtime.Capture().lastMeleeAttackState == 0);
		assert(!runtime.Capture().lastRangedFireState);
	}

	void VerifyBeginSessionInitializesPublicationState()
	{
		Coordinator runtime;
		Dirty(runtime.Session().localPlayer);

		const auto generation = runtime.BeginSession(7);
		assert(generation != kNoSession);
		assert(runtime.IsActive());
		assert(runtime.CurrentGeneration() == generation);
		assert(runtime.Session().generation == generation);
		AssertFresh(runtime.Session().localPlayer);

		Dirty(runtime.Session().localPlayer);
		const auto replacement = runtime.BeginSession(8);
		assert(replacement != generation);
		assert(runtime.CurrentGeneration() == replacement);
		AssertFresh(runtime.Session().localPlayer);
	}

	void VerifyEndSessionClearsSessionOwnedState()
	{
		Coordinator runtime;
		runtime.BeginSession(1);
		Dirty(runtime.Session().localPlayer);
		runtime.Capture().nextActionSequence = 42;
		runtime.Capture().lastMeleeAttackState = 3;
		runtime.Capture().lastRangedFireState = true;

		runtime.EndSession();
		assert(!runtime.IsActive());
		assert(runtime.CurrentGeneration() == kNoSession);
		assert(runtime.Session().generation == kNoSession);
		AssertFresh(runtime.Session().localPlayer);

		assert(runtime.Capture().nextActionSequence == 42);
		assert(runtime.Capture().lastMeleeAttackState == 3);
		assert(runtime.Capture().lastRangedFireState);

		runtime.EndSession();
		assert(!runtime.IsActive());
	}

	void VerifyStaleGenerationWorkIsRejected()
	{
		struct QueuedWork
		{
			Generation generation{};
			std::uint32_t target{};
		};

		Coordinator runtime;
		const auto disconnectedWork = runtime.CurrentGeneration();
		const auto first = runtime.BeginSession(1);
		const auto firstWork = runtime.CurrentGeneration();
		assert(runtime.Admits(first));
		runtime.EndSession();
		assert(!runtime.Admits(first));
		const auto second = runtime.BeginSession(2);
		assert(second != first);

		const std::vector<QueuedWork> queue{
			{ disconnectedWork, 3 },
			{ firstWork, 4 },
			{ second, 5 },
		};
		std::vector<std::uint32_t> admitted;
		for (const auto& work : queue) {
			if (runtime.Admits(work.generation)) {
				admitted.push_back(work.target);
			}
		}
		assert(admitted.size() == 1);
		assert(admitted.front() == 5);
		assert(!runtime.Admits(kNoSession));
		assert(!runtime.Admits(second + 1));
	}

	void VerifyAnimationFieldsChangeDetection()
	{
		using F4T::ReplicationRuntime::HasAnimationFieldsChanged;
		using F4T::ReplicationRuntime::PlayerMovementState;

		const PlayerMovementState idle{
			.isMoving = false,
			.isSprinting = false,
			.isSneaking = false,
			.isJumping = false,
			.isCrouching = false,
			.weaponDrawn = false,
			.movementSpeed = 0.0F,
		};
		auto same = idle;
		assert(!HasAnimationFieldsChanged(idle, same));

		auto aimed = idle;
		aimed.aimPitchDegrees = 12.0F;
		assert(HasAnimationFieldsChanged(idle, aimed));

		auto tinyAim = idle;
		tinyAim.aimPitchDegrees = 0.2F;
		assert(!HasAnimationFieldsChanged(idle, tinyAim));

		auto aimedAtEpsilon = idle;
		aimedAtEpsilon.aimPitchDegrees = F4T::ReplicationRuntime::kAimPitchChangeEpsilonDegrees;
		assert(HasAnimationFieldsChanged(idle, aimedAtEpsilon));

		auto turned = idle;
		turned.turnDeltaDegreesPerSecond = 30.0F;
		assert(HasAnimationFieldsChanged(idle, turned));

		auto tinyTurn = idle;
		tinyTurn.turnDeltaDegreesPerSecond = 4.0F;
		assert(!HasAnimationFieldsChanged(idle, tinyTurn));

		auto strafed = idle;
		strafed.animationDirectionDegrees = 90.0F;
		assert(HasAnimationFieldsChanged(idle, strafed));

		auto tinyStray = idle;
		tinyStray.animationDirectionDegrees = 0.4F;
		assert(!HasAnimationFieldsChanged(idle, tinyStray));

		auto stopped = strafed;
		stopped.animationDirectionDegrees = 0.0F;
		assert(HasAnimationFieldsChanged(strafed, stopped));

		same = idle;
		same.animationDirectionDegrees = 0.4F;
		same.aimPitchDegrees = 0.2F;
		same.turnDeltaDegreesPerSecond = 4.0F;
		assert(!HasAnimationFieldsChanged(idle, same));
	}

	void VerifySyncSessionTransitions()
	{
		Coordinator runtime;

		runtime.SyncSession(0, false);
		assert(!runtime.IsActive());

		runtime.SyncSession(1, false);
		assert(!runtime.IsActive());

		runtime.SyncSession(1, true);
		assert(runtime.IsActive());
		const auto first = runtime.CurrentGeneration();
		Dirty(runtime.Session().localPlayer);

		runtime.SyncSession(1, true);
		assert(runtime.CurrentGeneration() == first);
		assert(runtime.Session().localPlayer.hasLastTransform);
		assert(runtime.Session().localPlayer.soloCellTransformSuppressed);

		runtime.SyncSession(2, true);
		const auto second = runtime.CurrentGeneration();
		assert(runtime.IsActive());
		assert(second != first);
		assert(!runtime.Admits(first));
		AssertFresh(runtime.Session().localPlayer);

		Dirty(runtime.Session().localPlayer);
		runtime.SyncSession(2, false);
		assert(!runtime.IsActive());
		assert(!runtime.Admits(second));
		AssertFresh(runtime.Session().localPlayer);

		Dirty(runtime.Session().localPlayer);
		runtime.SyncSession(2, false);
		assert(runtime.Session().localPlayer.hasLastTransform);

		runtime.SyncSession(2, true);
		const auto third = runtime.CurrentGeneration();
		assert(runtime.IsActive());
		assert(third != second);
		AssertFresh(runtime.Session().localPlayer);
	}

	void VerifyGenerationForLiveEpoch()
	{
		Coordinator runtime;
		assert(runtime.GenerationFor(0) == kNoSession);

		runtime.SyncSession(7, true);
		const auto first = runtime.CurrentGeneration();
		assert(runtime.GenerationFor(7) == first);
		assert(runtime.GenerationFor(8) == kNoSession);

		runtime.SyncSession(8, true);
		const auto second = runtime.CurrentGeneration();
		assert(second != first);
		assert(runtime.GenerationFor(8) == second);
		assert(runtime.GenerationFor(7) == kNoSession);
	}
}

int main()
{
	VerifyNoSessionByDefault();
	VerifyBeginSessionInitializesPublicationState();
	VerifyEndSessionClearsSessionOwnedState();
	VerifyStaleGenerationWorkIsRejected();
	VerifySyncSessionTransitions();
	VerifyAnimationFieldsChangeDetection();
	VerifyGenerationForLiveEpoch();
	return 0;
}

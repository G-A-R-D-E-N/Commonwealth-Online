#pragma once

#include "F4TPowerArmorSync.h"
#include "F4TRemotePlayerState.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

namespace F4T::ReplicationRuntime
{
	using Generation = std::uint64_t;
	inline constexpr Generation kNoSession = 0;

	struct PlayerTransform
	{
		float x;
		float y;
		float z;
		float angleZ;
	};

	struct PlayerLocation
	{
		bool hasCellId;
		std::uint32_t cellId;
		bool hasWorldspaceId;
		std::uint32_t worldspaceId;
		bool isSoloCell;
	};

	struct PlayerMovementState
	{
		bool isMoving;
		bool isSprinting;
		bool isSneaking;
		bool isJumping;
		bool isCrouching;
		bool weaponDrawn;
		float movementSpeed;
		float animationDirectionDegrees{ 0.0F };
		float aimPitchDegrees{ 0.0F };
		float turnDeltaDegreesPerSecond{ 0.0F };
	};

	inline constexpr float kAnimationDirectionChangeEpsilonDegrees = 0.5F;
	inline constexpr float kAimPitchChangeEpsilonDegrees = 0.25F;
	inline constexpr float kTurnDeltaChangeEpsilonDegreesPerSecond = 5.0F;

	[[nodiscard]] inline bool HasAnimationFieldsChanged(
		const PlayerMovementState& a_previous,
		const PlayerMovementState& a_current)
	{
		return std::fabs(a_current.animationDirectionDegrees - a_previous.animationDirectionDegrees) >=
		           kAnimationDirectionChangeEpsilonDegrees ||
		       std::fabs(a_current.aimPitchDegrees - a_previous.aimPitchDegrees) >=
		           kAimPitchChangeEpsilonDegrees ||
		       std::fabs(a_current.turnDeltaDegreesPerSecond - a_previous.turnDeltaDegreesPerSecond) >=
		           kTurnDeltaChangeEpsilonDegreesPerSecond;
	}

	struct LocalPlayerPublicationState
	{
		bool hasLastTransform{ false };
		PlayerTransform lastSentTransform{};
		PlayerTransform lastDebugSampleTransform{};
		PlayerLocation lastKnownLocation{};
		PlayerMovementState lastSentMovementState{};
		std::vector<F4T::RemotePlayerState::RemoteEquippedItem> lastSentEquipmentState;
		F4T::RemotePlayerState::RemoteAppearance lastSentAppearanceState;
		F4T::PowerArmorSync::PowerArmorState lastSentPowerArmorState;
		std::chrono::steady_clock::time_point lastSendTime{};
		std::chrono::steady_clock::time_point lastLogTime{};
		std::chrono::steady_clock::time_point lastDebugSampleTime{};
		std::chrono::steady_clock::time_point debugMovingHoldUntil{};
		std::chrono::steady_clock::time_point networkMovingHoldUntil{};
		bool soloCellTransformSuppressed{ false };
	};

	struct LocalPlayerCaptureState
	{
		std::uint32_t lastMeleeAttackState{ 0 };
		bool lastRangedFireState{ false };
		std::uint32_t nextActionSequence{ 1 };
		std::chrono::steady_clock::time_point lastUnsentEquipmentLog{};
		std::vector<F4T::RemotePlayerState::RemoteEquippedItem> lastLoggedEquippedItems;
	};

	struct SessionState
	{
		Generation generation{ kNoSession };
		LocalPlayerPublicationState localPlayer;
	};

	class Coordinator
	{
	public:
		Generation BeginSession(std::uint64_t a_connectionEpoch)
		{
			_session = SessionState{};
			_session.generation = ++_lastGeneration;
			_connectionEpoch.store(a_connectionEpoch, std::memory_order_release);
			_current.store(_session.generation, std::memory_order_release);
			return _session.generation;
		}

		void EndSession()
		{
			_session = SessionState{};
			_current.store(kNoSession, std::memory_order_release);
		}

		void SyncSession(std::uint64_t a_connectionEpoch, bool a_connected)
		{
			if (IsActive() && (!a_connected || a_connectionEpoch != _connectionEpoch)) {
				EndSession();
			}
			if (!IsActive() && a_connected) {
				BeginSession(a_connectionEpoch);
			}
		}

		bool IsActive() const noexcept { return _session.generation != kNoSession; }

		Generation CurrentGeneration() const noexcept { return _current.load(std::memory_order_acquire); }

		Generation GenerationFor(std::uint64_t a_liveConnectionEpoch) const noexcept
		{
			return a_liveConnectionEpoch == _connectionEpoch.load(std::memory_order_acquire) ? CurrentGeneration() : kNoSession;
		}

		bool Admits(Generation a_generation) const noexcept
		{
			return a_generation != kNoSession && a_generation == CurrentGeneration();
		}

		SessionState& Session() noexcept { return _session; }
		LocalPlayerCaptureState& Capture() noexcept { return _capture; }

	private:
		SessionState _session;
		LocalPlayerCaptureState _capture;
		Generation _lastGeneration{ kNoSession };
		std::atomic<std::uint64_t> _connectionEpoch{ 0 };
		std::atomic<Generation> _current{ kNoSession };
	};

	void NoteConnectionEstablished() noexcept;
	Generation CurrentGeneration() noexcept;
	void Tick();

	void PollLocalPlayerTransform(SessionState& a_session, LocalPlayerCaptureState& a_capture);
	void SendPendingCombatHits(const Coordinator& a_runtime);
}

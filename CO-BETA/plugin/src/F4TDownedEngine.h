#pragma once

#include "F4TDownedState.h"

#include "RE/A/Actor.h"
#include "RE/A/ActorValue.h"
#include "RE/N/NiPoint3.h"
#include "RE/P/PlayerCharacter.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace F4T::Downed::Engine
{
	double NowSeconds();

	void LogInfo(std::string_view a_message);

	[[nodiscard]] bool CanTouchWorld();

	[[nodiscard]] RE::PlayerCharacter* LocalPlayer();
	[[nodiscard]] const RE::ActorValueInfo* HealthInfo();

	[[nodiscard]] float GetMaximumHealth(RE::Actor* a_actor, const RE::ActorValueInfo& a_health);
	[[nodiscard]] bool SetHealth(RE::Actor& a_actor, const RE::ActorValueInfo& a_health, float a_target);

	bool SetPose(RE::Actor& a_actor, bool a_downed);
	bool SetDownedFlags(RE::Actor& a_actor, bool a_downed);
	[[nodiscard]] bool IsEssentialDown(RE::Actor& a_actor);
	bool SetControlBlocked(RE::Actor& a_actor, bool a_blocked);
	void ReleaseStaleControlBlock();

	void ApplySessionFlags(bool a_apply);
	void RequestBleedoutRecovery();
	void CancelBleedoutRecovery();
	void ClearBleedoutGiveUp();
	void UpdateBleedoutRecovery(double a_nowSeconds);
	void RestoreLocalPlayerHealth();
	void RequestCameraRestore();
	void RequestWeaponReset();
	void ClearPostRecoveryState();
	void ForgetCamera();
	void SampleLocalCamera();
	[[nodiscard]] bool IsCameraInBleedout();
	void RestoreBleedoutCamera();
	void LogWeaponState(RE::Actor& a_actor, std::string_view a_tag);
	void LogLocalBleedoutState(std::string_view a_tag);
	[[nodiscard]] bool IsInBleedoutAnimation(RE::Actor& a_actor);

	void InstallReviveInput();
	[[nodiscard]] bool IsActivateHeld();
	void ClearActivateHeld();

	void RecordLastBed();
	[[nodiscard]] bool HasLastBedCell();
	void TeleportForRespawn(
		RespawnPoint a_point,
		std::optional<std::uint32_t> a_friendId,
		const std::optional<Checkpoint>& a_checkpoint);

	[[nodiscard]] bool CaptureCheckpoint(Checkpoint& a_out);
	[[nodiscard]] bool TeleportToCheckpoint(const Checkpoint& a_checkpoint);
	void ReseatLocalPlayer();

	[[nodiscard]] std::optional<std::uint32_t> CountStimpaks();
	bool SpendStimpak();
	bool SetCanStimpak(RE::Actor& a_actor, bool a_on);
	[[nodiscard]] ReviveIdleFacts ReadReviveIdleFacts(RE::Actor& a_target);
	bool EnsureCanStimpak(RE::Actor& a_target);
	bool PlayStimpakReviveIdle(RE::Actor& a_reviver, RE::Actor& a_target);
	bool DropHeldItem(RE::Actor& a_actor);

	[[nodiscard]] float DistanceBetween(const RE::NiPoint3& a_lhs, const RE::NiPoint3& a_rhs);
	[[nodiscard]] float FacingCosine(const RE::NiPoint3& a_from, float a_heading, const RE::NiPoint3& a_to);
}

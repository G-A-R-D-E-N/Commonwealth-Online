#include "pch.h"

#include "F4TDownedEngine.h"

#include "F4TDebugLogging.h"
#include "F4TDownedScript.h"

namespace
{
	constexpr std::string_view kLogComponent{ "Downed" };

	bool g_sessionFlagsApplied = false;
	bool g_sessionEndGiveUpCleared = false;
	F4T::Downed::BleedoutRecoveryRunner g_bleedoutRecovery;
}

namespace F4T::Downed::Engine
{
	void ApplySessionFlags(bool a_apply)
	{
		if (g_sessionFlagsApplied == a_apply) {
			return;
		}
		auto* player = LocalPlayer();
		if (!player) {
			return;
		}
		if (a_apply) {
			g_sessionEndGiveUpCleared = false;
			player->boolFlags.reset(
				RE::Actor::BOOL_FLAGS::kMovementBlocked,
				RE::Actor::BOOL_FLAGS::kAttackingDisabled);
			if (!SetDownedFlags(*player, true)) {
				return;
			}
		} else {
			if (!CanTouchWorld()) {
				return;
			}
			SetControlBlocked(*player, false);
			if (IsEssentialDown(*player)) {
				if (!g_sessionEndGiveUpCleared) {
					ClearBleedoutGiveUp();
					g_sessionEndGiveUpCleared = true;
				}
				RequestBleedoutRecovery();
				UpdateBleedoutRecovery(NowSeconds());
				return;
			}
			if (!SetPose(*player, false)) {
				return;
			}
			if (!SetDownedFlags(*player, false)) {
				return;
			}
			g_bleedoutRecovery.Reset();
			g_sessionEndGiveUpCleared = false;
		}
		g_sessionFlagsApplied = a_apply;
		F4T::DebugLogging::Log(kLogComponent, "session essential/bleedout flags applied={}", a_apply);
	}

	void RequestBleedoutRecovery()
	{
		g_bleedoutRecovery.Request();
	}

	void CancelBleedoutRecovery()
	{
		g_bleedoutRecovery.ClearGaveUp();
		auto* player = LocalPlayer();
		g_bleedoutRecovery.Cancel(NowSeconds(), player && CanTouchWorld(), [player](bool a_noRecovery) {
			return F4T::Downed::Script::CallActorBool(*player, "SetNoBleedoutRecovery", a_noRecovery);
		});
	}

	void ClearBleedoutGiveUp()
	{
		g_bleedoutRecovery.ClearGaveUp();
	}

	void UpdateBleedoutRecovery(double a_nowSeconds)
	{
		auto* player = LocalPlayer();
		const bool canTouch = player && CanTouchWorld();
		if (canTouch && player->IsDead(true)) {
			CancelBleedoutRecovery();
			return;
		}
		const bool essentialDown = canTouch && IsEssentialDown(*player);
		const auto attemptsBefore = g_bleedoutRecovery.Attempts();
		const auto step = g_bleedoutRecovery.Update(
			a_nowSeconds, canTouch, essentialDown, g_sessionFlagsApplied, [player](bool a_noRecovery) {
				return F4T::Downed::Script::CallActorBool(*player, "SetNoBleedoutRecovery", a_noRecovery);
			});
		switch (step) {
		case F4T::Downed::RecoveryStep::kStoodUp:
			SetPose(*player, false);
			break;
		case F4T::Downed::RecoveryStep::kGaveUp:
			if (IsEssentialDown(*player)) {
				player->Resurrect(false, true);
				ReseatLocalPlayer();
			}
			SetPose(*player, false);
			LogInfo("essential-down recovery reached the dispatch cap; gave up.");
			break;
		case F4T::Downed::RecoveryStep::kDispatched:
			if (attemptsBefore == 0) {
				F4T::DebugLogging::Log(kLogComponent, "essential-down local player: NoBleedoutRecovery clear queued attempt=1");
			}
			break;
		default:
			break;
		}
	}
}

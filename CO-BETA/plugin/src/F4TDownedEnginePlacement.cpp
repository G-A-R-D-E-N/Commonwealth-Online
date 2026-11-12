#include "pch.h"

#include "F4TDownedEngine.h"

#include "F4TDebugLogging.h"
#include "F4TLocalPlayerMovement.h"

#include "RE/T/TESForm.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESObjectREFR.h"

#include <optional>

namespace
{
	constexpr std::string_view kLogComponent{ "Downed" };
	constexpr double kPlacementTimeoutSeconds = 30.0;

	struct PendingPlacement
	{
		F4T::Downed::Checkpoint checkpoint;
		double deadlineSeconds{};
		std::uint64_t generation{};
	};

	std::optional<PendingPlacement> g_pending;
	std::uint64_t g_generation = 0;
}

namespace F4T::Downed::Engine
{
	void CancelPendingPlacement()
	{
		++g_generation;
		g_pending.reset();
	}

	void QueueCheckpointPlacement(const Checkpoint& a_checkpoint)
	{
		g_pending = PendingPlacement{ a_checkpoint, NowSeconds() + kPlacementTimeoutSeconds, g_generation };
	}

	void UpdatePendingPlacement(double a_nowSeconds)
	{
		if (!g_pending) {
			return;
		}
		if (g_pending->generation != g_generation) {
			g_pending.reset();
			return;
		}
		if (a_nowSeconds >= g_pending->deadlineSeconds) {
			F4T::DebugLogging::Log(
				kLogComponent,
				"checkpoint placement timed out for cell {:08X}; leaving the player at the cell marker",
				g_pending->checkpoint.space);
			g_pending.reset();
			return;
		}
		auto* player = LocalPlayer();
		if (!player || player->IsDead(true)) {
			g_pending.reset();
			return;
		}
		if (!CanTouchWorld()) {
			return;
		}
		auto* cell = RE::TESForm::GetFormByID<RE::TESObjectCELL>(g_pending->checkpoint.space);
		if (!cell) {
			g_pending.reset();
			return;
		}
		if (player->GetParentCell() != cell || player->Get3D() == nullptr) {
			return;
		}
		F4T::LocalPlayerMovement::RequestExplicitTeleport();
		PlacePlayerAtCheckpoint(*player, g_pending->checkpoint);
		F4T::DebugLogging::Log(
			kLogComponent,
			"checkpoint placement applied in cell {:08X} ({:.0f},{:.0f},{:.0f})",
			g_pending->checkpoint.space,
			g_pending->checkpoint.x,
			g_pending->checkpoint.y,
			g_pending->checkpoint.z);
		g_pending.reset();
	}
}

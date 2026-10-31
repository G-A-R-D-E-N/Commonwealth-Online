#include "pch.h"

#include "F4TPowerArmorSync.h"
#include "F4TProxyActorControllerInternal.h"

using namespace F4T::ProxyActorController::detail;

namespace F4T::ProxyActorController::detail
{
	void ApplyProxyPowerArmorFromRemoteState(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer)
	{
		auto& frame = a_slot.placedPowerArmorFrame;
		const auto logKey = std::to_string(a_remotePlayer.playerId);
		if (!a_remotePlayer.hasPowerArmorUpdate) {
			if (frame.baseFormId != 0 && a_slot.appliedPowerArmorFrameFormId == 0 &&
				F4T::PowerArmorSync::ApplyProxyPowerArmorFrame(a_proxy, frame.baseFormId, frame, logKey)) {
				a_slot.appliedPowerArmorFrameFormId = frame.baseFormId;
				RefreshProxyAfterEquipmentChange(a_slot, a_proxy);
			}
			return;
		}

		const F4T::PowerArmorSync::PowerArmorState current = F4T::PowerArmorSync::NormalizeForApply(F4T::PowerArmorSync::PowerArmorState{
			a_remotePlayer.powerArmorInFrame,
			a_remotePlayer.powerArmorFrameBaseFormId,
			a_remotePlayer.powerArmorPieces
		});
		const auto frameFormId = current.inFrame ? current.frameBaseFormId : 0U;

		bool changed = false;
		if (frame.baseFormId != frameFormId) {
			const auto unequipped = F4T::PowerArmorSync::UnequipProxyPowerArmorFrame(a_proxy, frame, logKey);
			changed = unequipped || changed;
			if (unequipped) {
				a_slot.appliedPowerArmorFrameFormId = 0;
			}
		}
		if (frameFormId != 0 && a_slot.appliedPowerArmorFrameFormId == 0 &&
			F4T::PowerArmorSync::ApplyProxyPowerArmorFrame(a_proxy, frameFormId, frame, logKey)) {
			a_slot.appliedPowerArmorFrameFormId = frameFormId;
			changed = true;
		}
		if (frameFormId != 0 && a_slot.appliedPowerArmorFrameFormId == 0) {
			LogThrottledWarning(
				"proxy_power_armor_frame_unapplied_" + logKey,
				std::format(
					"Remote player {} is in power armor frame {:08X}; the frame is not entered on proxy {:08X} yet (the engine reports inFrame={} for the proxy), only the pieces are.",
					a_remotePlayer.playerId,
					frameFormId,
					a_proxy.GetFormID(),
					F4T::PowerArmorSync::ProxyReportsInPowerArmor(a_proxy)));
		}

		const auto ops = F4T::PowerArmorSync::PlanPieceOperations(a_slot.lastAppliedPowerArmorState, current);
		if (ops.empty()) {
			a_slot.lastAppliedPowerArmorState = current;
		} else {
			const auto applied = F4T::PowerArmorSync::ApplyProxyPowerArmorPlan(
				a_proxy,
				a_slot.lastAppliedPowerArmorState,
				current,
				ops,
				logKey);
			a_slot.lastAppliedPowerArmorState = applied.applied;
			changed = changed || applied.changed;
		}

		if (changed) {
			RefreshProxyAfterEquipmentChange(a_slot, a_proxy);
			LogInfoWithLocalPlayerPrefix(std::format(
				"Applied synced power armor to runtime proxy actor {:08X} for remote player {}: inFrame={}, frameFormId={:08X}, pieces={}, operations={}.",
				a_proxy.GetFormID(),
				a_remotePlayer.playerId,
				current.inFrame,
				a_slot.appliedPowerArmorFrameFormId,
				current.pieces.size(),
				ops.size()));
		}
	}
}

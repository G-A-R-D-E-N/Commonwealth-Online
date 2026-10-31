#include "pch.h"

#include "F4TProxyActorControllerInternal.h"

using namespace F4T::ProxyActorController::detail;

namespace
{
	constexpr float kExteriorProxyRepresentationRange = 8192.0F;
}

namespace F4T::ProxyActorController::detail
{
	bool IsRemotePlayerInSameLocation(
		const RemotePlayer& a_remotePlayer,
		const RE::PlayerCharacter& a_player,
		const RE::TESObjectCELL& a_parentCell)
	{
		if (F4T::SoloCell::IsSoloCell(a_parentCell)) {
			LogThrottledInfo(
				"solo_cell_proxy_suppressed",
				"Character setup cell is solo; remote proxy representation is suppressed while local player is inside.");
			return false;
		}

		const auto remoteCellId = ParseHexFormId(a_remotePlayer.cellId);
		if (!remoteCellId) {
			LogThrottledWarning(
				"remote_cell_invalid_" + std::to_string(a_remotePlayer.playerId),
				std::format(
					"Remote player {} has an invalid cellId '{}'; proxy movement is skipped.",
					a_remotePlayer.playerId,
					a_remotePlayer.cellId));
			return false;
		}

		const auto localCellId = a_parentCell.GetFormID();
		if (localCellId == *remoteCellId) {
			const auto remoteWorldspaceId = ParseHexFormId(a_remotePlayer.worldspaceId);
			const auto localWorldspaceId = GetLocalWorldspaceId(a_player, a_parentCell);
			if (!localWorldspaceId && !remoteWorldspaceId) {
				return true;
			}

			if (!localWorldspaceId || !remoteWorldspaceId || *localWorldspaceId != *remoteWorldspaceId) {
				LogThrottledInfo(
					"remote_worldspace_mismatch_" + std::to_string(a_remotePlayer.playerId),
					std::format(
						"Remote player {} is in a different worldspace: local={}, remote={}. Proxy movement is skipped.",
						a_remotePlayer.playerId,
						localWorldspaceId ? std::format("{:08X}", *localWorldspaceId) : "unavailable",
						a_remotePlayer.worldspaceId.empty() ? "unavailable" : a_remotePlayer.worldspaceId));
				return false;
			}

			return true;
		}

		if (a_parentCell.IsExterior()) {
			const auto remoteWorldspaceId = ParseHexFormId(a_remotePlayer.worldspaceId);
			const auto localWorldspaceId = GetLocalWorldspaceId(a_player, a_parentCell);
			if (localWorldspaceId && remoteWorldspaceId && *localWorldspaceId == *remoteWorldspaceId) {
				const auto localPosition = a_player.GetPosition();
				const auto horizontalDistance = std::hypot(
					a_remotePlayer.x - localPosition.x,
					a_remotePlayer.y - localPosition.y);
				if (!std::isfinite(horizontalDistance) ||
					horizontalDistance > kExteriorProxyRepresentationRange) {
					LogThrottledInfo(
						"remote_same_worldspace_out_of_range_" + std::to_string(a_remotePlayer.playerId),
						std::format(
							"Remote player {} is in the same exterior worldspace but outside proxy load range ({:.1f} > {:.1f}); proxy representation is held. localCell={:08X}, remoteCell={}.",
							a_remotePlayer.playerId,
							horizontalDistance,
							kExteriorProxyRepresentationRange,
							localCellId,
							a_remotePlayer.cellId));
					return false;
				}

				LogThrottledInfo(
					"remote_same_worldspace_" + std::to_string(a_remotePlayer.playerId),
					std::format(
						"Remote player {} is in a nearby exterior cell in the same worldspace; proxy movement continues. distance={:.1f}, localCell={:08X}, remoteCell={}.",
						a_remotePlayer.playerId,
						horizontalDistance,
						localCellId,
						a_remotePlayer.cellId));
				return true;
			}
		}

		LogThrottledInfo(
			"remote_cell_mismatch_" + std::to_string(a_remotePlayer.playerId),
			std::format(
				"Remote player {} is in a different location: local={:08X}, remote={}. Proxy movement is skipped.",
				a_remotePlayer.playerId,
				localCellId,
				a_remotePlayer.cellId));
		return false;
	}

	bool IsProxyInSamePlaySpace(
		const RE::TESObjectCELL& a_proxyCell,
		const RE::TESObjectCELL& a_localCell)
	{
		if (std::addressof(a_proxyCell) == std::addressof(a_localCell)) {
			return true;
		}

		if (a_localCell.IsExterior() && a_proxyCell.IsExterior()) {
			return a_localCell.worldSpace != nullptr &&
			       a_localCell.worldSpace == a_proxyCell.worldSpace;
		}

		return a_proxyCell.GetFormID() == a_localCell.GetFormID();
	}

	bool IsValidProxyActor(const RE::Actor& a_proxy, const RE::TESObjectCELL& a_expectedCell, const RE::PlayerCharacter& a_player)
	{
		if (std::addressof(a_proxy) == std::addressof(a_player)) {
			return false;
		}

		const auto* proxyCell = a_proxy.GetParentCell();
		return proxyCell && IsProxyInSamePlaySpace(*proxyCell, a_expectedCell);
	}

	bool IsRuntimeProxyRef(const RE::TESObjectREFR& a_ref)
	{
		for (const auto& [_, slot] : g_proxySlots) {
			const auto spawnedRef = slot.proxyHandle.get();
			if (spawnedRef && spawnedRef.get() == std::addressof(a_ref)) {
				return true;
			}
			const auto pendingRef = slot.pendingSpawnHandle.get();
			if (pendingRef && pendingRef.get() == std::addressof(a_ref)) {
				return true;
			}
		}

		if (!g_runtimeProxyHandle) {
			return false;
		}

		const auto spawnedRef = g_runtimeProxyHandle.get();
		return spawnedRef && spawnedRef.get() == std::addressof(a_ref);
	}

	bool IsProxyPoolActorAssigned(const RE::Actor& a_actor)
	{
		for (const auto& [_, slot] : g_proxySlots) {
			const auto assignedRef = slot.proxyHandle.get();
			if (assignedRef && assignedRef.get() == std::addressof(a_actor)) {
				return true;
			}

			const auto pendingRef = slot.pendingSpawnHandle.get();
			if (pendingRef && pendingRef.get() == std::addressof(a_actor)) {
				return true;
			}

			if (slot.cachedProxyFormId && *slot.cachedProxyFormId == a_actor.GetFormID()) {
				return true;
			}
		}

		return false;
	}
}

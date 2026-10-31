#include "pch.h"

#include "F4TProxyGameAPI.h"

#include "F4TDebugLogging.h"
#include "F4TNetworking.h"

#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESWorldSpace.h"

#include <chrono>
#include <cmath>
#include <format>
#include <unordered_map>

namespace
{
	struct ActorMoveState
	{
		std::uint32_t callCount{ 0 };
		std::chrono::steady_clock::time_point lastTime{};
		float lastX{ 0.0F };
		float lastY{ 0.0F };
		float lastZ{ 0.0F };
	};

	static std::unordered_map<RE::Actor*, ActorMoveState> g_moveState;

	constexpr double kMoveIntervalSeconds = 1.0 / 20.0;
	constexpr float kMoveDeltaSq = 0.25F;
	constexpr std::uint32_t kForcedMoveCalls = 5;

	std::string GetLocalPlayerLogMessage(std::string_view a_message)
	{
		return F4T::Networking::GetLocalPlayerLogPrefix() + " " + std::string(a_message);
	}

	void LogThrottledCellFix(
		RE::Actor& a_proxy,
		const RE::NiPoint3& a_pos,
		RE::TESObjectCELL* a_cell,
		RE::TESWorldSpace* a_worldSpace,
		std::uint32_t a_callCount,
		bool a_migratedPlaySpace)
	{
		if (!F4T::DebugLogging::IsEnabled() ||
			(a_callCount > kForcedMoveCalls && a_callCount % 300 != 0)) {
			return;
		}

		const auto cellId = a_cell ? a_cell->GetFormID() : 0U;
		const auto worldId = a_worldSpace ? a_worldSpace->GetFormID() : 0U;
		F4T::DebugLogging::Log(
			"ProxyGameAPI",
			"{}",
			GetLocalPlayerLogMessage(std::format(
				"PositionRemoteActor proxy={:08X} call#{} pos=({:.1f},{:.1f},{:.1f}) mode={} ws={:08X} cell={:08X} migrated={}.",
				a_proxy.GetFormID(),
				a_callCount,
				a_pos.x,
				a_pos.y,
				a_pos.z,
				a_worldSpace ? "exterior" : (a_cell ? "interior" : "no-anchor"),
				worldId,
				cellId,
				a_migratedPlaySpace)));
	}

	void ResolveLocalPlaySpace(
		RE::TESObjectCELL*& a_outCell,
		RE::TESWorldSpace*& a_outWorldSpace)
	{
		a_outCell = nullptr;
		a_outWorldSpace = nullptr;

		const auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		const auto* playerCell = player->GetParentCell();
		if (!playerCell) {
			return;
		}

		if (playerCell->IsExterior()) {
			a_outWorldSpace = playerCell->worldSpace;
			if (!a_outWorldSpace) {
				a_outWorldSpace = player->cachedWorldspace;
			}
			return;
		}

		a_outCell = const_cast<RE::TESObjectCELL*>(playerCell);
	}

	bool IsFinitePosition(const RE::NiPoint3& a_pos)
	{
		return std::isfinite(a_pos.x) && std::isfinite(a_pos.y) && std::isfinite(a_pos.z);
	}

	bool ProxyInLocalPlaySpace(
		const RE::Actor& a_proxy,
		RE::TESObjectCELL* a_localCell,
		RE::TESWorldSpace* a_localWorldSpace)
	{
		const auto* proxyCell = a_proxy.GetParentCell();
		if (!proxyCell) {
			return false;
		}

		if (a_localWorldSpace) {
			return proxyCell->IsExterior() && proxyCell->worldSpace == a_localWorldSpace;
		}

		return a_localCell && proxyCell->GetFormID() == a_localCell->GetFormID();
	}

	void ApplyPositionInPlaySpace(
		RE::Actor& a_proxy,
		const RE::NiPoint3& a_pos,
		const RE::NiPoint3& a_rot,
		RE::TESObjectCELL* a_cell,
		RE::TESWorldSpace* a_worldSpace,
		std::uint32_t a_callCount)
	{
		const bool migrated =
			!ProxyInLocalPlaySpace(a_proxy, a_cell, a_worldSpace);
		if (migrated) {
			a_proxy.MoveRefToNewSpace(a_cell, a_worldSpace);
		}

		a_proxy.SetPosition(a_pos, true);
		a_proxy.SetAngleOnReference(a_rot);
		a_proxy.SetHeading(a_rot.z);

		LogThrottledCellFix(a_proxy, a_pos, a_cell, a_worldSpace, a_callCount, migrated);
	}
}

namespace F4T::ProxyGameAPI
{
	void PositionRemoteActor(RE::Actor& a_proxy, const RE::NiPoint3& a_pos, float a_headingZ, bool a_force)
	{
		PositionRemoteActor(a_proxy, a_pos, RE::NiPoint3{ 0.0F, 0.0F, a_headingZ }, a_force);
	}

	void PositionRemoteActor(
		RE::Actor& a_proxy,
		const RE::NiPoint3& a_pos,
		const RE::NiPoint3& a_rot,
		bool a_force)
	{
		if (!IsFinitePosition(a_pos)) {
			REX::WARN(std::string_view{ GetLocalPlayerLogMessage(std::format(
				"PositionRemoteActor skipped for proxy {:08X}: non-finite target position.",
				a_proxy.GetFormID())) });
			return;
		}

		RE::TESObjectCELL* cell = nullptr;
		RE::TESWorldSpace* worldSpace = nullptr;
		ResolveLocalPlaySpace(cell, worldSpace);
		if (!cell && !worldSpace) {
			REX::WARN(std::string_view{ GetLocalPlayerLogMessage(std::format(
				"PositionRemoteActor skipped for proxy {:08X}: local play space unavailable.",
				a_proxy.GetFormID())) });
			return;
		}

		auto& moveState = g_moveState[&a_proxy];
		++moveState.callCount;

		const float dx = a_pos.x - moveState.lastX;
		const float dy = a_pos.y - moveState.lastY;
		const float dz = a_pos.z - moveState.lastZ;
		const float deltaSq = (dx * dx) + (dy * dy) + (dz * dz);

		const auto now = std::chrono::steady_clock::now();
		const bool forceMove = a_force || moveState.callCount <= kForcedMoveCalls;
		const bool timedOut = moveState.lastTime.time_since_epoch().count() == 0 ||
		                      std::chrono::duration<double>(now - moveState.lastTime).count() >= kMoveIntervalSeconds;
		const bool movedEnough = deltaSq >= kMoveDeltaSq;

		if (!forceMove && !(timedOut && movedEnough)) {
			return;
		}

		moveState.lastX = a_pos.x;
		moveState.lastY = a_pos.y;
		moveState.lastZ = a_pos.z;
		moveState.lastTime = now;

		ApplyPositionInPlaySpace(a_proxy, a_pos, a_rot, cell, worldSpace, moveState.callCount);
	}
}

#include "pch.h"

#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"

// Source:https://git.nomadicinteractive.dev/GARDEN/CommonwealthOnline-Matrix/src/commit/e78801911c6d0d9628a49773236bb4874a62402e/CO-BETA/plugin/src/F4TNetworking.cpp; protocol field names and encoding must remain unchanged.
#include "F4TPvPCombatContract.h"
#include "F4TDebugLogging.h"
#include "F4TNetworkValidation.h"
#include "F4TReceivePolicy.h"
#include "F4TRemoteNpcState.h"
#include "F4TRemotePlayerState.h"
#include "F4TReplicationRuntime.h"
#include "F4TSessionLaunch.h"
#include "F4TUIClient.h"
#include "F4TWorldStateSync.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace F4T::Networking
{
	using namespace Detail;
	bool SendWorldStatePacket(
		float a_gameHour,
		float a_gameDaysPassed,
		bool a_hasWeatherFormId,
		std::uint32_t a_weatherFormId,
		bool a_timeSync)
	{
		if (!IsConnectedToServer()) {
			return false;
		}

		if (!std::isfinite(a_gameHour) || !std::isfinite(a_gameDaysPassed)) {
			return false;
		}

		const auto clientTime =
			std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();

		std::array<char, 320> packet{};
		int packetSize = 0;
		if (a_hasWeatherFormId) {
			packetSize = std::snprintf(
				packet.data(),
				packet.size(),
				"{\"type\":\"worldState\",\"gameHour\":%.3f,\"gameDaysPassed\":%.3f,\"weatherFormId\":\"%08X\",\"timeSync\":%s,\"clientTime\":%.3f}",
				a_gameHour,
				a_gameDaysPassed,
				a_weatherFormId,
				a_timeSync ? "true" : "false",
				clientTime);
		} else {
			packetSize = std::snprintf(
				packet.data(),
				packet.size(),
				"{\"type\":\"worldState\",\"gameHour\":%.3f,\"gameDaysPassed\":%.3f,\"timeSync\":%s,\"clientTime\":%.3f}",
				a_gameHour,
				a_gameDaysPassed,
				a_timeSync ? "true" : "false",
				clientTime);
		}

		if (packetSize <= 0 || static_cast<std::size_t>(packetSize) >= packet.size()) {
			LogWarningWithLocalPlayerPrefix("Could not format Commonwealth Online worldState packet.");
			return false;
		}

		if (!SendPacketBytes(std::string_view{ packet.data(), static_cast<std::size_t>(packetSize) })) {
			LogThrottledWarning("world_state_send_failed", "Could not send Commonwealth Online worldState packet.");
			return false;
		}
		return true;
	}

	bool SendNpcStatePacket(
		std::span<const F4T::RemoteNpcState::NpcState> a_npcs,
		std::uint64_t a_authorityEpoch,
		std::string_view a_authorityCellId,
		std::string_view a_authorityWorldspaceId)
	{
		if (!IsConnectedToServer() || a_npcs.size() > 16 || a_authorityEpoch == 0 ||
			!F4T::NetworkValidation::IsValidHexFormId(a_authorityCellId, false, false) ||
			!F4T::NetworkValidation::IsValidHexFormId(a_authorityWorldspaceId, true, true)) {
			return false;
		}

		nlohmann::json packet{
			{ "type", "npcState" },
			{ "fullReplace", true },
			{ "authorityEpoch", a_authorityEpoch },
			{ "authorityCellId", a_authorityCellId },
			{ "authorityWorldspaceId", a_authorityWorldspaceId },
			{ "clientTime", std::chrono::duration<double>(
					std::chrono::system_clock::now().time_since_epoch()).count() }
		};
		auto& npcs = packet["npcs"] = nlohmann::json::array();
		for (const auto& npc : a_npcs) {
			if (npc.npcId == 0 || npc.baseFormId == 0 || npc.sourceFormId == 0 ||
				F4T::RemoteNpcState::IsRuntimeFormId(npc.baseFormId) ||
				F4T::RemoteNpcState::IsRuntimeFormId(npc.sourceFormId) ||
				!F4T::NetworkValidation::IsValidCoordinate(npc.x) ||
				!F4T::NetworkValidation::IsValidCoordinate(npc.y) ||
				!F4T::NetworkValidation::IsValidCoordinate(npc.z) ||
				!F4T::NetworkValidation::IsValidCoordinate(npc.angleZ) ||
				!F4T::NetworkValidation::IsValidMovementSpeed(npc.movementSpeed)) {
				continue;
			}

			char baseFormId[9]{};
			char sourceFormId[9]{};
			std::snprintf(baseFormId, sizeof(baseFormId), "%08X", npc.baseFormId);
			std::snprintf(sourceFormId, sizeof(sourceFormId), "%08X", npc.sourceFormId);
			nlohmann::json entry{
				{ "npcId", npc.npcId },
				{ "baseFormId", baseFormId },
				{ "sourceFormId", sourceFormId },
				{ "x", npc.x },
				{ "y", npc.y },
				{ "z", npc.z },
				{ "angleZ", npc.angleZ },
				{ "cellId", npc.cellId },
				{ "worldspaceId", npc.worldspaceId },
				{ "isDead", npc.isDead },
				{ "isMoving", npc.isMoving },
				{ "isSprinting", npc.isSprinting },
				{ "isSneaking", npc.isSneaking },
				{ "movementSpeed", npc.movementSpeed }
			};
			if (npc.hasHealth && std::isfinite(npc.health) && std::isfinite(npc.maxHealth) &&
				npc.health >= 0.0F && npc.health <= npc.maxHealth &&
				npc.maxHealth <= F4T::NetworkValidation::MaximumActorHealth) {
				entry["health"] = npc.health;
				entry["maxHealth"] = npc.maxHealth;
			}
			npcs.push_back(std::move(entry));
		}

		auto packetText = packet.dump();
		if (!SendPacketBytes(packetText)) {
			LogThrottledWarning("npc_state_send_failed", "Could not send Commonwealth Online npcState packet.");
			return false;
		}
		return true;
	}

	bool SendPlayerAnimEventPacket(std::string_view a_eventName)
	{
		nlohmann::json packet{ { "type", "playerAnimEvent" }, { "event", std::string{ a_eventName } } };
		const auto packetStr = packet.dump();
		if (!SendPacketBytes(packetStr)) {
			LogThrottledWarning("anim_event_send_failed", "Could not send Commonwealth Online playerAnimEvent packet.");
			return false;
		}
		return true;
	}
	
	bool SendProjectileAimPacket(double a_x, double a_y, double a_z)
	{
		nlohmann::json packet{ { "type", "projectileAim" }, { "direction", { { "x", a_x }, { "y", a_y }, { "z", a_z } } } };
		const auto packetStr = packet.dump();
		if (!SendPacketBytes(packetStr)) {
			LogThrottledWarning("aim_send_failed", "Could not send Commonwealth Online projectileAim packet.");
			return false;
		}
		return true;
	}
	
	bool SendCombatHitPacket(
		std::uint32_t a_sequence,
		std::uint32_t a_targetPlayerId,
		float a_damage,
		std::uint32_t a_attackSequence,
		std::uint32_t a_weaponFormId)
	{
		if (!IsConnectedToServer()) {
			return false;
		}

		if (a_sequence == 0 ||
			a_targetPlayerId == 0 ||
			a_attackSequence == 0 ||
			!std::isfinite(a_damage) ||
			a_damage <= 0.0F ||
			a_damage > 10000.0F) {
			LogWarningWithLocalPlayerPrefix(std::format(
				"Could not send invalid combatHit: sequence={}, targetPlayerId={}, attackSequence={}, damage={}.",
				a_sequence,
				a_targetPlayerId,
				a_attackSequence,
				a_damage));
			return false;
		}

		nlohmann::json packet{
			{ "type", "combatHit" },
			{ "sequence", a_sequence },
			{ "targetPlayerId", a_targetPlayerId },
			{ "attackSequence", a_attackSequence },
			{ "damage", a_damage }
		};

		if (a_weaponFormId != 0) {
			packet["weaponFormId"] = std::format("{:08X}", a_weaponFormId);
		}

		const auto packetStr = packet.dump();
		if (!SendPacketBytes(packetStr)) {
			LogThrottledWarning("combat_hit_send_failed", "Could not send Commonwealth Online combatHit packet.");
			return false;
		}
		return true;
	}

}

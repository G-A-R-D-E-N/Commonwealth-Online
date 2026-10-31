// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission.
#include "pch.h"

#include "F4TProxyActorControllerInternal.h"

using namespace F4T::ProxyActorController::detail;

namespace
{
	bool g_runtimeActorBaseSourceLogged = false;
	std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_lastWarningLogTimes;
	// Throttle keys include server-supplied player IDs; cap keeps the map bounded.
	constexpr std::size_t kMaximumThrottleKeys = 2048;
	std::mutex g_logThrottleMutex;

	std::string GetLocalPlayerLogMessage(std::string_view a_message)
	{
		return F4T::Networking::GetLocalPlayerLogPrefix() + " " + std::string(a_message);
	}
}

namespace F4T::ProxyActorController::detail
{
	void LogInfoWithLocalPlayerPrefix(std::string_view a_message)
	{
		F4T::DebugLogging::Write("ProxyActor", GetLocalPlayerLogMessage(a_message));
	}

	void LogWarningWithLocalPlayerPrefix(std::string_view a_message)
	{
		const auto message = GetLocalPlayerLogMessage(a_message);
		REX::WARN(std::string_view{ message });
	}

	bool ShouldLog(std::string a_key, std::chrono::steady_clock::duration a_interval)
	{
		const std::scoped_lock lock(g_logThrottleMutex);
		const auto now = std::chrono::steady_clock::now();
		const auto lastLog = g_lastWarningLogTimes.find(a_key);
		if (lastLog != g_lastWarningLogTimes.end() && now - lastLog->second < a_interval) {
			return false;
		}

		if (g_lastWarningLogTimes.size() >= kMaximumThrottleKeys) {
			g_lastWarningLogTimes.clear();
		}
		g_lastWarningLogTimes[std::move(a_key)] = now;
		return true;
	}

	void LogThrottledWarning(
		const std::string& a_key,
		std::string_view a_message,
		std::chrono::steady_clock::duration a_interval)
	{
		if (ShouldLog(a_key, a_interval)) {
			LogWarningWithLocalPlayerPrefix(a_message);
		}
	}

	void LogThrottledInfo(
		const std::string& a_key,
		std::string_view a_message,
		std::chrono::steady_clock::duration a_interval)
	{
		if (!F4T::DebugLogging::IsEnabled()) {
			return;
		}
		if (ShouldLog(a_key, a_interval)) {
			LogInfoWithLocalPlayerPrefix(a_message);
		}
	}

	const char* GetProxyNeutralizationReasonLabel(ProxyNeutralizationReason a_reason)
	{
		switch (a_reason) {
		case ProxyNeutralizationReason::kSpawnPromoted:
			return "spawn_promoted";
		case ProxyNeutralizationReason::kSlotReassigned:
			return "slot_reassigned";
		case ProxyNeutralizationReason::kActiveMaintenance:
			return "active_maintenance";
		case ProxyNeutralizationReason::kHolding:
			return "holding";
		default:
			return "unknown";
		}
	}

	std::string FormatPosition(const RE::NiPoint3& a_position)
	{
		return std::format("({:.2f}, {:.2f}, {:.2f})", a_position.x, a_position.y, a_position.z);
	}

	float GetDistance(const RE::NiPoint3& a_left, const RE::NiPoint3& a_right)
	{
		const auto deltaX = a_left.x - a_right.x;
		const auto deltaY = a_left.y - a_right.y;
		const auto deltaZ = a_left.z - a_right.z;
		return std::sqrt((deltaX * deltaX) + (deltaY * deltaY) + (deltaZ * deltaZ));
	}

	const char* GetActiveProxySourceForLog(ActiveProxyKind a_kind)
	{
		switch (a_kind) {
		case ActiveProxyKind::kRuntimeSpawned:
			return "runtime";
		case ActiveProxyKind::kPlacedFallback:
			return "placed fallback";
		default:
			return "unknown";
		}
	}

	const char* GetActiveProxyDescription(ActiveProxyKind a_kind)
	{
		switch (a_kind) {
		case ActiveProxyKind::kRuntimeSpawned:
			return "runtime proxy";
		case ActiveProxyKind::kPlacedFallback:
			return "placed fallback proxy";
		default:
			return "proxy";
		}
	}

	const char* GetRuntimeActorBaseSourceLabel(RuntimeActorBaseSource a_source)
	{
		switch (a_source) {
		case RuntimeActorBaseSource::kCustomProxy:
			return "custom COPlayerProxy base";
		case RuntimeActorBaseSource::kVanillaDiagnosticNpc:
			return "vanilla diagnostic NPC";
		default:
			return "unknown";
		}
	}

	void LogRuntimeActorBaseSourceIfNeeded(RuntimeActorBaseSource a_source)
	{
		if (g_runtimeActorBaseSourceLogged && g_runtimeProxyActorBaseSource == a_source) {
			return;
		}

		g_runtimeProxyActorBaseSource = a_source;
		g_runtimeActorBaseSourceLogged = true;
		LogInfoWithLocalPlayerPrefix(std::format(
			"Actor base source: {}",
			GetRuntimeActorBaseSourceLabel(a_source)));
	}

	const char* GetProxyMovementDiagnosticModeName(ProxyMovementDiagnosticMode a_mode)
	{
		switch (a_mode) {
		case ProxyMovementDiagnosticMode::kRestore:
			return "restore";
		case ProxyMovementDiagnosticMode::kSmooth:
			return "smooth";
		case ProxyMovementDiagnosticMode::kSnap:
			return "snap";
		default:
			return "unknown";
		}
	}

	std::string GetActiveProxyLogName(ActiveProxyKind a_kind)
	{
		switch (a_kind) {
		case ActiveProxyKind::kRuntimeSpawned:
			return std::format("runtime proxy actor spawned from {}", kProxyBaseActorEditorId);
		case ActiveProxyKind::kPlacedFallback:
			return std::format("placed fallback proxy actor {}", kProxyEditorId);
		default:
			return "proxy actor";
		}
	}

	void LogReceivedEquipment(const RemotePlayer& a_remotePlayer)
	{
		std::string itemsDebug;
		for (const auto& item : a_remotePlayer.equippedItems) {
			if (!itemsDebug.empty()) itemsDebug += ", ";
			itemsDebug += std::format("{}={:08X}", item.slot, item.formId);
		}
		LogThrottledInfo(
			"proxy_equip_received_items_" + std::to_string(a_remotePlayer.playerId),
			std::format(
				"ApplyProxyEquipmentFromRemoteState for remote player {}: received=[{}]",
				a_remotePlayer.playerId,
				itemsDebug),
			std::chrono::seconds(1));
	}
}

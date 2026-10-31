#include "pch.h"

#include "F4TWorldStateSync.h"
#include "F4TWorldTimeMath.h"

#include "F4TNetworking.h"
#include "F4TCrashForensics.h"
#include "F4TDebugLogging.h"
#include "F4TRemotePlayerState.h"

#include "RE/B/BGSSaveLoadManager.h"
#include "RE/C/Calendar.h"
#include "RE/L/LoadingMenu.h"
#include "RE/M/Main.h"
#include "RE/M/MainMenu.h"
#include "RE/P/PipboyMenu.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/S/SitWaitMenu.h"
#include "RE/S/Sky.h"
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESWeather.h"
#include "RE/U/UI.h"

#include <cmath>
#include <charconv>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace
{
	constexpr auto kWorldStateHeartbeatInterval = 1s;
	constexpr float kGameHourSendThreshold = 0.05F;
	constexpr float kGameDaysPassedSendThreshold = 0.001F;
	constexpr float kGameHourApplyThreshold = 0.05F;
	constexpr float kGameDaysPassedApplyThreshold = 0.001F;
	constexpr auto kWorldStateLogInterval = 2s;

	std::mutex g_pendingMutex;
	bool g_hasPendingWorldState{ false };
	F4T::WorldStateSync::PendingWorldState g_pendingWorldState{};
	bool g_hasPendingServerWorldState{ false };
	F4T::WorldStateSync::PendingServerWorldState g_pendingServerWorldState{};
	std::optional<std::uint32_t> g_pendingWorldStateHostPlayerId{};
	bool g_resetRequested{ false };

	struct LastSentWorldState
	{
		float gameHour{ -1.0F };
		float gameDaysPassed{ -1.0F };
		std::optional<std::uint32_t> weatherFormId;
		bool hasWeatherFormId{ false };
		bool initialized{ false };
	};

	struct LastAppliedWorldState
	{
		float gameHour{ -1.0F };
		float gameDaysPassed{ -1.0F };
		std::optional<std::uint32_t> weatherFormId;
		bool hasWeatherFormId{ false };
		bool initialized{ false };
	};

	LastSentWorldState g_lastSentWorldState{};
	LastAppliedWorldState g_lastAppliedWorldState{};
	auto g_lastHostSendTime = std::chrono::steady_clock::time_point{};
	std::optional<std::uint32_t> g_worldStateHostPlayerId{};
	std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_lastLogTimes;
	bool g_wasGameTimeFrozen{ false };

	struct LatestHostTime
	{
		float gameHour{ -1.0F };
		float gameDaysPassed{ -1.0F };
		bool initialized{ false };
	};

	LatestHostTime g_latestHostTime{};

	std::string GetLocalPlayerLogMessage(std::string_view a_message)
	{
		return F4T::Networking::GetLocalPlayerLogPrefix() + " " + std::string(a_message);
	}

	void LogInfo(std::string_view a_message)
	{
		F4T::DebugLogging::Write("WorldState", GetLocalPlayerLogMessage(a_message));
	}

	void LogWarning(std::string_view a_message)
	{
		const auto message = GetLocalPlayerLogMessage(a_message);
		REX::WARN(std::string_view{ message });
	}

	bool ShouldLog(const std::string& a_key)
	{
		if (!F4T::DebugLogging::IsEnabled()) {
			return false;
		}
		const auto now = std::chrono::steady_clock::now();
		const auto lastLog = g_lastLogTimes.find(a_key);
		if (lastLog != g_lastLogTimes.end() && now - lastLog->second < kWorldStateLogInterval) {
			return false;
		}

		g_lastLogTimes[a_key] = now;
		return true;
	}

	bool IsLocalPlayerExterior()
	{
		const auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return false;
		}

		const auto* playerCell = player->GetParentCell();
		return playerCell && playerCell->IsExterior();
	}

	bool IsGameTimeFrozen()
	{
		const auto* main = RE::Main::GetSingleton();
		if (main && (main->freezeTime || main->inMenuMode || main->inDialog)) {
			return true;
		}

		const auto* player = RE::PlayerCharacter::GetSingleton();
		if (player && player->isSleeping) {
			return true;
		}

		const auto* ui = RE::UI::GetSingleton();
		if (ui) {
			if (ui->GetMenuOpen<RE::LoadingMenu>() ||
				ui->GetMenuOpen<RE::MainMenu>() ||
				ui->GetMenuOpen<RE::SitWaitMenu>() ||
				ui->GetMenuOpen<RE::PipboyMenu>()) {
				return true;
			}
		}

		const auto* saveLoadManager = RE::BGSSaveLoadManager::GetSingleton();
		if (saveLoadManager &&
			(saveLoadManager->saveLoadTasksThread.busy || saveLoadManager->queuedTasks != 0)) {
			return true;
		}

		return false;
	}

	bool TryReadCalendarValues(float& a_outGameHour, float& a_outGameDaysPassed)
	{
		const auto* calendar = RE::Calendar::GetSingleton();
		if (!calendar || !calendar->gameHour || !calendar->gameDaysPassed) {
			return false;
		}

		a_outGameHour = calendar->gameHour->GetValue();
		a_outGameDaysPassed = calendar->gameDaysPassed->GetValue();
		return std::isfinite(a_outGameHour) && std::isfinite(a_outGameDaysPassed);
	}

	std::optional<std::uint32_t> TryReadExteriorWeatherFormId()
	{
		const auto* sky = RE::Sky::GetSingleton();
		if (!sky || sky->mode == RE::Sky::Mode::kInterior) {
			return std::nullopt;
		}

		const auto* currentWeather = sky->currentWeather;
		if (!currentWeather) {
			return std::nullopt;
		}

		return currentWeather->GetFormID();
	}

	std::optional<std::uint32_t> ParseHexFormId(std::string_view a_hex)
	{
		if (a_hex.empty()) {
			return std::nullopt;
		}

		std::uint32_t value = 0;
		const auto* begin = a_hex.data();
		const auto* end = begin + a_hex.size();
		const auto result = std::from_chars(begin, end, value, 16);
		if (result.ec != std::errc{} || result.ptr != end) {
			return std::nullopt;
		}

		return value;
	}

	std::optional<float> ParseHHmmToGameHour(std::string_view a_hhmm)
	{
		if (a_hhmm.empty() || a_hhmm.size() > 4) {
			return std::nullopt;
		}

		std::string digits;
		digits.reserve(4);
		for (const auto ch : a_hhmm) {
			if (ch < '0' || ch > '9') {
				return std::nullopt;
			}
			digits.push_back(ch);
		}

		while (digits.size() < 4) {
			digits.insert(digits.begin(), '0');
		}

		int hours = 0;
		int minutes = 0;
		{
			const auto* begin = digits.data();
			const auto* split = begin + 2;
			const auto* end = begin + 4;

			const auto hourResult = std::from_chars(begin, split, hours);
			const auto minuteResult = std::from_chars(split, end, minutes);
			if (hourResult.ec != std::errc{} || hourResult.ptr != split ||
				minuteResult.ec != std::errc{} || minuteResult.ptr != end) {
				return std::nullopt;
			}
		}

		if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
			return std::nullopt;
		}

		return static_cast<float>(hours) + static_cast<float>(minutes) / 60.0F;
	}

	bool ForceHostWorldStateBroadcast(std::optional<std::uint32_t> a_forcedWeatherFormId, bool a_sendTime)
	{
		if (!F4T::WorldStateSync::IsWorldStateHost() || !F4T::Networking::IsConnectedToServer()) {
			return false;
		}

		float gameHour = 0.0F;
		float gameDaysPassed = 0.0F;
		if (!TryReadCalendarValues(gameHour, gameDaysPassed)) {
			return false;
		}

		bool hasWeatherFormId = false;
		std::uint32_t weatherFormId = 0U;
		if (a_forcedWeatherFormId) {
			hasWeatherFormId = true;
			weatherFormId = *a_forcedWeatherFormId;
		} else if (const auto currentWeatherFormId = TryReadExteriorWeatherFormId()) {
			hasWeatherFormId = true;
			weatherFormId = *currentWeatherFormId;
		}

		if (!F4T::Networking::SendWorldStatePacket(
				gameHour,
				gameDaysPassed,
				hasWeatherFormId,
				weatherFormId,
				a_sendTime)) {
			return false;
		}

		if (a_sendTime) {
			g_lastSentWorldState.gameHour = gameHour;
			g_lastSentWorldState.gameDaysPassed = gameDaysPassed;
		}
		g_lastSentWorldState.hasWeatherFormId = hasWeatherFormId;
		g_lastSentWorldState.weatherFormId = hasWeatherFormId ? std::optional<std::uint32_t>{ weatherFormId } : std::nullopt;
		g_lastSentWorldState.initialized = true;
		g_lastHostSendTime = std::chrono::steady_clock::now();

		if (ShouldLog("server_world_state_host_relay")) {
			if (hasWeatherFormId) {
				LogInfo(std::format(
					"Relayed host worldState after server weather command: gameHour={:.2f}, weatherFormId=0x{:08X}.",
					gameHour,
					weatherFormId));
			} else {
				LogInfo(std::format(
					"Relayed host worldState after server weather command: gameHour={:.2f}, weatherFormId=<none>.",
					gameHour));
			}
		}

		return true;
	}

	void RequeueServerWorldState(F4T::WorldStateSync::PendingServerWorldState a_state)
	{
		const std::scoped_lock lock(g_pendingMutex);
		g_pendingServerWorldState = std::move(a_state);
		g_hasPendingServerWorldState = true;
	}

	bool ShouldSendTimeState(float a_gameHour, float a_gameDaysPassed)
	{
		if (!g_lastSentWorldState.initialized) {
			return true;
		}

		return !F4T::WorldTimeMath::TimeStateIsConsistent(
			a_gameDaysPassed, a_gameHour, g_lastSentWorldState.gameDaysPassed, g_lastSentWorldState.gameHour,
			kGameHourSendThreshold, kGameDaysPassedSendThreshold);
	}

	bool ShouldSendWeatherState(
		bool a_hasWeatherFormId,
		std::optional<std::uint32_t> a_weatherFormId,
		const std::chrono::steady_clock::time_point& a_now)
	{
		if (!g_lastSentWorldState.initialized) {
			return true;
		}

		if (a_hasWeatherFormId != g_lastSentWorldState.hasWeatherFormId ||
			a_weatherFormId != g_lastSentWorldState.weatherFormId) {
			return true;
		}

		return g_lastHostSendTime.time_since_epoch().count() == 0 ||
		       a_now - g_lastHostSendTime >= kWorldStateHeartbeatInterval;
	}

	void ApplyTimeState(float a_gameHour, float a_gameDaysPassed, bool a_force)
	{
		auto* calendar = RE::Calendar::GetSingleton();
		if (!calendar || !calendar->gameHour || !calendar->gameDaysPassed) {
			return;
		}

		const auto localGameHour = calendar->gameHour->GetValue();
		const auto localGameDaysPassed = calendar->gameDaysPassed->GetValue();
		if (!a_force && F4T::WorldTimeMath::TimeStateIsConsistent(
				localGameDaysPassed, localGameHour, a_gameDaysPassed, a_gameHour,
				kGameHourApplyThreshold, kGameDaysPassedApplyThreshold)) {
			return;
		}

		F4T::CrashForensics::Record(F4T::MutationRing::MutationSite::kTimeState, 0, reinterpret_cast<std::uintptr_t>(calendar), 0);
		calendar->gameHour->value = a_gameHour;
		calendar->gameDaysPassed->value = a_gameDaysPassed;

		if (auto* sky = RE::Sky::GetSingleton()) {
			sky->currentGameHour = a_gameHour;
		}

		if (ShouldLog("world_state_time_apply")) {
			LogInfo(std::format(
				"Applied host world time: gameHour={:.2f}, gameDaysPassed={:.3f}.",
				a_gameHour,
				a_gameDaysPassed));
		}
	}

	void ApplyWeatherState(bool a_hasWeatherFormId, std::optional<std::uint32_t> a_weatherFormId)
	{
		if (!IsLocalPlayerExterior()) {
			return;
		}

		auto* sky = RE::Sky::GetSingleton();
		if (!sky) {
			return;
		}

		if (!a_hasWeatherFormId || !a_weatherFormId) {
			return;
		}

		auto* weather = RE::TESForm::GetFormByID<RE::TESWeather>(*a_weatherFormId);
		if (!weather) {
			if (ShouldLog("world_state_weather_lookup_failed")) {
				LogWarning(std::format(
					"Could not resolve host weather form 0x{:08X} for world-state apply.",
					*a_weatherFormId));
			}
			return;
		}

		if (g_lastAppliedWorldState.initialized &&
			g_lastAppliedWorldState.hasWeatherFormId &&
			g_lastAppliedWorldState.weatherFormId == a_weatherFormId &&
			sky->overrideWeather == weather) {
			return;
		}

		// source:CO-BETA/plugin/include/F4TCrashForensics.h
		F4T::CrashForensics::Record(F4T::MutationRing::MutationSite::kWeatherState, *a_weatherFormId, reinterpret_cast<std::uintptr_t>(weather), 0);
		sky->ForceWeather(weather, true);

		if (ShouldLog("world_state_weather_apply")) {
			LogInfo(std::format("Applied host weather form 0x{:08X}.", *a_weatherFormId));
		}
	}

	void SeedHostCaptureFromLocalState()
	{
		float gameHour = 0.0F;
		float gameDaysPassed = 0.0F;
		if (!TryReadCalendarValues(gameHour, gameDaysPassed)) {
			g_lastSentWorldState = {};
			g_lastHostSendTime = std::chrono::steady_clock::now();
			return;
		}

		const auto weatherFormId = TryReadExteriorWeatherFormId();
		g_lastSentWorldState.gameHour = gameHour;
		g_lastSentWorldState.gameDaysPassed = gameDaysPassed;
		g_lastSentWorldState.hasWeatherFormId = weatherFormId.has_value();
		g_lastSentWorldState.weatherFormId = weatherFormId;
		g_lastSentWorldState.initialized = false;
		g_lastHostSendTime = std::chrono::steady_clock::now();
	}

	void ApplyServerWorldState(const F4T::WorldStateSync::PendingServerWorldState& a_state)
	{
		if (a_state.timeHHmm) {
			const auto parsedGameHour = ParseHHmmToGameHour(*a_state.timeHHmm);
			float currentGameHour = 0.0F;
			float gameDaysPassed = 0.0F;
			if (parsedGameHour && TryReadCalendarValues(currentGameHour, gameDaysPassed)) {
				ApplyTimeState(*parsedGameHour, gameDaysPassed, true);
				g_latestHostTime.gameHour = *parsedGameHour;
				g_latestHostTime.gameDaysPassed = gameDaysPassed;
				g_latestHostTime.initialized = true;
				g_lastAppliedWorldState.gameHour = *parsedGameHour;
				g_lastAppliedWorldState.gameDaysPassed = gameDaysPassed;
				g_lastAppliedWorldState.initialized = true;
				if (F4T::WorldStateSync::IsWorldStateHost()) {
					g_lastSentWorldState.gameHour = *parsedGameHour;
					g_lastSentWorldState.gameDaysPassed = gameDaysPassed;
					g_lastSentWorldState.initialized = true;
				}
			}
		}

		if (!a_state.weatherFormId) {
			return;
		}

		const auto forcedWeatherFormId = ParseHexFormId(*a_state.weatherFormId);
		if (!forcedWeatherFormId) {
			return;
		}

		const auto assignedPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
		const auto hostPlayerId = F4T::WorldStateSync::GetWorldStateHostPlayerId();
		if (!assignedPlayerId || !hostPlayerId) {
			RequeueServerWorldState(a_state);
			return;
		}

		if (!F4T::WorldStateSync::IsWorldStateHost()) {
			return;
		}

		ApplyWeatherState(true, forcedWeatherFormId);
		g_lastAppliedWorldState.hasWeatherFormId = true;
		g_lastAppliedWorldState.weatherFormId = forcedWeatherFormId;
		g_lastAppliedWorldState.initialized = true;

		if (!ForceHostWorldStateBroadcast(forcedWeatherFormId, false) &&
			ShouldLog("server_world_state_weather_no_relay")) {
			LogWarning("Could not relay host world-state after server weather apply.");
		}
	}
}

namespace F4T::WorldStateSync
{
	bool IsWorldStateHost()
	{
		const auto assignedPlayerId = RemotePlayerState::GetAssignedPlayerId();
		return assignedPlayerId && g_worldStateHostPlayerId && *assignedPlayerId == *g_worldStateHostPlayerId;
	}

	std::optional<std::uint32_t> GetWorldStateHostPlayerId()
	{
		return g_worldStateHostPlayerId;
	}

	void SetWorldStateHostPlayerId(std::uint32_t a_hostPlayerId)
	{
		const auto assignedPlayerId = RemotePlayerState::GetAssignedPlayerId();
		const bool wasHost =
			assignedPlayerId && g_worldStateHostPlayerId && *assignedPlayerId == *g_worldStateHostPlayerId;
		const bool hostChanged = !g_worldStateHostPlayerId || *g_worldStateHostPlayerId != a_hostPlayerId;
		g_worldStateHostPlayerId = a_hostPlayerId;

		const bool isNowHost = assignedPlayerId && *assignedPlayerId == a_hostPlayerId;
		if (isNowHost && (!wasHost || hostChanged)) {
			{
				const std::scoped_lock lock(g_pendingMutex);
				g_hasPendingWorldState = false;
				g_pendingWorldState = {};
			}

			SeedHostCaptureFromLocalState();

			if (ShouldLog("world_state_host_promoted")) {
				LogInfo(std::format(
					"Promoted to world-state host (hostPlayerId={}). Continuing with current local time and weather.",
					a_hostPlayerId));
			}
			return;
		}

		if (!isNowHost && wasHost) {
			g_lastSentWorldState = {};
			g_lastHostSendTime = {};
			if (ShouldLog("world_state_host_demoted")) {
				LogInfo(std::format("No longer world-state host; new host is player {}.", a_hostPlayerId));
			}
			return;
		}

		if (!isNowHost && hostChanged && ShouldLog("world_state_host_changed")) {
			LogInfo(std::format("World-state host is now player {}.", a_hostPlayerId));
		}
	}

	void QueueWorldStateHostPlayerId(std::uint32_t a_hostPlayerId)
	{
		const std::scoped_lock lock(g_pendingMutex);
		g_pendingWorldStateHostPlayerId = a_hostPlayerId;
	}

	void Reset()
	{
		g_lastSentWorldState = {};
		g_lastAppliedWorldState = {};
		g_lastHostSendTime = {};
		g_worldStateHostPlayerId = std::nullopt;
		g_lastLogTimes.clear();
		g_wasGameTimeFrozen = false;
		g_latestHostTime = {};
	}

	void RequestReset()
	{
		const std::scoped_lock lock(g_pendingMutex);
		g_resetRequested = true;
		g_pendingWorldStateHostPlayerId.reset();
		g_hasPendingWorldState = false;
		g_pendingWorldState = {};
		g_hasPendingServerWorldState = false;
		g_pendingServerWorldState = {};
	}

	void SetPendingWorldState(PendingWorldState a_state)
	{
		const std::scoped_lock lock(g_pendingMutex);
		g_pendingWorldState = std::move(a_state);
		g_hasPendingWorldState = true;
	}

	void SetPendingServerWorldState(PendingServerWorldState a_state)
	{
		const std::scoped_lock lock(g_pendingMutex);
		g_pendingServerWorldState = std::move(a_state);
		g_hasPendingServerWorldState = true;
	}

	void PollHostCapture()
	{
		if (!Networking::IsConnectedToServer() || !IsWorldStateHost()) {
			return;
		}

		const bool timeFrozen = IsGameTimeFrozen();
		const bool justResumed = g_wasGameTimeFrozen && !timeFrozen;
		g_wasGameTimeFrozen = timeFrozen;

		float gameHour = 0.0F;
		float gameDaysPassed = 0.0F;
		if (!TryReadCalendarValues(gameHour, gameDaysPassed)) {
			return;
		}

		const auto weatherFormId = TryReadExteriorWeatherFormId();
		const bool hasWeatherFormId = weatherFormId.has_value();
		const auto now = std::chrono::steady_clock::now();
		const bool sendTime = justResumed || ShouldSendTimeState(gameHour, gameDaysPassed);
		const bool sendWeather = ShouldSendWeatherState(hasWeatherFormId, weatherFormId, now);
		if (!sendTime && !sendWeather) {
			return;
		}

		if (!Networking::SendWorldStatePacket(
				gameHour,
				gameDaysPassed,
				hasWeatherFormId,
				hasWeatherFormId ? *weatherFormId : 0U,
				sendTime)) {
			return;
		}

		if (sendTime) {
			g_lastSentWorldState.gameHour = gameHour;
			g_lastSentWorldState.gameDaysPassed = gameDaysPassed;
		}
		g_lastSentWorldState.hasWeatherFormId = hasWeatherFormId;
		g_lastSentWorldState.weatherFormId = weatherFormId;
		g_lastSentWorldState.initialized = true;
		g_lastHostSendTime = now;

		if (ShouldLog("world_state_host_send")) {
			const auto timeSyncNote = sendTime ? "timeSync=yes" : "timeSync=no";
			if (hasWeatherFormId) {
				LogInfo(std::format(
					"Sent worldState ({}): gameHour={:.2f}, gameDaysPassed={:.3f}, weatherFormId=0x{:08X}.",
					timeSyncNote,
					gameHour,
					gameDaysPassed,
					*weatherFormId));
			} else {
				LogInfo(std::format(
					"Sent worldState ({}): gameHour={:.2f}, gameDaysPassed={:.3f}, weatherFormId=<none>.",
					timeSyncNote,
					gameHour,
					gameDaysPassed));
			}
		}
	}

	void ApplyPendingState()
	{
		bool resetRequested = false;
		std::optional<std::uint32_t> pendingHostPlayerId;
		{
			const std::scoped_lock lock(g_pendingMutex);
			resetRequested = g_resetRequested;
			g_resetRequested = false;
			pendingHostPlayerId = g_pendingWorldStateHostPlayerId;
			g_pendingWorldStateHostPlayerId.reset();
		}

		if (resetRequested) {
			Reset();
		}
		if (pendingHostPlayerId) {
			SetWorldStateHostPlayerId(*pendingHostPlayerId);
		}

		if (!Networking::IsConnectedToServer()) {
			return;
		}

		PendingServerWorldState pendingServerState{};
		bool hasPendingServerState = false;
		{
			const std::scoped_lock lock(g_pendingMutex);
			if (g_hasPendingServerWorldState) {
				pendingServerState = g_pendingServerWorldState;
				g_hasPendingServerWorldState = false;
				hasPendingServerState = true;
			}
		}

		if (hasPendingServerState) {
			ApplyServerWorldState(pendingServerState);
		}

		if (IsWorldStateHost()) {
			return;
		}

		const bool timeFrozen = IsGameTimeFrozen();
		const bool justResumed = g_wasGameTimeFrozen && !timeFrozen;
		g_wasGameTimeFrozen = timeFrozen;

		PendingWorldState pendingState{};
		bool hasPendingState = false;
		{
			const std::scoped_lock lock(g_pendingMutex);
			if (g_hasPendingWorldState) {
				pendingState = g_pendingWorldState;
				g_hasPendingWorldState = false;
				hasPendingState = true;
			}
		}

		if (hasPendingState) {
			g_latestHostTime.gameHour = pendingState.gameHour;
			g_latestHostTime.gameDaysPassed = pendingState.gameDaysPassed;
			g_latestHostTime.initialized = true;
		}

		const bool shouldApplyTime =
			!timeFrozen && g_latestHostTime.initialized && (justResumed || (hasPendingState && pendingState.applyTime));
		if (shouldApplyTime) {
			ApplyTimeState(g_latestHostTime.gameHour, g_latestHostTime.gameDaysPassed, justResumed);
			g_lastAppliedWorldState.gameHour = g_latestHostTime.gameHour;
			g_lastAppliedWorldState.gameDaysPassed = g_latestHostTime.gameDaysPassed;
		}

		if (!hasPendingState) {
			return;
		}

		ApplyWeatherState(pendingState.hasWeatherFormId, pendingState.weatherFormId);

		g_lastAppliedWorldState.hasWeatherFormId = pendingState.hasWeatherFormId;
		g_lastAppliedWorldState.weatherFormId = pendingState.weatherFormId;
		g_lastAppliedWorldState.initialized = true;
	}
}

#include "pch.h"

#include "F4TSettlementCaptureClient.h"
#include "F4TSettlementCaptureInternal.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TSettlementAccessClient.h"
#include "F4TSettlementCapture.h"
#include "F4TSettlementCapturePolicy.h"
#include "F4TSettlementRaidClient.h"
#include "F4TFactionPayload.h"
#include "F4TClientIdentity.h"
#include "F4TGamePaths.h"
#include "F4TInteractWheel.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TSessionWorldState.h"
#include "F4TTextEntryMode.h"
#include "F4TUIClient.h"
#include "F4TWorkshopReplicationClient.h"

#include "RE/C/Console.h"
#include "RE/T/TESObjectREFR.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace F4T::SettlementCaptureClient
{
	using namespace F4T::Networking::Detail;
	namespace Policy = F4T::SettlementCapturePolicy;
	using Policy::IsActiveStatus;

	namespace
	{
		constexpr std::size_t kMaximumProfileIdBytes = 128;
		constexpr std::size_t kMaximumActionJsonBytes = 1024;
		constexpr std::size_t kMaximumStoredSettlements = 64;
		constexpr std::size_t kMaximumZoneAnchors = 1024;
		constexpr std::chrono::seconds kHeartbeatInterval{ 5 };
		constexpr std::chrono::seconds kStaleCaptureInterval{ 15 };
		constexpr int kCaptureKey = 'H';

		std::optional<std::uint64_t> g_nearBuildZoneId;
		std::chrono::steady_clock::time_point g_lastHeartbeat{};
		std::chrono::steady_clock::time_point g_localAttemptStartedAt{};
		const void* g_playerCell{};
		bool g_atWorkbench{};
		std::string g_discoveredProfile;
		bool g_captureKeyDown{};

		bool IsFailureStatus(std::string_view a_status)
		{
			return !IsActiveStatus(a_status) && a_status != "captured" && a_status != "cancelled";
		}

		std::string ResultCode(std::string_view a_status)
		{
			if (a_status == "noAttempt") {
				return "no_attempt";
			}
			if (a_status == "notInRange") {
				return "too_far";
			}
			if (a_status == "alreadyOwner") {
				return "already_yours";
			}
			if (a_status == "unknownBuildZone") {
				return "unknown_settlement";
			}
			if (a_status == "settlementLimit") {
				return "hold_limit";
			}
			if (a_status == "workbenchIntact") {
				return "workbench_intact";
			}
			if (a_status == "timedOut") {
				return "timed_out";
			}
			return "not_allowed";
		}

		bool IsRetryableRejectStatus(std::string_view a_status)
		{
			return a_status == "rateLimited" || a_status == "persistenceUnavailable";
		}


		std::optional<std::uint64_t> ReadBuildZoneId(const Json& a_packet)
		{
			const auto found = a_packet.find("buildZoneId");
			if (found == a_packet.end() || !found->is_number_unsigned()) {
				return std::nullopt;
			}
			const auto value = found->get<std::uint64_t>();
			return value == 0 ? std::nullopt : std::optional<std::uint64_t>{ value };
		}

		void QueueGameTask(std::function<void()> a_task)
		{
			const auto* taskInterface = F4SE::GetTaskInterface();
			if (!taskInterface) {
				LogWarningWithLocalPlayerPrefix("Cannot queue settlement capture work: task interface unavailable.");
				return;
			}
			taskInterface->AddTask(std::move(a_task));
		}

		void QueuePacket(Json a_packet)
		{
			QueueGameTask([packet = std::move(a_packet)]() {
				if (!F4T::Networking::IsConnectedToServer()) {
					LogThrottledWarning("capture_send_offline", "Dropped a settlement capture packet: not connected to a server.");
					return;
				}
				if (!SendPacketBytes(packet.dump())) {
					LogThrottledWarning("capture_send_failed", "Could not send Commonwealth Online settlement capture packet.");
					return;
				}
				LogInfoWithLocalPlayerPrefix(std::format("Settlement capture {} sent for zone {}.", packet.value("op", std::string{}), packet.value("buildZoneId", std::uint64_t{ 0 })));
			});
		}

		std::optional<std::filesystem::path> DiscoveredFile()
		{
			std::filesystem::path folder;
			if (!F4T::GamePaths::ResolveF4SEFolder(folder)) {
				return std::nullopt;
			}
			return folder / "CommonwealthOnline" / "discovered-settlements.json";
		}

		Json ReadDiscoveredFile(const std::filesystem::path& a_file)
		{
			std::ifstream stream(a_file);
			auto json = stream.is_open() ? Json::parse(stream, nullptr, false) : Json::object();
			return json.is_object() ? json : Json::object();
		}

		std::set<std::uint64_t> LoadDiscovered(const std::string& a_profileId)
		{
			std::set<std::uint64_t> discovered;
			const auto file = DiscoveredFile();
			if (!file) {
				return discovered;
			}
			const auto json = ReadDiscoveredFile(*file);
			const auto found = json.find(a_profileId);
			if (found == json.end() || !found->is_array()) {
				return discovered;
			}
			for (const auto& entry : *found) {
				if (entry.is_number_unsigned() && discovered.size() < kMaximumZoneAnchors) {
					discovered.insert(entry.get<std::uint64_t>());
				}
			}
			return discovered;
		}

		void SaveDiscovered(const std::string& a_profileId, const std::set<std::uint64_t>& a_discovered)
		{
			const auto file = DiscoveredFile();
			if (!file) {
				return;
			}
			auto json = ReadDiscoveredFile(*file);
			json[a_profileId] = a_discovered;
			std::error_code ignored;
			std::filesystem::create_directories(file->parent_path(), ignored);
			std::ofstream stream(*file, std::ios::trunc);
			stream << json.dump();
			if (!stream) {
				LogThrottledWarning("capture_discovered_save", "Could not save discovered settlements.");
			}
		}

		void CloseCaptureFactionPrompt()
		{
			auto* api = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI3>();
			if (api && api->IsFactionWindowVisible && api->CloseFactionWindow && api->IsFactionWindowVisible()) {
				api->CloseFactionWindow();
			}
		}

		void QueueStateChanged()
		{
			QueueGameTask([]() {
				F4T::UIClient::NotifyFactionStateChanged();
				PushCaptureHud();
			});
		}

		void HandleStatePacket(const Json& a_packet)
		{
			const auto decoded = F4T::SettlementCapture::DecodeState(a_packet);
			if (!decoded || !F4T::Factions::Payload::IsSafeToken(decoded->capturerProfileId, kMaximumProfileIdBytes) ||
				(!decoded->ownerProfileId.empty() && !F4T::Factions::Payload::IsSafeToken(decoded->ownerProfileId, kMaximumProfileIdBytes))) {
				LogThrottledWarning("capture_state_malformed", "Ignoring malformed settlementCaptureState packet.");
				return;
			}
			const auto buildZoneId = decoded->buildZoneId;
			const auto self = F4T::ClientIdentity::ConnectedProfileId();
			LogInfoWithLocalPlayerPrefix(std::format("Settlement capture state for zone {}: {}, {:.0f} of {:.0f} s.", buildZoneId, decoded->status, decoded->elapsedSeconds, decoded->holdSeconds));
			{
				std::scoped_lock lock(g_mutex);
				if (g_captures.size() >= kMaximumStoredSettlements && !g_captures.contains(buildZoneId)) {
					const auto evictable = std::find_if(g_captures.begin(), g_captures.end(), [](const auto& a_entry) {
						return !IsActiveStatus(a_entry.second.status) && a_entry.first != g_localBuildZoneId;
					});
					if (evictable == g_captures.end()) {
						LogThrottledWarning("capture_store_full", "Dropping settlementCaptureState: settlement store is full.");
						return;
					}
					g_captures.erase(evictable);
				}
				g_captures[buildZoneId] = Record{
					decoded->status,
					decoded->capturerProfileId,
					decoded->ownerProfileId,
					decoded->raid,
					decoded->elapsedSeconds,
					decoded->holdSeconds,
					decoded->hasCapturePoint,
					Policy::Point{
						static_cast<float>(decoded->workbenchX),
						static_cast<float>(decoded->workbenchY),
						static_cast<float>(decoded->workbenchZ),
					},
					decoded->hasCapturePoint ? static_cast<float>(decoded->captureRadius) : Policy::kDefaultCaptureRadius,
					std::chrono::steady_clock::now(),
				};
				if (g_localBuildZoneId == buildZoneId && !IsActiveStatus(decoded->status)) {
					g_localBuildZoneId.reset();
				}
				if (decoded->capturerProfileId == self && IsFailureStatus(decoded->status)) {
					g_lastResult = Result{ ResultCode(decoded->status), std::chrono::steady_clock::now() };
				}
			}
			if (decoded->status == "captured" && !self.empty() && decoded->capturerProfileId == self) {
				QueueGameTask([]() { CloseCaptureFactionPrompt(); });
			}
			QueueStateChanged();
		}

		void HandleZonesPacket(const Json& a_packet)
		{
			const auto zones = F4T::SettlementCapture::DecodeZones(a_packet);
			if (!zones) {
				LogThrottledWarning("capture_zones_malformed", "Ignoring malformed settlementCaptureZones packet.");
				return;
			}
			{
				std::scoped_lock lock(g_mutex);
				for (const auto& zone : *zones) {
					if (g_zoneAnchors.size() >= kMaximumZoneAnchors && !g_zoneAnchors.contains(zone.buildZoneId)) {
						LogThrottledWarning("capture_zone_store_full", "Dropping settlement capture zones: the zone store is full.");
						break;
					}
					g_zoneAnchors[zone.buildZoneId] = Policy::Anchor{
						zone.buildZoneId,
						true,
						Policy::Point{ static_cast<float>(zone.x), static_cast<float>(zone.y), static_cast<float>(zone.z) },
						static_cast<float>(zone.radius),
					};
					g_zoneLabels[zone.buildZoneId] = ZoneLabel{ zone.name, zone.space };
				}
			}
			QueueStateChanged();
		}

		void HandleActivePacket(const Json& a_packet)
		{
			auto zones = F4T::SettlementCapture::DecodeActiveZones(a_packet);
			if (!zones) {
				LogThrottledWarning("capture_active_malformed", "Ignoring malformed settlementCaptureActive packet.");
				return;
			}
			std::scoped_lock lock(g_mutex);
			g_activeZones = std::move(*zones);
		}

		void HandleRejectPacket(const Json& a_packet)
		{
			const auto buildZoneId = ReadBuildZoneId(a_packet);
			const auto found = a_packet.find("status");
			if (!buildZoneId || found == a_packet.end() || !found->is_string()) {
				LogThrottledWarning("capture_reject_malformed", "Ignoring malformed settlementCaptureReject packet.");
				return;
			}
			const auto status = found->get<std::string>();
			LogInfoWithLocalPlayerPrefix(std::format("Settlement capture reject for zone {}: {}.", *buildZoneId, status));
			if (IsRetryableRejectStatus(status)) {
				return;
			}
			{
				std::scoped_lock lock(g_mutex);
				if (g_localBuildZoneId == buildZoneId && status != "vetoed") {
					g_localBuildZoneId.reset();
				}
				Policy::CancelOnReject(g_captures, *buildZoneId, status);
				g_lastResult = Result{ ResultCode(status), std::chrono::steady_clock::now() };
			}
			QueueStateChanged();
		}
	}

	bool TryHandlePacket(std::string_view a_type, const Json& a_packet)
	{
		if (a_type == "settlementCaptureState") {
			HandleStatePacket(a_packet);
		} else if (a_type == "settlementCaptureReject") {
			HandleRejectPacket(a_packet);
		} else if (a_type == "settlementCaptureZones") {
			HandleZonesPacket(a_packet);
		} else if (a_type == "settlementCaptureActive") {
			HandleActivePacket(a_packet);
		} else {
			return false;
		}
		return true;
	}

	void Reset()
	{
		{
			std::scoped_lock lock(g_mutex);
			g_captures.clear();
			g_zoneAnchors.clear();
			g_zoneLabels.clear();
			g_activeZones.clear();
			g_playerCell = nullptr;
			g_playerSpace.reset();
			g_atWorkbench = false;
			g_discoveredProfile.clear();
			g_discovered.clear();
			g_localBuildZoneId.reset();
			g_lastResult.reset();
			g_playerPosition.reset();
			g_nearBuildZoneId.reset();
			g_lastHeartbeat = {};
		}
		QueueStateChanged();
	}

	void NoteLocalPosition(float a_x, float a_y, float a_z, const RE::TESObjectREFR& a_player)
	{
		const auto self = F4T::ClientIdentity::ConnectedProfileId();
		const void* cell = a_player.GetParentCell();
		bool cellChanged = false;
		bool profileChanged = false;
		{
			std::scoped_lock lock(g_mutex);
			cellChanged = cell != g_playerCell || !g_playerSpace;
			profileChanged = !self.empty() && self != g_discoveredProfile;
		}
		std::optional<F4T::Workshop::FormKey> space;
		if (cellChanged) {
			space = F4T::WorkshopReplicationClient::SpaceKeyOf(a_player);
		}
		std::set<std::uint64_t> loaded;
		if (profileChanged) {
			loaded = LoadDiscovered(self);
		}
		std::optional<std::set<std::uint64_t>> toSave;
		std::optional<std::uint64_t> heartbeatZone;
		bool nearChanged = false;
		{
			std::scoped_lock lock(g_mutex);
			if (cellChanged) {
				g_playerCell = cell;
				g_playerSpace = std::move(space);
			}
			if (profileChanged) {
				g_discoveredProfile = self;
				g_discovered = std::move(loaded);
			}
			g_playerPosition = Policy::Point{ a_x, a_y, a_z };
			const auto nearest = Policy::FindNearest(*g_playerPosition, AnchorsLocked());
			const auto atWorkbench = nearest && IsAtWorkbenchLocked(*nearest);
			nearChanged = nearest != g_nearBuildZoneId || atWorkbench != g_atWorkbench;
			g_nearBuildZoneId = nearest;
			g_atWorkbench = atWorkbench;
			if (nearest && g_playerSpace && !g_discoveredProfile.empty() && IsNamedLocked(*nearest) && g_discovered.insert(*nearest).second) {
				toSave = g_discovered;
			}
			const auto now = std::chrono::steady_clock::now();
			if (g_localBuildZoneId && now - g_lastHeartbeat >= kHeartbeatInterval) {
				heartbeatZone = g_localBuildZoneId;
				g_lastHeartbeat = now;
			}
		}
		if (toSave) {
			SaveDiscovered(self, *toSave);
		}
		if (heartbeatZone) {
			QueuePacket(Json{ { "type", "settlementCapture" }, { "buildZoneId", *heartbeatZone }, { "op", "continue" } });
		}
		if (nearChanged) {
			QueueStateChanged();
		}
	}

	bool CannotBuildAtWorkbench(const RE::TESObjectREFR& a_workbench)
	{
		const auto self = F4T::ClientIdentity::ConnectedProfileId();
		if (self.empty()) {
			return false;
		}
		const auto buildable = F4T::SettlementAccessClient::BuildZonesAllowedFor(self);
		auto owners = F4T::SettlementAccessClient::OwnersByBuildZone();
		const auto position = a_workbench.GetPosition();
		const Policy::Point point{ position.x, position.y, position.z };
		std::scoped_lock lock(g_mutex);
		for (const auto& [buildZoneId, record] : g_captures) {
			owners.emplace(buildZoneId, record.ownerProfileId);
		}
		const auto anchors = AnchorsLocked();
		const auto anchor = std::find_if(anchors.begin(), anchors.end(), [&point](const Policy::Anchor& a_anchor) {
			return Policy::IsAtWorkbench(point, a_anchor);
		});
		if (anchor == anchors.end() || buildable.contains(anchor->buildZoneId)) {
			return false;
		}
		const auto owner = owners.find(anchor->buildZoneId);
		if (owner != owners.end() && !owner->second.empty()) {
			return owner->second != self;
		}
		return !F4T::SettlementAccess::UnownedZoneOpenToBuild(IsNamedLocked(anchor->buildZoneId));
	}

	namespace
	{
		bool IsWorkbenchBase(const RE::TESObjectREFR& a_workbench)
		{
			const auto* base = a_workbench.GetObjectReference();
			if (!base) {
				return false;
			}
			const auto expected = F4T::WorkshopReplicationClient::FormIdOfKey(F4T::WorkshopReplicationClient::kCampWorkbenchForm);
			return expected && base->GetFormID() == *expected;
		}
	}

	std::optional<std::array<float, 3>> WorkbenchPointOf(std::uint64_t a_buildZoneId)
	{
		std::scoped_lock lock(g_mutex);
		for (const auto& anchor : AnchorsLocked()) {
			if (anchor.buildZoneId == a_buildZoneId && anchor.hasWorkbench) {
				return std::array<float, 3>{ anchor.workbench.x, anchor.workbench.y, anchor.workbench.z };
			}
		}
		return std::nullopt;
	}

	std::optional<std::uint64_t> BuildZoneForWorkbench(const RE::TESObjectREFR& a_workbench)
	{
		if (!IsWorkbenchBase(a_workbench)) {
			return std::nullopt;
		}
		const auto position = a_workbench.GetPosition();
		const Policy::Point point{ position.x, position.y, position.z };
		std::scoped_lock lock(g_mutex);
		std::optional<std::uint64_t> nearest;
		double nearestDistance{};
		for (const auto& anchor : AnchorsLocked()) {
			if (!IsNamedLocked(anchor.buildZoneId) || !Policy::IsAtWorkbench(point, anchor)) {
				continue;
			}
			const auto distance = Policy::SquaredDistance(point, anchor.workbench);
			if (!nearest || distance < nearestDistance) {
				nearest = anchor.buildZoneId;
				nearestDistance = distance;
			}
		}
		return nearest;
	}

	bool HandleAction(std::string_view a_actionJson)
	{
		if (a_actionJson.empty() || a_actionJson.size() > kMaximumActionJsonBytes) {
			return false;
		}
		const auto action = Json::parse(std::string{ a_actionJson }, nullptr, false);
		if (!action.is_object()) {
			return false;
		}
		const auto op = action.value("op", "");
		if (op == "refreshCapture") {
			QueueStateChanged();
			return true;
		}
		if (op == "cancelCapture") {
			std::optional<std::uint64_t> attempt;
			{
				std::scoped_lock lock(g_mutex);
				attempt = g_localBuildZoneId;
				g_localBuildZoneId.reset();
			}
			if (attempt) {
				QueuePacket(Json{ { "type", "settlementCapture" }, { "buildZoneId", *attempt }, { "op", "cancel" } });
			}
			QueueStateChanged();
			return true;
		}
		if (op == "capture") {
			const auto buildZoneId = ReadBuildZoneId(action);
			if (!buildZoneId) {
				LogThrottledWarning("capture_action_malformed", "Ignoring a settlement capture action with an invalid buildZoneId.");
				return true;
			}
			{
				std::scoped_lock lock(g_mutex);
				g_localBuildZoneId = *buildZoneId;
				g_lastHeartbeat = std::chrono::steady_clock::now();
				g_localAttemptStartedAt = g_lastHeartbeat;
				g_lastResult.reset();
			}
			QueuePacket(Json{ { "type", "settlementCapture" }, { "buildZoneId", *buildZoneId }, { "op", "start" } });
			QueueStateChanged();
			return true;
		}
		return false;
	}

	void RefreshHud()
	{
		QueueStateChanged();
	}

	std::string ZoneName(std::uint64_t a_buildZoneId)
	{
		std::scoped_lock lock(g_mutex);
		const auto found = g_zoneLabels.find(a_buildZoneId);
		return found == g_zoneLabels.end() ? std::string{} : found->second.name;
	}

	void Tick()
	{
		bool changed = false;
		std::optional<std::uint64_t> timedOut;
		{
			std::scoped_lock lock(g_mutex);
			const auto now = std::chrono::steady_clock::now();
			if (g_lastResult && now - g_lastResult->seenAt >= kResultDisplayInterval) {
				g_lastResult.reset();
				changed = true;
			}
			const auto expired = Policy::CancelStale(g_captures, now, kStaleCaptureInterval);
			changed = changed || !expired.empty();
			if (g_localBuildZoneId) {
				const auto attempt = g_captures.find(*g_localBuildZoneId);
				const bool attemptActive = attempt != g_captures.end() && IsActiveStatus(attempt->second.status);
				if (!attemptActive && Policy::IsStale(g_localAttemptStartedAt, now, kStaleCaptureInterval)) {
					timedOut = g_localBuildZoneId;
					g_localBuildZoneId.reset();
					g_lastResult = Result{ "timed_out", now };
					changed = true;
				}
			}
		}
		if (timedOut) {
			LogInfoWithLocalPlayerPrefix(std::format("Settlement capture for zone {} timed out: no server update for {} s.", *timedOut, kStaleCaptureInterval.count()));
		}
		if (changed) {
			QueueStateChanged();
		}
		if (g_hudRefreshAt != std::chrono::steady_clock::time_point{} && std::chrono::steady_clock::now() >= g_hudRefreshAt) {
				g_hudRefreshAt = {};
				QueueGameTask([]() { PushCaptureHud(); });
			}
			const bool down = (GetAsyncKeyState(kCaptureKey) & 0x8000) != 0;
		const bool pressed = down && !g_captureKeyDown;
		g_captureKeyDown = down;
		if (!pressed) {
			return;
		}
		DWORD foregroundProcess = 0;
		GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
		if (foregroundProcess != GetCurrentProcessId() || F4T::UIClient::IsCustomMenuInputCaptured() || F4T::TextEntryMode::IsChatInputFocused() ||
			F4T::SessionWorldState::IsMenuOpen(RE::Console::MENU_NAME.data()) || F4T::InteractWheel::IsBlockedByMenu()) {
			return;
		}
		const auto state = BuildState();
		if (!state["promptCapture"].get<bool>()) {
			return;
		}
		const auto buildZoneId = state["nearBuildZoneId"].get<std::uint64_t>();
		{
			std::scoped_lock lock(g_mutex);
			g_localBuildZoneId = buildZoneId;
			g_lastHeartbeat = std::chrono::steady_clock::now();
			g_localAttemptStartedAt = g_lastHeartbeat;
			g_lastResult.reset();
		}
		QueuePacket(Json{ { "type", "settlementCapture" }, { "buildZoneId", buildZoneId }, { "op", "start" } });
		QueueStateChanged();
	}
}

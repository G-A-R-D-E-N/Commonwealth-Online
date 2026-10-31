#include "pch.h"

#include "F4TSettlementCaptureClient.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TSettlementAccessClient.h"
#include "F4TSettlementCapture.h"
#include "F4TSettlementCapturePolicy.h"
#include "F4TFactionPayload.h"
#include "F4TClientIdentity.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TUIClient.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace F4T::SettlementCaptureClient
{
	using namespace F4T::Networking::Detail;
	namespace Policy = F4T::SettlementCapturePolicy;

	namespace
	{
		constexpr std::size_t kMaximumProfileIdBytes = 128;
		constexpr std::size_t kMaximumActionJsonBytes = 1024;
		constexpr std::size_t kMaximumStoredSettlements = 64;
		constexpr std::size_t kMaximumZoneAnchors = 1024;
		constexpr std::chrono::seconds kResultDisplayInterval{ 5 };
		constexpr std::chrono::seconds kHeartbeatInterval{ 5 };
		constexpr double kMaximumSecondsLeft = 86400.0;
		constexpr std::size_t kCaptureOutputBudgetBytes = 26 * 1024;

		struct Record
		{
			std::string status;
			std::string capturerProfileId;
			std::string ownerProfileId;
			bool raid{};
			double elapsedSeconds{};
			double holdSeconds{};
			bool hasWorkbench{};
			Policy::Point workbench{};
			float captureRadius{ Policy::kDefaultCaptureRadius };
		};

		struct Result
		{
			std::string code;
			std::chrono::steady_clock::time_point seenAt{};
		};

		std::mutex g_mutex;
		std::map<std::uint64_t, Record> g_captures;
		std::map<std::uint64_t, Policy::Anchor> g_zoneAnchors;
		std::optional<std::uint64_t> g_localBuildZoneId;
		std::optional<Result> g_lastResult;
		std::optional<Policy::Point> g_playerPosition;
		std::optional<std::uint64_t> g_nearBuildZoneId;
		std::chrono::steady_clock::time_point g_lastHeartbeat{};

		bool IsActiveStatus(std::string_view a_status)
		{
			return a_status == "started" || a_status == "holding" || a_status == "contested";
		}

		bool IsFailureStatus(std::string_view a_status)
		{
			return !IsActiveStatus(a_status) && a_status != "captured" && a_status != "cancelled";
		}

		std::string ResultCode(std::string_view a_status)
		{
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
			return "not_allowed";
		}

		std::uint32_t ProgressPercent(const Record& a_record)
		{
			if (a_record.holdSeconds <= 0.0) {
				return 0;
			}
			return static_cast<std::uint32_t>(std::clamp(a_record.elapsedSeconds / a_record.holdSeconds * 100.0, 0.0, 100.0));
		}

		std::uint32_t SecondsLeft(const Record& a_record)
		{
			return static_cast<std::uint32_t>((std::min)((std::max)(0.0, a_record.holdSeconds - a_record.elapsedSeconds), kMaximumSecondsLeft) + 0.5);
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
				}
			});
		}

		COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI15* CaptureHudApi()
		{
			static auto* api = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI15>();
			return api;
		}

		std::vector<Policy::Anchor> AnchorsLocked()
		{
			std::vector<Policy::Anchor> anchors;
			for (const auto& entry : g_zoneAnchors) {
				anchors.push_back(entry.second);
			}
			for (const auto& [buildZoneId, record] : g_captures) {
				if (record.hasWorkbench) {
					anchors.push_back(Policy::Anchor{ buildZoneId, true, record.workbench, record.captureRadius });
				}
			}
			return anchors;
		}

		Json RowFor(std::uint64_t a_buildZoneId, const std::string& a_ownerProfileId, const Record* a_record)
		{
			std::string state = a_ownerProfileId.empty() ? "neutral" : "owned";
			std::uint32_t progressPercent = 0;
			std::uint32_t secondsLeft = 0;
			std::string capturerProfileId;
			bool raid = false;
			if (a_record) {
				capturerProfileId = a_record->capturerProfileId;
				raid = a_record->raid;
				if (a_record->status == "captured") {
					state = "owned";
				} else if (IsActiveStatus(a_record->status)) {
					state = a_record->status == "contested" ? "contested" : "capturing";
					progressPercent = ProgressPercent(*a_record);
					secondsLeft = SecondsLeft(*a_record);
				}
			}
			return Json{
				{ "buildZoneId", a_buildZoneId },
				{ "state", state },
				{ "ownerProfileId", a_ownerProfileId },
				{ "ownerName", "" },
				{ "capturerProfileId", capturerProfileId },
				{ "capturerName", "" },
				{ "raid", raid },
				{ "progressPercent", progressPercent },
				{ "secondsLeft", secondsLeft },
				{ "cooldownSeconds", 0 },
			};
		}

		int CapturePriority(std::uint64_t a_buildZoneId, const std::string& a_ownerProfileId, const Record* a_record, const std::string& a_self, const std::optional<std::uint64_t>& a_nearest, const std::optional<std::uint64_t>& a_local)
		{
			if (a_nearest == a_buildZoneId) {
				return 0;
			}
			if (a_local == a_buildZoneId) {
				return 1;
			}
			if (a_record && IsActiveStatus(a_record->status)) {
				return 2;
			}
			if (!a_self.empty() && a_ownerProfileId == a_self) {
				return 3;
			}
			return 4;
		}

		Json BuildState()
		{
			const auto self = F4T::ClientIdentity::ConnectedProfileId();
			auto owners = F4T::SettlementAccessClient::OwnersByBuildZone();
			Json state{
				{ "settlements", Json::array() },
				{ "nearBuildZoneId", nullptr },
				{ "canCapture", false },
				{ "localProgress", 0 },
				{ "localCapturing", false },
				{ "lastResult", nullptr },
			};
			std::scoped_lock lock(g_mutex);
			for (const auto& [buildZoneId, record] : g_captures) {
				owners.emplace(buildZoneId, record.ownerProfileId);
			}
			for (const auto& entry : g_zoneAnchors) {
				owners.emplace(entry.first, std::string{});
			}
			std::optional<std::uint64_t> nearest;
			if (g_playerPosition) {
				nearest = Policy::FindNearest(*g_playerPosition, AnchorsLocked());
				if (nearest) {
					state["nearBuildZoneId"] = *nearest;
					state["canCapture"] = Policy::CanStartCapture(g_localBuildZoneId.has_value(), owners.at(*nearest), self);
				}
			}
			std::vector<std::pair<int, Json>> ranked;
			for (const auto& [buildZoneId, ownerProfileId] : owners) {
				const auto found = g_captures.find(buildZoneId);
				const auto* record = found == g_captures.end() ? nullptr : &found->second;
				ranked.emplace_back(CapturePriority(buildZoneId, ownerProfileId, record, self, nearest, g_localBuildZoneId), RowFor(buildZoneId, ownerProfileId, record));
			}
			std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a_left, const auto& a_right) {
				return a_left.first < a_right.first;
			});
			for (auto& entry : ranked) {
				state["settlements"].push_back(std::move(entry.second));
			}
			state["localCapturing"] = g_localBuildZoneId.has_value();
			if (g_localBuildZoneId) {
				const auto found = g_captures.find(*g_localBuildZoneId);
				if (found != g_captures.end() && IsActiveStatus(found->second.status)) {
					state["localProgress"] = ProgressPercent(found->second);
				}
			}
			if (g_lastResult && std::chrono::steady_clock::now() - g_lastResult->seenAt < kResultDisplayInterval) {
				state["lastResult"] = Json{ { "ok", false }, { "code", g_lastResult->code } };
			}
			return state;
		}

		void PushCaptureHud()
		{
			auto* api = CaptureHudApi();
			if (!api || !api->PushCaptureHud) {
				return;
			}
			auto state = BuildState();
			Json nearRows = Json::array();
			for (const auto& row : state["settlements"]) {
				if (row["buildZoneId"] == state["nearBuildZoneId"]) {
					nearRows.push_back(row);
				}
			}
			state["settlements"] = std::move(nearRows);
			const auto text = state.dump();
			api->PushCaptureHud(text.c_str());
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
				};
				if (g_localBuildZoneId == buildZoneId && !IsActiveStatus(decoded->status)) {
					g_localBuildZoneId.reset();
				}
				if (decoded->capturerProfileId == self && IsFailureStatus(decoded->status)) {
					g_lastResult = Result{ ResultCode(decoded->status), std::chrono::steady_clock::now() };
				}
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
				}
			}
			QueueStateChanged();
		}

		void HandleRejectPacket(const Json& a_packet)
		{
			const auto buildZoneId = ReadBuildZoneId(a_packet);
			const auto found = a_packet.find("status");
			if (!buildZoneId || found == a_packet.end() || !found->is_string()) {
				LogThrottledWarning("capture_reject_malformed", "Ignoring malformed settlementCaptureReject packet.");
				return;
			}
			{
				std::scoped_lock lock(g_mutex);
				if (g_localBuildZoneId == buildZoneId) {
					g_localBuildZoneId.reset();
				}
				g_lastResult = Result{ ResultCode(found->get<std::string>()), std::chrono::steady_clock::now() };
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
			g_localBuildZoneId.reset();
			g_lastResult.reset();
			g_playerPosition.reset();
			g_nearBuildZoneId.reset();
			g_lastHeartbeat = {};
		}
		QueueStateChanged();
	}

	void NoteLocalPosition(float a_x, float a_y, float a_z)
	{
		std::optional<std::uint64_t> heartbeatZone;
		bool nearChanged = false;
		{
			std::scoped_lock lock(g_mutex);
			g_playerPosition = Policy::Point{ a_x, a_y, a_z };
			const auto nearest = Policy::FindNearest(*g_playerPosition, AnchorsLocked());
			nearChanged = nearest != g_nearBuildZoneId;
			g_nearBuildZoneId = nearest;
			const auto now = std::chrono::steady_clock::now();
			if (g_localBuildZoneId && now - g_lastHeartbeat >= kHeartbeatInterval) {
				heartbeatZone = g_localBuildZoneId;
				g_lastHeartbeat = now;
			}
		}
		if (heartbeatZone) {
			QueuePacket(Json{ { "type", "settlementCapture" }, { "buildZoneId", *heartbeatZone }, { "op", "continue" } });
		}
		if (nearChanged) {
			QueueStateChanged();
		}
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
		if (op != "capture") {
			return false;
		}
		const auto buildZoneId = ReadBuildZoneId(action);
		if (!buildZoneId) {
			LogThrottledWarning("capture_action_malformed", "Ignoring a settlement capture action with an invalid buildZoneId.");
			return true;
		}
		{
			std::scoped_lock lock(g_mutex);
			g_localBuildZoneId = *buildZoneId;
			g_lastHeartbeat = std::chrono::steady_clock::now();
			g_lastResult.reset();
		}
		QueuePacket(Json{ { "type", "settlementCapture" }, { "buildZoneId", *buildZoneId }, { "op", "start" } });
		QueueStateChanged();
		return true;
	}

	std::string WithCaptureState(std::string a_stateJson)
	{
		try {
			auto state = Json::parse(a_stateJson);
			if (!state.is_object()) {
				return a_stateJson;
			}
			auto capture = BuildState();
			const auto budget = a_stateJson.size() < kCaptureOutputBudgetBytes ? kCaptureOutputBudgetBytes - a_stateJson.size() : std::size_t{ 0 };
			Json settlements = Json::array();
			std::size_t used = 0;
			bool truncated = false;
			for (auto& row : capture["settlements"]) {
				const auto cost = row.dump().size() + 1;
				if (used + cost > budget) {
					truncated = true;
					break;
				}
				used += cost;
				settlements.push_back(std::move(row));
			}
			capture["settlements"] = std::move(settlements);
			capture["truncated"] = truncated;
			state["settlementCapture"] = std::move(capture);
			return state.dump();
		} catch (...) {
			LogThrottledWarning("capture_state_dump", "Could not merge settlement capture into the faction state JSON.");
			return a_stateJson;
		}
	}
}

#include "pch.h"

#include "F4TSettlementCaptureInternal.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TClientIdentity.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TSettlementAccessClient.h"
#include "F4TSettlementCapturePolicy.h"
#include "F4TSettlementRaidClient.h"
#include "F4TWorkshopReplicationClient.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
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
	using Policy::IsActiveStatus;

	namespace
	{
		constexpr double kMaximumSecondsLeft = 86400.0;
		constexpr std::size_t kCaptureOutputBudgetBytes = 26 * 1024;

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
		COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI15* CaptureHudApi()
		{
			static auto* api = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI15>();
			return api;
		}

		bool InPlayerSpaceLocked(std::uint64_t a_buildZoneId)
		{
			const auto found = g_zoneLabels.find(a_buildZoneId);
			if (found == g_zoneLabels.end() || found->second.space.plugin.empty() || !g_playerSpace) {
				return true;
			}
			const auto& space = found->second.space;
			return space.localFormId == g_playerSpace->localFormId && _stricmp(space.plugin.c_str(), g_playerSpace->plugin.c_str()) == 0;
		}

		Json RaidProtectionJson(std::uint64_t a_secondsLeft, std::string_view a_reason)
		{
			if (a_secondsLeft == 0) {
				return Json{ { "active", false }, { "secondsLeft", std::uint64_t{ 0 } } };
			}
			return Json{ { "active", true }, { "reason", std::string{ a_reason } }, { "secondsLeft", a_secondsLeft } };
		}

		std::uint64_t ShieldSecondsLeft(const F4T::SettlementAccessClient::Workbench* a_workbench)
		{
			if (!a_workbench) {
				return 0;
			}
			const auto now = F4T::SettlementRaidClient::ServerUnixNow();
			return a_workbench->protectedUntilUnix > now ? a_workbench->protectedUntilUnix - now : 0;
		}

		Json WorkbenchRaidProtectionJson(const F4T::SettlementAccessClient::Workbench* a_workbench)
		{
			return RaidProtectionJson(ShieldSecondsLeft(a_workbench), "settlementProtected");
		}

		Json RowFor(std::uint64_t a_buildZoneId, const std::string& a_name, const std::string& a_ownerProfileId, const Record* a_record, const F4T::SettlementAccessClient::Workbench* a_workbench)
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
				{ "name", a_name },
				{ "state", state },
				{ "ownerProfileId", a_ownerProfileId },
				{ "ownerName", "" },
				{ "capturerProfileId", capturerProfileId },
				{ "capturerName", "" },
				{ "raid", raid },
				{ "progressPercent", progressPercent },
				{ "secondsLeft", secondsLeft },
				{ "cooldownSeconds", 0 },
				{ "workbenchHp", a_workbench ? Json(a_workbench->hp) : Json(nullptr) },
				{ "workbenchMaxHp", a_workbench ? Json(a_workbench->maxHp) : Json(nullptr) },
				{ "workbenchBroken", a_workbench && a_workbench->broken },
				{ "raidProtection", WorkbenchRaidProtectionJson(a_workbench) },
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

		std::chrono::steady_clock::time_point ProtectionExpiry(const Json& a_state)
		{
			std::uint64_t seconds = 0;
			const auto consider = [&seconds](const Json& a_protection) {
				if (!a_protection.is_object() || !a_protection.value("active", false)) {
					return;
				}
				const auto left = a_protection.value("secondsLeft", std::uint64_t{ 0 });
				seconds = seconds == 0 ? left : (std::min)(seconds, left);
			};
			consider(a_state.value("selfRaidProtection", Json{}));
			for (const auto& row : a_state.value("settlements", Json::array())) {
				consider(row.value("raidProtection", Json{}));
			}
			if (seconds == 0) {
				return {};
			}
			return std::chrono::steady_clock::now() + std::chrono::seconds{ static_cast<std::int64_t>(seconds) };
		}
	}

	bool IsNamedLocked(std::uint64_t a_buildZoneId)
	{
		const auto found = g_zoneLabels.find(a_buildZoneId);
		return found != g_zoneLabels.end() && !found->second.name.empty();
	}

	std::vector<Policy::Anchor> AnchorsLocked()
	{
		std::vector<Policy::Anchor> anchors;
		for (const auto& entry : g_zoneAnchors) {
			if (InPlayerSpaceLocked(entry.first)) {
				anchors.push_back(entry.second);
			}
		}
		for (const auto& [buildZoneId, record] : g_captures) {
			if (record.hasWorkbench && InPlayerSpaceLocked(buildZoneId)) {
				anchors.push_back(Policy::Anchor{ buildZoneId, true, record.workbench, record.captureRadius });
			}
		}
		return anchors;
	}

	bool IsAtWorkbenchLocked(std::uint64_t a_buildZoneId)
	{
		if (!g_playerPosition) {
			return false;
		}
		const auto anchors = AnchorsLocked();
		return std::any_of(anchors.begin(), anchors.end(), [a_buildZoneId](const Policy::Anchor& a_anchor) {
			return a_anchor.buildZoneId == a_buildZoneId && Policy::IsAtWorkbench(*g_playerPosition, a_anchor);
		});
	}

	Json BuildState()
	{
		const auto self = F4T::ClientIdentity::ConnectedProfileId();
		auto owners = F4T::SettlementAccessClient::OwnersByBuildZone();
		const auto workbenches = F4T::SettlementAccessClient::WorkbenchByBuildZone();
		Json state{
			{ "settlements", Json::array() },
			{ "nearBuildZoneId", nullptr },
			{ "canCapture", false },
			{ "promptCapture", false },
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
				const auto canCapture = Policy::CanStartCapture(g_localBuildZoneId.has_value(), owners.at(*nearest), self);
				const auto record = g_captures.find(*nearest);
				const auto beingCaptured = record != g_captures.end() && IsActiveStatus(record->second.status);
				state["canCapture"] = canCapture;
				state["promptCapture"] = canCapture && !beingCaptured && IsNamedLocked(*nearest) && IsAtWorkbenchLocked(*nearest);
			}
		}
		std::vector<std::pair<int, Json>> ranked;
		for (const auto& [buildZoneId, ownerProfileId] : owners) {
			const auto discovered = IsNamedLocked(buildZoneId) && (g_discovered.contains(buildZoneId) || nearest == buildZoneId);
			if (!discovered && g_localBuildZoneId != buildZoneId && (self.empty() || ownerProfileId != self)) {
				continue;
			}
			const auto label = g_zoneLabels.find(buildZoneId);
			const auto found = g_captures.find(buildZoneId);
			const auto* record = found == g_captures.end() ? nullptr : &found->second;
			const auto workbench = workbenches.find(buildZoneId);
			const auto* workbenchPointer = workbench == workbenches.end() ? nullptr : &workbench->second;
			ranked.emplace_back(CapturePriority(buildZoneId, ownerProfileId, record, self, nearest, g_localBuildZoneId), RowFor(buildZoneId, label == g_zoneLabels.end() ? std::string{} : label->second.name, ownerProfileId, record, workbenchPointer));
		}
		state["selfOwnsSettlement"] = !self.empty() && std::any_of(owners.begin(), owners.end(), [&self](const auto& a_entry) {
			return a_entry.second == self;
		});
		std::uint64_t selfShieldLeft = 0;
		for (const auto& [buildZoneId, ownerProfileId] : owners) {
			const auto bench = workbenches.find(buildZoneId);
			if (!self.empty() && ownerProfileId == self && bench != workbenches.end()) {
				selfShieldLeft = (std::max)(selfShieldLeft, ShieldSecondsLeft(&bench->second));
			}
		}
		state["selfRaidProtection"] = RaidProtectionJson(selfShieldLeft, "settlementProtected");
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
		g_hudRefreshAt = ProtectionExpiry(state);
		const auto text = state.dump();
		api->PushCaptureHud(text.c_str());
	}

	nlohmann::json MapCaptures()
	{
		struct ActiveZone
		{
			F4T::Workshop::BuildZoneId id{};
			Policy::Point workbench{};
			F4T::Workshop::FormKey space;
		};
		std::vector<ActiveZone> zones;
		{
			std::scoped_lock lock(g_mutex);
			for (const auto buildZoneId : g_activeZones) {
				const auto anchor = g_zoneAnchors.find(buildZoneId);
				const auto label = g_zoneLabels.find(buildZoneId);
				if (anchor != g_zoneAnchors.end() && label != g_zoneLabels.end()) {
					zones.push_back({ buildZoneId, anchor->second.workbench, label->second.space });
				}
			}
		}
		auto captures = Json::array();
		for (const auto& zone : zones) {
			const auto worldspaceId = F4T::WorkshopReplicationClient::FormIdOfKey(zone.space);
			if (!worldspaceId) {
				continue;
			}
			captures.push_back(Json{
				{ "id", std::to_string(zone.id) },
				{ "x", zone.workbench.x },
				{ "y", zone.workbench.y },
				{ "worldspaceId", std::format("{:08X}", *worldspaceId) },
			});
		}
		return captures;
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

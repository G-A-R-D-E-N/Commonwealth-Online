#pragma once

#include "F4TSettlementCapturePolicy.h"
#include "F4TWorkshopTransactions.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace F4T::SettlementCaptureClient
{
	struct Record
	{
		std::string status;
		std::string capturerProfileId;
		std::string ownerProfileId;
		bool raid{};
		double elapsedSeconds{};
		double holdSeconds{};
		bool hasWorkbench{};
		F4T::SettlementCapturePolicy::Point workbench{};
		float captureRadius{ F4T::SettlementCapturePolicy::kDefaultCaptureRadius };
		std::chrono::steady_clock::time_point receivedAt{};
	};

	struct ZoneLabel
	{
		std::string name;
		F4T::Workshop::FormKey space;
	};

	struct Result
	{
		std::string code;
		std::chrono::steady_clock::time_point seenAt{};
	};

	inline constexpr std::chrono::seconds kResultDisplayInterval{ 5 };

	inline std::mutex g_mutex;
	inline std::map<std::uint64_t, Record> g_captures;
	inline std::map<std::uint64_t, F4T::SettlementCapturePolicy::Anchor> g_zoneAnchors;
	inline std::map<std::uint64_t, ZoneLabel> g_zoneLabels;
	inline std::set<F4T::Workshop::BuildZoneId> g_activeZones;
	inline std::optional<std::uint64_t> g_localBuildZoneId;
	inline std::optional<Result> g_lastResult;
	inline std::optional<F4T::SettlementCapturePolicy::Point> g_playerPosition;
	inline std::optional<F4T::Workshop::FormKey> g_playerSpace;
	inline std::set<std::uint64_t> g_discovered;
	inline std::chrono::steady_clock::time_point g_hudRefreshAt{};

	[[nodiscard]] std::vector<F4T::SettlementCapturePolicy::Anchor> AnchorsLocked();
	[[nodiscard]] bool IsNamedLocked(std::uint64_t a_buildZoneId);
	[[nodiscard]] bool IsAtWorkbenchLocked(std::uint64_t a_buildZoneId);
	[[nodiscard]] nlohmann::json BuildState();
	void PushCaptureHud();
}

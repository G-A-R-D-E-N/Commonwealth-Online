#pragma once

#include "F4TWorkshopProtocol.h"
#include "F4TWorkshopReplicationClient.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

namespace F4T::WorkshopReplicationClient
{
	enum class PendingKind : std::uint8_t
	{
		Place,
		Move,
		Scrap
	};

	struct Pending
	{
		PendingKind kind{ PendingKind::Place };
		Workshop::Request request;
		RE::ObjectRefHandle localHandle;
		std::chrono::steady_clock::time_point sentAt{};
		std::chrono::steady_clock::time_point lastSendAt{};
		Workshop::Transform priorTransform{};
		bool hasPriorTransform{};
	};

	struct DoorSeen
	{
		bool open{};
		bool locked{};
	};

	struct ZoneState
	{
		Workshop::Revision revision{};
		std::map<Workshop::ObjectId, RE::ObjectRefHandle> objects;
		std::map<Workshop::ObjectId, std::uint8_t> crackStages;
		std::set<Workshop::ObjectId> damagedObjects;
		std::set<Workshop::ObjectId> poweredObjects;
		std::vector<F4T::WorkshopProtocol::PowerWire> wires;
		std::map<Workshop::ObjectId, F4T::WorkshopProtocol::PowerWire> appliedWires;
		RE::ObjectRefHandle settlementBench;
		std::chrono::steady_clock::time_point nextPowerRetryAt{};
		std::uint8_t powerTries{};
		bool powerGaveUp{};
		std::map<Workshop::ObjectId, DoorSeen> doors;
		std::map<Workshop::ObjectId, Workshop::ObjectState> pendingSnapshot;
		std::map<Workshop::ObjectId, Workshop::ObjectState> pendingTombstones;
		bool assemblingSnapshot{};
		std::optional<Workshop::ObjectState> campWorkbench;
		bool campBenchOwned{};
		std::chrono::steady_clock::time_point nextBenchClaimAt{};
		std::uint8_t benchClaimTries{};
	};

	inline std::mutex g_mutex;
	inline std::map<Workshop::BuildZoneId, ZoneState> g_zones;
	inline std::map<Workshop::OperationId, Pending> g_pending;
	inline std::uint64_t g_nextOperationId{ static_cast<std::uint64_t>(std::time(nullptr)) * 1000ULL };
	inline std::set<Workshop::BuildZoneId> g_ownCamps;
	struct PendingCamp
	{
		std::uint32_t dropEntityId{};
		RE::ObjectRefHandle localHandle;
		std::chrono::steady_clock::time_point sentAt{};
	};
	inline std::map<std::uint64_t, PendingCamp> g_pendingCamps;
	inline std::chrono::steady_clock::time_point g_nextDoorPoll{};
	inline constexpr std::chrono::seconds kPendingTimeout{ 15 };

	void RollbackPending(Pending& a_pending);
	bool HandleReject(const nlohmann::json& a_packet);
	void QueuePacket(nlohmann::json a_packet);
	void ForgetCampStores();
	void TickCampStores();
	void ForgetCampGrants();
	void TickCampGrants();
	void ShowCampMoveAlert(Workshop::BuildZoneId a_buildZoneId);
	void Warn(const char* a_key, std::string_view a_message);
	[[nodiscard]] Workshop::Transform TransformOf(const RE::TESObjectREFR& a_ref);
	[[nodiscard]] std::optional<Workshop::FormKey> SpaceOf(const RE::TESObjectREFR& a_ref);
	[[nodiscard]] RE::ObjectRefHandle HandleOf(const RE::TESObjectREFR& a_ref);
	void Despawn(RE::ObjectRefHandle& a_handle);
	bool EnqueuePending(Pending a_pending);
	bool HandleDoorState(const nlohmann::json& a_packet);
	void CollectDoorOps(std::vector<nlohmann::json>& a_outbound, std::chrono::steady_clock::time_point a_now);
}

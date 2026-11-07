#include "pch.h"

#include "F4TSettlementAccessClient.h"

#include "F4TAppliedOperations.h"
#include "F4TFactionPayload.h"
#include "F4TClientIdentity.h"
#include "F4TInventoryGrant.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TSettlementCaptureClient.h"
#include "F4TUIClient.h"
#include "F4TWorkshopReplicationClient.h"


#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace F4T::SettlementAccessClient
{
	using namespace F4T::Networking::Detail;

	namespace
	{
		constexpr std::size_t kMaximumProfileIdBytes = 128;
		constexpr std::size_t kMaximumActionJsonBytes = 1024;
		constexpr std::size_t kMaximumAllowedProfiles = 15;
		constexpr std::size_t kMaximumStoredSettlements = 1024;
		constexpr std::size_t kMaximumRefreshGets = 3;
		constexpr std::size_t kMaximumDestroyedStructures = 64;
		constexpr std::size_t kStateOutputBudgetBytes = 24 * 1024;
		constexpr std::chrono::seconds kMinimumGetInterval{ 1 };
		constexpr std::chrono::seconds kRejectDisplayInterval{ 5 };

		struct DestroyedStructure
		{
			std::uint64_t objectId{};
			std::string pieceType;
			std::uint32_t rebuildComponents{};
		};

		struct Record
		{
			std::string ownerProfileId;
			std::vector<std::string> allowedProfileIds;
			Workbench workbench;
			std::uint64_t upkeepPaidUntilUnix{};
			std::vector<DestroyedStructure> destroyedStructures;
		};

		std::optional<double> ReadNumber(const Json& a_packet, const char* a_key)
		{
			const auto found = a_packet.find(a_key);
			if (found == a_packet.end() || !found->is_number()) {
				return std::nullopt;
			}
			return found->get<double>();
		}

		struct Reject
		{
			std::uint64_t buildZoneId{};
			std::string status;
			std::chrono::steady_clock::time_point seenAt{};
		};

		std::mutex g_mutex;
		std::map<std::uint64_t, Record> g_settlements;
		std::optional<Reject> g_lastReject;
		constexpr std::string_view kAppliedChargesScope{ "settlement-charges" };
		std::chrono::steady_clock::time_point g_nextGetAllowed{};
		std::size_t g_refreshCursor{};

		bool IsKnownStatus(std::string_view a_status)
		{
			return a_status == "invalidRequest" || a_status == "unknownBuildZone" || a_status == "notOwner" ||
				a_status == "alreadyAllowed" || a_status == "notAllowed" || a_status == "full" ||
				a_status == "rateLimited" || a_status == "persistenceUnavailable" || a_status == "underAttack" ||
				a_status == "insufficientMaterials" || a_status == "nothingToRepair" || a_status == "chargeFailed" || a_status == "tooFar";
		}

		std::optional<std::uint64_t> ReadBuildZoneId(const Json& a_packet)
		{
			const auto found = a_packet.find("buildZoneId");
			if (found == a_packet.end()) {
				return std::nullopt;
			}
			try {
				if (found->is_number_unsigned()) {
					const auto value = found->get<std::uint64_t>();
					return value == 0 ? std::optional<std::uint64_t>{} : std::optional<std::uint64_t>{ value };
				}
				if (found->is_number_integer()) {
					const auto value = found->get<std::int64_t>();
					if (value <= 0) {
						return std::nullopt;
					}
					return static_cast<std::uint64_t>(value);
				}
			} catch (...) {
			}
			return std::nullopt;
		}


		void QueueGameTask(std::function<void()> a_task)
		{
			const auto* taskInterface = F4SE::GetTaskInterface();
			if (!taskInterface) {
				LogWarningWithLocalPlayerPrefix("Cannot queue build access work: task interface unavailable.");
				return;
			}
			taskInterface->AddTask(std::move(a_task));
		}

		void QueueStateChanged()
		{
			QueueGameTask([]() {
				F4T::UIClient::NotifyFactionStateChanged();
				F4T::SettlementCaptureClient::RefreshHud();
			});
		}

		void QueuePacket(Json a_packet)
		{
			QueueGameTask([packet = std::move(a_packet)]() {
				if (!F4T::Networking::IsConnectedToServer()) {
					LogThrottledWarning("build_access_send_offline", "Dropped a build access packet: not connected to a server.");
					return;
				}
				if (!SendPacketBytes(packet.dump())) {
					LogThrottledWarning("build_access_send_failed", "Could not send Commonwealth Online build access packet.");
				}
			});
		}

		void QueueGetRequest(std::uint64_t a_buildZoneId)
		{
			const auto now = std::chrono::steady_clock::now();
			{
				std::scoped_lock lock(g_mutex);
				if (g_nextGetAllowed != std::chrono::steady_clock::time_point{} && now < g_nextGetAllowed) {
					return;
				}
				g_nextGetAllowed = now + kMinimumGetInterval;
			}
			QueuePacket(Json{ { "type", "settlementAccessGet" }, { "buildZoneId", a_buildZoneId } });
		}

		void RefreshStoredZones()
		{
			const auto now = std::chrono::steady_clock::now();
			std::vector<std::uint64_t> zones;
			{
				std::scoped_lock lock(g_mutex);
				if (g_settlements.empty()) {
					return;
				}
				if (g_nextGetAllowed != std::chrono::steady_clock::time_point{} && now < g_nextGetAllowed) {
					return;
				}
				const auto count = std::min(kMaximumRefreshGets, g_settlements.size());
				g_nextGetAllowed = now + kMinimumGetInterval * static_cast<std::chrono::seconds::rep>(count);
				auto entry = g_settlements.begin();
				std::advance(entry, static_cast<std::ptrdiff_t>(g_refreshCursor % g_settlements.size()));
				for (std::size_t taken = 0; taken < count; ++taken) {
					zones.push_back(entry->first);
					++entry;
					if (entry == g_settlements.end()) {
						entry = g_settlements.begin();
					}
				}
				g_refreshCursor = (g_refreshCursor + count) % g_settlements.size();
			}
			for (const auto zone : zones) {
				QueuePacket(Json{ { "type", "settlementAccessGet" }, { "buildZoneId", zone } });
			}
		}

		std::vector<DestroyedStructure> ReadDestroyedStructures(const Json& a_packet)
		{
			std::vector<DestroyedStructure> result;
			const auto found = a_packet.find("destroyedStructures");
			if (found == a_packet.end() || !found->is_array()) {
				return result;
			}
			for (const auto& entry : *found) {
				if (result.size() >= kMaximumDestroyedStructures) {
					break;
				}
				if (!entry.is_object()) {
					continue;
				}
				const auto objectId = entry.find("objectId");
				const auto pieceType = entry.find("pieceType");
				const auto components = entry.find("rebuildComponents");
				if (objectId == entry.end() || !objectId->is_number_unsigned() || pieceType == entry.end() || !pieceType->is_string() ||
					components == entry.end() || !components->is_number_unsigned()) {
					continue;
				}
				result.push_back(DestroyedStructure{ objectId->get<std::uint64_t>(), pieceType->get<std::string>(), components->get<std::uint32_t>() });
			}
			return result;
		}

		void HandleStatePacket(const Json& a_packet)
		{
			const auto buildZoneId = ReadBuildZoneId(a_packet);
			const auto ownerFound = a_packet.find("ownerProfileId");
			const auto allowedFound = a_packet.find("allowedProfileIds");
			if (!buildZoneId || ownerFound == a_packet.end() || !ownerFound->is_string() ||
				allowedFound == a_packet.end() || !allowedFound->is_array() ||
				allowedFound->size() > kMaximumAllowedProfiles) {
				LogThrottledWarning("build_access_state_malformed", "Ignoring malformed settlementAccessState packet.");
				return;
			}
			const auto owner = ownerFound->get<std::string>();
			if (!owner.empty() && !F4T::Factions::Payload::IsSafeToken(owner, kMaximumProfileIdBytes)) {
				LogThrottledWarning("build_access_state_owner", "Ignoring settlementAccessState with an invalid owner.");
				return;
			}
			std::vector<std::string> allowed;
			for (const auto& entry : *allowedFound) {
				if (!entry.is_string()) {
					LogThrottledWarning("build_access_state_allowed", "Ignoring settlementAccessState with an invalid allow-list.");
					return;
				}
				const auto id = entry.get<std::string>();
				if (!F4T::Factions::Payload::IsSafeToken(id, kMaximumProfileIdBytes)) {
					LogThrottledWarning("build_access_state_allowed", "Ignoring settlementAccessState with an invalid allow-list.");
					return;
				}
				allowed.push_back(id);
			}
			Workbench workbench;
			workbench.hp = static_cast<float>(ReadNumber(a_packet, "workbenchHp").value_or(workbench.hp));
			workbench.maxHp = static_cast<float>(ReadNumber(a_packet, "workbenchMaxHp").value_or(workbench.maxHp));
			const auto broken = a_packet.find("workbenchBroken");
			workbench.broken = broken != a_packet.end() && broken->is_boolean() && broken->get<bool>();
			workbench.protectedUntilUnix = static_cast<std::uint64_t>(ReadNumber(a_packet, "protectedUntilUnix").value_or(0.0));
			workbench.ownerProtectedUntilUnix = static_cast<std::uint64_t>(ReadNumber(a_packet, "ownerProtectedUntilUnix").value_or(0.0));
			const auto upkeepPaidUntil = static_cast<std::uint64_t>(ReadNumber(a_packet, "upkeepPaidUntil").value_or(0.0));
			auto destroyedStructures = ReadDestroyedStructures(a_packet);
			bool dropped = false;
			bool ownerChanged = false;
			{
				std::scoped_lock lock(g_mutex);
				const auto known = g_settlements.find(*buildZoneId);
				if (known == g_settlements.end() && g_settlements.size() >= kMaximumStoredSettlements) {
					dropped = true;
				} else {
					ownerChanged = known != g_settlements.end() && known->second.ownerProfileId != owner;
					g_settlements[*buildZoneId] = Record{ owner, std::move(allowed), workbench, upkeepPaidUntil, std::move(destroyedStructures) };
					if (g_lastReject && g_lastReject->buildZoneId == *buildZoneId) {
						g_lastReject.reset();
					}
				}
			}
			if (dropped) {
				LogThrottledWarning("build_access_state_full", "Dropping settlementAccessState: settlement store is full.");
				return;
			}
			if (ownerChanged) {
				QueueGetRequest(*buildZoneId);
			}
			QueueStateChanged();
		}

		void HandleRejectPacket(const Json& a_packet)
		{
			const auto buildZoneId = ReadBuildZoneId(a_packet);
			const auto status = ReadString(a_packet, "status");
			if (!buildZoneId || !status || !IsKnownStatus(*status)) {
				LogThrottledWarning("build_access_reject_malformed", "Ignoring malformed settlementAccessReject packet.");
				return;
			}
			{
				std::scoped_lock lock(g_mutex);
				g_lastReject = Reject{ *buildZoneId, *status, std::chrono::steady_clock::now() };
			}
			QueueStateChanged();
		}

		void SendChargeResult(std::uint64_t a_operationId, bool a_ok, std::string_view a_reason)
		{
			Json result{ { "type", "settlementChargeResult" }, { "operationId", a_operationId }, { "ok", a_ok } };
			if (!a_reason.empty()) {
				result["reason"] = std::string{ a_reason };
			}
			SendPacketBytes(result.dump());
		}

		void ApplyCharge(std::uint64_t a_operationId, const std::vector<std::pair<std::string, std::uint32_t>>& a_lines)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->GetParentCell()) {
				return;
			}
			if (F4T::AppliedOperations::Contains(kAppliedChargesScope, a_operationId)) {
				SendChargeResult(a_operationId, true, "");
				return;
			}
			std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> removals;
			for (const auto& [key, count] : a_lines) {
				auto* object = F4T::InventoryGrant::ResolveItem(key);
				if (!object) {
					SendChargeResult(a_operationId, false, "unresolvedItem");
					return;
				}
				if (player->GetInventoryObjectCount(object) < static_cast<std::int32_t>(count)) {
					SendChargeResult(a_operationId, false, "missingMaterials");
					return;
				}
				removals.emplace_back(object, static_cast<std::int32_t>(count));
			}
			for (const auto& [object, count] : removals) {
				RE::TESObjectREFR::RemoveItemData removal{ object, count };
				player->RemoveItem(removal);
			}
			if (!F4T::AppliedOperations::Remember(kAppliedChargesScope, a_operationId)) {
				LogThrottledWarning("settlement_charge_remember", "Settlement charge applied but could not be remembered.");
			}
			SendChargeResult(a_operationId, true, "");
		}

		void HandleChargePacket(const Json& a_packet)
		{
			const auto operationId = a_packet.find("operationId");
			const auto items = a_packet.find("items");
			if (operationId == a_packet.end() || !operationId->is_number_unsigned() || items == a_packet.end() || !items->is_array()) {
				LogThrottledWarning("settlement_charge_malformed", "Ignoring malformed settlementCharge packet.");
				return;
			}
			std::vector<std::pair<std::string, std::uint32_t>> lines;
			for (const auto& line : *items) {
				const auto key = line.find("itemKey");
				const auto count = line.find("count");
				if (!line.is_object() || key == line.end() || !key->is_string() || count == line.end() || !count->is_number_unsigned()) {
					LogThrottledWarning("settlement_charge_malformed", "Ignoring malformed settlementCharge packet.");
					return;
				}
				lines.emplace_back(key->get<std::string>(), count->get<std::uint32_t>());
			}
			QueueGameTask([id = operationId->get<std::uint64_t>(), lines = std::move(lines)]() { ApplyCharge(id, lines); });
		}
	}

	bool TryHandlePacket(std::string_view a_type, const Json& a_packet)
	{
		if (a_type == "settlementAccessState") {
			HandleStatePacket(a_packet);
		} else if (a_type == "settlementAccessReject") {
			HandleRejectPacket(a_packet);
		} else if (a_type == "settlementCharge") {
			HandleChargePacket(a_packet);
		} else {
			return false;
		}
		return true;
	}

	void Reset()
	{
		{
			std::scoped_lock lock(g_mutex);
			g_settlements.clear();
			g_lastReject.reset();
			g_nextGetAllowed = {};
			g_refreshCursor = 0;
		}
		F4T::ClientIdentity::ClearConnectedProfileId();
		QueueStateChanged();
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
		if (op == "refresh") {
			RefreshStoredZones();
			return false;
		}
		const bool charge = op == "structureRepair" || op == "structureRebuild" || op == "workbenchRepair" || op == "upkeepPay";
		if (!charge && op != "buildAccessGet" && op != "buildAccessGrant" && op != "buildAccessRevoke") {
			return false;
		}
		const auto buildZoneId = ReadBuildZoneId(action);
		if (!buildZoneId) {
			LogThrottledWarning("build_access_action_malformed", "Ignoring a build access action with an invalid buildZoneId.");
			return true;
		}
		{
			std::scoped_lock lock(g_mutex);
			g_lastReject.reset();
		}
		if (charge) {
			if (op == "structureRepair" || op == "structureRebuild") {
				const auto objectId = action.find("objectId");
				if (objectId == action.end() || !objectId->is_number_unsigned()) {
					LogThrottledWarning("build_access_action_object", "Ignoring a structure action with an invalid objectId.");
					return true;
				}
				QueuePacket(Json{ { "type", op }, { "buildZoneId", *buildZoneId }, { "objectId", objectId->get<std::uint64_t>() } });
				return true;
			}
			QueuePacket(Json{ { "type", op }, { "buildZoneId", *buildZoneId } });
			return true;
		}
		if (op == "buildAccessGet") {
			QueueGetRequest(*buildZoneId);
			return true;
		}
		const auto target = ReadString(action, "targetProfileId");
		if (!target || !F4T::Factions::Payload::IsSafeToken(*target, kMaximumProfileIdBytes)) {
			LogThrottledWarning("build_access_action_target", "Ignoring a build access action with an invalid targetProfileId.");
			return true;
		}
		QueuePacket(Json{
			{ "type", "settlementAccessUpdate" },
			{ "buildZoneId", *buildZoneId },
			{ "op", op == "buildAccessGrant" ? "grant" : "revoke" },
			{ "targetProfileId", *target },
		});
		return true;
	}

	std::map<std::uint64_t, std::string> OwnersByBuildZone()
	{
		std::map<std::uint64_t, std::string> owners;
		std::scoped_lock lock(g_mutex);
		for (const auto& [zone, record] : g_settlements) {
			owners[zone] = record.ownerProfileId;
		}
		return owners;
	}

	std::map<std::uint64_t, Workbench> WorkbenchByBuildZone()
	{
		std::map<std::uint64_t, Workbench> workbenches;
		std::scoped_lock lock(g_mutex);
		for (const auto& [zone, record] : g_settlements) {
			workbenches[zone] = record.workbench;
		}
		return workbenches;
	}

	std::set<std::uint64_t> BuildZonesAllowedFor(std::string_view a_profileId)
	{
		std::set<std::uint64_t> zones;
		std::scoped_lock lock(g_mutex);
		for (const auto& [zone, record] : g_settlements) {
			if (record.ownerProfileId == a_profileId ||
				std::find(record.allowedProfileIds.begin(), record.allowedProfileIds.end(), a_profileId) != record.allowedProfileIds.end()) {
				zones.insert(zone);
			}
		}
		return zones;
	}

	std::string WithBuildAccess(std::string a_stateJson)
	{
		try {
			auto state = Json::parse(a_stateJson);
			if (!state.is_object()) {
				return a_stateJson;
			}
			Json access{ { "settlements", Json::array() }, { "damagedStructures", Json::array() }, { "destroyedStructures", Json::array() }, { "lastReject", nullptr }, { "truncated", false } };
			const auto budget = a_stateJson.size() < kStateOutputBudgetBytes ? kStateOutputBudgetBytes - a_stateJson.size() : std::size_t{ 0 };
			std::size_t used = 0;
			std::map<std::uint64_t, std::string> zoneNames;
			{
				std::scoped_lock lock(g_mutex);
				for (const auto& [zone, record] : g_settlements) {
					zoneNames.emplace(zone, std::string{});
				}
			}
			for (auto& [zone, name] : zoneNames) {
				name = F4T::SettlementCaptureClient::ZoneName(zone);
			}
			{
				std::scoped_lock lock(g_mutex);
				for (const auto& [zone, record] : g_settlements) {
					Json allowed = Json::array();
					for (const auto& id : record.allowedProfileIds) {
						allowed.push_back(id);
					}
					Json entry{
						{ "buildZoneId", zone },
						{ "name", zoneNames[zone] },
						{ "ownerProfileId", record.ownerProfileId },
						{ "allowedProfileIds", std::move(allowed) },
						{ "workbenchHp", record.workbench.hp },
						{ "workbenchMaxHp", record.workbench.maxHp },
						{ "workbenchBroken", record.workbench.broken },
						{ "upkeepPaidUntil", record.upkeepPaidUntilUnix },
					};
					const auto cost = entry.dump().size() + 1;
					if (used + cost > budget) {
						access["truncated"] = true;
						break;
					}
					used += cost;
					access["settlements"].push_back(std::move(entry));
				}
				if (g_lastReject && std::chrono::steady_clock::now() - g_lastReject->seenAt < kRejectDisplayInterval) {
					access["lastReject"] = Json{ { "buildZoneId", g_lastReject->buildZoneId }, { "status", g_lastReject->status } };
				}
			}
			const auto self = F4T::ClientIdentity::ConnectedProfileId();
			for (const auto zone : BuildZonesAllowedFor(self)) {
				for (const auto objectId : F4T::WorkshopReplicationClient::DamagedObjectIds(zone)) {
					access["damagedStructures"].push_back(Json{ { "buildZoneId", zone }, { "objectId", objectId } });
				}
				std::scoped_lock lock(g_mutex);
				const auto known = g_settlements.find(zone);
				if (known == g_settlements.end()) {
					continue;
				}
				for (const auto& structure : known->second.destroyedStructures) {
					access["destroyedStructures"].push_back(Json{
						{ "buildZoneId", zone },
						{ "objectId", structure.objectId },
						{ "pieceType", structure.pieceType },
						{ "rebuildComponents", structure.rebuildComponents } });
				}
			}
			state["selfProfileId"] = self;
			state["buildAccess"] = std::move(access);
			return state.dump();
		} catch (...) {
			LogThrottledWarning("build_access_state_dump", "Could not merge build access into the faction state JSON.");
			return a_stateJson;
		}
	}
}

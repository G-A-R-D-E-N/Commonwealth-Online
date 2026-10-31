#include "pch.h"

#include "F4TSettlementAccessClient.h"

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
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
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
		constexpr std::size_t kStateOutputBudgetBytes = 24 * 1024;
		constexpr std::chrono::seconds kMinimumGetInterval{ 1 };
		constexpr std::chrono::seconds kRejectDisplayInterval{ 5 };

		struct Record
		{
			std::string ownerProfileId;
			std::vector<std::string> allowedProfileIds;
		};

		struct Reject
		{
			std::uint64_t buildZoneId{};
			std::string status;
			std::chrono::steady_clock::time_point seenAt{};
		};

		std::mutex g_mutex;
		std::map<std::uint64_t, Record> g_settlements;
		std::optional<Reject> g_lastReject;
		std::chrono::steady_clock::time_point g_nextGetAllowed{};
		std::size_t g_refreshCursor{};

		bool IsKnownStatus(std::string_view a_status)
		{
			return a_status == "invalidRequest" || a_status == "unknownBuildZone" || a_status == "notOwner" ||
				a_status == "alreadyAllowed" || a_status == "notAllowed" || a_status == "full" ||
				a_status == "rateLimited" || a_status == "persistenceUnavailable";
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
			QueueGameTask([]() { F4T::UIClient::NotifyFactionStateChanged(); });
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
			bool dropped = false;
			bool ownerChanged = false;
			{
				std::scoped_lock lock(g_mutex);
				const auto known = g_settlements.find(*buildZoneId);
				if (known == g_settlements.end() && g_settlements.size() >= kMaximumStoredSettlements) {
					dropped = true;
				} else {
					ownerChanged = known != g_settlements.end() && known->second.ownerProfileId != owner;
					g_settlements[*buildZoneId] = Record{ owner, std::move(allowed) };
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
	}

	bool TryHandlePacket(std::string_view a_type, const Json& a_packet)
	{
		if (a_type == "settlementAccessState") {
			HandleStatePacket(a_packet);
		} else if (a_type == "settlementAccessReject") {
			HandleRejectPacket(a_packet);
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
		if (op != "buildAccessGet" && op != "buildAccessGrant" && op != "buildAccessRevoke") {
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

	std::string WithBuildAccess(std::string a_stateJson)
	{
		try {
			auto state = Json::parse(a_stateJson);
			if (!state.is_object()) {
				return a_stateJson;
			}
			Json access{ { "settlements", Json::array() }, { "lastReject", nullptr }, { "truncated", false } };
			const auto budget = a_stateJson.size() < kStateOutputBudgetBytes ? kStateOutputBudgetBytes - a_stateJson.size() : std::size_t{ 0 };
			std::size_t used = 0;
			{
				std::scoped_lock lock(g_mutex);
				for (const auto& [zone, record] : g_settlements) {
					Json allowed = Json::array();
					for (const auto& id : record.allowedProfileIds) {
						allowed.push_back(id);
					}
					Json entry{
						{ "buildZoneId", zone },
						{ "ownerProfileId", record.ownerProfileId },
						{ "allowedProfileIds", std::move(allowed) },
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
			state["selfProfileId"] = F4T::ClientIdentity::ConnectedProfileId();
			state["buildAccess"] = std::move(access);
			return state.dump();
		} catch (...) {
			LogThrottledWarning("build_access_state_dump", "Could not merge build access into the faction state JSON.");
			return a_stateJson;
		}
	}
}

#include "pch.h"

#include "F4TSettlementRaidClient.h"

#include "F4TFactionPayload.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TRemotePlayerState.h"
#include "F4TSettlementCaptureClient.h"
#include "F4TUIClient.h"
#include "F4TWorkshopProtocol.h"
#include "F4TWorkshopRaids.h"

#include "RE/S/SendHUDMessage.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

namespace F4T::SettlementRaidClient
{
	using namespace F4T::Networking::Detail;

	namespace
	{
		constexpr std::size_t kMaximumProfileIdBytes = 128;
		constexpr std::size_t kMaximumActionJsonBytes = 1024;
		constexpr std::size_t kMaximumStoredZones = 256;
		constexpr std::size_t kMaximumSettlersPerZone = 32;
		constexpr std::size_t kStateReserveBytes = 128;
		constexpr double kMaximumRaidLifetimeSeconds = 86400.0 * 365.0;
		constexpr std::chrono::seconds kRaidHitRefusedNoticeInterval{ 5 };
		constexpr std::chrono::seconds kAttackNoticeInterval{ 60 };
		constexpr std::chrono::seconds kTurretHitNoticeInterval{ 5 };
		constexpr std::chrono::seconds kRefusalShowInterval{ 30 };
		constexpr std::string_view kWindowedCapability = "settlement-raid-windowed-v1";

		struct Window
		{
			float startHour{};
			float durationHours{};
			float offsetHours{};
		};

		struct Raid
		{
			std::string declarerProfileId;
			std::chrono::steady_clock::time_point liveUntil{};
			double readyUnix{};
			double expiresUnix{};
		};

		struct Settler
		{
			std::uint64_t settlerId{};
			std::string assignment;
			float health{};
			bool dead{};
		};

		struct Defense
		{
			float rating{};
			bool underAttack{};
			std::uint32_t attackers{};
			std::string detail;

			bool operator==(const Defense&) const = default;
		};

		struct Refusal
		{
			std::string text;
			std::chrono::steady_clock::time_point until{};
		};

		struct Zone
		{
			std::optional<Window> window;
			std::optional<Raid> raid;
			std::map<std::uint64_t, Settler> settlers;
			std::optional<Defense> defense;
		};

		std::mutex g_mutex;
		std::chrono::steady_clock::time_point g_nextRaidHitRefusedNotice{};
		std::map<std::uint64_t, std::chrono::steady_clock::time_point> g_nextAttackNotice;
		std::chrono::steady_clock::time_point g_nextTurretHitNotice{};
		double g_serverClockOffsetSeconds{};
		std::map<std::uint64_t, Zone> g_zones;
		bool g_windowedRaids{};
		std::optional<Refusal> g_refusal;

		std::optional<std::uint64_t> ReadU64Field(const Json& a_packet, const char* a_fieldName)
		{
			const auto found = a_packet.find(a_fieldName);
			return found == a_packet.end() ? std::nullopt : F4T::WorkshopProtocol::Codec::ReadU64(*found);
		}

		std::optional<std::uint64_t> ReadZoneId(const Json& a_packet)
		{
			const auto zoneId = ReadU64Field(a_packet, "buildZoneId");
			if (!zoneId || *zoneId == 0) {
				return std::nullopt;
			}
			return zoneId;
		}

		bool IsAssignment(std::string_view a_value)
		{
			return a_value == "guard" || a_value == "farm" || a_value == "scavenge" || a_value == "purifier" || a_value == "idle";
		}

		float LocalOffsetHours()
		{
			try {
				return static_cast<float>(std::chrono::current_zone()->get_info(std::chrono::system_clock::now()).offset.count()) / 3600.0F;
			} catch (const std::exception&) {
				LogThrottledWarning("raid_zone_offset", "Could not read the local time zone; settlement windows use UTC.");
				return 0.0F;
			}
		}

		void QueueGameTask(std::function<void()> a_task)
		{
			const auto* taskInterface = F4SE::GetTaskInterface();
			if (!taskInterface) {
				LogWarningWithLocalPlayerPrefix("Cannot queue settlement raid work: task interface unavailable.");
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
					LogThrottledWarning("raid_send_offline", "Dropped a settlement raid packet: not connected to a server.");
					return;
				}
				if (!SendPacketBytes(packet.dump())) {
					LogThrottledWarning("raid_send_failed", "Could not send Commonwealth Online settlement raid packet.");
				}
			});
		}

		Zone* FindOrAddZone(std::uint64_t a_zoneId)
		{
			if (const auto found = g_zones.find(a_zoneId); found != g_zones.end()) {
				return &found->second;
			}
			if (g_zones.size() >= kMaximumStoredZones) {
				LogThrottledWarning("raid_zone_full", "Dropping settlement raid state: the zone store is full.");
				return nullptr;
			}
			return &g_zones[a_zoneId];
		}

		void HandleWindowPacket(const Json& a_packet)
		{
			const auto zoneId = ReadZoneId(a_packet);
			const auto startHour = ReadFloat(a_packet, "windowStartHour");
			const auto durationHours = ReadFloat(a_packet, "windowDurationHours");
			const auto offsetHours = ReadFloat(a_packet, "timezoneOffsetHours");
			if (!zoneId || !startHour || !durationHours || !offsetHours ||
				!F4T::WorkshopRaids::ValidWindowSet(*startHour, *durationHours, *offsetHours)) {
				LogThrottledWarning("raid_window_state_malformed", "Ignoring malformed settlementWindowState packet.");
				return;
			}
			{
				std::scoped_lock lock(g_mutex);
				auto* zone = FindOrAddZone(*zoneId);
				if (!zone) {
					return;
				}
				zone->window = Window{ *startHour, *durationHours, *offsetHours };
			}
			QueueStateChanged();
		}

		void HandleRaidDeclarePacket(const Json& a_packet)
		{
			const auto zoneId = ReadZoneId(a_packet);
			const auto declarer = ReadString(a_packet, "declarerProfileId");
			const auto readyMonotonic = ReadDouble(a_packet, "readyMonotonic");
			const auto expiresMonotonic = ReadDouble(a_packet, "expiresMonotonic");
			const auto readyUnix = ReadDouble(a_packet, "readyUnix");
			const auto expiresUnix = ReadDouble(a_packet, "expiresUnix");
			if (!zoneId || !declarer || !F4T::Factions::Payload::IsSafeToken(*declarer, kMaximumProfileIdBytes) ||
				!readyMonotonic || !expiresMonotonic || *expiresMonotonic < *readyMonotonic || !readyUnix || !expiresUnix) {
				LogThrottledWarning("raid_declare_state_malformed", "Ignoring malformed raidDeclareState packet.");
				return;
			}
			const auto lifetimeSeconds = std::clamp(F4T::WorkshopRaids::DeclarationWarningSeconds + (*expiresMonotonic - *readyMonotonic), 0.0, kMaximumRaidLifetimeSeconds);
			const auto lifetime = std::chrono::seconds{ static_cast<std::int64_t>(lifetimeSeconds) };
			{
				std::scoped_lock lock(g_mutex);
				auto* zone = FindOrAddZone(*zoneId);
				if (!zone) {
					return;
				}
				zone->raid = Raid{ *declarer, std::chrono::steady_clock::now() + lifetime, *readyUnix, *expiresUnix };
			}
			QueueStateChanged();
		}

		void HandleSettlerStatePacket(const Json& a_packet)
		{
			const auto zoneId = ReadZoneId(a_packet);
			const auto settlerId = ReadU64Field(a_packet, "settlerId");
			const auto assignment = ReadString(a_packet, "assignment");
			const auto health = ReadFloat(a_packet, "health");
			const auto dead = ReadBool(a_packet, "dead");
			if (!zoneId || !settlerId || !assignment || !IsAssignment(*assignment) || !health || !dead) {
				LogThrottledWarning("raid_settler_state_malformed", "Ignoring malformed settlerState packet.");
				return;
			}
			{
				std::scoped_lock lock(g_mutex);
				auto* zone = FindOrAddZone(*zoneId);
				if (!zone) {
					return;
				}
				if (zone->settlers.size() >= kMaximumSettlersPerZone && !zone->settlers.contains(*settlerId)) {
					LogThrottledWarning("raid_settler_full", "Dropping settlerState: this settlement's settler list is full.");
					return;
				}
				zone->settlers[*settlerId] = Settler{ *settlerId, *assignment, *health, *dead };
			}
			QueueStateChanged();
		}

		void NoteAttackState(std::uint64_t a_zoneId, bool a_underAttack)
		{
			if (!a_underAttack) {
				std::scoped_lock lock(g_mutex);
				g_nextAttackNotice.erase(a_zoneId);
				return;
			}
			const auto now = std::chrono::steady_clock::now();
			{
				std::scoped_lock lock(g_mutex);
				const auto next = g_nextAttackNotice.find(a_zoneId);
				if (next != g_nextAttackNotice.end() && now < next->second) {
					return;
				}
				g_nextAttackNotice[a_zoneId] = now + kAttackNoticeInterval;
			}
			QueueGameTask([a_zoneId]() {
				const auto name = F4T::SettlementCaptureClient::ZoneName(a_zoneId);
				const auto text = name.empty() ? std::string{ "This settlement is under attack." } : std::format("{} is under attack.", name);
				RE::SendHUDMessage::ShowHUDMessage(text.c_str(), nullptr, true, true);
			});
		}

		void HandleZoneAttackPacket(const Json& a_packet)
		{
			const auto zoneId = ReadZoneId(a_packet);
			const auto underAttack = ReadBool(a_packet, "underAttack");
			if (!zoneId || !underAttack) {
				LogThrottledWarning("zone_attack_state_malformed", "Ignoring malformed zoneAttackState packet.");
				return;
			}
			NoteAttackState(*zoneId, *underAttack);
		}

		void HandleDefenderEventPacket(const Json& a_packet)
		{
			const auto kind = ReadString(a_packet, "kind");
			if (!kind || *kind != "turretFire") {
				return;
			}
			const auto targetPlayerId = ReadU64Field(a_packet, "targetPlayerId");
			const auto damage = ReadFloat(a_packet, "damage");
			if (!targetPlayerId || !damage || !(*damage > 0.0F)) {
				LogThrottledWarning("defender_event_malformed", "Ignoring malformed defenderEvent packet.");
				return;
			}
			const auto assigned = F4T::RemotePlayerState::GetAssignedPlayerId();
			if (!assigned || *assigned != *targetPlayerId) {
				return;
			}
			const auto now = std::chrono::steady_clock::now();
			bool notify = false;
			{
				std::scoped_lock lock(g_mutex);
				notify = now >= g_nextTurretHitNotice;
				if (notify) {
					g_nextTurretHitNotice = now + kTurretHitNoticeInterval;
				}
			}
			QueueGameTask([damageValue = *damage, notify]() {
				F4T::RemotePlayerState::ApplyServerDamageToLocalPlayer(damageValue);
				if (notify) {
					const auto text = std::format("A settlement turret hit you for {:.0f} damage.", damageValue);
					RE::SendHUDMessage::ShowHUDMessage(text.c_str(), nullptr, true, true);
				}
			});
		}

		void HandleDefenderStatePacket(const Json& a_packet)
		{
			const auto zoneId = ReadZoneId(a_packet);
			const auto rating = ReadFloat(a_packet, "defenseRating");
			const auto underAttack = ReadBool(a_packet, "underAttack");
			const auto attackers = ReadU64Field(a_packet, "attackerCount");
			const auto detail = ReadString(a_packet, "detail");
			if (!zoneId || !rating || !underAttack || !attackers || *attackers > UINT32_MAX || !detail ||
				(*detail != "full" && *detail != "coarse" && *detail != "abstract")) {
				LogThrottledWarning("raid_defender_state_malformed", "Ignoring malformed defenderState packet.");
				return;
			}
			const Defense next{ *rating, *underAttack, static_cast<std::uint32_t>(*attackers), *detail };
			{
				std::scoped_lock lock(g_mutex);
				auto* zone = FindOrAddZone(*zoneId);
				if (!zone || zone->defense == next) {
					return;
				}
				zone->defense = next;
			}
			QueueStateChanged();
			NoteAttackState(*zoneId, next.underAttack);
		}

		std::string FormatDuration(std::uint64_t a_seconds)
		{
			if (a_seconds >= 3600) {
				return std::format("{}h {}m", a_seconds / 3600, a_seconds % 3600 / 60);
			}
			if (a_seconds >= 60) {
				return std::format("{}m", a_seconds / 60);
			}
			return std::format("{}s", a_seconds);
		}

		std::string RefusalText(std::string_view a_reason, std::uint64_t a_secondsLeft)
		{
			const auto remaining = FormatDuration(a_secondsLeft);
			if (a_reason == "settlementProtected") {
				return std::format("This settlement is protected for {} more. Its workbench cannot be hit.", remaining);
			}
			if (a_reason == "factionLeaveBlocked") {
				return std::format("You left this settlement's faction recently. You cannot raid it for {} more.", remaining);
			}
			if (a_reason == "factionMate") {
				return "Your faction holds this settlement. Faction members cannot raid it.";
			}
			if (a_reason == "weaponUnknown") {
				return "Your weapon is not on the server weapon list, so it cannot damage this settlement.";
			}
			return "This settlement can only be raided during its raid window, by a declared raid.";
		}

		std::string SettlementRefusalText(std::string_view a_reason)
		{
			if (a_reason == "outsideRaidWindow") {
				return "A raid declared now cannot overlap this settlement's raid window. Declare it closer to the window.";
			}
			if (a_reason == "dailyLimit") {
				return "Your group has used its raid declarations for today.";
			}
			if (a_reason == "alreadyDeclared") {
				return "A raid is already declared on this settlement, or you already have one pending.";
			}
			if (a_reason == "ownSettlement") {
				return "You cannot raid a settlement you help run.";
			}
			if (a_reason == "settlerCapSettlement") {
				return "This settlement already has its maximum settlers.";
			}
			if (a_reason == "settlerCapPlayer") {
				return "You already have the maximum settlers across your settlements.";
			}
			if (a_reason == "settlerCapServer") {
				return "This server has reached its settler limit.";
			}
			return "This settlement action was refused.";
		}

		void HandleSettlementRefusedPacket(const Json& a_packet)
		{
			const auto reason = a_packet.find("reason");
			if (reason == a_packet.end() || !reason->is_string()) {
				LogThrottledWarning("settlement_refused_malformed", "Ignoring malformed settlementRefused packet.");
				return;
			}
			const auto text = SettlementRefusalText(reason->get<std::string>());
			{
				std::scoped_lock lock(g_mutex);
				g_refusal = Refusal{ text, std::chrono::steady_clock::now() + kRefusalShowInterval };
			}
			QueueStateChanged();
			QueueGameTask([text]() { RE::SendHUDMessage::ShowHUDMessage(text.c_str(), nullptr, true, true); });
		}

		void HandleRaidHitRefusedPacket(const Json& a_packet)
		{
			const auto reason = a_packet.find("reason");
			if (reason == a_packet.end() || !reason->is_string()) {
				LogThrottledWarning("raid_hit_refused_malformed", "Ignoring malformed raidHitRefused packet.");
				return;
			}
			std::uint64_t secondsLeft = 0;
			if (const auto found = a_packet.find("secondsLeft"); found != a_packet.end() && found->is_number_unsigned()) {
				secondsLeft = found->get<std::uint64_t>();
			}
			const auto now = std::chrono::steady_clock::now();
			{
				std::scoped_lock lock(g_mutex);
				if (now < g_nextRaidHitRefusedNotice) {
					return;
				}
				g_nextRaidHitRefusedNotice = now + kRaidHitRefusedNoticeInterval;
			}
			const auto text = RefusalText(reason->get<std::string>(), secondsLeft);
			QueueGameTask([text]() { RE::SendHUDMessage::ShowHUDMessage(text.c_str(), nullptr, true, true); });
		}
	}

	bool TryHandlePacket(std::string_view a_type, const Json& a_packet)
	{
		if (a_type == "settlementWindowState") {
			HandleWindowPacket(a_packet);
		} else if (a_type == "raidDeclareState") {
			HandleRaidDeclarePacket(a_packet);
		} else if (a_type == "settlerState") {
			HandleSettlerStatePacket(a_packet);
		} else if (a_type == "defenderState") {
			HandleDefenderStatePacket(a_packet);
		} else if (a_type == "raidHitRefused") {
			HandleRaidHitRefusedPacket(a_packet);
		} else if (a_type == "settlementRefused") {
			HandleSettlementRefusedPacket(a_packet);
		} else if (a_type == "zoneAttackState") {
			HandleZoneAttackPacket(a_packet);
		} else if (a_type == "defenderEvent") {
			HandleDefenderEventPacket(a_packet);
		} else {
			return false;
		}
		return true;
	}

	void NoteCapabilities(const Json& a_packet)
	{
		bool windowed = false;
		if (const auto capabilities = a_packet.find("capabilities"); capabilities != a_packet.end() && capabilities->is_array()) {
			windowed = std::any_of(capabilities->begin(), capabilities->end(), [](const Json& a_capability) {
				return a_capability.is_string() && a_capability.get<std::string>() == kWindowedCapability;
			});
		}
		{
			std::scoped_lock lock(g_mutex);
			g_windowedRaids = windowed;
		}
		QueueStateChanged();
	}

	void NoteServerTime(const Json& a_welcome)
	{
		const auto serverTime = ReadDouble(a_welcome, "serverTime");
		const auto localNow = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
		std::scoped_lock lock(g_mutex);
		g_serverClockOffsetSeconds = serverTime ? *serverTime - localNow : 0.0;
	}

	std::uint64_t ServerUnixNow()
	{
		const auto localNow = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
		std::scoped_lock lock(g_mutex);
		return static_cast<std::uint64_t>(std::max(0.0, localNow + g_serverClockOffsetSeconds));
	}

	void Reset()
	{
		{
			std::scoped_lock lock(g_mutex);
			g_zones.clear();
			g_nextAttackNotice.clear();
			g_windowedRaids = false;
			g_refusal.reset();
		}
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
		if (op != "raidDeclare" && op != "settlementWindowSet" && op != "settlerRecruit" && op != "settlerAssign" && op != "settlerRecover") {
			return false;
		}
		const auto zoneId = ReadZoneId(action);
		if (!zoneId) {
			LogThrottledWarning("raid_action_zone", "Ignoring a settlement raid action with an invalid buildZoneId.");
			return true;
		}
		if (op == "raidDeclare") {
			QueuePacket(F4T::WorkshopProtocol::MakeRaidDeclare(*zoneId));
			return true;
		}
		if (op == "settlementWindowSet") {
			const auto startHour = ReadFloat(action, "windowStartHour");
			const auto durationHours = ReadFloat(action, "windowDurationHours");
			const auto offsetHours = LocalOffsetHours();
			if (!startHour || !durationHours || !F4T::WorkshopRaids::ValidWindowSet(*startHour, *durationHours, offsetHours)) {
				LogThrottledWarning("raid_window_action", "Ignoring a settlementWindowSet action with an invalid window.");
				return true;
			}
			QueuePacket(F4T::WorkshopProtocol::MakeSettlementWindowSet(*zoneId, *startHour, *durationHours, offsetHours));
			return true;
		}
		if (op == "settlerRecruit") {
			QueuePacket(F4T::WorkshopProtocol::MakeSettlerRecruit(*zoneId));
			return true;
		}
		const auto settlerId = ReadU64Field(action, "settlerId");
		if (!settlerId) {
			LogThrottledWarning("raid_action_settler", "Ignoring a settler action with an invalid settlerId.");
			return true;
		}
		if (op == "settlerRecover") {
			QueuePacket(F4T::WorkshopProtocol::MakeSettlerRecover(*zoneId, *settlerId));
			return true;
		}
		const auto assignment = ReadString(action, "assignment");
		if (!assignment || !IsAssignment(*assignment)) {
			LogThrottledWarning("raid_action_assignment", "Ignoring a settlerAssign action with an invalid assignment.");
			return true;
		}
		QueuePacket(F4T::WorkshopProtocol::MakeSettlerAssign(*zoneId, *settlerId, *assignment));
		return true;
	}

	std::string WithSettlementRaid(std::string a_stateJson, std::size_t a_jsonOutSize)
	{
		try {
			auto state = Json::parse(a_stateJson);
			if (!state.is_object()) {
				return a_stateJson;
			}
			const auto now = std::chrono::steady_clock::now();
			const auto unixNow = static_cast<double>(ServerUnixNow());
			const auto budget = a_jsonOutSize > a_stateJson.size() + kStateReserveBytes ? a_jsonOutSize - a_stateJson.size() - kStateReserveBytes : std::size_t{ 0 };
			Json zones = Json::array();
			std::size_t used = 0;
			bool truncated = false;
			bool windowed = false;
			Json refusal = nullptr;
			{
				std::scoped_lock lock(g_mutex);
				windowed = g_windowedRaids;
				if (g_refusal && now < g_refusal->until) {
					refusal = Json{ { "text", g_refusal->text } };
				}
				for (const auto& [zoneId, zone] : g_zones) {
					const bool raided = zone.raid && now < zone.raid->liveUntil;
					if (!zone.window && !raided && zone.settlers.empty() && !zone.defense) {
						continue;
					}
					Json entry{ { "buildZoneId", zoneId }, { "window", nullptr }, { "raid", nullptr }, { "settlers", Json::array() }, { "defense", nullptr } };
					if (zone.defense) {
						entry["defense"] = Json{ { "defenseRating", zone.defense->rating }, { "underAttack", zone.defense->underAttack }, { "attackerCount", zone.defense->attackers }, { "detail", zone.defense->detail } };
					}
					if (zone.window) {
						entry["window"] = Json{ { "windowStartHour", zone.window->startHour }, { "windowDurationHours", zone.window->durationHours }, { "timezoneOffsetHours", zone.window->offsetHours } };
					}
					if (raided) {
						const bool pending = unixNow < zone.raid->readyUnix;
						const auto secondsLeft = std::max(0.0, (pending ? zone.raid->readyUnix : zone.raid->expiresUnix) - unixNow);
						entry["raid"] = Json{ { "declarerProfileId", zone.raid->declarerProfileId }, { "pending", pending }, { "secondsLeft", static_cast<std::uint64_t>(secondsLeft) } };
					}
					for (const auto& [settlerId, settler] : zone.settlers) {
						entry["settlers"].push_back(Json{ { "settlerId", settler.settlerId }, { "assignment", settler.assignment }, { "health", settler.health }, { "dead", settler.dead } });
					}
					const auto cost = entry.dump().size() + 1;
					if (used + cost > budget) {
						truncated = true;
						break;
					}
					used += cost;
					zones.push_back(std::move(entry));
				}
			}
			state["settlementRaid"] = Json{ { "zones", std::move(zones) }, { "truncated", truncated }, { "windowedRaids", windowed }, { "refusal", std::move(refusal) } };
			auto merged = state.dump();
			return merged.size() + 1 > a_jsonOutSize ? a_stateJson : merged;
		} catch (...) {
			LogThrottledWarning("raid_state_dump", "Could not merge settlement raid state into the faction state JSON.");
			return a_stateJson;
		}
	}
}

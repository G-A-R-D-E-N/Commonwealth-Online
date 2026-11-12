#include "pch.h"

#include "F4TAdminClient.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TDebugLogging.h"
#include "F4TFactionPayload.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TRemotePlayerCapacity.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace F4T::Admin
{
	using namespace F4T::Networking::Detail;

	namespace
	{
		constexpr std::size_t kMaximumActionJsonBytes = 1024;
		constexpr std::size_t kMaximumNameBytes = 64;
		constexpr std::size_t kMaximumRequestIdBytes = 64;
		constexpr std::size_t kMaximumMessageBytes = 256;
		constexpr std::size_t kMaximumListEntries = 64;
		constexpr std::size_t kMaximumParams = 16;
		constexpr std::size_t kMaximumResults = 20;
		constexpr std::size_t kMaximumPlayers = F4T::RemotePlayerCapacity::MaximumSessionPlayers;
		constexpr std::size_t kMaximumPlayerNameBytes = 128;
		constexpr std::size_t kMaximumProfileIdBytes = 128;
		constexpr std::size_t kMaximumStoredDataBytes = 16384;
		constexpr std::size_t kMaximumStateBytes = 60000;
		constexpr std::array<std::string_view, 8> kQueryActions{ "status", "clients", "bans", "admin_list", "factions", "resources", "map_public", "reports" };
		constexpr std::string_view kPlayersAction = "players";
		namespace Payload = F4T::Factions::Payload;

		std::mutex g_mutex;
		COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI18* g_api = nullptr;
		bool g_isAdmin = false;
		double g_serverTime = 0.0;
		std::vector<std::string> g_actions;
		std::vector<std::string> g_menus;
		Json g_players = Json::array();
		Json g_data = Json::object();
		Json g_results = Json::array();
		std::uint64_t g_nextRequestId = 1;

		void WriteError(char* a_errorBuf, std::size_t a_errorBufSize, std::string_view a_error)
		{
			if (!a_errorBuf || a_errorBufSize == 0) {
				return;
			}
			const auto length = (std::min)(a_error.size(), a_errorBufSize - 1);
			std::memcpy(a_errorBuf, a_error.data(), length);
			a_errorBuf[length] = '\0';
		}

		std::optional<std::string> ReadToken(const Json& a_json, const char* a_field, std::size_t a_maximumBytes)
		{
			auto text = ReadString(a_json, a_field);
			if (!text || !Payload::IsSafeToken(*text, a_maximumBytes)) {
				return std::nullopt;
			}
			return text;
		}

		std::optional<std::vector<std::string>> ReadTokenList(const Json& a_packet, const char* a_field)
		{
			const auto found = a_packet.find(a_field);
			if (found == a_packet.end() || !found->is_array() || found->size() > kMaximumListEntries) {
				return std::nullopt;
			}
			std::vector<std::string> names;
			for (const auto& entry : *found) {
				if (!entry.is_string()) {
					return std::nullopt;
				}
				const auto& name = entry.get_ref<const std::string&>();
				if (!Payload::IsSafeToken(name, kMaximumNameBytes)) {
					return std::nullopt;
				}
				names.push_back(name);
			}
			return names;
		}

		std::optional<Json> ParsePlayers(const Json& a_data)
		{
			const auto found = a_data.find("players");
			if (found == a_data.end() || !found->is_array() || found->size() > kMaximumPlayers) {
				return std::nullopt;
			}
			auto players = Json::array();
			for (const auto& player : *found) {
				const auto playerId = ReadPlayerId(player, "playerId");
				if (!playerId || *playerId == 0) {
					return std::nullopt;
				}
				players.push_back(Json{
					{ "playerId", *playerId },
					{ "profileId", Payload::SanitizeText(ReadString(player, "profileId").value_or(""), kMaximumProfileIdBytes) },
					{ "name", Payload::SanitizeText(ReadString(player, "name").value_or(""), kMaximumPlayerNameBytes) }
				});
			}
			return players;
		}

		bool IsQueryAction(std::string_view a_action)
		{
			return std::ranges::contains(kQueryActions, a_action);
		}

		Json TruncatedMarker(std::size_t a_bytes)
		{
			return Json{ { "truncated", true }, { "bytes", a_bytes } };
		}

		Json BoundedPayload(const Json& a_payload)
		{
			const auto bytes = a_payload.dump().size();
			return bytes > kMaximumStoredDataBytes ? TruncatedMarker(bytes) : a_payload;
		}

		bool IsTruncated(const Json& a_value)
		{
			return a_value.is_object() && a_value.contains("truncated");
		}

		Json* LargestPayload(Json& a_state)
		{
			Json* largest = nullptr;
			std::size_t largestBytes = 0;
			const auto consider = [&](Json& a_value) {
				if (a_value.is_null() || IsTruncated(a_value)) {
					return;
				}
				const auto bytes = a_value.dump().size();
				if (!largest || bytes > largestBytes) {
					largest = &a_value;
					largestBytes = bytes;
				}
			};
			for (auto& value : a_state["data"]) {
				consider(value);
			}
			for (auto& result : a_state["results"]) {
				consider(result["data"]);
			}
			return largest;
		}

		std::string FitStateJson(Json a_state)
		{
			auto text = a_state.dump();
			while (text.size() > kMaximumStateBytes) {
				if (auto* largest = LargestPayload(a_state)) {
					*largest = TruncatedMarker(largest->dump().size());
				} else if (!a_state["results"].empty()) {
					a_state["results"].erase(a_state["results"].begin());
				} else {
					break;
				}
				text = a_state.dump();
			}
			return text;
		}

		bool IsValidParams(const Json& a_params)
		{
			if (!a_params.is_object() || a_params.size() > kMaximumParams) {
				return false;
			}
			for (const auto& [key, value] : a_params.items()) {
				if (key == "type" || key == "action" || key == "requestId" || !Payload::IsSafeToken(key, kMaximumNameBytes)) {
					return false;
				}
				if (!value.is_string() && !value.is_number() && !value.is_boolean()) {
					return false;
				}
			}
			return true;
		}

		void QueueGameTask(std::function<void()> a_task)
		{
			const auto* taskInterface = F4SE::GetTaskInterface();
			if (!taskInterface) {
				LogWarningWithLocalPlayerPrefix("Cannot queue admin work: task interface unavailable.");
				return;
			}
			taskInterface->AddTask(std::move(a_task));
		}

		void NotifyWindow()
		{
			if (!g_api) {
				return;
			}
			bool available = false;
			{
				std::scoped_lock lock(g_mutex);
				available = g_isAdmin;
			}
			if (g_api->SetAdminAvailable) {
				g_api->SetAdminAvailable(available);
			}
			if (g_api->NotifyAdminStateChanged) {
				g_api->NotifyAdminStateChanged();
			}
		}

		void QueueStateChanged()
		{
			QueueGameTask([]() { NotifyWindow(); });
		}

		void QueuePacket(Json a_packet)
		{
			QueueGameTask([packet = std::move(a_packet)]() {
				if (!F4T::Networking::IsConnectedToServer()) {
					return;
				}
				if (!SendPacketBytes(packet.dump())) {
					LogThrottledWarning("admin_send_failed", "Could not send Commonwealth Online admin packet.");
				}
			});
		}

		void ClearLocked()
		{
			g_isAdmin = false;
			g_serverTime = 0.0;
			g_actions.clear();
			g_menus.clear();
			g_players = Json::array();
			g_data = Json::object();
			g_results = Json::array();
		}

		void HandleStatePacket(const Json& a_packet)
		{
			const auto isAdmin = ReadBool(a_packet, "isAdmin");
			const auto actions = ReadTokenList(a_packet, "actions");
			const auto menus = ReadTokenList(a_packet, "menus");
			if (!isAdmin || !actions || !menus) {
				LogThrottledWarning("admin_state_malformed", "Ignoring malformed adminState packet.");
				return;
			}
			const auto serverTime = ReadDouble(a_packet, "serverTime").value_or(0.0);
			{
				std::scoped_lock lock(g_mutex);
				if (*isAdmin) {
					g_isAdmin = true;
					g_serverTime = serverTime;
					g_actions = *actions;
					g_menus = *menus;
				} else {
					ClearLocked();
				}
			}
			QueueStateChanged();
		}

		void HandleResultPacket(const Json& a_packet)
		{
			const auto action = ReadToken(a_packet, "action", kMaximumNameBytes);
			const auto ok = ReadBool(a_packet, "ok");
			if (!action || !ok) {
				LogThrottledWarning("admin_result_malformed", "Ignoring malformed adminResult packet.");
				return;
			}
			const auto requestId = ReadToken(a_packet, "requestId", kMaximumRequestIdBytes);
			auto message = Payload::SanitizeText(ReadString(a_packet, "message").value_or(""), kMaximumMessageBytes);
			const auto dataFound = a_packet.find("data");
			const Json payload = dataFound == a_packet.end() ? Json(nullptr) : *dataFound;
			const bool playersAction = *action == kPlayersAction;
			const bool queryAction = IsQueryAction(*action);
			bool resultOk = *ok;
			std::optional<Json> players;
			if (resultOk && playersAction) {
				players = ParsePlayers(payload);
				if (!players) {
					resultOk = false;
					message = "Player list was invalid or too large.";
				}
			}
			const Json bounded = BoundedPayload(payload);
			{
				std::scoped_lock lock(g_mutex);
				if (!g_isAdmin) {
					return;
				}
				if (*ok && playersAction) {
					g_players = players ? std::move(*players) : Json::array();
				} else if (*ok && queryAction && !bounded.is_null()) {
					g_data[*action] = bounded;
				}
				if (!resultOk || (!playersAction && !queryAction)) {
					if (g_results.size() >= kMaximumResults) {
						g_results.erase(g_results.begin());
					}
					g_results.push_back(Json{
						{ "requestId", requestId ? Json(*requestId) : Json(nullptr) },
						{ "action", *action },
						{ "ok", resultOk },
						{ "message", message },
						{ "data", playersAction || queryAction ? Json(nullptr) : bounded }
					});
				}
			}
			QueueStateChanged();
		}

		void SendAdminAction(const std::string& a_action, const Json& a_params)
		{
			std::string requestId;
			{
				std::scoped_lock lock(g_mutex);
				if (!std::ranges::contains(g_actions, a_action)) {
					LogThrottledWarning("admin_action_unlisted", "Dropped an admin action that the server did not list.");
					return;
				}
				requestId = std::format("adm-{}", g_nextRequestId++);
			}
			Json packet{ { "type", "adminAction" }, { "action", a_action }, { "requestId", requestId } };
			for (const auto& [key, value] : a_params.items()) {
				packet[key] = value;
			}
			QueuePacket(std::move(packet));
		}

		bool Callback_GetStateJson(
			char*       a_jsonOut,
			std::size_t a_jsonOutSize,
			char*       a_errorBuf,
			std::size_t a_errorBufSize)
		{
			if (!a_jsonOut || a_jsonOutSize == 0) {
				WriteError(a_errorBuf, a_errorBufSize, "JSON output buffer is missing.");
				return false;
			}

			try {
				const auto state = BuildStateJson();
				if (state.size() + 1 > a_jsonOutSize) {
					WriteError(a_errorBuf, a_errorBufSize, "Admin state JSON exceeds output buffer.");
					return false;
				}

				std::memcpy(a_jsonOut, state.c_str(), state.size() + 1);
				return true;
			} catch (...) {
				WriteError(a_errorBuf, a_errorBufSize, "Admin state JSON could not be built.");
				return false;
			}
		}

		void Callback_SendAction(const char* a_actionJson)
		{
			if (!a_actionJson) {
				return;
			}
			try {
				HandleAction(a_actionJson);
			} catch (...) {
				try {
					F4T::DebugLogging::Log("Admin", "dropped an action after an internal error");
				} catch (...) {
				}
			}
		}

		const COMMONWEALTH_ONLINE_UI_API::AdminCallbacks kAdminCallbacks{
			Callback_GetStateJson,
			Callback_SendAction
		};
	}

	void Attach()
	{
		g_api = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
			COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI18>();
		if (!g_api) {
			F4T::DebugLogging::Log("Admin", "the V18 admin window API is unavailable");
			return;
		}
		if (g_api->SetAdminCallbacks) {
			g_api->SetAdminCallbacks(&kAdminCallbacks);
		}
		QueueStateChanged();
	}

	bool TryHandlePacket(std::string_view a_type, const Json& a_packet)
	{
		if (a_type == "adminState") {
			HandleStatePacket(a_packet);
		} else if (a_type == "adminResult") {
			HandleResultPacket(a_packet);
		} else {
			return false;
		}
		return true;
	}

	void Reset()
	{
		{
			std::scoped_lock lock(g_mutex);
			ClearLocked();
		}
		QueueStateChanged();
	}

	std::string BuildStateJson()
	{
		const bool connected = F4T::Networking::IsConnectedToServer();
		Json state;
		{
			std::scoped_lock lock(g_mutex);
			state = Json{
				{ "connected", connected },
				{ "isAdmin", g_isAdmin },
				{ "serverTime", g_serverTime },
				{ "actions", g_actions },
				{ "menus", g_menus },
				{ "players", g_players },
				{ "data", g_data },
				{ "results", g_results }
			};
		}
		return FitStateJson(std::move(state));
	}

	void HandleAction(std::string_view a_actionJson)
	{
		if (a_actionJson.empty() || a_actionJson.size() > kMaximumActionJsonBytes) {
			LogThrottledWarning("admin_action_size", "Ignoring an admin action outside the size limit.");
			return;
		}

		const auto action = Json::parse(std::string{ a_actionJson }, nullptr, false);
		if (!action.is_object()) {
			LogThrottledWarning("admin_action_shape", "Ignoring an admin action that is not a JSON object.");
			return;
		}

		const auto op = ReadString(action, "op").value_or("");
		if (op == "refresh") {
			SendAdminAction(std::string{ kPlayersAction }, Json::object());
			return;
		}
		if (op == "action") {
			const auto name = ReadToken(action, "action", kMaximumNameBytes);
			const auto params = action.find("params");
			const Json paramsJson = params == action.end() ? Json::object() : *params;
			if (!name || !IsValidParams(paramsJson)) {
				LogThrottledWarning("admin_action_malformed", "Ignoring a malformed admin action.");
				return;
			}
			SendAdminAction(*name, paramsJson);
			return;
		}
		LogThrottledWarning("admin_action_op", "Ignoring an admin action with an unknown op.");
	}
}

#include "pch.h"

#include "F4TReport.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TFactions.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TRemotePlayerState.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::Report
{
	using namespace F4T::Networking::Detail;

	namespace
	{
		using Json = nlohmann::json;

		constexpr std::uint32_t kAdminNoticeMs = 15000U;

		std::atomic<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI21*> g_ui{ nullptr };

		void QueuePacket(Json a_packet)
		{
			F4T::Factions::QueueGameTask([packet = std::move(a_packet)]() {
				if (!F4T::Networking::IsConnectedToServer()) {
					return;
				}
				if (!SendPacketBytes(packet.dump())) {
					LogThrottledWarning("report_send_failed", "Could not send a Commonwealth Online report packet.");
				}
			});
		}

		void PushEvent(const std::string& a_json)
		{
			const auto* ui = g_ui.load();
			if (ui && ui->PushReportEvent) {
				ui->PushReportEvent(a_json.c_str());
			}
		}

		COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1* AlertUi()
		{
			auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
			return ui && ui->ShowAlert ? ui : nullptr;
		}

		void ShowAdminNotice(const Json& a_packet)
		{
			auto* ui = AlertUi();
			if (!ui) {
				return;
			}
			const auto kind = ReadString(a_packet, "kind").value_or("");
			const auto reporter = ReadString(a_packet, "reporterName").value_or("A player");
			const auto note = ReadString(a_packet, "note").value_or("");
			const bool isPlayer = kind == "player";
			auto message = isPlayer
				? std::format("{} reported {} for {}.",
					reporter,
					ReadString(a_packet, "targetName").value_or("a player"),
					ReadString(a_packet, "reason").value_or("an unknown reason"))
				: std::format("{} asked admins for help.", reporter);
			if (!note.empty()) {
				message += " " + note;
			}
			const auto alertId = "report-received-" + ReadString(a_packet, "reportId").value_or("");
			COMMONWEALTH_ONLINE_UI_API::AlertRequest request{};
			request.id = alertId.c_str();
			request.category = "Admin";
			request.title = isPlayer ? "Player report" : "Admin help";
			request.message = message.c_str();
			request.tone = "info";
			request.durationMs = kAdminNoticeMs;
			(void)ui->ShowAlert(&request, nullptr, nullptr, nullptr, 0);
		}

		void HandleReceivedPacket(const Json& a_packet)
		{
			F4T::Factions::QueueGameTask([packet = a_packet]() { ShowAdminNotice(packet); });
		}

		void HandleResultPacket(const Json& a_packet)
		{
			const auto json = a_packet.dump();
			F4T::Factions::QueueGameTask([json]() { PushEvent(json); });
		}

		void HandleSubmit(std::string_view a_actionJson)
		{
			const auto parsed = Json::parse(a_actionJson, nullptr, false);
			if (!parsed.is_object() || ReadString(parsed, "action").value_or("") != "submit") {
				return;
			}
			const auto kind = ReadString(parsed, "kind").value_or("");
			if (kind != "player" && kind != "support") {
				return;
			}
			Json packet{
				{ "type", "reportSubmit" },
				{ "kind", kind },
				{ "note", ReadString(parsed, "note").value_or("") }
			};
			if (kind == "player") {
				packet["targetPlayerId"] = ReadUint32(parsed, "targetPlayerId").value_or(0);
				packet["reason"] = ReadString(parsed, "reason").value_or("");
			}
			QueuePacket(std::move(packet));
		}

		Json BuildRoster()
		{
			const auto self = F4T::RemotePlayerState::GetAssignedPlayerId().value_or(0);
			Json players = Json::array();
			for (const auto& remote : F4T::RemotePlayerState::GetRemotePlayerSnapshot()) {
				if (remote.playerId == 0 || remote.playerId == self) {
					continue;
				}
				const auto& name = remote.characterName.empty() ? remote.displayName : remote.characterName;
				players.push_back(Json{
					{ "playerId", remote.playerId },
					{ "name", name.empty() ? std::format("Player {}", remote.playerId) : name }
				});
			}
			return Json{ { "players", players } };
		}

		bool Fail(char* a_errorBuf, std::size_t a_errorBufSize, std::string_view a_message)
		{
			if (a_errorBuf && a_errorBufSize > 0) {
				const auto length = std::min(a_message.size(), a_errorBufSize - 1);
				std::memcpy(a_errorBuf, a_message.data(), length);
				a_errorBuf[length] = '\0';
			}
			return false;
		}

		bool Callback_GetReportStateJson(char* a_jsonOut, std::size_t a_jsonOutSize, char* a_errorBuf, std::size_t a_errorBufSize)
		{
			try {
				const auto dumped = BuildRoster().dump();
				if (dumped.size() + 1 > a_jsonOutSize) {
					return Fail(a_errorBuf, a_errorBufSize, "Player list is too large for the report window.");
				}
				std::memcpy(a_jsonOut, dumped.c_str(), dumped.size() + 1);
				return true;
			} catch (...) {
				return Fail(a_errorBuf, a_errorBufSize, "Player list could not be read.");
			}
		}

		void Callback_SendReportAction(const char* a_actionJson)
		{
			if (!a_actionJson) {
				return;
			}
			try {
				HandleSubmit(a_actionJson);
			} catch (...) {
				REX::WARN(
					"{} [Report] Dropped a report action after an internal error.",
					F4T::Networking::GetLocalPlayerLogPrefix());
			}
		}

		const COMMONWEALTH_ONLINE_UI_API::ReportCallbacks kReportCallbacks{
			Callback_GetReportStateJson,
			Callback_SendReportAction
		};
	}

	void Register()
	{
		auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI21>();
		g_ui.store(ui);
		if (!ui) {
			REX::WARN(
				"{} [Report] CommonwealthOnline.dll lacks report interface V21 (IVCommonwealthOnlineUI21).",
				F4T::Networking::GetLocalPlayerLogPrefix());
			return;
		}
		if (ui->SetReportCallbacks) {
			ui->SetReportCallbacks(&kReportCallbacks);
		}
	}

	bool TryHandlePacket(std::string_view a_type, const Json& a_packet)
	{
		if (a_type == "reportResult") {
			HandleResultPacket(a_packet);
		} else if (a_type == "reportReceived") {
			HandleReceivedPacket(a_packet);
		} else {
			return false;
		}
		return true;
	}
}

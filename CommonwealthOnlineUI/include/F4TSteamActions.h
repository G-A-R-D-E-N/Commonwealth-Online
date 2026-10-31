#pragma once

#include "CommonwealthOnlineUI_API.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>

namespace F4T::Steam
{
	constexpr std::size_t      kMaxNameLength = 64;
	constexpr std::size_t      kMaxPasswordLength = 128;
	constexpr std::size_t      kMaxFriendCodeLength = 64;
	constexpr std::int64_t     kMinPlayers = 2;
	constexpr std::int64_t     kMaxPlayers = 16;
	constexpr const char*      kUnavailableReason = "Steam hosting is not available";
	constexpr std::string_view kSteamAddressPrefix = "steam:";
	constexpr const char*      kListHostsJoinToken = "\x01steamListHosts";

	struct HostStartRequest
	{
		std::string   name;
		std::string   password;
		std::uint32_t maxPlayers{ 0 };
	};

	struct JoinRequest
	{
		std::string friendCode;
		std::string password;
	};

	using SteamEventSink = void (*)(void* a_userData, const nlohmann::json& a_event);

	inline bool IsSteamAction(std::string_view a_action)
	{
		return a_action == "steamHostStart" || a_action == "steamHostStop" || a_action == "steamHostEnter" || a_action == "steamJoin" || a_action == "steamInvite" || a_action == "steamListHosts";
	}

	inline void EmitSteamError(SteamEventSink a_sink, void* a_userData, const char* a_type, const std::string& a_reason)
	{
		if (a_sink) {
			a_sink(a_userData, nlohmann::json{ { "type", a_type }, { "reason", a_reason } });
		}
	}

	inline bool ParseHostStart(const nlohmann::json& a_action, HostStartRequest& a_out, std::string& a_error)
	{
		a_out.name = a_action.value("name", "");
		if (a_out.name.empty()) {
			a_error = "Server name is required.";
			return false;
		}
		if (a_out.name.size() > kMaxNameLength) {
			a_error = "Server name must be 32 characters or fewer.";
			return false;
		}

		a_out.password = a_action.value("password", "");
		if (a_out.password.size() > kMaxPasswordLength) {
			a_error = "Password must be 128 characters or fewer.";
			return false;
		}

		if (!a_action.contains("maxPlayers") || !a_action.at("maxPlayers").is_number_integer()) {
			a_error = "Max players must be a whole number.";
			return false;
		}
		std::int64_t maxPlayers = 0;
		try {
			maxPlayers = a_action.at("maxPlayers").get<std::int64_t>();
		} catch (const std::exception&) {
			maxPlayers = kMaxPlayers;
		}
		if (maxPlayers < kMinPlayers) {
			maxPlayers = kMinPlayers;
		}
		if (maxPlayers > kMaxPlayers) {
			maxPlayers = kMaxPlayers;
		}
		a_out.maxPlayers = static_cast<std::uint32_t>(maxPlayers);
		return true;
	}

	inline bool ParseJoin(const nlohmann::json& a_action, JoinRequest& a_out, std::string& a_error)
	{
		std::string address = a_action.value("address", "");
		if (address.starts_with(kSteamAddressPrefix)) {
			address.erase(0, kSteamAddressPrefix.size());
		}
		if (address.empty()) {
			a_error = "Friend Code is required.";
			return false;
		}
		if (address.size() > kMaxFriendCodeLength) {
			a_error = "Friend Code must be 64 characters or fewer.";
			return false;
		}
		a_out.friendCode = address;

		a_out.password = a_action.value("password", "");
		if (a_out.password.size() > kMaxPasswordLength) {
			a_error = "Password must be 128 characters or fewer.";
			return false;
		}
		return true;
	}

	inline bool RouteSteamAction(
		const nlohmann::json&                             a_action,
		const COMMONWEALTH_ONLINE_UI_API::SteamCallbacks& a_callbacks,
		SteamEventSink                                    a_sink,
		void*                                             a_userData)
	{
		if (!a_action.is_object()) {
			return false;
		}
		const auto action = a_action.value("action", "");
		if (!IsSteamAction(action)) {
			return false;
		}

		if (action == "steamHostStart") {
			if (!a_callbacks.HostStart) {
				EmitSteamError(a_sink, a_userData, "steamHostFailed", kUnavailableReason);
				return true;
			}
			HostStartRequest request;
			std::string error;
			if (!ParseHostStart(a_action, request, error)) {
				EmitSteamError(a_sink, a_userData, "steamHostFailed", error);
				return true;
			}
			a_callbacks.HostStart(request.name.c_str(), request.password.c_str(), request.maxPlayers);
			return true;
		}

		if (action == "steamHostStop") {
			if (!a_callbacks.HostStop) {
				EmitSteamError(a_sink, a_userData, "steamHostFailed", kUnavailableReason);
				return true;
			}
			a_callbacks.HostStop();
			return true;
		}

		if (action == "steamHostEnter") {
			if (!a_callbacks.HostEnter) {
				EmitSteamError(a_sink, a_userData, "steamHostFailed", kUnavailableReason);
				return true;
			}
			a_callbacks.HostEnter();
			return true;
		}

		if (action == "steamInvite") {
			if (!a_callbacks.Invite) {
				EmitSteamError(a_sink, a_userData, "steamInviteFailed", kUnavailableReason);
				return true;
			}
			a_callbacks.Invite();
			return true;
		}

		if (action == "steamListHosts") {
			if (!a_callbacks.Join) {
				if (a_sink) {
					a_sink(a_userData, nlohmann::json{ { "type", "steamFriendHosts" }, { "hosts", nlohmann::json::array() } });
				}
				return true;
			}
			a_callbacks.Join(kListHostsJoinToken, "");
			return true;
		}

		if (!a_callbacks.Join) {
			EmitSteamError(a_sink, a_userData, "steamJoinFailed", kUnavailableReason);
			return true;
		}
		JoinRequest request;
		std::string error;
		if (!ParseJoin(a_action, request, error)) {
			EmitSteamError(a_sink, a_userData, "steamJoinFailed", error);
			return true;
		}
		a_callbacks.Join(request.friendCode.c_str(), request.password.c_str());
		return true;
	}

	void SetCallbacks(const COMMONWEALTH_ONLINE_UI_API::SteamCallbacks* a_callbacks);
	void PushCapabilities();
	void PushEvent(const char* a_json);
	bool HandleAction(const nlohmann::json& a_action);
}

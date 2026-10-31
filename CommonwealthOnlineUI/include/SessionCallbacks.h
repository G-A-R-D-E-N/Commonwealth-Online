#pragma once

#include "CommonwealthOnlineUI_API.h"

#include <optional>
#include <string>

namespace COUI::Session
{
	struct LastJoinSnapshot
	{
		std::string id;
		std::string name;
		std::string host;
		std::uint16_t port{ 7777 };
		bool passworded{ false };
	};

	void SetCallbacks(const COMMONWEALTH_ONLINE_UI_API::SessionCallbacks* a_callbacks);
	const COMMONWEALTH_ONLINE_UI_API::SessionCallbacks& GetCallbacks();

	bool HasJoinSession();
	bool HasDisconnect();
	bool HasConnectLocal();
	bool HasIsConnected();
	bool HasScanLocalServers();
	bool HasLastJoin();
	bool HasGetRemotePlayersMapJson();
	std::optional<LastJoinSnapshot> GetLastJoin();

	void SetRemotePlayersMapJsonCallback(COMMONWEALTH_ONLINE_UI_API::RemotePlayersMapJsonFn a_callback);
	void SetPasswordJoinCallback(COMMONWEALTH_ONLINE_UI_API::PasswordJoinFn a_callback);

	bool JoinSession(
		const COMMONWEALTH_ONLINE_UI_API::JoinRequest& a_request,
		COMMONWEALTH_ONLINE_UI_API::JoinCompleteFn     a_onComplete,
		void*                                          a_userData,
		std::string&                                   a_error);

	bool JoinSessionWithPassword(
		const COMMONWEALTH_ONLINE_UI_API::JoinRequest& a_request,
		const std::string&                             a_password,
		COMMONWEALTH_ONLINE_UI_API::JoinCompleteFn     a_onComplete,
		void*                                          a_userData,
		std::string&                                   a_error);

	void Disconnect();
	bool ConnectLocal(std::string& a_error);
	bool IsConnected();
	bool ScanLocalServers(std::string& a_jsonOut, std::string& a_error);
	bool RetryLastJoin(
		COMMONWEALTH_ONLINE_UI_API::JoinCompleteFn a_onComplete,
		void*                                     a_userData,
		std::string&                              a_error);
	bool GetRemotePlayersMapJson(std::string& a_jsonOut, std::string& a_error);

	const char* GetLogPrefix();
}

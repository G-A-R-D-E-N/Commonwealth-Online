#include "pch.h"

#include "SessionCallbacks.h"

#include <cstring>
#include <mutex>
#include <optional>

namespace COUI::Session
{
	namespace
	{
		COMMONWEALTH_ONLINE_UI_API::SessionCallbacks g_callbacks{};
		COMMONWEALTH_ONLINE_UI_API::RemotePlayersMapJsonFn g_remotePlayersMapJson{ nullptr };
		COMMONWEALTH_ONLINE_UI_API::PasswordJoinFn g_passwordJoin{ nullptr };
		std::mutex g_lastJoinMutex;
		std::optional<LastJoinSnapshot> g_lastJoin;

		void RecordLastJoin(const COMMONWEALTH_ONLINE_UI_API::JoinRequest& a_request)
		{
			LastJoinSnapshot last{};
			last.id = a_request.id ? a_request.id : "";
			last.name = a_request.name ? a_request.name : "";
			last.host = a_request.host ? a_request.host : "";
			last.port = a_request.port;
			last.passworded = a_request.passworded;
			const std::scoped_lock lock(g_lastJoinMutex);
			g_lastJoin = last;
		}
	}

	void SetCallbacks(const COMMONWEALTH_ONLINE_UI_API::SessionCallbacks* a_callbacks)
	{
		g_callbacks = {};
		{
			const std::scoped_lock lock(g_lastJoinMutex);
			g_lastJoin.reset();
		}
		if (!a_callbacks) {
			return;
		}

		g_callbacks.JoinSession = a_callbacks->JoinSession;
		g_callbacks.Disconnect = a_callbacks->Disconnect;
		g_callbacks.ConnectLocal = a_callbacks->ConnectLocal;
		g_callbacks.IsConnected = a_callbacks->IsConnected;
		g_callbacks.ScanLocalServers = a_callbacks->ScanLocalServers;
	}

	void SetRemotePlayersMapJsonCallback(COMMONWEALTH_ONLINE_UI_API::RemotePlayersMapJsonFn a_callback)
	{
		g_remotePlayersMapJson = a_callback;
	}

	void SetPasswordJoinCallback(COMMONWEALTH_ONLINE_UI_API::PasswordJoinFn a_callback)
	{
		g_passwordJoin = a_callback;
	}

	const COMMONWEALTH_ONLINE_UI_API::SessionCallbacks& GetCallbacks()
	{
		return g_callbacks;
	}

	bool HasJoinSession()
	{
		return g_callbacks.JoinSession != nullptr;
	}

	bool HasDisconnect()
	{
		return g_callbacks.Disconnect != nullptr;
	}

	bool HasConnectLocal()
	{
		return g_callbacks.ConnectLocal != nullptr;
	}

	bool HasIsConnected()
	{
		return g_callbacks.IsConnected != nullptr;
	}

	bool HasScanLocalServers()
	{
		return g_callbacks.ScanLocalServers != nullptr;
	}

	bool HasLastJoin()
	{
		const std::scoped_lock lock(g_lastJoinMutex);
		return g_lastJoin.has_value();
	}

	bool HasGetRemotePlayersMapJson()
	{
		return g_remotePlayersMapJson != nullptr;
	}

	std::optional<LastJoinSnapshot> GetLastJoin()
	{
		const std::scoped_lock lock(g_lastJoinMutex);
		return g_lastJoin;
	}

	bool JoinSession(
		const COMMONWEALTH_ONLINE_UI_API::JoinRequest& a_request,
		COMMONWEALTH_ONLINE_UI_API::JoinCompleteFn     a_onComplete,
		void*                                          a_userData,
		std::string&                                   a_error)
	{
		if (!g_callbacks.JoinSession) {
			a_error = "Session JoinSession callback is not registered.";
			return false;
		}

		RecordLastJoin(a_request);

		char errorBuf[512]{};
		if (!g_callbacks.JoinSession(&a_request, a_onComplete, a_userData, errorBuf, sizeof(errorBuf))) {
			a_error = errorBuf[0] ? errorBuf : "JoinSession failed.";
			return false;
		}

		return true;
	}

	bool JoinSessionWithPassword(
		const COMMONWEALTH_ONLINE_UI_API::JoinRequest& a_request,
		const std::string&                             a_password,
		COMMONWEALTH_ONLINE_UI_API::JoinCompleteFn     a_onComplete,
		void*                                          a_userData,
		std::string&                                   a_error)
	{
		if (!g_passwordJoin) {
			a_error = "Update Commonwealth Online to join passworded servers.";
			return false;
		}

		RecordLastJoin(a_request);

		char errorBuf[512]{};
		if (!g_passwordJoin(&a_request, a_password.c_str(), a_onComplete, a_userData, errorBuf, sizeof(errorBuf))) {
			a_error = errorBuf[0] ? errorBuf : "JoinSession failed.";
			return false;
		}

		return true;
	}

	void Disconnect()
	{
		if (g_callbacks.Disconnect) {
			g_callbacks.Disconnect();
		}
	}

	bool ConnectLocal(std::string& a_error)
	{
		if (!g_callbacks.ConnectLocal) {
			a_error = "Session ConnectLocal callback is not registered.";
			return false;
		}

		char errorBuf[512]{};
		if (!g_callbacks.ConnectLocal(errorBuf, sizeof(errorBuf))) {
			a_error = errorBuf[0] ? errorBuf : "ConnectLocal failed.";
			return false;
		}

		return true;
	}

	bool IsConnected()
	{
		if (!g_callbacks.IsConnected) {
			return false;
		}
		return g_callbacks.IsConnected();
	}

	bool ScanLocalServers(std::string& a_jsonOut, std::string& a_error)
	{
		if (!g_callbacks.ScanLocalServers) {
			a_error = "Session ScanLocalServers callback is not registered.";
			return false;
		}

		constexpr std::size_t kJsonCapacity = 64 * 1024;
		std::string           jsonBuf(kJsonCapacity, '\0');
		char                  errorBuf[512]{};

		if (!g_callbacks.ScanLocalServers(jsonBuf.data(), jsonBuf.size(), errorBuf, sizeof(errorBuf))) {
			a_error = errorBuf[0] ? errorBuf : "ScanLocalServers failed.";
			return false;
		}

		a_jsonOut.assign(jsonBuf.c_str());
		return true;
	}

	bool RetryLastJoin(
		COMMONWEALTH_ONLINE_UI_API::JoinCompleteFn a_onComplete,
		void*                                     a_userData,
		std::string&                              a_error)
	{
		LastJoinSnapshot last{};
		{
			const std::scoped_lock lock(g_lastJoinMutex);
			if (!g_lastJoin) {
				a_error = "There is no previous join to retry.";
				return false;
			}
			last = *g_lastJoin;
		}

		if (last.passworded) {
			a_error = "Enter the server password to rejoin.";
			return false;
		}

		COMMONWEALTH_ONLINE_UI_API::JoinRequest request{};
		request.id = last.id.c_str();
		request.name = last.name.c_str();
		request.host = last.host.c_str();
		request.port = last.port;
		request.passworded = last.passworded;
		return JoinSession(request, a_onComplete, a_userData, a_error);
	}

	bool GetRemotePlayersMapJson(std::string& a_jsonOut, std::string& a_error)
	{
		if (!g_remotePlayersMapJson) {
			a_jsonOut = "[]";
			a_error.clear();
			return true;
		}

		constexpr std::size_t kJsonCapacity = 64 * 1024;
		std::string           jsonBuf(kJsonCapacity, '\0');
		char                  errorBuf[512]{};

		if (!g_remotePlayersMapJson(jsonBuf.data(), jsonBuf.size(), errorBuf, sizeof(errorBuf))) {
			a_error = errorBuf[0] ? errorBuf : "GetRemotePlayersMapJson failed.";
			a_jsonOut = "[]";
			return false;
		}

		a_jsonOut.assign(jsonBuf.c_str());
		return true;
	}

	const char* GetLogPrefix()
	{
		return "[CommonwealthOnlineUI]";
	}
}

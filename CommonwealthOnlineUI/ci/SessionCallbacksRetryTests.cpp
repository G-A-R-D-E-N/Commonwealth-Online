#include "SessionCallbacks.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>

namespace
{
	bool g_startResult = true;
	int g_calls = 0;
	int g_passwordCalls = 0;
	std::string g_password;
	std::string g_id;
	std::string g_name;
	std::string g_host;
	std::uint16_t g_port = 0;
	bool g_passworded = false;

	bool FakePasswordJoin(
		const COMMONWEALTH_ONLINE_UI_API::JoinRequest* a_request,
		const char* a_password,
		COMMONWEALTH_ONLINE_UI_API::JoinCompleteFn,
		void*,
		char*,
		std::size_t)
	{
		++g_passwordCalls;
		g_password = a_password ? a_password : "";
		g_port = a_request ? a_request->port : 0;
		g_passworded = a_request && a_request->passworded;
		return true;
	}

	bool FakeJoinSession(
		const COMMONWEALTH_ONLINE_UI_API::JoinRequest* a_request,
		COMMONWEALTH_ONLINE_UI_API::JoinCompleteFn,
		void*,
		char* a_errorBuf,
		std::size_t a_errorBufSize)
	{
		++g_calls;
		g_id = a_request && a_request->id ? a_request->id : "";
		g_name = a_request && a_request->name ? a_request->name : "";
		g_host = a_request && a_request->host ? a_request->host : "";
		g_port = a_request ? a_request->port : 0;
		g_passworded = a_request && a_request->passworded;

		if (g_startResult) {
			return true;
		}

		constexpr char message[] = "blocked";
		if (a_errorBuf && a_errorBufSize > 0) {
			const auto copyLen = (std::min)(sizeof(message) - 1, a_errorBufSize - 1);
			std::memcpy(a_errorBuf, message, copyLen);
			a_errorBuf[copyLen] = '\0';
		}
		return false;
	}
}

int main()
{
	if (static_cast<std::uint8_t>(COMMONWEALTH_ONLINE_UI_API::InterfaceVersion::V1) != 1 ||
		static_cast<std::uint8_t>(COMMONWEALTH_ONLINE_UI_API::InterfaceVersion::V2) != 2) {
		return 1;
	}

	COMMONWEALTH_ONLINE_UI_API::SessionCallbacks callbacks{};
	callbacks.JoinSession = FakeJoinSession;
	COUI::Session::SetCallbacks(&callbacks);
	if (COUI::Session::HasLastJoin()) {
		return 2;
	}

	COMMONWEALTH_ONLINE_UI_API::JoinRequest first{};
	first.id = "first-id";
	first.name = "First";
	first.host = "first.example";
	first.port = 7777;
	first.passworded = false;

	std::string error;
	g_startResult = true;
	if (!COUI::Session::JoinSession(first, nullptr, nullptr, error)) {
		return 3;
	}
	if (!COUI::Session::HasLastJoin()) {
		return 4;
	}

	g_startResult = true;
	error.clear();
	if (!COUI::Session::RetryLastJoin(nullptr, nullptr, error)) {
		return 16;
	}
	if (g_calls != 2 || g_id != "first-id" || g_name != "First" || g_host != "first.example") {
		return 17;
	}
	if (g_port != 7777 || g_passworded) {
		return 18;
	}

	COMMONWEALTH_ONLINE_UI_API::JoinRequest second{};
	second.id = "second-id";
	second.name = "Second";
	second.host = "second.example";
	second.port = 8888;
	second.passworded = true;

	g_startResult = false;
	error.clear();
	if (COUI::Session::JoinSession(second, nullptr, nullptr, error)) {
		return 5;
	}
	if (error != "blocked") {
		return 6;
	}

	const auto stored = COUI::Session::GetLastJoin();
	if (!stored || stored->id != "second-id" || stored->name != "Second" || stored->host != "second.example") {
		return 7;
	}
	if (stored->port != 8888 || !stored->passworded) {
		return 8;
	}

	g_startResult = true;
	error.clear();
	if (COUI::Session::RetryLastJoin(nullptr, nullptr, error)) {
		return 9;
	}
	if (g_calls != 3 || error != "Enter the server password to rejoin.") {
		return 10;
	}

	COMMONWEALTH_ONLINE_UI_API::JoinRequest third{};
	third.id = "third-id";
	third.name = "Third";
	third.host = "third.example";
	third.port = 9999;
	third.passworded = true;

	error.clear();
	if (COUI::Session::JoinSessionWithPassword(third, "hunter2-secret", nullptr, nullptr, error)) {
		return 11;
	}
	if (error != "Update Commonwealth Online to join passworded servers." || g_passwordCalls != 0) {
		return 12;
	}
	if (COUI::Session::GetLastJoin()->id != "second-id") {
		return 13;
	}

	COUI::Session::SetPasswordJoinCallback(FakePasswordJoin);
	error.clear();
	if (!COUI::Session::JoinSessionWithPassword(third, "hunter2-secret", nullptr, nullptr, error)) {
		return 14;
	}
	if (g_passwordCalls != 1 || g_password != "hunter2-secret" || g_port != 9999 || !g_passworded) {
		return 15;
	}

	const auto passwordLast = COUI::Session::GetLastJoin();
	assert(passwordLast && passwordLast->id == "third-id" && passwordLast->passworded);
	assert(passwordLast->id.find("hunter2") == std::string::npos);
	assert(passwordLast->name.find("hunter2") == std::string::npos);
	assert(passwordLast->host.find("hunter2") == std::string::npos);

	error.clear();
	const bool passwordRetry = COUI::Session::RetryLastJoin(nullptr, nullptr, error);
	assert(!passwordRetry);
	assert(error == "Enter the server password to rejoin.");
	assert(g_calls == 3);
	assert(g_passwordCalls == 1);

	return 0;
}

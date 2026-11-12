#include "pch.h"

#include "F4THttpGet.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <format>
#include <string_view>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace F4T::Http
{
	namespace
	{
		std::wstring Widen(std::string_view a_text)
		{
			if (a_text.empty()) {
				return {};
			}

			const auto needed = MultiByteToWideChar(
				CP_UTF8,
				0,
				a_text.data(),
				static_cast<int>(a_text.size()),
				nullptr,
				0);
			if (needed <= 0) {
				return {};
			}

			std::wstring wide(static_cast<std::size_t>(needed), L'\0');
			MultiByteToWideChar(
				CP_UTF8,
				0,
				a_text.data(),
				static_cast<int>(a_text.size()),
				wide.data(),
				needed);
			return wide;
		}
	}

	bool Get(const std::string& a_url, std::string& a_outBody, std::string& a_outError, unsigned long a_timeoutMs)
	{
		a_outBody.clear();
		a_outError.clear();

		const auto wideUrl = Widen(a_url);
		if (wideUrl.empty()) {
			a_outError = "Invalid URL.";
			return false;
		}

		URL_COMPONENTS components{};
		components.dwStructSize = sizeof(components);
		components.dwSchemeLength = static_cast<DWORD>(-1);
		components.dwHostNameLength = static_cast<DWORD>(-1);
		components.dwUrlPathLength = static_cast<DWORD>(-1);
		components.dwExtraInfoLength = static_cast<DWORD>(-1);

		std::wstring mutableUrl = wideUrl;
		if (!WinHttpCrackUrl(mutableUrl.data(), static_cast<DWORD>(mutableUrl.size()), 0, &components)) {
			a_outError = std::format("Failed to parse URL (WinHTTP {}).", GetLastError());
			return false;
		}

		const std::wstring host(components.lpszHostName, components.dwHostNameLength);
		std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
		if (components.dwExtraInfoLength > 0 && components.lpszExtraInfo) {
			path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
		}
		if (path.empty()) {
			path = L"/";
		}

		const bool secure = components.nScheme == INTERNET_SCHEME_HTTPS;
		INTERNET_PORT port = components.nPort;
		if (port == 0) {
			port = secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
		}

		const auto session = WinHttpOpen(
			L"CommonwealthOnlineUI/1.0",
			WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
			WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS,
			0);
		if (!session) {
			a_outError = std::format("WinHttpOpen failed ({}).", GetLastError());
			return false;
		}

		WinHttpSetTimeouts(
			session,
			static_cast<int>(a_timeoutMs),
			static_cast<int>(a_timeoutMs),
			static_cast<int>(a_timeoutMs),
			static_cast<int>(a_timeoutMs));

		const auto connection = WinHttpConnect(session, host.c_str(), port, 0);
		if (!connection) {
			a_outError = std::format("WinHttpConnect failed ({}).", GetLastError());
			WinHttpCloseHandle(session);
			return false;
		}

		const DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
		const auto request = WinHttpOpenRequest(
			connection,
			L"GET",
			path.c_str(),
			nullptr,
			WINHTTP_NO_REFERER,
			WINHTTP_DEFAULT_ACCEPT_TYPES,
			flags);
		if (!request) {
			a_outError = std::format("WinHttpOpenRequest failed ({}).", GetLastError());
			WinHttpCloseHandle(connection);
			WinHttpCloseHandle(session);
			return false;
		}

		const BOOL sent = WinHttpSendRequest(
			request,
			WINHTTP_NO_ADDITIONAL_HEADERS,
			0,
			WINHTTP_NO_REQUEST_DATA,
			0,
			0,
			0);
		if (!sent) {
			a_outError = std::format("WinHttpSendRequest failed ({}).", GetLastError());
			WinHttpCloseHandle(request);
			WinHttpCloseHandle(connection);
			WinHttpCloseHandle(session);
			return false;
		}

		if (!WinHttpReceiveResponse(request, nullptr)) {
			a_outError = std::format("WinHttpReceiveResponse failed ({}).", GetLastError());
			WinHttpCloseHandle(request);
			WinHttpCloseHandle(connection);
			WinHttpCloseHandle(session);
			return false;
		}

		DWORD statusCode = 0;
		DWORD statusSize = sizeof(statusCode);
		if (!WinHttpQueryHeaders(
				request,
				WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				WINHTTP_HEADER_NAME_BY_INDEX,
				&statusCode,
				&statusSize,
				WINHTTP_NO_HEADER_INDEX)) {
			a_outError = std::format("WinHttpQueryHeaders failed ({}).", GetLastError());
			WinHttpCloseHandle(request);
			WinHttpCloseHandle(connection);
			WinHttpCloseHandle(session);
			return false;
		}

		if (statusCode < 200 || statusCode >= 300) {
			a_outError = std::format("HTTP {} from server directory.", statusCode);
			WinHttpCloseHandle(request);
			WinHttpCloseHandle(connection);
			WinHttpCloseHandle(session);
			return false;
		}

		std::string body;
		for (;;) {
			DWORD available = 0;
			if (!WinHttpQueryDataAvailable(request, &available)) {
				a_outError = std::format("WinHttpQueryDataAvailable failed ({}).", GetLastError());
				WinHttpCloseHandle(request);
				WinHttpCloseHandle(connection);
				WinHttpCloseHandle(session);
				return false;
			}
			if (available == 0) {
				break;
			}
			if (static_cast<std::size_t>(available) > kMaxResponseBytes - body.size()) {
				a_outError = "Server directory response exceeds 512 KiB.";
				WinHttpCloseHandle(request);
				WinHttpCloseHandle(connection);
				WinHttpCloseHandle(session);
				return false;
			}

			std::vector<char> buffer(available);
			DWORD read = 0;
			if (!WinHttpReadData(request, buffer.data(), available, &read)) {
				a_outError = std::format("WinHttpReadData failed ({}).", GetLastError());
				WinHttpCloseHandle(request);
				WinHttpCloseHandle(connection);
				WinHttpCloseHandle(session);
				return false;
			}
			if (read == 0) {
				break;
			}
			if (!AppendResponseChunk(body, { buffer.data(), read }, a_outError)) {
				WinHttpCloseHandle(request);
				WinHttpCloseHandle(connection);
				WinHttpCloseHandle(session);
				return false;
			}
		}

		WinHttpCloseHandle(request);
		WinHttpCloseHandle(connection);
		WinHttpCloseHandle(session);

		a_outBody = std::move(body);
		return true;
	}
}

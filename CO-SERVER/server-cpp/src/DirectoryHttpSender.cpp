#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	define NOMINMAX
#endif

#include "DirectoryRegistrar.h"

#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <system_error>

#if defined(_WIN32)
#	include <winsock2.h>
#	include <ws2tcpip.h>
#	include <Windows.h>
#	include <winhttp.h>
#	if defined(_MSC_VER)
#		pragma comment(lib, "ws2_32.lib")
#	endif
#	ifndef WINHTTP_OPTION_RESOLUTION_HOSTNAME
#		define WINHTTP_OPTION_RESOLUTION_HOSTNAME 165
#	endif
#else
#	include <curl/curl.h>
#endif

namespace CommonwealthOnline::Server
{
	namespace
	{
		constexpr int TimeoutMilliseconds = 10'000;

#if defined(_WIN32)
		std::wstring Widen(std::string_view a_text)
		{
			if (a_text.empty()) {
				return {};
			}
			const auto length = MultiByteToWideChar(CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()), nullptr, 0);
			std::wstring result(static_cast<std::size_t>(length), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()), result.data(), length);
			return result;
		}

		struct InternetHandle
		{
			HINTERNET value{};
			~InternetHandle()
			{
				if (value) {
					WinHttpCloseHandle(value);
				}
			}
		};

		std::optional<std::wstring> ResolveIpv4Literal(const std::wstring& a_host)
		{
			static const bool winsockReady = [] {
				WSADATA data{};
				return WSAStartup(MAKEWORD(2, 2), &data) == 0;
			}();
			if (!winsockReady) {
				return std::nullopt;
			}
			ADDRINFOW hints{};
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			PADDRINFOW addresses = nullptr;
			if (GetAddrInfoW(a_host.c_str(), nullptr, &hints, &addresses) != 0 || addresses == nullptr) {
				return std::nullopt;
			}
			std::optional<std::wstring> literal;
			for (auto* address = addresses; address != nullptr; address = address->ai_next) {
				if (address->ai_family != AF_INET || address->ai_addr == nullptr || address->ai_addrlen < sizeof(sockaddr_in)) {
					continue;
				}
				const auto* octets = reinterpret_cast<const unsigned char*>(&reinterpret_cast<const sockaddr_in*>(address->ai_addr)->sin_addr);
				literal = Widen(std::format("{}.{}.{}.{}",
					static_cast<unsigned>(octets[0]), static_cast<unsigned>(octets[1]),
					static_cast<unsigned>(octets[2]), static_cast<unsigned>(octets[3])));
				break;
			}
			FreeAddrInfoW(addresses);
			return literal;
		}

		DirectoryHttpResponse Send(std::string_view a_method, const std::string& a_url, const std::string& a_bearer, const std::string& a_body, bool a_ipv4Only)
		{
			DirectoryHttpResponse response;
			auto url = Widen(a_url);
			URL_COMPONENTS components{};
			components.dwStructSize = sizeof(components);
			components.dwHostNameLength = static_cast<DWORD>(-1);
			components.dwUrlPathLength = static_cast<DWORD>(-1);
			components.dwExtraInfoLength = static_cast<DWORD>(-1);
			if (!WinHttpCrackUrl(url.data(), static_cast<DWORD>(url.size()), 0, &components)) {
				response.transportError = std::format("invalid directory URL ({})", GetLastError());
				return response;
			}
			const std::wstring host(components.lpszHostName, components.dwHostNameLength);
			std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
			if (components.lpszExtraInfo) {
				path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
			}

			InternetHandle session{ WinHttpOpen(L"CommonwealthOnlineServer/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
			if (!session.value) {
				response.transportError = std::format("WinHttpOpen failed ({})", GetLastError());
				return response;
			}
			WinHttpSetTimeouts(session.value, TimeoutMilliseconds, TimeoutMilliseconds, TimeoutMilliseconds, TimeoutMilliseconds);

			InternetHandle connection{ WinHttpConnect(session.value, host.c_str(), components.nPort, 0) };
			if (!connection.value) {
				response.transportError = std::format("WinHttpConnect failed ({})", GetLastError());
				return response;
			}

			const auto method = Widen(a_method);
			const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
			InternetHandle request{ WinHttpOpenRequest(connection.value, method.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags) };
			if (!request.value) {
				response.transportError = std::format("WinHttpOpenRequest failed ({})", GetLastError());
				return response;
			}
			DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
			if (!WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy))) {
				response.transportError = std::format("redirect policy failed ({})", GetLastError());
				return response;
			}

			std::optional<std::wstring> resolutionHostname;
			if (a_ipv4Only) {
				resolutionHostname = ResolveIpv4Literal(host);
				if (resolutionHostname) {
					(void)WinHttpSetOption(request.value, WINHTTP_OPTION_RESOLUTION_HOSTNAME, const_cast<wchar_t*>(resolutionHostname->c_str()),
						static_cast<DWORD>((resolutionHostname->size() + 1) * sizeof(wchar_t)));
				}
			}

			std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json\r\n";
			if (!a_bearer.empty()) {
				headers += L"Authorization: Bearer " + Widen(a_bearer) + L"\r\n";
			}
			if (!WinHttpSendRequest(
					request.value,
					headers.c_str(),
					static_cast<DWORD>(-1L),
					a_body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(a_body.data()),
					static_cast<DWORD>(a_body.size()),
					static_cast<DWORD>(a_body.size()),
					0) ||
				!WinHttpReceiveResponse(request.value, nullptr)) {
				response.transportError = std::format("request failed ({})", GetLastError());
				return response;
			}

			DWORD status = 0;
			DWORD statusSize = sizeof(status);
			WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
			response.status = static_cast<int>(status);
			DWORD contentLength = 0;
			DWORD contentLengthSize = sizeof(contentLength);
			const bool hasContentLength = WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
				WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &contentLengthSize, WINHTTP_NO_HEADER_INDEX);
			if ((!hasContentLength && GetLastError() != ERROR_WINHTTP_HEADER_NOT_FOUND) ||
				(hasContentLength && contentLength > DirectoryRegistrar::MaximumResponseBytes)) {
				response.transportError = "invalid or oversized response length";
				return response;
			}

			for (;;) {
				DWORD available = 0;
				if (!WinHttpQueryDataAvailable(request.value, &available)) {
					response.transportError = std::format("response read failed ({})", GetLastError());
					return response;
				}
				if (available == 0) {
					if (hasContentLength && response.body.size() != contentLength) {
						response.transportError = "response truncated";
					}
					break;
				}
				if (response.body.size() + available > DirectoryRegistrar::MaximumResponseBytes) {
					response.transportError = "response too large";
					return response;
				}
				const auto offset = response.body.size();
				response.body.resize(offset + available);
				DWORD read = 0;
				if (!WinHttpReadData(request.value, response.body.data() + offset, available, &read)) {
					response.transportError = std::format("read failed ({})", GetLastError());
					return response;
				}
				response.body.resize(offset + read);
			}
			return response;
		}
#else
		std::size_t Collect(char* a_data, std::size_t a_size, std::size_t a_count, void* a_user)
		{
			auto* body = static_cast<std::string*>(a_user);
			const auto bytes = a_size * a_count;
			if (body->size() + bytes > DirectoryRegistrar::MaximumResponseBytes) {
				return 0;
			}
			body->append(a_data, bytes);
			return bytes;
		}

		DirectoryHttpResponse Send(std::string_view a_method, const std::string& a_url, const std::string& a_bearer, const std::string& a_body, bool a_ipv4Only)
		{
			DirectoryHttpResponse response;
			CURL* curl = curl_easy_init();
			if (!curl) {
				response.transportError = "curl_easy_init failed";
				return response;
			}

			curl_slist* headers = nullptr;
			headers = curl_slist_append(headers, "Content-Type: application/json");
			headers = curl_slist_append(headers, "Accept: application/json");
			const auto authorization = std::format("Authorization: Bearer {}", a_bearer);
			if (!a_bearer.empty()) {
				headers = curl_slist_append(headers, authorization.c_str());
			}
			const std::string method{ a_method };

			curl_easy_setopt(curl, CURLOPT_URL, a_url.c_str());
			curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(TimeoutMilliseconds));
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(TimeoutMilliseconds));
			curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
			curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
			curl_easy_setopt(curl, CURLOPT_USERAGENT, "CommonwealthOnlineServer/1");
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &Collect);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
			if (a_ipv4Only) {
				curl_easy_setopt(curl, CURLOPT_IPRESOLVE, static_cast<long>(CURL_IPRESOLVE_V4));
			}
			const auto caBundle = SelectDirectoryCaBundle(DirectoryCaBundleCandidates(), [](std::string_view a_path) {
				std::error_code error;
				return std::filesystem::is_regular_file(std::filesystem::path{ a_path }, error);
			});
			if (caBundle) {
				curl_easy_setopt(curl, CURLOPT_CAINFO, caBundle->c_str());
			}
			if (!a_body.empty()) {
				curl_easy_setopt(curl, CURLOPT_POSTFIELDS, a_body.c_str());
				curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(a_body.size()));
			}

			const auto result = curl_easy_perform(curl);
			if (result != CURLE_OK) {
				response.transportError = curl_easy_strerror(result);
			} else {
				long status = 0;
				curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
				response.status = static_cast<int>(status);
			}
			curl_slist_free_all(headers);
			curl_easy_cleanup(curl);
			return response;
		}
#endif
	}

	DirectoryHttpSender CreatePlatformDirectorySender()
	{
#if !defined(_WIN32)
		static const bool initialized = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
		if (!initialized) {
			return DirectoryHttpSender{ DirectoryHttpSender::Scoped{ [](std::string_view, const std::string&, const std::string&, const std::string&, bool) {
				return DirectoryHttpResponse{ 0, {}, "libcurl initialisation failed" };
			} } };
		}
#endif
		return DirectoryHttpSender{ DirectoryHttpSender::Scoped{ &Send } };
	}
}

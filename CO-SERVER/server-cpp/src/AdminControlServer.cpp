#include "AdminControlServer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <iostream>
#include <utility>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <cerrno>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>
#endif

namespace CommonwealthOnline::Server
{
	namespace
	{
		using Json = nlohmann::json;
		constexpr std::size_t kMaximumAdminMessageBytes = 64uz * 1024uz;
		constexpr auto kAdminExecutionGrace = std::chrono::seconds(2);
		constexpr std::size_t kMaximumAdminClients = 8;
		constexpr auto kAdminRequestDeadline = std::chrono::seconds(15);
		constexpr auto kAdminReadWait = std::chrono::milliseconds(50);
		constexpr std::uintptr_t kInvalidStoredSocket = static_cast<std::uintptr_t>(-1);
		constexpr std::size_t kMaximumAdminAuthFailures = 5;
		constexpr auto kAdminAuthLockout = std::chrono::seconds(60);
		constexpr std::size_t kMinimumAdminTokenLength = 32;
		constexpr std::size_t kMinimumAdminTokenDistinctChars = 12;

#ifdef _WIN32
		using NativeSocket = SOCKET;
		constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
		using NativeSocket = int;
		constexpr NativeSocket kInvalidSocket = -1;
#endif

		NativeSocket FromStorage(std::uintptr_t a_socket) noexcept
		{
			return static_cast<NativeSocket>(a_socket);
		}

		std::uintptr_t ToStorage(NativeSocket a_socket) noexcept
		{
			return static_cast<std::uintptr_t>(a_socket);
		}

		void CloseSocket(NativeSocket a_socket) noexcept
		{
			if (a_socket == kInvalidSocket) {
				return;
			}
#ifdef _WIN32
			closesocket(a_socket);
#else
			close(a_socket);
#endif
		}

		void ShutdownSocket(NativeSocket a_socket) noexcept
		{
			if (a_socket == kInvalidSocket) {
				return;
			}
#ifdef _WIN32
			shutdown(a_socket, SD_BOTH);
#else
			shutdown(a_socket, SHUT_RDWR);
#endif
		}

		std::string SocketErrorDetail()
		{
#ifdef _WIN32
			return " (Winsock error " + std::to_string(static_cast<int>(WSAGetLastError())) + ")";
#else
			return " (errno " + std::to_string(errno) + ")";
#endif
		}

#ifdef _WIN32
		bool SetExclusiveAddressUse(NativeSocket a_socket) noexcept
		{
			const int exclusive = 1;
			return setsockopt(
				a_socket,
				SOL_SOCKET,
				SO_EXCLUSIVEADDRUSE,
				reinterpret_cast<const char*>(&exclusive),
				static_cast<int>(sizeof(exclusive))) == 0;
		}

		bool HoldsLoopbackPortExclusively(std::uint16_t a_port)
		{
			const auto probe = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (probe == kInvalidSocket) {
				return false;
			}
			const int reuse = 1;
			(void)setsockopt(
				probe,
				SOL_SOCKET,
				SO_REUSEADDR,
				reinterpret_cast<const char*>(&reuse),
				static_cast<int>(sizeof(reuse)));
			sockaddr_in address{};
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			address.sin_port = htons(a_port);
			const bool competitorBound = bind(probe, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
			CloseSocket(probe);
			return !competitorBound;
		}
#endif

		bool IsRetryableReceiveError() noexcept
		{
#ifdef _WIN32
			const auto error = WSAGetLastError();
			return error == WSAETIMEDOUT || error == WSAEWOULDBLOCK || error == WSAEINTR;
#else
			return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
		}

		bool SetReceiveTimeout(NativeSocket a_socket)
		{
#ifdef _WIN32
			const DWORD timeout = 500;
			return setsockopt(
				a_socket,
				SOL_SOCKET,
				SO_RCVTIMEO,
				reinterpret_cast<const char*>(&timeout),
				static_cast<int>(sizeof(timeout))) == 0;
#else
			const timeval timeout{ 0, 500'000 };
			return setsockopt(a_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0;
#endif
		}

		bool SetSendTimeout(NativeSocket a_socket)
		{
#ifdef _WIN32
			const DWORD timeout = 500;
			return setsockopt(
				a_socket,
				SOL_SOCKET,
				SO_SNDTIMEO,
				reinterpret_cast<const char*>(&timeout),
				static_cast<int>(sizeof(timeout))) == 0;
#else
			const timeval timeout{ 0, 500'000 };
			return setsockopt(a_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0;
#endif
		}

		int WaitReadable(NativeSocket a_socket, std::chrono::milliseconds a_wait) noexcept
		{
			fd_set readable;
			FD_ZERO(&readable);
			FD_SET(a_socket, &readable);
			timeval timeout{};
			timeout.tv_sec = static_cast<long>(a_wait.count() / 1000);
			timeout.tv_usec = static_cast<long>((a_wait.count() % 1000) * 1000);
			const auto ready = select(static_cast<int>(a_socket) + 1, &readable, nullptr, nullptr, &timeout);
			if (ready > 0) {
				return 1;
			}
			return ready == 0 ? 0 : -1;
		}

		bool SendAll(NativeSocket a_socket, std::string_view a_payload)
		{
			std::size_t sent = 0;
			while (sent < a_payload.size()) {
#ifdef _WIN32
				const auto remaining = static_cast<int>((std::min)(
					a_payload.size() - sent,
					static_cast<std::size_t>((std::numeric_limits<int>::max)())));
				const auto result = send(a_socket, a_payload.data() + sent, remaining, 0);
#else
				const auto result = send(a_socket, a_payload.data() + sent, a_payload.size() - sent, MSG_NOSIGNAL);
#endif
				if (result <= 0) {
					return false;
				}
				sent += static_cast<std::size_t>(result);
			}
			return true;
		}

		std::string Trim(std::string_view a_value)
		{
			std::size_t begin = 0;
			std::size_t end = a_value.size();
			while (begin < end && std::isspace(static_cast<unsigned char>(a_value[begin])) != 0) {
				++begin;
			}
			while (end > begin && std::isspace(static_cast<unsigned char>(a_value[end - 1])) != 0) {
				--end;
			}
			return std::string{ a_value.substr(begin, end - begin) };
		}

		std::string Lower(std::string a_value)
		{
			std::transform(a_value.begin(), a_value.end(), a_value.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
			return a_value;
		}
	}

	struct AdminControlServer::Waiter
	{
		std::mutex gate;
		std::condition_variable ready;
		std::optional<Json> response;
	};

	AdminControlServer::AdminControlServer(
		std::uint16_t a_port,
		std::string a_adminToken,
		TimeoutInstaller a_timeoutInstaller,
		DirectHandler a_directHandler,
		std::chrono::milliseconds a_responseTimeout) :
		_requestedPort(a_port),
		_adminToken(std::move(a_adminToken)),
		_timeoutInstaller(a_timeoutInstaller ? std::move(a_timeoutInstaller) : TimeoutInstaller{ [](std::uintptr_t a_socket, bool a_receive) {
			const auto nativeSocket = FromStorage(a_socket);
			return a_receive ? SetReceiveTimeout(nativeSocket) : SetSendTimeout(nativeSocket);
		} }),
		_directHandler(std::move(a_directHandler)),
		_responseTimeout(a_responseTimeout)
	{}

	AdminControlServer::~AdminControlServer()
	{
		Stop();
	}

	bool AdminControlServer::Start(std::string& a_error)
	{
		if (_running.load(std::memory_order_acquire)) {
			return true;
		}
		if (_adminToken.empty()) {
			a_error = "Admin token cannot be empty.";
			return false;
		}
		if (WeakAdminToken(_adminToken)) {
			a_error = "Admin token is too weak. Use at least 32 random characters.";
			return false;
		}

#ifdef _WIN32
		WSADATA winsock{};
		if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
			a_error = "WSAStartup failed for the admin control server.";
			return false;
		}
		_socketRuntimeHeld = true;
#endif

		const auto socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (socketHandle == kInvalidSocket) {
			a_error = "Could not create the admin control socket.";
#ifdef _WIN32
			WSACleanup();
			_socketRuntimeHeld = false;
#endif
			return false;
		}

#ifdef _WIN32
		if (!SetExclusiveAddressUse(socketHandle)) {
			const auto detail = SocketErrorDetail();
			CloseSocket(socketHandle);
			a_error = "Could not reserve the admin control port exclusively" + detail + "; admin control is disabled.";
			WSACleanup();
			_socketRuntimeHeld = false;
			return false;
		}
#else
		int reuse = 1;
		(void)setsockopt(socketHandle, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = htons(_requestedPort);
		if (bind(socketHandle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
			const auto detail = SocketErrorDetail();
			CloseSocket(socketHandle);
			a_error = "Could not bind the admin control socket to 127.0.0.1:" + std::to_string(_requestedPort) + detail +
				". Another local process may already be listening on that port; stop it or set a different admin_port.";
#ifdef _WIN32
			WSACleanup();
			_socketRuntimeHeld = false;
#endif
			return false;
		}
		if (listen(socketHandle, 8) != 0) {
			CloseSocket(socketHandle);
			a_error = "Could not listen on the admin control socket.";
#ifdef _WIN32
			WSACleanup();
			_socketRuntimeHeld = false;
#endif
			return false;
		}

		sockaddr_in actual{};
#ifdef _WIN32
		int actualSize = sizeof(actual);
#else
		socklen_t actualSize = sizeof(actual);
#endif
		if (getsockname(socketHandle, reinterpret_cast<sockaddr*>(&actual), &actualSize) != 0) {
			CloseSocket(socketHandle);
			a_error = "Could not determine the admin control port.";
#ifdef _WIN32
			WSACleanup();
			_socketRuntimeHeld = false;
#endif
			return false;
		}

		const auto boundPort = ntohs(actual.sin_port);
		if (_requestedPort != 0 && boundPort != _requestedPort) {
			CloseSocket(socketHandle);
			a_error = "Admin control bound 127.0.0.1:" + std::to_string(boundPort) + " instead of the requested port " +
				std::to_string(_requestedPort) + ".";
#ifdef _WIN32
			WSACleanup();
			_socketRuntimeHeld = false;
#endif
			return false;
		}
#ifdef _WIN32
		if (!HoldsLoopbackPortExclusively(boundPort)) {
			CloseSocket(socketHandle);
			a_error = "The admin control port 127.0.0.1:" + std::to_string(boundPort) +
				" is not held exclusively, so another local process could capture admin requests; admin control is disabled.";
			WSACleanup();
			_socketRuntimeHeld = false;
			return false;
		}
#endif

		_listenSocket = ToStorage(socketHandle);
		_localPort.store(boundPort, std::memory_order_release);
		_running.store(true, std::memory_order_release);
		_thread = std::thread([this, socketHandle] { AcceptLoop(ToStorage(socketHandle)); });
		return true;
	}

	void AdminControlServer::Stop()
	{
		(void)_running.exchange(false, std::memory_order_acq_rel);
		const auto stored = _listenSocket;
		_listenSocket = kInvalidStoredSocket;
		const auto socketHandle = FromStorage(stored);
		if (stored != kInvalidStoredSocket && socketHandle != kInvalidSocket) {
			ShutdownSocket(socketHandle);
			CloseSocket(socketHandle);
		}
		{
			const std::scoped_lock lock(_clientGate);
			for (const auto clientStored : _clientSockets) {
				ShutdownSocket(FromStorage(clientStored));
			}
		}
		FailOutstanding("Admin server stopped.");
		if (_thread.joinable()) {
			_thread.join();
		}
		{
			std::unique_lock lock(_clientGate);
			_clientsIdle.wait(lock, [this] { return _activeClients == 0; });
		}
		_localPort.store(0, std::memory_order_release);
#ifdef _WIN32
		if (_socketRuntimeHeld) {
			WSACleanup();
			_socketRuntimeHeld = false;
		}
#endif
	}

	std::uint16_t AdminControlServer::LocalPort() const noexcept
	{
		return _localPort.load(std::memory_order_acquire);
	}

	bool AdminControlServer::TryPop(AdminCommand& a_command)
	{
		std::scoped_lock lock(_queueGate);
		if (_commands.empty()) {
			return false;
		}
		a_command = std::move(_commands.front());
		_commands.pop_front();
		_states[a_command.ticket] = CommandState::Executing;
		return true;
	}

	AdminControlServer::CancelOutcome AdminControlServer::CancelIfQueued(std::uint64_t a_ticket)
	{
		const std::scoped_lock lock(_queueGate);
		const auto found = _states.find(a_ticket);
		if (found == _states.end()) {
			return CancelOutcome::Settled;
		}
		if (found->second == CommandState::Executing) {
			return CancelOutcome::Executing;
		}
		_states.erase(found);
		_waiters.erase(a_ticket);
		std::erase_if(_commands, [a_ticket](const AdminCommand& a_command) {
			return a_command.ticket == a_ticket;
		});
		return CancelOutcome::Cancelled;
	}

	bool AdminControlServer::Complete(std::uint64_t a_ticket, nlohmann::json a_response)
	{
		std::shared_ptr<Waiter> waiter;
		{
			std::scoped_lock lock(_queueGate);
			_states.erase(a_ticket);
			const auto found = _waiters.find(a_ticket);
			if (found == _waiters.end()) {
				return false;
			}
			waiter = found->second;
			_waiters.erase(found);
		}
		{
			std::scoped_lock lock(waiter->gate);
			waiter->response = std::move(a_response);
		}
		waiter->ready.notify_one();
		return true;
	}

	bool AdminControlServer::TokenEquals(std::string_view a_supplied, std::string_view a_expected) noexcept
	{
		if (a_supplied.size() != a_expected.size()) {
			return false;
		}
		volatile std::uint8_t difference = 0;
		for (std::size_t index = 0; index < a_supplied.size(); ++index) {
			difference = static_cast<std::uint8_t>(difference |
				(static_cast<std::uint8_t>(a_supplied[index]) ^ static_cast<std::uint8_t>(a_expected[index])));
		}
		return difference == 0;
	}

	nlohmann::json AdminControlServer::Failure(std::string a_error, const nlohmann::json* a_request, nlohmann::json a_data)
	{
		Json response{
			{ "ok", false },
			{ "error", std::move(a_error) }
		};
		if (a_request != nullptr) {
			const auto id = a_request->find("id");
			if (id != a_request->end() && !id->is_null()) {
				response["id"] = *id;
			}
		}
		if (!a_data.is_null()) {
			response["data"] = std::move(a_data);
		}
		return response;
	}

	nlohmann::json AdminControlServer::Success(
		nlohmann::json a_data,
		std::string a_message,
		const nlohmann::json* a_request)
	{
		Json response{ { "ok", true } };
		if (a_request != nullptr) {
			const auto id = a_request->find("id");
			if (id != a_request->end() && !id->is_null()) {
				response["id"] = *id;
			}
		}
		if (!a_message.empty()) {
			response["message"] = std::move(a_message);
		}
		if (!a_data.is_null()) {
			response["data"] = std::move(a_data);
		}
		return response;
	}

	void AdminControlServer::AcceptLoop(std::uintptr_t a_listenSocket)
	{
		const auto listener = FromStorage(a_listenSocket);
		while (_running.load(std::memory_order_acquire)) {
			sockaddr_in remote{};
#ifdef _WIN32
			int remoteSize = sizeof(remote);
#else
			socklen_t remoteSize = sizeof(remote);
#endif
			const auto client = accept(listener, reinterpret_cast<sockaddr*>(&remote), &remoteSize);
			if (client == kInvalidSocket) {
				if (!_running.load(std::memory_order_acquire)) {
					break;
				}
				continue;
			}
			if (!_timeoutInstaller(ToStorage(client), true) || !_timeoutInstaller(ToStorage(client), false)) {
				ShutdownSocket(client);
				CloseSocket(client);
				continue;
			}

			const auto stored = ToStorage(client);
			{
				std::unique_lock lock(_clientGate);
				if (_activeClients >= kMaximumAdminClients || !_running.load(std::memory_order_acquire)) {
					lock.unlock();
					const auto busy = Failure("Admin server is busy.").dump() + "\n";
					(void)SendAll(client, busy);
					ShutdownSocket(client);
					CloseSocket(client);
					continue;
				}
				++_activeClients;
				_clientSockets.push_back(stored);
			}

			std::thread([this, stored] {
				HandleClient(stored);
				const auto socketHandle = FromStorage(stored);
				{
					const std::scoped_lock lock(_clientGate);
					std::erase(_clientSockets, stored);
				}
				ShutdownSocket(socketHandle);
				CloseSocket(socketHandle);
				{
					const std::scoped_lock lock(_clientGate);
					--_activeClients;
					_clientsIdle.notify_all();
				}
			}).detach();
		}
	}

	void AdminControlServer::HandleClient(std::uintptr_t a_clientSocket)
	{
		const auto client = FromStorage(a_clientSocket);
		std::array<char, 4096> buffer{};
		std::string line;
		line.reserve(4096);
		const auto deadline = std::chrono::steady_clock::now() + kAdminRequestDeadline;
		while (_running.load(std::memory_order_acquire)) {
			if (std::chrono::steady_clock::now() >= deadline) {
				return;
			}
			if (WaitReadable(client, kAdminReadWait) == 0) {
				continue;
			}
#ifdef _WIN32
			const auto count = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
			const auto count = recv(client, buffer.data(), buffer.size(), 0);
#endif
			if (count == 0) {
				break;
			}
			if (count < 0) {
				if (IsRetryableReceiveError()) {
					continue;
				}
				break;
			}

			for (std::ptrdiff_t index = 0; index < count; ++index) {
				const char ch = buffer[static_cast<std::size_t>(index)];
				if (ch == '\n') {
					if (line.empty()) {
						continue;
					}
					const auto response = ProcessLine(line);
					const auto encoded = response.dump(-1, ' ', false, Json::error_handler_t::replace) + "\n";
					line.clear();
					(void)SendAll(client, encoded);
					return;
				}
				line.push_back(ch);
				if (line.size() > kMaximumAdminMessageBytes) {
					return;
				}
			}
		}
	}

	nlohmann::json AdminControlServer::ProcessLine(std::string_view a_line)
	{
		Json request;
		try {
			request = Json::parse(a_line);
			if (!request.is_object()) {
				return Failure("Invalid JSON: request must be an object.");
			}
		} catch (const Json::exception& exception) {
			return Failure("Invalid JSON: " + std::string{ exception.what() });
		}

		if (AuthenticationLockedOut()) {
			return Failure("Admin authentication is temporarily locked out after repeated failed attempts.", &request);
		}
		const auto token = request.find("adminToken");
		if (token == request.end() || !token->is_string() ||
			!TokenEquals(token->get_ref<const std::string&>(), _adminToken)) {
			RecordAuthenticationFailure();
			return Failure("Unauthorized admin request.", &request);
		}
		RecordAuthenticationSuccess();
		request.erase("adminToken");

		std::string command;
		if (const auto found = request.find("cmd"); found != request.end() && found->is_string()) {
			command = found->get<std::string>();
		} else if (const auto found = request.find("command"); found != request.end() && found->is_string()) {
			command = found->get<std::string>();
		}
		request["cmd"] = Lower(Trim(command));
		request.erase("command");
		if (_directHandler) {
			try {
				return _directHandler(request);
			} catch (const std::exception&) {
				return Failure("Admin request failed.", &request);
			}
		}
		return QueueAndWait(std::move(request));
	}

	nlohmann::json AdminControlServer::QueueAndWait(nlohmann::json a_request)
	{
		auto waiter = std::make_shared<Waiter>();
		std::uint64_t ticket = 0;
		{
			std::scoped_lock lock(_queueGate);
			ticket = _nextTicket++;
			if (ticket == 0) {
				ticket = _nextTicket++;
			}
			_waiters[ticket] = waiter;
			_states[ticket] = CommandState::Queued;
			_commands.push_back(AdminCommand{ ticket, a_request });
		}

		std::unique_lock lock(waiter->gate);
		const bool ready = waiter->ready.wait_for(lock, _responseTimeout, [&] {
			return waiter->response.has_value() || !_running.load(std::memory_order_acquire);
		});
		if (ready && waiter->response) {
			return std::move(*waiter->response);
		}
		lock.unlock();

		if (CancelIfQueued(ticket) != CancelOutcome::Cancelled) {
			std::unique_lock settleLock(waiter->gate);
			const bool settled = waiter->ready.wait_for(settleLock, kAdminExecutionGrace, [&] {
				return waiter->response.has_value() || !_running.load(std::memory_order_acquire);
			});
			std::optional<Json> response;
			if (settled && waiter->response) {
				response = std::move(*waiter->response);
			}
			settleLock.unlock();
			{
				std::scoped_lock queueLock(_queueGate);
				_waiters.erase(ticket);
				_states.erase(ticket);
			}
			if (response) {
				return std::move(*response);
			}
			return Failure(
				_running.load(std::memory_order_acquire) ?
					"Admin command is still executing; its effect is unknown. Check the server state before retrying." :
					"Admin server stopped.",
				&a_request);
		}
		return Failure(
			_running.load(std::memory_order_acquire) ? "Admin command timed out." : "Admin server stopped.",
			&a_request);
	}

	bool AdminControlServer::WeakAdminToken(std::string_view a_token) noexcept
	{
		if (a_token.size() < kMinimumAdminTokenLength) {
			return true;
		}
		bool seen[256]{};
		std::size_t distinct = 0;
		for (const auto ch : a_token) {
			const auto value = static_cast<unsigned char>(ch);
			if (!seen[value]) {
				seen[value] = true;
				++distinct;
			}
		}
		return distinct < kMinimumAdminTokenDistinctChars;
	}

	std::size_t AdminControlServer::FailedAuthenticationCount() const noexcept
	{
		const std::scoped_lock lock(_authGate);
		return _failedAuthTotal;
	}

	bool AdminControlServer::AuthenticationLockedOut() const
	{
		const std::scoped_lock lock(_authGate);
		return std::chrono::steady_clock::now() < _authLockoutUntil;
	}

	void AdminControlServer::RecordAuthenticationFailure()
	{
		const std::scoped_lock lock(_authGate);
		++_failedAuthTotal;
		++_consecutiveAuthFailures;
		if (_consecutiveAuthFailures >= kMaximumAdminAuthFailures) {
			_authLockoutUntil = std::chrono::steady_clock::now() + kAdminAuthLockout;
			_consecutiveAuthFailures = 0;
			std::cerr << "Admin authentication locked out for " << kAdminAuthLockout.count()
			          << " s after repeated failed attempts.\n";
		}
	}

	void AdminControlServer::RecordAuthenticationSuccess()
	{
		const std::scoped_lock lock(_authGate);
		_consecutiveAuthFailures = 0;
	}

	void AdminControlServer::FailOutstanding(std::string_view a_reason)
	{
		std::vector<std::shared_ptr<Waiter>> waiters;
		{
			std::scoped_lock lock(_queueGate);
			waiters.reserve(_waiters.size());
			for (auto& [ticket, waiter] : _waiters) {
				(void)ticket;
				waiters.push_back(waiter);
			}
			_waiters.clear();
			_commands.clear();
			_states.clear();
		}
		for (const auto& waiter : waiters) {
			{
				std::scoped_lock lock(waiter->gate);
				waiter->response = Failure(std::string{ a_reason });
			}
			waiter->ready.notify_one();
		}
	}
}

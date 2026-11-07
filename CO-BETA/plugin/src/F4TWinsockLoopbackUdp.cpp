#include "F4TWinsockLoopbackUdp.h"

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace F4T::Steam
{
	namespace
	{
		constexpr std::size_t kMaximumDatagramBytes = 2048;
		constexpr std::size_t kMaximumDrainPerCall = 64;

#ifdef _WIN32
		using NativeSocket = SOCKET;
		using AddressLength = int;
		constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;

		void CloseSocket(NativeSocket a_socket)
		{
			::closesocket(a_socket);
		}

		bool ConfigureSocket(NativeSocket a_socket)
		{
			u_long nonBlocking = 1;
			if (::ioctlsocket(a_socket, FIONBIO, &nonBlocking) == SOCKET_ERROR) return false;

			BOOL reportConnectionReset = FALSE;
			DWORD returned = 0;
			::WSAIoctl(
				a_socket,
				SIO_UDP_CONNRESET,
				&reportConnectionReset,
				sizeof(reportConnectionReset),
				nullptr,
				0,
				&returned,
				nullptr,
				nullptr);
			return true;
		}

		bool LastReceiveWasOversize()
		{
			return ::WSAGetLastError() == WSAEMSGSIZE;
		}
#else
		using NativeSocket = int;
		using AddressLength = socklen_t;
		constexpr NativeSocket kInvalidSocket = -1;

		void CloseSocket(NativeSocket a_socket)
		{
			::close(a_socket);
		}

		bool ConfigureSocket(NativeSocket a_socket)
		{
			const int flags = ::fcntl(a_socket, F_GETFL, 0);
			return flags != -1 && ::fcntl(a_socket, F_SETFL, flags | O_NONBLOCK) != -1;
		}

		bool LastReceiveWasOversize()
		{
			return false;
		}
#endif

		std::mutex& WinsockMutex()
		{
			static std::mutex mutex;
			return mutex;
		}

		int& WinsockReferences()
		{
			static int references = 0;
			return references;
		}

		bool AcquireWinsock()
		{
			std::lock_guard lock{ WinsockMutex() };
			if (WinsockReferences() == 0) {
#ifdef _WIN32
				WSADATA data{};
				if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
#endif
			}
			++WinsockReferences();
			return true;
		}

		void ReleaseWinsock()
		{
			std::lock_guard lock{ WinsockMutex() };
			if (WinsockReferences() == 0) return;
#ifdef _WIN32
			if (--WinsockReferences() == 0) WSACleanup();
#else
			WinsockReferences()--;
#endif
		}

		struct LoopbackState
		{
			~LoopbackState()
			{
				for (const auto& entry : sockets) {
					CloseSocket(entry.second);
				}
				if (winsockHeld) ReleaseWinsock();
			}

			std::optional<std::uint16_t> OpenLoopback()
			{
				std::lock_guard lock{ mutex };
				if (!winsockHeld) {
					if (!AcquireWinsock()) return std::nullopt;
					winsockHeld = true;
				}

				const NativeSocket socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
				if (socket == kInvalidSocket) return std::nullopt;

				sockaddr_in address{};
				address.sin_family = AF_INET;
				address.sin_port = 0;
				address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
				if (::bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
					CloseSocket(socket);
					return std::nullopt;
				}

				AddressLength length = sizeof(address);
				if (::getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length) < 0) {
					CloseSocket(socket);
					return std::nullopt;
				}

				if (!ConfigureSocket(socket)) {
					CloseSocket(socket);
					return std::nullopt;
				}

				const auto port = static_cast<std::uint16_t>(ntohs(address.sin_port));
				if (port == 0) {
					CloseSocket(socket);
					return std::nullopt;
				}

				sockets.emplace(port, socket);
				return port;
			}

			void Close(std::uint16_t a_port)
			{
				NativeSocket socket = kInvalidSocket;
				{
					std::lock_guard lock{ mutex };
					const auto found = sockets.find(a_port);
					if (found == sockets.end()) return;
					socket = found->second;
					sockets.erase(found);
				}
				CloseSocket(socket);
			}

			bool SendTo(std::uint16_t a_fromPort, std::uint16_t a_toPort, std::span<const std::uint8_t> a_payload)
			{
				std::lock_guard lock{ mutex };
				const auto found = sockets.find(a_fromPort);
				if (found == sockets.end()) return false;

				sockaddr_in address{};
				address.sin_family = AF_INET;
				address.sin_port = htons(a_toPort);
				address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

				const auto* bytes = reinterpret_cast<const char*>(a_payload.data());
				const auto length = static_cast<int>(a_payload.size());
				return ::sendto(found->second, bytes, length, 0, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) >= 0;
			}

			std::vector<Transport::UdpDatagram> Receive(std::uint16_t a_port)
			{
				std::vector<Transport::UdpDatagram> datagrams;
				std::lock_guard lock{ mutex };
				const auto found = sockets.find(a_port);
				if (found == sockets.end()) return datagrams;

				std::array<std::uint8_t, kMaximumDatagramBytes + 1> buffer{};
				for (std::size_t drained = 0; drained < kMaximumDrainPerCall; ++drained) {
					sockaddr_in from{};
					AddressLength fromLength = sizeof(from);
					const auto received = ::recvfrom(
						found->second,
						reinterpret_cast<char*>(buffer.data()),
						static_cast<int>(buffer.size()),
						0,
						reinterpret_cast<sockaddr*>(&from),
						&fromLength);
					if (received < 0) {
						if (LastReceiveWasOversize()) continue;
						break;
					}

					const auto size = static_cast<std::size_t>(received);
					if (size > kMaximumDatagramBytes) continue;

					Transport::UdpDatagram datagram;
					datagram.sourcePort = static_cast<std::uint16_t>(ntohs(from.sin_port));
					datagram.payload.assign(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(size));
					datagrams.push_back(std::move(datagram));
				}
				return datagrams;
			}

			std::mutex mutex;
			std::map<std::uint16_t, NativeSocket> sockets;
			bool winsockHeld{ false };
		};
	}

	[[nodiscard]] Transport::UdpSocketApi MakeWinsockLoopbackUdp()
	{
		const auto state = std::make_shared<LoopbackState>();
		Transport::UdpSocketApi api;
		api.openLoopback = [state] { return state->OpenLoopback(); };
		api.close = [state](std::uint16_t a_port) { state->Close(a_port); };
		api.sendTo = [state](std::uint16_t a_fromPort, std::uint16_t a_toPort, std::span<const std::uint8_t> a_payload) {
			return state->SendTo(a_fromPort, a_toPort, a_payload);
		};
		api.receive = [state](std::uint16_t a_port) { return state->Receive(a_port); };
		return api;
	}
}

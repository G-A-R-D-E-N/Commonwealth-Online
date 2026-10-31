#pragma once

#include <cerrno>
#include <cstdint>
#include <stdexcept>
#include <system_error>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

inline std::uint16_t FindAvailableUdpPort()
{
#if defined(_WIN32)
    // Tests call this before the bridge has started Winsock.
    static const bool winsockStarted = [] {
        WSADATA data{};
        return ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!winsockStarted) {
        throw std::runtime_error("WSAStartup failed");
    }
    const SOCKET socketFd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketFd == INVALID_SOCKET) {
        throw std::system_error(WSAGetLastError(), std::generic_category(), "socket");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(0);

    if (::bind(socketFd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        const int error = WSAGetLastError();
        ::closesocket(socketFd);
        throw std::system_error(error, std::generic_category(), "bind");
    }

    int addressLength = sizeof(address);
    if (::getsockname(socketFd, reinterpret_cast<sockaddr*>(&address), &addressLength) != 0) {
        const int error = WSAGetLastError();
        ::closesocket(socketFd);
        throw std::system_error(error, std::generic_category(), "getsockname");
    }

    const auto port = ntohs(address.sin_port);
    ::closesocket(socketFd);

    if (port == 0) {
        throw std::runtime_error("OS returned UDP port 0");
    }

    return port;
#else
    const int socketFd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketFd < 0) {
        throw std::system_error(errno, std::generic_category(), "socket");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(0);

    if (::bind(socketFd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        const int error = errno;
        ::close(socketFd);
        throw std::system_error(error, std::generic_category(), "bind");
    }

    socklen_t addressLength = sizeof(address);
    if (::getsockname(socketFd, reinterpret_cast<sockaddr*>(&address), &addressLength) != 0) {
        const int error = errno;
        ::close(socketFd);
        throw std::system_error(error, std::generic_category(), "getsockname");
    }

    const auto port = ntohs(address.sin_port);
    ::close(socketFd);

    if (port == 0) {
        throw std::runtime_error("OS returned UDP port 0");
    }

    return port;
#endif
}

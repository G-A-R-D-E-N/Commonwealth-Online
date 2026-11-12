#include "lab_tload_state.h"
#include "lab_tload_wire.h"

#include <winsock2.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>

namespace VoiceLab::Tload
{
	namespace
	{
		constexpr DWORD kAdminTimeoutMs = 5000;
		constexpr std::size_t kAdminReplyBytes = 64 * 1024;
		constexpr std::size_t kAdminChunkBytes = 1024;

		bool ReadToken(const std::string& a_path, std::string& a_token)
		{
			std::ifstream file(a_path);
			if (!file) {
				return false;
			}
			std::getline(file, a_token);
			const auto end = a_token.find_last_not_of(" \t\r\n");
			a_token = end == std::string::npos ? std::string{} : a_token.substr(0, end + 1);
			return !a_token.empty();
		}

		bool Exchange(std::uint16_t a_port, const std::string& a_request, std::string& a_reply)
		{
			const SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (socket == INVALID_SOCKET) {
				return false;
			}
			const DWORD timeout = kAdminTimeoutMs;
			(void)::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
			sockaddr_in address{};
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			address.sin_port = htons(a_port);
			bool ok = ::connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0 &&
				::send(socket, a_request.data(), static_cast<int>(a_request.size()), 0) == static_cast<int>(a_request.size());
			std::array<char, kAdminChunkBytes> chunk{};
			a_reply.clear();
			while (ok && a_reply.find('\n') == std::string::npos && a_reply.size() < kAdminReplyBytes) {
				const int count = ::recv(socket, chunk.data(), static_cast<int>(chunk.size()), 0);
				if (count <= 0) {
					break;
				}
				a_reply.append(chunk.data(), static_cast<std::size_t>(count));
			}
			::closesocket(socket);
			return ok && a_reply.find('\n') != std::string::npos;
		}
	}

	bool ReadVoiceCounters(std::uint16_t a_port, const std::string& a_tokenFile, VoiceCounters& a_out)
	{
		std::string token;
		if (!ReadToken(a_tokenFile, token)) {
			return false;
		}
		const std::string request = "{\"adminToken\":\"" + token + "\",\"cmd\":\"status\"}\n";
		std::string reply;
		if (!Exchange(a_port, request, reply) || reply.find("\"ok\":true") == std::string::npos) {
			return false;
		}
		const auto voice = reply.find("\"voice\":");
		if (voice == std::string::npos) {
			return false;
		}
		const std::string_view tail = std::string_view{ reply }.substr(voice);
		double sent = 0.0;
		double relayed = 0.0;
		double droppedEgress = 0.0;
		double droppedSoftLimit = 0.0;
		if (!FindNumber(tail, "sent", sent) || !FindNumber(tail, "relayed", relayed) ||
			!FindNumber(tail, "dropped_egress", droppedEgress) || !FindNumber(tail, "dropped_soft_limit", droppedSoftLimit)) {
			return false;
		}
		a_out.sent = static_cast<std::uint64_t>(sent);
		a_out.relayed = static_cast<std::uint64_t>(relayed);
		a_out.droppedEgress = static_cast<std::uint64_t>(droppedEgress);
		a_out.droppedSoftLimit = static_cast<std::uint64_t>(droppedSoftLimit);
		return true;
	}
}

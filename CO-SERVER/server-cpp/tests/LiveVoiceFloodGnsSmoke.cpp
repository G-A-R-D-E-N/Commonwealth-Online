#include "F4TVoiceWire.h"
#include "co_gns_server_bridge.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
	using Bytes = std::vector<std::byte>;
	using Clock = std::chrono::steady_clock;
	using Json = nlohmann::json;

	void Require(bool a_condition, std::string_view a_message)
	{
		if (!a_condition) {
			throw std::runtime_error(std::string{ a_message });
		}
	}

	bool ParsePort(std::string_view a_value, std::uint16_t& a_port)
	{
		std::uint32_t parsed = 0;
		const auto result = std::from_chars(a_value.data(), a_value.data() + a_value.size(), parsed);
		if (result.ec != std::errc{} || result.ptr != a_value.data() + a_value.size() || parsed == 0 || parsed > 65535) {
			return false;
		}
		a_port = static_cast<std::uint16_t>(parsed);
		return true;
	}

	Bytes VoiceDatagram()
	{
		const Bytes payload(60, std::byte{ 0x5A });
		Bytes datagram(F4T::Voice::kVoiceMaxDatagramBytes);
		F4T::Voice::VoiceHeader header{};
		header.sequence = 1;
		datagram.resize(F4T::Voice::EncodeVoicePacket(header, payload, datagram));
		return datagram;
	}

	Json Hello(const std::string& a_profileId)
	{
		return Json{
			{ "type", "hello" },
			{ "protocolVersion", 2 },
			{ "clientVersion", "voice-flood-smoke-1" },
			{ "falloutRuntimeVersion", "1.11.240" },
			{ "profileId", a_profileId },
			{ "contentManifestFingerprint", std::string(64, 'c') },
			{ "capabilities", Json::array({ "interest-v1", "player-state-v1", "save-attestation-v1" }) }
		};
	}

	std::optional<Json> ParseJsonObject(const std::uint8_t* a_data, std::size_t a_size)
	{
		const auto* first = reinterpret_cast<const char*>(a_data);
		auto packet = Json::parse(first, first + a_size, nullptr, false);
		return packet.is_object() ? std::optional<Json>{ std::move(packet) } : std::nullopt;
	}

	std::optional<std::string> SessionEndedCode(const std::vector<Json>& a_packets)
	{
		const auto found = std::find_if(a_packets.begin(), a_packets.end(), [](const Json& packet) {
			return packet.value("type", std::string{}) == "sessionEnded";
		});
		if (found == a_packets.end()) {
			return std::nullopt;
		}
		return found->value("code", std::string{});
	}

	class VoiceClient
	{
	public:
		VoiceClient(std::string_view a_host, std::uint16_t a_port, std::string a_profileId) :
			host_{ a_host },
			port_{ a_port },
			profileId_{ std::move(a_profileId) },
			buffer_(64 * 1024)
		{}

		VoiceClient(const VoiceClient&) = delete;
		VoiceClient& operator=(const VoiceClient&) = delete;

		~VoiceClient()
		{
			if (handle_ != nullptr) {
				(void)co_gns_client_disconnect(handle_, 0, "voice flood smoke complete");
				std::this_thread::sleep_for(std::chrono::milliseconds(150));
				co_gns_client_destroy(handle_);
			}
		}

		void Connect()
		{
			std::array<char, 512> error{};
			const std::string host{ host_ };
			if (co_gns_client_create(host.c_str(), port_, &handle_, error.data(), error.size()) == 0 || handle_ == nullptr) {
				throw std::runtime_error("GNS client create failed: " + std::string{ error.data() });
			}
			Require(WaitForType("welcome", std::chrono::seconds(5)).has_value(), "client did not receive welcome");
			Require(SendReliable(Hello(profileId_)), "failed to send identity hello");
			const auto ready = WaitForType("sessionReady", std::chrono::seconds(5));
			Require(ready.has_value(), "identity hello did not produce sessionReady");
			Require(SendReliable(Json{
				{ "type", "keepAlive" },
				{ "sessionReadyAck", true },
				{ "resumeTokenAck", ready->value("resumeToken", std::string{}) }
			}), "failed to send sessionReady acknowledgment");
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}

		bool Send(const Bytes& a_datagram)
		{
			return co_gns_client_send(
				handle_,
				a_datagram.data(),
				static_cast<std::uint32_t>(a_datagram.size()),
				CO_GNS_DELIVERY_UNRELIABLE_SEQUENCED) == CO_GNS_SEND_SENT;
		}

		bool Pump(std::vector<Json>& a_packets)
		{
			for (;;) {
				co_gns_event event{};
				const auto result = co_gns_client_poll(handle_, &event, buffer_.data(), static_cast<std::uint32_t>(buffer_.size()));
				if (result < 0 || event.type == CO_GNS_EVENT_DISCONNECTED) {
					return false;
				}
				if (result == 0) {
					return true;
				}
				if (event.type == CO_GNS_EVENT_MESSAGE && event.payload_size <= buffer_.size()) {
					auto packet = ParseJsonObject(buffer_.data(), event.payload_size);
					if (packet) {
						a_packets.push_back(std::move(*packet));
					}
				}
			}
		}

	private:
		bool SendReliable(const Json& a_packet)
		{
			const auto text = a_packet.dump();
			return co_gns_client_send(
				handle_,
				text.data(),
				static_cast<std::uint32_t>(text.size()),
				CO_GNS_DELIVERY_RELIABLE_ORDERED) == CO_GNS_SEND_SENT;
		}

		std::optional<Json> WaitForType(std::string_view a_type, std::chrono::seconds a_timeout)
		{
			const auto deadline = Clock::now() + a_timeout;
			while (Clock::now() < deadline) {
				std::vector<Json> packets;
				const bool open = Pump(packets);
				for (auto& packet : packets) {
					if (packet.value("type", std::string{}) == a_type) {
						return packet;
					}
				}
				if (!open) {
					return std::nullopt;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			return std::nullopt;
		}

		std::string host_;
		std::uint16_t port_{};
		std::string profileId_;
		std::vector<std::uint8_t> buffer_;
		co_gns_client_handle handle_{};
	};

	struct Flow
	{
		VoiceClient& client;
		const Bytes& datagram;
		int perSecond{};
		std::uint64_t sent{};
		bool open{ true };
		std::vector<Json> packets;
	};

	void Drive(Flow& a_flow, Clock::time_point a_start)
	{
		if (!a_flow.open) {
			return;
		}
		const auto elapsed = std::chrono::duration<double>(Clock::now() - a_start).count();
		const auto target = static_cast<std::uint64_t>(elapsed * a_flow.perSecond);
		while (a_flow.sent < target) {
			if (!a_flow.client.Send(a_flow.datagram)) {
				(void)a_flow.client.Pump(a_flow.packets);
				a_flow.open = false;
				return;
			}
			++a_flow.sent;
		}
		a_flow.open = a_flow.client.Pump(a_flow.packets);
	}

	void RequireStayedConnected(Flow& a_flow, std::uint64_t a_minimumSent, const std::string& a_rate)
	{
		Require(a_flow.open, a_rate + " voice packets per second disconnected the client during the flood");
		Require(a_flow.sent >= a_minimumSent, a_rate + " voice packets per second client did not send five seconds of voice");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		a_flow.open = a_flow.client.Pump(a_flow.packets);
		Require(a_flow.open, a_rate + " voice packets per second disconnected the client after the flood");
		Require(!SessionEndedCode(a_flow.packets).has_value(), a_rate + " voice packets per second client received a session-ended packet");
	}

	void VoiceFloodIsMeteredPerConnection(std::string_view a_host, std::uint16_t a_port)
	{
		const auto datagram = VoiceDatagram();
		VoiceClient steadyClient{ a_host, a_port, "voice-flood-steady" };
		VoiceClient pulseClient{ a_host, a_port, "voice-flood-pulse" };
		VoiceClient abuserClient{ a_host, a_port, "voice-flood-abuser" };
		steadyClient.Connect();
		pulseClient.Connect();
		abuserClient.Connect();

		Flow steady{ steadyClient, datagram, 50 };
		Flow pulse{ pulseClient, datagram, 130 };
		Flow abuser{ abuserClient, datagram, 200 };
		const auto start = Clock::now();
		while (Clock::now() - start < std::chrono::seconds(5)) {
			Drive(steady, start);
			Drive(pulse, start);
			Drive(abuser, start);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		RequireStayedConnected(steady, 240, "50");
		RequireStayedConnected(pulse, 600, "130");

		Require(!abuser.open, "200 voice packets per second were not disconnected within 5 seconds");
		const auto code = SessionEndedCode(abuser.packets);
		Require(code.has_value() && *code == "rate_limited",
			"voice flood disconnect did not carry session-ended code rate_limited; received code='" + code.value_or("<none>") + "'");

		std::cout << "PASS voice flood: 50 and 130 voice packets per second stayed connected; 200 per second were disconnected with rate_limited\n";
	}
}

int main(int argc, char** argv)
{
	if (argc != 3) {
		std::cerr << "Usage: CommonwealthOnlineServerLiveVoiceFloodGnsSmoke HOST PORT\n";
		return 2;
	}
	std::uint16_t port = 0;
	if (!ParsePort(argv[2], port)) {
		std::cerr << "Invalid port\n";
		return 2;
	}

	try {
		VoiceFloodIsMeteredPerConnection(argv[1], port);
	} catch (const std::exception& error) {
		std::cerr << "FAIL voice flood: " << error.what() << '\n';
		return 1;
	}

	std::cout << "C++ GNS voice flood smoke passed\n";
	return 0;
}

#include "co_gns_server_bridge.h"

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace
{
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

	std::optional<Json> DecodePacket(std::span<const std::uint8_t> a_payload)
	{
		if (a_payload.empty()) {
			return std::nullopt;
		}
		try {
			auto packet = Json::parse(std::string{
				reinterpret_cast<const char*>(a_payload.data()),
				a_payload.size()
			});
			return packet.is_object() ? std::optional{ std::move(packet) } : std::nullopt;
		} catch (...) {
			return std::nullopt;
		}
	}

	class Client
	{
	public:
		Client(std::string_view a_host, std::uint16_t a_port) :
			// Identity is mandatory; the rejected third client must still reach admission.
			profileId_{ "admission-smoke-" + std::to_string(NextProfile()) }
		{
			std::array<char, 512> error{};
			const std::string host{ a_host };
			if (co_gns_client_create(host.c_str(), a_port, &handle_, error.data(), error.size()) == 0 || handle_ == nullptr) {
				throw std::runtime_error("GNS client create failed: " + std::string{ error.data() });
			}
			try {
				Json welcome;
				Require(ReceiveUntil([](const Json& packet) {
					return packet.value("type", std::string{}) == "welcome";
				}, welcome, std::chrono::seconds(5)), "client did not receive welcome");
				playerId_ = welcome.value("playerId", 0u);
				Require(playerId_ != 0, "welcome did not assign a player id");
			} catch (...) {
				co_gns_client_destroy(handle_);
				handle_ = nullptr;
				throw;
			}
		}

		Client(const Client&) = delete;
		Client& operator=(const Client&) = delete;

		~Client()
		{
			if (handle_ != nullptr) {
				(void)co_gns_client_disconnect(handle_, 0, "admission acceptance complete");
				std::this_thread::sleep_for(std::chrono::milliseconds(75));
				co_gns_client_destroy(handle_);
			}
		}

		[[nodiscard]] std::uint32_t PlayerId() const noexcept
		{
			return playerId_;
		}

		bool Send(const Json& a_packet)
		{
			const auto text = a_packet.dump();
			return co_gns_client_send(
				handle_,
				text.data(),
				static_cast<std::uint32_t>(text.size()),
				CO_GNS_DELIVERY_RELIABLE_ORDERED) == CO_GNS_SEND_SENT;
		}

		[[nodiscard]] Json Hello() const
		{
			return Json{
				{ "type", "hello" },
				{ "protocolVersion", 2 },
				{ "clientVersion", "admission-smoke-1" },
				{ "falloutRuntimeVersion", "1.11.240" },
				{ "profileId", profileId_ },
				{ "contentManifestFingerprint", std::string(64, 'f') },
				{ "capabilities", Json::array({ "interest-v1", "player-state-v1", "save-attestation-v1" }) }
			};
		}

		bool Activate()
		{
			if (!Send(Hello())) {
				return false;
			}
			Json ready;
			if (!ReceiveUntil([&](const Json& packet) {
				return packet.value("type", std::string{}) == "sessionReady" &&
					packet.value("playerId", 0u) == playerId_;
			}, ready, std::chrono::seconds(5))) {
				return false;
			}
			if (ready.value("resumeAckRequired", false)) {
				const auto resumeToken = ready.value("resumeToken", std::string{});
				(void)Send(Json{
					{ "type", "keepAlive" },
					{ "sessionReadyAck", true },
					{ "resumeTokenAck", resumeToken }
				});
			}
			return true;
		}

		bool ReceiveUntil(const std::function<bool(const Json&)>& a_predicate, Json& a_output, std::chrono::milliseconds a_timeout)
		{
			std::array<std::uint8_t, 64 * 1024> payload{};
			const auto deadline = std::chrono::steady_clock::now() + a_timeout;
			while (std::chrono::steady_clock::now() < deadline) {
				co_gns_event event{};
				const auto result = co_gns_client_poll(handle_, &event, payload.data(), static_cast<std::uint32_t>(payload.size()));
				if (result < 0 || event.type == CO_GNS_EVENT_OVERSIZE_MESSAGE) {
					return false;
				}
				if (result == 0) {
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
					continue;
				}
				if (event.type == CO_GNS_EVENT_MESSAGE && event.payload_size > 0 && event.payload_size <= payload.size()) {
					const auto packet = DecodePacket(std::span<const std::uint8_t>{ payload.data(), event.payload_size });
					if (packet && a_predicate(*packet)) {
						a_output = *packet;
						return true;
					}
				}
				if (event.type == CO_GNS_EVENT_DISCONNECTED) {
					return false;
				}
			}
			return false;
		}

		bool ExpectSessionEnded(std::string_view a_code, std::chrono::milliseconds a_timeout)
		{
			Json ended;
			return ReceiveUntil([&](const Json& packet) {
				return packet.value("type", std::string{}) == "sessionEnded" &&
					packet.value("code", std::string{}) == a_code;
			}, ended, a_timeout);
		}

		static std::uint32_t NextProfile() noexcept
		{
			static std::atomic<std::uint32_t> nextProfile{ 1 };
			return nextProfile.fetch_add(1);
		}

	private:
		const std::string profileId_;
		co_gns_client_handle handle_{};
		std::uint32_t playerId_{};
	};
}

int main(int argc, char** argv)
{
	if (argc != 3) {
		std::cerr << "Usage: CommonwealthOnlineServerLiveAdmissionGnsSmoke HOST PORT\n";
		return 2;
	}
	std::uint16_t port = 0;
	if (!ParsePort(argv[2], port)) {
		std::cerr << "Invalid port\n";
		return 2;
	}

	try {
		Client first{ argv[1], port };
		Client second{ argv[1], port };
		Require(first.Activate(), "first client did not activate");
		Require(second.Activate(), "second client did not activate");
		Require(first.PlayerId() != second.PlayerId(), "active clients received duplicate player ids");

		Client rejected{ argv[1], port };
		Require(rejected.Send(rejected.Hello()),
			"third client could not send hello");
		Require(rejected.ExpectSessionEnded("server_full", std::chrono::seconds(5)),
			"third activation was not rejected as server_full");

		Require(first.Send(Json{ { "type", "playerState" }, { "characterName", "CapacitySurvivor" } }),
			"first active client could not send after capacity rejection");
		Json relay;
		Require(second.ReceiveUntil([&](const Json& packet) {
			return packet.value("type", std::string{}) == "playerState" &&
				packet.value("playerId", 0u) == first.PlayerId() &&
				packet.value("characterName", std::string{}) == "CapacitySurvivor";
		}, relay, std::chrono::seconds(3)), "existing active sessions were disrupted by server_full rejection");

		std::cout << "PASS admission: max_players rejects third activation and preserves active sessions\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << "FAIL native GNS admission acceptance: " << exception.what() << '\n';
		return 1;
	}
}

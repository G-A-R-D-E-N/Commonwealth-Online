#include "F4TGnsSnapshotEnvelope.h"
#include "co_gns_server_bridge.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using F4T::GnsSnapshotEnvelope::DecodeResult;
	using F4T::GnsSnapshotEnvelope::Family;

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

		F4T::GnsSnapshotEnvelope::Header header{};
		const auto decoded = F4T::GnsSnapshotEnvelope::Decode(a_payload, header);
		if (decoded == DecodeResult::Valid) {
			a_payload = a_payload.subspan(F4T::GnsSnapshotEnvelope::HeaderSize);
		} else if (decoded != DecodeResult::NotEnvelope) {
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
		Client(std::string_view a_host, std::uint16_t a_port)
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
				Require(playerId_ != 0, "welcome did not contain a player id");
				// Identity is mandatory; per-connection profile ids avoid duplicate_identity.
				static std::atomic<std::uint32_t> nextProfile{ 1 };
				const auto profileId =
					"protocol-fault-smoke-" + std::to_string(nextProfile.fetch_add(1));
				Require(SendReliable(Json{
					{ "type", "hello" },
					{ "protocolVersion", 2 },
					{ "clientVersion", "protocol-fault-smoke-1" },
					{ "falloutRuntimeVersion", "1.11.240" },
					{ "profileId", profileId },
					{ "contentManifestFingerprint", std::string(64, 'd') },
					{ "capabilities", Json::array({ "interest-v1", "player-state-v1", "save-attestation-v1" }) }
				}) == CO_GNS_SEND_SENT, "failed to send hello");
				Json ready;
				Require(ReceiveUntil([](const Json& packet) {
					return packet.value("type", std::string{}) == "sessionReady";
				}, ready, std::chrono::seconds(5)), "client did not receive sessionReady");
				Require(ready.value("playerId", 0u) == playerId_, "sessionReady player id did not match welcome");
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
				(void)co_gns_client_disconnect(handle_, 0, "protocol fault acceptance client complete");
				std::this_thread::sleep_for(std::chrono::milliseconds(75));
				co_gns_client_destroy(handle_);
			}
		}

		[[nodiscard]] std::uint32_t PlayerId() const noexcept
		{
			return playerId_;
		}

		int SendRaw(std::span<const std::uint8_t> a_payload, std::uint32_t a_delivery = CO_GNS_DELIVERY_RELIABLE_ORDERED)
		{
			return co_gns_client_send(
				handle_,
				a_payload.data(),
				static_cast<std::uint32_t>(a_payload.size()),
				a_delivery);
		}

		int SendReliable(const Json& a_packet)
		{
			const auto text = a_packet.dump();
			return SendRaw(std::span<const std::uint8_t>{
				reinterpret_cast<const std::uint8_t*>(text.data()),
				text.size()
			});
		}

		bool SendTransform(const Json& a_packet)
		{
			sequence_ = F4T::GnsSnapshotEnvelope::NextSequence(sequence_);
			const auto text = a_packet.dump();
			std::vector<std::uint8_t> payload(F4T::GnsSnapshotEnvelope::HeaderSize + text.size());
			if (!F4T::GnsSnapshotEnvelope::WriteHeader(std::span<std::uint8_t>{ payload }, Family::Transform, sequence_)) {
				return false;
			}
			std::copy(text.begin(), text.end(), payload.begin() + static_cast<std::ptrdiff_t>(F4T::GnsSnapshotEnvelope::HeaderSize));
			return SendRaw(payload, CO_GNS_DELIVERY_UNRELIABLE_SEQUENCED) == CO_GNS_SEND_SENT;
		}

		bool ReceiveUntil(const std::function<bool(const Json&)>& a_predicate, Json& a_output, std::chrono::milliseconds a_timeout)
		{
			std::array<std::uint8_t, 64 * 1024> payload{};
			const auto deadline = std::chrono::steady_clock::now() + a_timeout;
			while (std::chrono::steady_clock::now() < deadline) {
				co_gns_event event{};
				const auto result = co_gns_client_poll(handle_, &event, payload.data(), static_cast<std::uint32_t>(payload.size()));
				if (result < 0 || event.type == CO_GNS_EVENT_DISCONNECTED || event.type == CO_GNS_EVENT_OVERSIZE_MESSAGE) {
					return false;
				}
				if (result == 0 || event.type != CO_GNS_EVENT_MESSAGE || event.payload_size == 0 || event.payload_size > payload.size()) {
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
					continue;
				}
				const auto packet = DecodePacket(std::span<const std::uint8_t>{ payload.data(), event.payload_size });
				if (packet && a_predicate(*packet)) {
					a_output = *packet;
					return true;
				}
			}
			return false;
		}

		std::vector<Json> Collect(std::chrono::milliseconds a_window)
		{
			std::vector<Json> packets;
			std::array<std::uint8_t, 64 * 1024> payload{};
			const auto deadline = std::chrono::steady_clock::now() + a_window;
			while (std::chrono::steady_clock::now() < deadline) {
				co_gns_event event{};
				const auto result = co_gns_client_poll(handle_, &event, payload.data(), static_cast<std::uint32_t>(payload.size()));
				if (result < 0 || event.type == CO_GNS_EVENT_DISCONNECTED || event.type == CO_GNS_EVENT_OVERSIZE_MESSAGE) {
					break;
				}
				if (result == 0 || event.type != CO_GNS_EVENT_MESSAGE || event.payload_size == 0 || event.payload_size > payload.size()) {
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
					continue;
				}
				const auto packet = DecodePacket(std::span<const std::uint8_t>{ payload.data(), event.payload_size });
				if (packet) {
					packets.push_back(*packet);
				}
			}
			return packets;
		}

		bool WaitForRateLimitDisconnect(std::chrono::milliseconds a_timeout)
		{
			std::array<std::uint8_t, 64 * 1024> payload{};
			const auto deadline = std::chrono::steady_clock::now() + a_timeout;
			bool sawRateLimited = false;
			while (std::chrono::steady_clock::now() < deadline) {
				co_gns_event event{};
				const auto result = co_gns_client_poll(handle_, &event, payload.data(), static_cast<std::uint32_t>(payload.size()));
				if (result < 0) {
					return false;
				}
				if (result == 0) {
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
					continue;
				}
				if (event.type == CO_GNS_EVENT_MESSAGE && event.payload_size > 0 && event.payload_size <= payload.size()) {
					const auto packet = DecodePacket(std::span<const std::uint8_t>{ payload.data(), event.payload_size });
					if (packet && packet->value("type", std::string{}) == "sessionEnded" &&
						packet->value("code", std::string{}) == "rate_limited") {
						sawRateLimited = true;
					}
				}
				if (event.type == CO_GNS_EVENT_DISCONNECTED) {
					return sawRateLimited;
				}
			}
			return false;
		}

	private:
		co_gns_client_handle handle_{};
		std::uint32_t playerId_{};
		std::uint32_t sequence_{};
	};

	Json Transform(double a_x, std::string_view a_movementType)
	{
		return Json{
			{ "type", "transform" },
			{ "x", a_x },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 },
			{ "cellId", "0000F100" },
			{ "worldspaceId", "0000F200" },
			{ "movementType", a_movementType }
		};
	}

	void MalformedAndOversizeAreContained(std::string_view a_host, std::uint16_t a_port)
	{
		Client sender{ a_host, a_port };
		Client peer{ a_host, a_port };
		Require(sender.SendTransform(Transform(0.0, "spawn")), "failed to establish sender scope");
		Require(peer.SendTransform(Transform(0.0, "spawn")), "failed to establish peer scope");
		(void)sender.Collect(std::chrono::milliseconds(200));
		(void)peer.Collect(std::chrono::milliseconds(200));

		const std::string malformed = "{this-is-not-json";
		Require(sender.SendRaw(std::span<const std::uint8_t>{
			reinterpret_cast<const std::uint8_t*>(malformed.data()), malformed.size() }) == CO_GNS_SEND_SENT,
			"bridge did not accept malformed payload for server-side validation");
		const auto afterMalformed = peer.Collect(std::chrono::milliseconds(300));
		const auto malformedMutation = std::any_of(afterMalformed.begin(), afterMalformed.end(), [&](const Json& packet) {
			return packet.value("playerId", 0u) == sender.PlayerId() &&
				(packet.value("type", std::string{}) == "transform" || packet.value("type", std::string{}) == "playerState");
		});
		Require(!malformedMutation, "malformed payload produced gameplay relay");

		Require(sender.SendReliable(Json{
			{ "type", "playerState" },
			{ "characterName", "RecoveredAfterMalformed" }
		}) == CO_GNS_SEND_SENT, "client could not send after malformed payload");
		Json recovered;
		Require(peer.ReceiveUntil([&](const Json& packet) {
			return packet.value("type", std::string{}) == "playerState" &&
				packet.value("playerId", 0u) == sender.PlayerId() &&
				packet.value("characterName", std::string{}) == "RecoveredAfterMalformed";
		}, recovered, std::chrono::seconds(3)), "connection did not remain usable after malformed payload");

		std::vector<std::uint8_t> oversized((64uz * 1024uz) + 1uz, static_cast<std::uint8_t>('x'));
		Require(sender.SendRaw(oversized) == CO_GNS_SEND_TOO_LARGE,
			"oversized payload was not rejected by the 64 KiB GNS bridge boundary");
		Require(sender.SendReliable(Json{
			{ "type", "playerState" },
			{ "characterName", "RecoveredAfterOversize" }
		}) == CO_GNS_SEND_SENT, "client could not send after oversized payload rejection");
		Require(peer.ReceiveUntil([&](const Json& packet) {
			return packet.value("type", std::string{}) == "playerState" &&
				packet.value("playerId", 0u) == sender.PlayerId() &&
				packet.value("characterName", std::string{}) == "RecoveredAfterOversize";
		}, recovered, std::chrono::seconds(3)), "connection did not remain usable after oversize rejection");
	}

	void RateLimitDisconnectsAfterThreeViolationWindows(std::string_view a_host, std::uint16_t a_port)
	{
		Client client{ a_host, a_port };
		const Json packet{ { "type", "playerState" }, { "characterName", "RateBurst" } };

		for (int window = 0; window < 3; ++window) {
			bool disconnectedWhileSending = false;
			for (int index = 0; index < 140; ++index) {
				const auto result = client.SendReliable(packet);
				if (result == CO_GNS_SEND_NOT_CONNECTED) {
					disconnectedWhileSending = true;
					break;
				}
				Require(result == CO_GNS_SEND_SENT, "rate-limit burst hit unexpected GNS send result");
			}

			if (window < 2) {
				Require(!disconnectedWhileSending, "rate limiter disconnected before third violation window");
				std::this_thread::sleep_for(std::chrono::milliseconds(1150));
			} else {
				Require(client.WaitForRateLimitDisconnect(std::chrono::seconds(5)),
					"third packet-rate violation did not produce rate_limited disconnect");
			}
		}
	}
}

int main(int argc, char** argv)
{
	if (argc != 3) {
		std::cerr << "Usage: CommonwealthOnlineServerLiveProtocolFaultGnsSmoke HOST PORT\n";
		return 2;
	}
	std::uint16_t port = 0;
	if (!ParsePort(argv[2], port)) {
		std::cerr << "Invalid port\n";
		return 2;
	}
	const std::string_view host{ argv[1] };

	try {
		MalformedAndOversizeAreContained(host, port);
		std::cout << "PASS protocol faults: malformed and oversized payloads contained\n";
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		RateLimitDisconnectsAfterThreeViolationWindows(host, port);
		std::cout << "PASS rate limit: third violating window disconnects client\n";
		std::cout << "native GNS protocol-fault acceptance passed\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << "FAIL native GNS protocol-fault acceptance: " << exception.what() << '\n';
		return 1;
	}
}

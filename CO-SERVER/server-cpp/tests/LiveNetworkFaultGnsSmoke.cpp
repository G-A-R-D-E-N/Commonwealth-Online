#include "F4TGnsSnapshotEnvelope.h"
#include "co_gns_server_bridge.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
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

	class NetworkConditions
	{
	public:
		NetworkConditions()
		{
			Require(Set(0.0f, 0.0f), "failed to reset fake network conditions");
		}

		NetworkConditions(const NetworkConditions&) = delete;
		NetworkConditions& operator=(const NetworkConditions&) = delete;

		~NetworkConditions()
		{
			(void)co_gns_test_set_network_conditions(0.0f, 0.0f);
		}

		bool Set(float a_loss, float a_reorder)
		{
			if (co_gns_test_set_network_conditions(a_loss, a_reorder) != 1) {
				return false;
			}
			float observedLoss = -1.0f;
			float observedReorder = -1.0f;
			if (co_gns_test_get_network_conditions(&observedLoss, &observedReorder) != 1) {
				return false;
			}
			return std::abs(observedLoss - a_loss) < 0.001f && std::abs(observedReorder - a_reorder) < 0.001f;
		}
	};

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
				Require(ReceiveUntil([](const Json& packet) { return packet.value("type", std::string{}) == "welcome"; }, welcome, std::chrono::seconds(5)),
					"client did not receive welcome");
				playerId_ = welcome.value("playerId", 0u);
				Require(playerId_ != 0, "welcome did not contain a player id");
				// Identity is mandatory; per-connection profile ids avoid duplicate_identity.
				static std::atomic<std::uint32_t> nextProfile{ 1 };
				const auto profileId =
					"network-fault-smoke-" + std::to_string(nextProfile.fetch_add(1));
				Require(SendReliable(Json{
					{ "type", "hello" },
					{ "protocolVersion", 2 },
					{ "clientVersion", "network-fault-smoke-1" },
					{ "falloutRuntimeVersion", "1.11.240" },
					{ "profileId", profileId },
					{ "contentManifestFingerprint", std::string(64, 'b') },
					{ "capabilities", Json::array({ "interest-v1", "player-state-v1", "save-attestation-v1", "social-names-v1" }) }
				}), "failed to send hello");
				Json ready;
				Require(ReceiveUntil([](const Json& packet) { return packet.value("type", std::string{}) == "sessionReady"; }, ready, std::chrono::seconds(5)),
					"client did not receive sessionReady");
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
				(void)co_gns_client_disconnect(handle_, 0, "fault acceptance client complete");
				std::this_thread::sleep_for(std::chrono::milliseconds(75));
				co_gns_client_destroy(handle_);
			}
		}

		[[nodiscard]] std::uint32_t PlayerId() const noexcept
		{
			return playerId_;
		}

		bool SendReliable(const Json& a_packet)
		{
			const auto text = a_packet.dump();
			return co_gns_client_send(
				handle_,
				text.data(),
				static_cast<std::uint32_t>(text.size()),
				CO_GNS_DELIVERY_RELIABLE_ORDERED) == CO_GNS_SEND_SENT;
		}

		bool SendSnapshot(const Json& a_packet, std::uint32_t a_sequence)
		{
			const auto text = a_packet.dump();
			std::vector<std::uint8_t> payload(F4T::GnsSnapshotEnvelope::HeaderSize + text.size());
			if (!F4T::GnsSnapshotEnvelope::WriteHeader(std::span<std::uint8_t>{ payload }, Family::Transform, a_sequence)) {
				return false;
			}
			std::copy(text.begin(), text.end(), payload.begin() + static_cast<std::ptrdiff_t>(F4T::GnsSnapshotEnvelope::HeaderSize));
			return co_gns_client_send(
				handle_,
				payload.data(),
				static_cast<std::uint32_t>(payload.size()),
				CO_GNS_DELIVERY_UNRELIABLE_SEQUENCED) == CO_GNS_SEND_SENT;
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

	private:
		co_gns_client_handle handle_{};
		std::uint32_t playerId_{};
	};

	Json Transform(double a_x)
	{
		return Json{
			{ "type", "transform" },
			{ "x", a_x },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 },
			{ "cellId", "00001111" },
			{ "worldspaceId", "00002222" },
			{ "movementType", "normal" }
		};
	}

	void EstablishSharedScope(Client& a_a, Client& a_b)
	{
		auto firstA = Transform(0.0);
		firstA["movementType"] = "spawn";
		auto firstB = Transform(0.0);
		firstB["movementType"] = "spawn";
		Require(a_a.SendSnapshot(firstA, 1), "failed to send sender spawn");
		Require(a_b.SendSnapshot(firstB, 1), "failed to send recipient spawn");
		(void)a_a.Collect(std::chrono::milliseconds(200));
		(void)a_b.Collect(std::chrono::milliseconds(200));
	}

	void ReliableRetransmitsButSnapshotDoesNot(std::string_view a_host, std::uint16_t a_port)
	{
		Client a{ a_host, a_port };
		Client b{ a_host, a_port };
		EstablishSharedScope(a, b);
		NetworkConditions network;
		Require(network.Set(100.0f, 0.0f), "failed to enable 100% fake packet loss");

		Require(a.SendReliable(Json{
			{ "type", "playerState" },
			{ "characterName", "ReliableAfterLoss" }
		}), "failed to queue reliable player state under loss");
		Require(a.SendSnapshot(Transform(1.0), 2), "failed to send unreliable snapshot under loss");

		const auto duringLoss = b.Collect(std::chrono::milliseconds(300));
		const auto leakedReliable = std::any_of(duringLoss.begin(), duringLoss.end(), [&](const Json& packet) {
			return packet.value("type", std::string{}) == "playerState" &&
				packet.value("playerId", 0u) == a.PlayerId() &&
				packet.value("displayName", std::string{}) == "Stranger" &&
				!packet.contains("characterName");
		});
		const auto leakedSnapshot = std::any_of(duringLoss.begin(), duringLoss.end(), [&](const Json& packet) {
			return packet.value("type", std::string{}) == "transform" &&
				packet.value("playerId", 0u) == a.PlayerId() &&
				std::abs(packet.value("x", -1.0) - 1.0) < 0.001;
		});
		Require(!leakedReliable && !leakedSnapshot, "traffic crossed a 100% fake-loss window");

		Require(network.Set(0.0f, 0.0f), "failed to restore normal network conditions");
		Json reliable;
		Require(b.ReceiveUntil([&](const Json& packet) {
			return packet.value("type", std::string{}) == "playerState" &&
				packet.value("playerId", 0u) == a.PlayerId() &&
				packet.value("displayName", std::string{}) == "Stranger" &&
				!packet.contains("characterName");
		}, reliable, std::chrono::seconds(5)), "reliable player state did not retransmit after packet loss ended");
		// Constraint: distinct smoke profiles are strangers, so recovery proves delivery while the mask shapes the name.
		Require(reliable.value("displayName", std::string{}) == "Stranger", "retransmitted player state was not masked for a stranger");
		Require(!reliable.contains("characterName"), "stranger character name leaked after retransmit");

		const auto afterRecovery = b.Collect(std::chrono::milliseconds(500));
		const auto snapshotRetransmitted = std::any_of(afterRecovery.begin(), afterRecovery.end(), [&](const Json& packet) {
			return packet.value("type", std::string{}) == "transform" &&
				packet.value("playerId", 0u) == a.PlayerId() &&
				std::abs(packet.value("x", -1.0) - 1.0) < 0.001;
		});
		Require(!snapshotRetransmitted, "unreliable snapshot was retransmitted after a 100% loss window");
	}

	void ReliableOrderAndSnapshotMonotonicitySurviveReorder(std::string_view a_host, std::uint16_t a_port)
	{
		Client a{ a_host, a_port };
		Client b{ a_host, a_port };
		EstablishSharedScope(a, b);
		NetworkConditions network;
		Require(network.Set(0.0f, 100.0f), "failed to enable 100% fake packet reordering");

		// Constraint: ordering is proven by the preserved concept-level equipment marker.
		Require(a.SendReliable(Json{ { "type", "playerState" },
			{ "characterName", "ReliableOrder1" },
			{ "equippedItems", Json::array({ Json{ { "slot", "rightHand" }, { "formId", "00000001" } } }) } }), "failed to send first reliable state");
		Require(a.SendReliable(Json{ { "type", "playerState" },
			{ "characterName", "ReliableOrder2" },
			{ "equippedItems", Json::array({ Json{ { "slot", "rightHand" }, { "formId", "00000002" } } }) } }), "failed to send second reliable state");
		Require(a.SendSnapshot(Transform(1.0), 2), "failed to send first reordered snapshot");
		Require(a.SendSnapshot(Transform(2.0), 3), "failed to send second reordered snapshot");
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		Require(network.Set(0.0f, 0.0f), "failed to restore network conditions after reorder test");

		std::vector<std::string> reliableMarkers;
		std::vector<double> transformX;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (std::chrono::steady_clock::now() < deadline && (reliableMarkers.size() < 2 || transformX.empty() || transformX.back() < 1.999)) {
			for (const auto& packet : b.Collect(std::chrono::milliseconds(100))) {
				if (packet.value("playerId", 0u) != a.PlayerId()) {
					continue;
				}
				if (packet.value("type", std::string{}) == "playerState") {
					if (packet.value("displayName", std::string{}) != "Stranger" || packet.contains("characterName")) {
						continue;
					}
					const auto& items = packet.value("equippedItems", Json::array());
					if (!items.empty() && items.at(0).is_object()) {
						reliableMarkers.push_back(items.at(0).value("formId", std::string{}));
					}
				} else if (packet.value("type", std::string{}) == "transform") {
					const auto x = packet.value("x", -1.0);
					if (std::abs(x - 1.0) < 0.001 || std::abs(x - 2.0) < 0.001) {
						transformX.push_back(x);
					}
				}
			}
		}

		Require(reliableMarkers.size() >= 2, "did not receive both reliable messages after forced reordering");
		Require(reliableMarkers[0] == "00000001" && reliableMarkers[1] == "00000002",
			"reliable ordered messages arrived out of order");
		Require(!transformX.empty() && std::abs(transformX.back() - 2.0) < 0.001,
			"latest sequenced snapshot did not converge to the newest transform");
		bool sawNewest = false;
		for (const auto x : transformX) {
			if (std::abs(x - 2.0) < 0.001) {
				sawNewest = true;
			}
			if (sawNewest) {
				Require(std::abs(x - 1.0) >= 0.001, "sequenced snapshot state regressed after newer state arrived");
			}
		}
	}
}

int main(int argc, char** argv)
{
	if (argc != 3) {
		std::cerr << "Usage: CommonwealthOnlineServerLiveNetworkFaultGnsSmoke HOST PORT\n";
		return 2;
	}
	std::uint16_t port = 0;
	if (!ParsePort(argv[2], port)) {
		std::cerr << "Invalid port\n";
		return 2;
	}
	const std::string_view host{ argv[1] };

	try {
		ReliableRetransmitsButSnapshotDoesNot(host, port);
		std::cout << "PASS loss: reliable retransmits while unreliable snapshot does not\n";
		ReliableOrderAndSnapshotMonotonicitySurviveReorder(host, port);
		std::cout << "PASS reorder: reliable ordering and sequenced snapshot monotonicity preserved\n";
		std::cout << "native GNS network-fault acceptance passed\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << "FAIL native GNS network-fault acceptance: " << exception.what() << '\n';
		(void)co_gns_test_set_network_conditions(0.0f, 0.0f);
		return 1;
	}
}

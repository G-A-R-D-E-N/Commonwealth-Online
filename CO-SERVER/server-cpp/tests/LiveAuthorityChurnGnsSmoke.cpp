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
#include <memory>
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
					"authority-churn-smoke-" + std::to_string(nextProfile.fetch_add(1));
				Require(SendReliable(Json{
					{ "type", "hello" },
					{ "protocolVersion", 2 },
					{ "clientVersion", "authority-churn-smoke-1" },
					{ "falloutRuntimeVersion", "1.11.240" },
					{ "profileId", profileId },
					{ "contentManifestFingerprint", std::string(64, 'e') },
					{ "capabilities", Json::array({ "interest-v1", "player-state-v1", "save-attestation-v1" }) }
				}), "failed to send hello");
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
			Disconnect();
		}

		void Disconnect()
		{
			if (handle_ == nullptr) {
				return;
			}
			(void)co_gns_client_disconnect(handle_, 0, "authority churn client complete");
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			co_gns_client_destroy(handle_);
			handle_ = nullptr;
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

		bool SendSnapshot(const Json& a_packet, Family a_family)
		{
			auto& sequence = a_family == Family::Transform ? transformSequence_ : npcSequence_;
			sequence = F4T::GnsSnapshotEnvelope::NextSequence(sequence);
			const auto text = a_packet.dump();
			std::vector<std::uint8_t> payload(F4T::GnsSnapshotEnvelope::HeaderSize + text.size());
			if (!F4T::GnsSnapshotEnvelope::WriteHeader(std::span<std::uint8_t>{ payload }, a_family, sequence)) {
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
		std::uint32_t transformSequence_{};
		std::uint32_t npcSequence_{};
	};

	Json Transform(double a_x = 0.0)
	{
		return Json{
			{ "type", "transform" },
			{ "x", a_x },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 },
			{ "cellId", "0000A700" },
			{ "worldspaceId", "0000B700" },
			{ "movementType", "spawn" }
		};
	}

	// Each submission is tagged by the NPC identity it carries rather than by a
	// custom field. Relays are rebuilt from an allow-list, so an extra field
	// would not survive to be recognized on the way back out.
	constexpr std::string_view kEpoch1Npc = "0001A701";
	constexpr std::string_view kStaleEpoch1Npc = "0001A7F1";
	constexpr std::string_view kEpoch2Npc = "0001A702";
	constexpr std::string_view kStaleEpoch2Npc = "0001A7F2";
	constexpr std::string_view kEpoch3Npc = "0001A703";

	Json NpcState(std::uint32_t a_epoch, std::string_view a_npcFormId)
	{
		return Json{
			{ "type", "npcState" },
			{ "authorityEpoch", a_epoch },
			{ "authorityCellId", "0000A700" },
			{ "authorityWorldspaceId", "0000B700" },
			{ "npcs", Json::array({ Json{
				{ "sourceFormId", a_npcFormId },
				{ "cellId", "0000A700" },
				{ "worldspaceId", "0000B700" },
				{ "x", 100.0 },
				{ "y", 0.0 },
				{ "z", 0.0 },
				{ "angleZ", 0.0 }
			} }) }
		};
	}

	bool CarriesNpc(const Json& a_packet, std::uint32_t a_playerId, std::string_view a_npcFormId)
	{
		return a_packet.value("type", std::string{}) == "npcState" &&
			a_packet.value("playerId", 0u) == a_playerId &&
			a_packet.contains("npcs") && a_packet.at("npcs").is_array() && !a_packet.at("npcs").empty() &&
			a_packet.at("npcs").at(0).value("sourceFormId", std::string{}) == a_npcFormId;
	}

	std::uint32_t WaitForAuthority(Client& a_client, std::uint32_t a_expectedPlayerId, std::uint32_t a_minimumEpoch)
	{
		Json grant;
		Require(a_client.ReceiveUntil([&](const Json& packet) {
			return packet.value("type", std::string{}) == "npcAuthority" &&
				packet.value("authorityPlayerId", 0u) == a_expectedPlayerId &&
				packet.value("authorityCellId", std::string{}) == "0000A700" &&
				packet.value("authorityWorldspaceId", std::string{}) == "0000B700" &&
				packet.value("authorityEpoch", 0u) >= a_minimumEpoch;
		}, grant, std::chrono::seconds(4)), "expected NPC authority assignment was not observed");
		return grant.value("authorityEpoch", 0u);
	}

	bool SawNpcStateFrom(const std::vector<Json>& a_packets, std::uint32_t a_playerId, std::string_view a_npcFormId)
	{
		return std::any_of(a_packets.begin(), a_packets.end(), [&](const Json& packet) {
			return CarriesNpc(packet, a_playerId, a_npcFormId);
		});
	}

	void AuthorityHandoffClearsStaleEpochAndCache(std::string_view a_host, std::uint16_t a_port)
	{
		auto a = std::make_unique<Client>(a_host, a_port);
		auto b = std::make_unique<Client>(a_host, a_port);
		auto c = std::make_unique<Client>(a_host, a_port);
		Require(a->PlayerId() < b->PlayerId() && b->PlayerId() < c->PlayerId(), "player ids were not assigned monotonically");

		Require(a->SendSnapshot(Transform(), Family::Transform), "failed to place player A in scope");
		const auto epoch1 = WaitForAuthority(*a, a->PlayerId(), 1);
		Require(epoch1 == 1, "initial authority epoch was not 1");
		Require(b->SendSnapshot(Transform(10.0), Family::Transform), "failed to place player B in scope");
		Require(c->SendSnapshot(Transform(20.0), Family::Transform), "failed to place player C in scope");
		(void)a->Collect(std::chrono::milliseconds(200));
		(void)b->Collect(std::chrono::milliseconds(200));
		(void)c->Collect(std::chrono::milliseconds(200));

		Require(a->SendSnapshot(NpcState(epoch1, kEpoch1Npc), Family::NpcState), "authority A failed to send epoch1 NPC state");
		Json epoch1Relay;
		Require(c->ReceiveUntil([&](const Json& packet) {
			return CarriesNpc(packet, a->PlayerId(), kEpoch1Npc);
		}, epoch1Relay, std::chrono::seconds(3)), "epoch1 NPC state was not relayed");

		const auto aPlayerId = a->PlayerId();
		a.reset();
		const auto epoch2 = WaitForAuthority(*b, b->PlayerId(), epoch1 + 1);
		Require(epoch2 == epoch1 + 1, "first handoff did not increment authority epoch");
		(void)c->Collect(std::chrono::milliseconds(150));

		Require(b->SendSnapshot(NpcState(epoch1, kStaleEpoch1Npc), Family::NpcState), "new authority could not submit stale-epoch probe");
		Require(!SawNpcStateFrom(c->Collect(std::chrono::milliseconds(350)), b->PlayerId(), kStaleEpoch1Npc),
			"stale authority epoch was accepted after handoff");

		Require(b->SendSnapshot(NpcState(epoch2, kEpoch2Npc), Family::NpcState), "authority B failed to send epoch2 NPC state");
		Json epoch2Relay;
		Require(c->ReceiveUntil([&](const Json& packet) {
			return CarriesNpc(packet, b->PlayerId(), kEpoch2Npc);
		}, epoch2Relay, std::chrono::seconds(3)), "epoch2 NPC state was not relayed");

		auto d = std::make_unique<Client>(a_host, a_port);
		Require(d->SendSnapshot(Transform(30.0), Family::Transform), "failed to place late joiner D in scope");
		Json replay;
		Require(d->ReceiveUntil([&](const Json& packet) {
			return CarriesNpc(packet, b->PlayerId(), kEpoch2Npc) && packet.value("fullReplace", false);
		}, replay, std::chrono::seconds(3)), "late joiner did not receive current epoch2 cached NPC state");

		const auto bPlayerId = b->PlayerId();
		b.reset();
		const auto epoch3 = WaitForAuthority(*c, c->PlayerId(), epoch2 + 1);
		Require(epoch3 == epoch2 + 1, "second handoff did not increment authority epoch");
		(void)d->Collect(std::chrono::milliseconds(150));

		auto e = std::make_unique<Client>(a_host, a_port);
		Require(e->SendSnapshot(Transform(40.0), Family::Transform), "failed to place late joiner E in scope");
		const auto beforeEpoch3 = e->Collect(std::chrono::milliseconds(400));
		Require(!SawNpcStateFrom(beforeEpoch3, bPlayerId, kEpoch2Npc),
			"stale cached epoch2 NPC state survived authority handoff");
		Require(!SawNpcStateFrom(beforeEpoch3, aPlayerId, kEpoch1Npc),
			"stale cached epoch1 NPC state survived authority handoff");

		Require(c->SendSnapshot(NpcState(epoch2, kStaleEpoch2Npc), Family::NpcState), "authority C could not submit stale-epoch probe");
		Require(!SawNpcStateFrom(e->Collect(std::chrono::milliseconds(350)), c->PlayerId(), kStaleEpoch2Npc),
			"previous authority epoch was accepted after second handoff");

		Require(c->SendSnapshot(NpcState(epoch3, kEpoch3Npc), Family::NpcState), "authority C failed to send epoch3 NPC state");
		Json epoch3Relay;
		Require(e->ReceiveUntil([&](const Json& packet) {
			return CarriesNpc(packet, c->PlayerId(), kEpoch3Npc);
		}, epoch3Relay, std::chrono::seconds(3)), "epoch3 NPC state was not relayed");
	}
}

int main(int argc, char** argv)
{
	if (argc != 3) {
		std::cerr << "Usage: CommonwealthOnlineServerLiveAuthorityChurnGnsSmoke HOST PORT\n";
		return 2;
	}
	std::uint16_t port = 0;
	if (!ParsePort(argv[2], port)) {
		std::cerr << "Invalid port\n";
		return 2;
	}

	try {
		AuthorityHandoffClearsStaleEpochAndCache(argv[1], port);
		std::cout << "PASS authority churn: deterministic handoff, stale epoch rejection, and cache invalidation\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << "FAIL native GNS authority churn acceptance: " << exception.what() << '\n';
		return 1;
	}
}

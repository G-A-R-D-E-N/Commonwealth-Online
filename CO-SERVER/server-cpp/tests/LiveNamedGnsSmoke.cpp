#include "F4TConnectionPolicy.h"
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
#include <deque>
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

	// The server admits a bounded number of connection attempts per source address
	// in a sliding window, and a refused attempt still spends budget, so retrying
	// only digs deeper. Every scenario opens its own connections - more than one
	// window can hold - so the harness paces itself rather than asking the
	// production limiter to relax for a test. The bounds come from the shared
	// policy header so tightening the limiter cannot silently strand this suite.
	class ConnectBudget
	{
	public:
		static void Take()
		{
			auto& recent = Recent();
			Prune(recent);
			if (recent.size() >= kMaximumAttempts) {
				const auto until = recent.front() + kWindow + kSlack;
				std::this_thread::sleep_for(until - std::chrono::steady_clock::now());
				Prune(recent);
			}
			recent.push_back(std::chrono::steady_clock::now());
		}

	private:
		static constexpr auto kWindow =
			std::chrono::duration_cast<std::chrono::steady_clock::duration>(
				std::chrono::duration<double>{ F4T::ConnectionPolicy::DefaultConnectAttemptWindowSeconds });
		static constexpr std::size_t kMaximumAttempts =
			static_cast<std::size_t>(F4T::ConnectionPolicy::DefaultMaximumConnectAttempts);
		// The server stamps the attempt when it arrives, not when we call connect.
		static constexpr auto kSlack = std::chrono::milliseconds(250);

		static std::deque<std::chrono::steady_clock::time_point>& Recent()
		{
			static std::deque<std::chrono::steady_clock::time_point> recent;
			return recent;
		}

		static void Prune(std::deque<std::chrono::steady_clock::time_point>& a_recent)
		{
			const auto cutoff = std::chrono::steady_clock::now() - kWindow;
			while (!a_recent.empty() && a_recent.front() <= cutoff) {
				a_recent.pop_front();
			}
		}
	};

	class Client
	{
	public:
		Client(std::string_view a_host, std::uint16_t a_port)
		{
			ConnectBudget::Take();
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
					"named-smoke-" + std::to_string(nextProfile.fetch_add(1));
				Require(SendReliable(Json{
					{ "type", "hello" },
					{ "protocolVersion", 2 },
					{ "clientVersion", "named-smoke-1" },
					{ "falloutRuntimeVersion", "1.11.240" },
					{ "profileId", profileId },
					{ "contentManifestFingerprint", std::string(64, 'a') },
					{ "capabilities", Json::array({ "interest-v1", "player-state-v1", "save-attestation-v1" }) }
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
				(void)co_gns_client_disconnect(handle_, 0, "named acceptance client complete");
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

		void Drain(std::chrono::milliseconds a_window)
		{
			Json ignored;
			(void)ReceiveUntil([](const Json&) { return false; }, ignored, a_window);
		}

	private:
		co_gns_client_handle handle_{};
		std::uint32_t playerId_{};
		std::uint32_t transformSequence_{};
		std::uint32_t npcSequence_{};
	};

	Json Transform(std::string_view a_cell, std::string_view a_world, double a_x, double a_y, double a_z, double a_angleZ, std::string_view a_movementType)
	{
		return Json{
			{ "type", "transform" },
			{ "x", a_x },
			{ "y", a_y },
			{ "z", a_z },
			{ "angleZ", a_angleZ },
			{ "cellId", a_cell },
			{ "worldspaceId", a_world },
			{ "movementType", a_movementType }
		};
	}

	Json Npc(std::string_view a_form, std::string_view a_cell, std::string_view a_world, double a_x, double a_y)
	{
		return Json{
			{ "sourceFormId", a_form },
			{ "cellId", a_cell },
			{ "worldspaceId", a_world },
			{ "x", a_x },
			{ "y", a_y },
			{ "z", 0.0 },
			{ "angleZ", 0.0 }
		};
	}

	void DrainPair(Client& a_a, Client& a_b)
	{
		a_a.Drain(std::chrono::milliseconds(150));
		a_b.Drain(std::chrono::milliseconds(150));
	}

	void NearbyExteriorInterestRelays(Client& a, Client& b)
	{
		Require(a.SendSnapshot(Transform("00001001", "00002000", 0, 0, 0, 0, "normal"), Family::Transform), "failed to send sender transform");
		Require(b.SendSnapshot(Transform("00001002", "00002000", 100, 100, 0, 0, "normal"), Family::Transform), "failed to send recipient transform");
		DrainPair(a, b);

		Require(a.SendSnapshot(Transform("00001001", "00002000", 10, 10, 0, 0, "normal"), Family::Transform), "failed to send nearby transform");
		Json relay;
		Require(b.ReceiveUntil([&](const Json& packet) {
			return packet.value("type", std::string{}) == "transform" && packet.value("playerId", 0u) == a.PlayerId();
		}, relay, std::chrono::seconds(3)), "nearby exterior transform was not relayed");
		Require(relay.value("cellId", std::string{}) == "00001001", "transform relay lost sender cell");
		Require(relay.value("worldspaceId", std::string{}) == "00002000", "transform relay lost shared worldspace");
	}

	void NpcSnapshotIsFilteredPerRecipient(Client& authority, Client& recipient)
	{
		Require(authority.SendSnapshot(Transform("0000A500", "0000B500", 0, 0, 0, 0, "normal"), Family::Transform), "failed to send authority transform");
		Json grant;
		Require(authority.ReceiveUntil([&](const Json& packet) {
			return packet.value("type", std::string{}) == "npcAuthority" &&
				packet.value("authorityPlayerId", 0u) == authority.PlayerId() &&
				packet.value("authorityCellId", std::string{}) == "0000A500";
		}, grant, std::chrono::seconds(3)), "authority grant was not received");
		const auto epoch = grant.value("authorityEpoch", 0u);
		Require(epoch != 0, "authority epoch was zero");

		Require(recipient.SendSnapshot(Transform("0000A501", "0000B500", 1000, 0, 0, 0, "normal"), Family::Transform), "failed to send recipient transform");
		DrainPair(authority, recipient);

		const Json snapshot{
			{ "type", "npcState" },
			{ "authorityEpoch", epoch },
			{ "authorityCellId", "0000A500" },
			{ "authorityWorldspaceId", "0000B500" },
			{ "npcs", Json::array({
				Npc("00010001", "0000A500", "0000B500", 1000, 0),
				Npc("00010002", "0000A500", "0000B500", 20000, 0)
			}) }
		};
		Require(authority.SendSnapshot(snapshot, Family::NpcState), "failed to send NPC snapshot");
		Json relay;
		Require(recipient.ReceiveUntil([&](const Json& packet) {
			return packet.value("type", std::string{}) == "npcState" && packet.value("playerId", 0u) == authority.PlayerId();
		}, relay, std::chrono::seconds(3)), "recipient did not receive NPC snapshot");
		Require(relay.contains("npcs") && relay.at("npcs").is_array(), "NPC relay did not contain an array");
		Require(relay.at("npcs").size() == 1, "far NPC was not filtered from recipient snapshot");
		Require(relay.at("npcs").at(0).value("sourceFormId", std::string{}) == "00010001", "near NPC was not preserved");
	}

	void FullDurablePlayerStateRelays(Client& a, Client& b)
	{
		Require(a.SendSnapshot(Transform("0000C100", "0000C100", 0, 0, 0, 0, "normal"), Family::Transform), "failed to send player A transform");
		Require(b.SendSnapshot(Transform("0000C100", "0000C100", 0, 0, 0, 0, "normal"), Family::Transform), "failed to send player B transform");
		DrainPair(a, b);

		const Json state{
			{ "type", "playerState" },
			{ "characterName", "Nomad" },
			{ "equippedItems", Json::array({
				Json{ { "slot", "rightHand" }, { "formId", "0001F66A" } },
				Json{ { "slot", "armor0" }, { "formId", "00023432" } }
			}) },
			{ "appearance", Json{
				{ "version", 4 },
				{ "raceFormId", "00013746" },
				{ "height", 1.0 },
				{ "isFemale", false },
				{ "morphWeight", Json{ { "thin", 0.1 }, { "muscular", 0.2 }, { "large", 0.3 } } }
			} }
		};
		Require(a.SendReliable(state), "failed to send player state");
		Json relay;
		Require(b.ReceiveUntil([&](const Json& packet) {
			return packet.value("type", std::string{}) == "playerState" && packet.value("playerId", 0u) == a.PlayerId();
		}, relay, std::chrono::seconds(3)), "peer did not receive player state");
		Require(relay.value("characterName", std::string{}) == "Nomad", "character name was not relayed");
		Require(relay.contains("equippedItems") && relay.at("equippedItems").size() == 2, "equipment array was not relayed");
		Require(relay.contains("appearance") && relay.at("appearance").value("version", 0u) == 4, "appearance was not relayed");
	}

	void DistantActionEventsAreFiltered(Client& a, Client& b)
	{
		Require(a.SendSnapshot(Transform("0000D100", "0000E100", 0, 0, 0, 0, "normal"), Family::Transform), "failed to send distant player A transform");
		Require(b.SendSnapshot(Transform("0000D200", "0000E200", 0, 0, 0, 0, "normal"), Family::Transform), "failed to send distant player B transform");
		DrainPair(a, b);

		Require(a.SendReliable(Json{
			{ "type", "playerState" },
			{ "characterName", "DistantNomad" },
			{ "actionEvents", Json::array({ Json{ { "sequence", 1 }, { "type", 3 }, { "eventName", "fireSingle" } } }) }
		}), "failed to send distant player state");
		Json relay;
		Require(b.ReceiveUntil([&](const Json& packet) {
			return packet.value("type", std::string{}) == "playerState" && packet.value("playerId", 0u) == a.PlayerId();
		}, relay, std::chrono::seconds(3)), "distant peer did not receive durable state");
		Require(relay.value("characterName", std::string{}) == "DistantNomad", "durable state was lost out of interest");
		Require(!relay.contains("actionEvents"), "out-of-interest action events were not removed");
	}

	void PositionCorrectionMatchesLastKnownGood(Client& a)
	{
		Require(a.SendSnapshot(Transform("0000F500", "0000F600", 11, 22, 33, 0.75, "normal"), Family::Transform), "failed to send good transform");
		a.Drain(std::chrono::milliseconds(150));
		Require(a.SendSnapshot(Transform("0000F500", "0000F600", 1'000'000, 22, 33, 0.75, "normal"), Family::Transform), "failed to send invalid transform");
		Json correction;
		Require(a.ReceiveUntil([](const Json& packet) { return packet.value("type", std::string{}) == "positionCorrection"; }, correction, std::chrono::seconds(3)),
			"server did not return a position correction");
		Require(std::abs(correction.value("x", 0.0) - 11.0) < 0.001, "correction x was not last-known-good");
		Require(std::abs(correction.value("y", 0.0) - 22.0) < 0.001, "correction y was not last-known-good");
		Require(std::abs(correction.value("z", 0.0) - 33.0) < 0.001, "correction z was not last-known-good");
		Require(std::abs(correction.value("angleZ", 0.0) - 0.75) < 0.001, "correction heading was not last-known-good");
		Require(correction.value("cellId", std::string{}) == "0000F500", "correction cell was not last-known-good");
		Require(correction.value("worldspaceId", std::string{}) == "0000F600", "correction worldspace was not last-known-good");
		Require(!correction.value("reason", std::string{}).empty(), "correction did not include a rejection reason");
	}

	void CachedNpcStateReplaysAfterFirstTransform(Client& authority, std::string_view a_host, std::uint16_t a_port)
	{
		Require(authority.SendSnapshot(Transform("0000A700", "0000B700", 0, 0, 0, 0, "normal"), Family::Transform), "failed to send replay authority transform");
		Json grant;
		Require(authority.ReceiveUntil([&](const Json& packet) {
			return packet.value("type", std::string{}) == "npcAuthority" &&
				packet.value("authorityPlayerId", 0u) == authority.PlayerId() &&
				packet.value("authorityCellId", std::string{}) == "0000A700";
		}, grant, std::chrono::seconds(3)), "replay authority grant was not received");
		const auto epoch = grant.value("authorityEpoch", 0u);
		Require(epoch != 0, "replay authority epoch was zero");

		Require(authority.SendSnapshot(Json{
			{ "type", "npcState" },
			{ "authorityEpoch", epoch },
			{ "authorityCellId", "0000A700" },
			{ "authorityWorldspaceId", "0000B700" },
			{ "npcs", Json::array({ Npc("0001A701", "0000A700", "0000B700", 100, 0) }) }
		}, Family::NpcState), "failed to seed cached NPC state");
		authority.Drain(std::chrono::milliseconds(150));

		Client lateJoiner{ a_host, a_port };
		lateJoiner.Drain(std::chrono::milliseconds(150));
		Require(lateJoiner.SendSnapshot(Transform("0000A701", "0000B700", 100, 0, 0, 0, "normal"), Family::Transform), "failed to establish late-joiner scope");

		Json replay;
		Require(lateJoiner.ReceiveUntil([&](const Json& packet) {
			return packet.value("type", std::string{}) == "npcState" && packet.value("playerId", 0u) == authority.PlayerId();
		}, replay, std::chrono::seconds(3)), "cached NPC state was not replayed after late-joiner scope");
		Require(replay.value("fullReplace", false), "replayed NPC snapshot was not a full replacement");
		Require(replay.value("authorityCellId", std::string{}) == "0000A700", "replayed authority cell was wrong");
		Require(replay.value("authorityWorldspaceId", std::string{}) == "0000B700", "replayed authority worldspace was wrong");
		Require(replay.contains("npcs") && replay.at("npcs").is_array() && replay.at("npcs").size() == 1, "replayed NPC count was wrong");
		Require(replay.at("npcs").at(0).value("sourceFormId", std::string{}) == "0001A701", "replayed NPC identity was wrong");
	}

	void Run(std::string_view a_name, const std::function<void()>& a_test, int& a_passed, int& a_failed)
	{
		try {
			a_test();
			++a_passed;
			std::cout << "PASS " << a_name << '\n';
		} catch (const std::exception& exception) {
			++a_failed;
			std::cerr << "FAIL " << a_name << ": " << exception.what() << '\n';
		}
	}
}

int main(int argc, char** argv)
{
	if (argc != 3) {
		std::cerr << "Usage: CommonwealthOnlineServerLiveNamedGnsSmoke HOST PORT\n";
		return 2;
	}
	std::uint16_t port = 0;
	if (!ParsePort(argv[2], port)) {
		std::cerr << "Invalid port\n";
		return 2;
	}
	const std::string_view host{ argv[1] };

	try {
		int passed = 0;
		int failed = 0;
		// Every scenario gets its own connections. Sharing clients made each
		// scenario's opening transform a mid-session relocation, and once the
		// server started refusing those the later scenarios silently ran against
		// whatever position an earlier one had left behind.
		Run("interest: nearby clients in different cells of one worldspace relay transforms", [&] {
			Client a{ host, port };
			Client b{ host, port };
			NearbyExteriorInterestRelays(a, b);
		}, passed, failed);
		Run("npc interest: recipient receives only nearby NPCs from an authoritative snapshot", [&] {
			Client authority{ host, port };
			Client recipient{ host, port };
			NpcSnapshotIsFilteredPerRecipient(authority, recipient);
		}, passed, failed);
		Run("player-state: equipment, appearance, and character name relay together", [&] {
			Client a{ host, port };
			Client b{ host, port };
			FullDurablePlayerStateRelays(a, b);
		}, passed, failed);
		Run("player-state: distant action events are filtered while durable state remains", [&] {
			Client a{ host, port };
			Client b{ host, port };
			DistantActionEventsAreFiltered(a, b);
		}, passed, failed);
		Run("movement: correction contains the exact last-known-good transform", [&] {
			Client a{ host, port };
			PositionCorrectionMatchesLastKnownGood(a);
		}, passed, failed);
		Run("npc replay: cached nearby NPC state is sent after a late joiner establishes scope", [&] {
			Client authority{ host, port };
			CachedNpcStateReplaysAfterFirstTransform(authority, host, port);
		}, passed, failed);

		std::cout << "native GNS named/replay acceptance: " << passed << " passed, " << failed << " failed\n";
		return failed == 0 ? 0 : 1;
	} catch (const std::exception& exception) {
		std::cerr << "FAIL native GNS named/replay setup: " << exception.what() << '\n';
		return 1;
	}
}

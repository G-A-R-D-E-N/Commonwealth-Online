#include "co_gns_server_bridge.h"
#include "PersistenceSmokeSeed.h"
#include "../../../co-core/include/F4TConnectionPolicy.h"
#include "../../../co-core/include/F4TGnsProtocolAdapter.h"
#include "../../../co-core/include/F4TGnsSnapshotEnvelope.h"

#include <nlohmann/json.hpp>

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	constexpr std::string_view kPrimaryProfile = "persistence-smoke-primary";
	constexpr std::string_view kPrimarySlotProfile = "persistence-smoke-primary#0";
	constexpr std::string_view kObserverProfile = "persistence-smoke-observer";
	constexpr std::string_view kCharacterName = "Persistence Smoke Survivor";

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

	bool SendJson(co_gns_client_handle a_client, const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return co_gns_client_send(
			a_client,
			text.data(),
			static_cast<std::uint32_t>(text.size()),
			CO_GNS_DELIVERY_RELIABLE_ORDERED) == CO_GNS_SEND_SENT;
	}

	bool AckSessionReady(co_gns_client_handle a_client, std::string_view a_resumeToken)
	{
		return SendJson(a_client, Json{
			{ "type", "keepAlive" },
			{ "sessionReadyAck", true },
			{ "resumeTokenAck", a_resumeToken }
		});
	}

	std::optional<Json> DecodePacket(std::span<const std::uint8_t> a_bytes)
	{
		using namespace F4T::GnsSnapshotEnvelope;
		Header header;
		const auto decoded = Decode(a_bytes, header);
		if (decoded != DecodeResult::NotEnvelope && decoded != DecodeResult::Valid) return std::nullopt;
		if (decoded == DecodeResult::Valid) a_bytes = a_bytes.subspan(HeaderSize);
		try { return Json::parse(a_bytes.begin(), a_bytes.end()); }
		catch (...) { return std::nullopt; }
	}

	bool WaitForPacket(
		co_gns_client_handle a_client,
		std::string_view a_type,
		Json& a_output,
		std::chrono::milliseconds a_timeout,
		const std::function<void()>& a_service = {})
	{
		std::array<std::uint8_t, 64 * 1024> payload{};
		const auto deadline = std::chrono::steady_clock::now() + a_timeout;
		while (std::chrono::steady_clock::now() < deadline) {
			if (a_service) a_service();
			co_gns_event event{};
			const auto result = co_gns_client_poll(a_client, &event, payload.data(), static_cast<std::uint32_t>(payload.size()));
			if (result < 0) {
				return false;
			}
			if (result == 0) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}
			if (event.type == CO_GNS_EVENT_MESSAGE && event.payload_size > 0 && event.payload_size <= payload.size()) {
				const auto packet = DecodePacket({ payload.data(), event.payload_size });
				if (!packet || !packet->is_object()) return false;
				if (packet->value("type", std::string{}) == a_type) {
					a_output = *packet;
					return true;
				}
				if (packet->value("type", std::string{}) == "sessionEnded" ||
					packet->value("type", std::string{}) == "positionCorrection") return false;
			}
			if (event.type == CO_GNS_EVENT_OVERSIZE_MESSAGE || event.type == CO_GNS_EVENT_DISCONNECTED) {
				return false;
			}
		}
		return false;
	}

	co_gns_client_handle Connect(std::string_view a_host, std::uint16_t a_port, Json* a_welcome = nullptr, const std::function<void()>& a_service = {})
	{
		co_gns_client_handle client = nullptr;
		std::array<char, 512> error{};
		const std::string host{ a_host };
		if (co_gns_client_create(host.c_str(), a_port, &client, error.data(), error.size()) == 0 || client == nullptr) {
			std::cerr << "GNS client create failed: " << error.data() << '\n';
			return nullptr;
		}
		Json welcome;
		bool received = false;
		try { received = WaitForPacket(client, "welcome", welcome, std::chrono::seconds(5), a_service); }
		catch (...) { co_gns_client_destroy(client); throw; }
		if (!received) {
			std::cerr << "Did not receive welcome\n";
			co_gns_client_destroy(client);
			return nullptr;
		}
		if (a_welcome) *a_welcome = welcome;
		return client;
	}

	Json IdentityHello(std::string_view a_profileId, std::optional<std::string> a_resumeToken = std::nullopt)
	{
		Json hello{
			{ "type", "hello" },
			{ "protocolVersion", 2 },
			{ "clientVersion", "persistence-smoke-1" },
			{ "falloutRuntimeVersion", "1.11.240" },
			{ "profileId", std::string{ a_profileId } },
			{ "contentManifestFingerprint", std::string(64, 'a') },
			{ "capabilities", Json::array({ "interest-v1", "player-state-v1", "save-attestation-v1" }) }
		};
		if (a_resumeToken) {
			hello["resumeToken"] = *a_resumeToken;
		}
		return hello;
	}

	void Close(co_gns_client_handle a_client)
	{
		if (a_client == nullptr) {
			return;
		}
		(void)co_gns_client_disconnect(a_client, 0, "persistence smoke step complete");
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		co_gns_client_destroy(a_client);
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	bool WriteToken(std::string_view a_path, std::string_view a_token)
	{
		std::ofstream output(std::string{ a_path }, std::ios::binary | std::ios::trunc);
		if (!output) {
			return false;
		}
		output << a_token << '\n';
		output.flush();
		return output.good();
	}

	std::optional<std::string> ReadToken(std::string_view a_path)
	{
		std::ifstream input(std::string{ a_path }, std::ios::binary);
		std::string token;
		if (!input || !std::getline(input, token) || token.empty()) {
			return std::nullopt;
		}
		return token;
	}

	int Seed(std::string_view a_host, std::uint16_t a_port, std::string_view a_tokenPath, std::string_view a_statePath)
	{
		auto client = Connect(a_host, a_port);
		if (client == nullptr || !SendJson(client, IdentityHello(kPrimaryProfile))) {
			Close(client);
			return 1;
		}

		Json ready;
		if (!WaitForPacket(client, "sessionReady", ready, std::chrono::seconds(5))) {
			std::cerr << "Initial persistence identity did not receive sessionReady\n";
			Close(client);
			return 1;
		}
		const auto token = ready.value("resumeToken", std::string{});
		if (token.empty() || !WriteToken(a_tokenPath, token)) {
			std::cerr << "Could not capture the initial persistence resume token\n";
			Close(client);
			return 1;
		}
		if (!AckSessionReady(client, token)) {
			std::cerr << "Could not acknowledge initial persistence sessionReady\n";
			Close(client);
			return 1;
		}

		const Json playerState{
			{ "type", "playerState" },
			{ "characterName", std::string{ kCharacterName } },
			{ "actionEvents", Json::array() }
		};
		if (!SendJson(client, playerState)) {
			std::cerr << "Could not send durable player state\n";
			Close(client);
			return 1;
		}
		if (!CommonwealthOnline::Server::Testing::WaitForSeedCommit(
			std::filesystem::path{ a_statePath }, kPrimarySlotProfile, kCharacterName, std::chrono::seconds(5))) {
			std::cerr << "Durable playerState was not committed before restart\n";
			Close(client);
			return 1;
		}
		Close(client);
		std::cout << "C++ persistence seed smoke passed\n";
		return 0;
	}

	int Resume(std::string_view a_host, std::uint16_t a_port, std::string_view a_tokenPath)
	{
		const auto token = ReadToken(a_tokenPath);
		if (!token) {
			std::cerr << "Could not read persistence resume token\n";
			return 1;
		}

		auto primary = Connect(a_host, a_port);
		if (primary == nullptr || !SendJson(primary, IdentityHello(kPrimaryProfile, *token))) {
			Close(primary);
			return 1;
		}
		Json primaryReady;
		if (!WaitForPacket(primary, "sessionReady", primaryReady, std::chrono::seconds(5))) {
			std::cerr << "Persisted resume credential did not survive restart\n";
			Close(primary);
			return 1;
		}
		const auto rotatedToken = primaryReady.value("resumeToken", std::string{});
		if (rotatedToken.empty() || rotatedToken == *token || !WriteToken(a_tokenPath, rotatedToken)) {
			std::cerr << "Persisted resume token did not rotate after restart\n";
			Close(primary);
			return 1;
		}
		if (!AckSessionReady(primary, rotatedToken)) {
			std::cerr << "Could not acknowledge resumed persistence sessionReady\n";
			Close(primary);
			return 1;
		}

		auto observer = Connect(a_host, a_port);
		if (observer == nullptr || !SendJson(observer, IdentityHello(kObserverProfile))) {
			Close(observer);
			Close(primary);
			return 1;
		}
		Json observerReady;
		if (!WaitForPacket(observer, "sessionReady", observerReady, std::chrono::seconds(5))) {
			std::cerr << "Observer did not receive sessionReady\n";
			Close(observer);
			Close(primary);
			return 1;
		}
		const auto observerToken = observerReady.value("resumeToken", std::string{});
		if (!observerToken.empty()) {
			(void)AckSessionReady(observer, observerToken);
		}

		Json restored;
		if (!WaitForPacket(observer, "playerState", restored, std::chrono::seconds(5)) ||
			restored.value("characterName", std::string{}) != kCharacterName ||
			restored.contains("actionEvents")) {
			std::cerr << "Restored durable playerState was not replayed cleanly after restart\n";
			Close(observer);
			Close(primary);
			return 1;
		}

		Close(observer);
		Close(primary);
		std::cout << "C++ persistence restart/resume smoke passed\n";
		return 0;
	}

	int Movement(std::string_view a_host, std::uint16_t a_port, unsigned a_players, unsigned a_samples, bool a_reconnect = false, std::string_view a_worldTokenPath = {})
	{
		using namespace F4T::GnsProtocolAdapter;
		using namespace F4T::GnsSnapshotEnvelope;
		struct Peer
		{
			co_gns_client_handle handle{};
			Session adapter;
			std::uint32_t id{}, sequence{};
			std::string token;
			double x{}, maxGapMs{}, minGapMs{1000000};
			std::chrono::steady_clock::time_point lastSend{};
		};
		const auto connections = a_reconnect ? std::max(2u, a_players) : a_players;
		std::vector<Peer> peers(connections);
		struct Cleanup
		{
			std::vector<Peer>& peers;
			~Cleanup() { for (auto& peer : peers) Close(peer.handle); }
		} cleanup{ peers };
		auto require = [](bool ok, const char* message) { if (!ok) throw std::runtime_error(message); };
		const auto equipment = Json::array({ Json{ { "slot", "right" }, { "formId", "00001234" } } });
		unsigned reliable = 0, transforms = 0, receipts = 0, worldReceipts = 0;
		std::vector<std::string> worldTimes(connections);
		std::string expectedWorldTime;
		std::string primaryName = "Movement 0";
		bool awaitingReplay = false;
		Json cycles = Json::array();
		std::vector<std::uint32_t> retiredIds;
		auto send = [&](Peer& peer, const OutboundMessage& message) {
			std::string payload = message.payload;
			if (message.delivery == Delivery::UnreliableSequenced) {
				peer.x = Json::parse(payload).at("x").get<double>();
				const auto now = std::chrono::steady_clock::now();
				if (peer.lastSend != std::chrono::steady_clock::time_point{}) {
					const auto gap = std::chrono::duration<double, std::milli>(now - peer.lastSend).count();
					peer.maxGapMs = std::max(peer.maxGapMs, gap);
					peer.minGapMs = std::min(peer.minGapMs, gap);
				}
				peer.lastSend = now;
				payload.insert(0, HeaderSize, '\0');
				require(WriteHeader({ reinterpret_cast<std::uint8_t*>(payload.data()), payload.size() },
					Family::Transform, ++peer.sequence), "snapshot encode failed");
			}
			require(co_gns_client_send(peer.handle, payload.data(), static_cast<std::uint32_t>(payload.size()),
				message.delivery == Delivery::ReliableOrdered ? CO_GNS_DELIVERY_RELIABLE_ORDERED :
				CO_GNS_DELIVERY_UNRELIABLE_SEQUENCED) == CO_GNS_SEND_SENT, "movement send failed");
		};
		std::function<void()> service;
		auto activate = [&](unsigned index, bool resume) {
				auto& peer = peers[index];
				Json welcome;
				peer.handle = Connect(a_host, a_port, &welcome, service);
				require(peer.handle != nullptr, "movement connection failed");
				peer.id = welcome.at("playerId").get<std::uint32_t>();
				auto metadata = F4T::ClientCompatibility::AE();
				metadata.contentManifestFingerprint = std::string(64, 'a');
				peer.adapter.SetClientIdentity({ "movement-load-" + std::to_string(index), metadata, resume ? std::optional{ peer.token } : std::nullopt },
					[&peer](const ReadyIdentity& identity, std::string&) { peer.token = identity.resumeToken; return !peer.token.empty(); });
				auto hello = peer.adapter.AdaptInbound(welcome.dump());
				require(!hello.protocolViolation && hello.controlMessages.size() == 1, "adapter hello failed");
				for (const auto& message : hello.controlMessages) send(peer, message);
				Json ready;
				require(WaitForPacket(peer.handle, "sessionReady", ready, std::chrono::seconds(5), service), "movement sessionReady missing");
				auto accepted = peer.adapter.AdaptInbound(ready.dump());
				require(!accepted.protocolViolation && !accepted.localHandshakeFailure && peer.adapter.IsReady() &&
					accepted.controlMessages.size() == 1, "movement handshake failed");
				for (const auto& message : accepted.controlMessages) send(peer, message);
		};
		try {
			for (unsigned index = 0; index < connections; ++index) {
				if (index != 0 && index % F4T::ConnectionPolicy::DefaultMaximumConnectAttempts == 0)
					std::this_thread::sleep_for(std::chrono::duration<double>(F4T::ConnectionPolicy::DefaultConnectAttemptWindowSeconds + .1));
				activate(index, false);
			}
			std::vector<std::vector<double>> latest(connections, std::vector<double>(connections, -1));
			std::vector<std::vector<bool>> durable(connections, std::vector<bool>(connections, false));
			auto drain = [&] {
				std::array<std::uint8_t, 64 * 1024> payload{};
				for (unsigned index = 0; index < connections; ++index) {
					auto& peer = peers[index];
					if (!peer.handle || !peer.adapter.IsReady()) continue;
					for (unsigned count = 0; count < 4096; ++count) {
						co_gns_event event{};
						const auto result = co_gns_client_poll(peer.handle, &event, payload.data(), static_cast<std::uint32_t>(payload.size()));
						require(result >= 0 && event.type != CO_GNS_EVENT_DISCONNECTED && event.type != CO_GNS_EVENT_OVERSIZE_MESSAGE,
							"movement transport failed");
						if (result == 0) break;
						if (event.type != CO_GNS_EVENT_MESSAGE) continue;
						std::span<const std::uint8_t> bytes{ payload.data(), event.payload_size };
						Header header;
						const auto decoded = Decode(bytes, header);
						require(decoded == DecodeResult::NotEnvelope || decoded == DecodeResult::Valid, "invalid snapshot envelope");
						if (decoded == DecodeResult::Valid) bytes = bytes.subspan(HeaderSize);
						const std::string text{ reinterpret_cast<const char*>(bytes.data()), bytes.size() };
						const auto packet = Json::parse(text);
						const auto type = packet.value("type", std::string{});
						require(type != "positionCorrection" && type != "sessionEnded", "movement correction or rejection");
						const auto adapted = peer.adapter.AdaptInbound(text);
						require(!adapted.protocolViolation && !adapted.localHandshakeFailure && !adapted.rejection, "movement inbound failed");
						if (!a_worldTokenPath.empty() && type == "serverWorldState") {
							require(packet.contains("timeHHmm") && packet.at("timeHHmm").is_string(), "world packet time missing");
							worldTimes[index] = packet.at("timeHHmm").get<std::string>();
							++worldReceipts;
							continue;
						}
						if (type != "transform" && type != "playerState") continue;
						const auto found = std::find_if(peers.begin(), peers.end(), [&](const Peer& source) {
							return source.id == packet.value("playerId", 0u);
						});
						if (found == peers.end() && std::find(retiredIds.begin(), retiredIds.end(), packet.value("playerId", 0u)) != retiredIds.end()) continue;
						require(found != peers.end() && found != peers.begin() + index, "unexpected movement source");
						const auto source = static_cast<unsigned>(found - peers.begin());
						if (type == "transform") {
							require(decoded == DecodeResult::Valid && header.family == Family::Transform &&
								packet.value("cellId", "") == "00001111" && packet.value("worldspaceId", "") == "00002222" &&
								packet.value("movementType", "") == "normal", "unexpected movement snapshot");
							const auto x = packet.at("x").get<double>();
							require(x >= 0 && x <= found->x, "unexpected movement position");
							latest[index][source] = std::max(latest[index][source], x);
							++receipts;
						} else {
							require(packet.value("characterName", "") == (source == 0 ? primaryName : "Movement " + std::to_string(source)) &&
								packet.at("equippedItems") == equipment && (!(awaitingReplay && source == 0) || !packet.contains("actionEvents")), "unexpected concept-level equipment fixture in durable peer state");
							durable[index][source] = true;
						}
					}
				}
			};
			unsigned movingDuringReconnect = 0;
			bool restored = false, rotated = false;
			const auto start = std::chrono::steady_clock::now();
			for (unsigned sample = 0; sample < a_samples; ++sample) {
				if (a_reconnect && sample >= 1 && sample <= 3) {
					const auto cycleStart = std::chrono::steady_clock::now();
					const auto movementBefore = movingDuringReconnect;
					for (unsigned index = 1; index < a_players; ++index) { peers[index].maxGapMs = 0; peers[index].minGapMs = 1000000; }
					const auto previousToken = peers[0].token;
					const auto oldId = peers[0].id;
					const auto durableBefore = reliable;
					auto next = std::chrono::steady_clock::now();
					service = [&] {
						if (std::chrono::steady_clock::now() >= next) {
							for (unsigned index = 1; index < a_players; ++index) {
								const auto background = peers[index].adapter.AdaptOutbound(Json{
									{ "type", "transform" }, { "x", peers[index].x + 4.0 }, { "y", 0.0 }, { "z", 0.0 }, { "angleZ", 0.0 },
									{ "cellId", "00001111" }, { "worldspaceId", "00002222" }, { "movementType", "normal" },
									{ "equippedItems", equipment }, { "characterName", "Movement " + std::to_string(index) }
								}.dump());
								require(background.size() == 1 && background.front().delivery == Delivery::UnreliableSequenced,
									"background movement changed durable state");
								send(peers[index], background.front()); ++transforms; ++movingDuringReconnect;
							}
							next = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
						}
						drain();
					};
					require(co_gns_client_disconnect(peers[0].handle, 0, "authenticated reconnect workload") == 1, "reconnect disconnect failed");
					co_gns_client_destroy(peers[0].handle);
					peers[0].handle = nullptr;
					if (!a_worldTokenPath.empty()) {
						expectedWorldTime = std::to_string(1200 + sample);
						worldTimes[0].clear();
						std::cout << "CO_WORLD_COMMAND " << expectedWorldTime << std::endl;
					}
					const auto allowed = std::chrono::steady_clock::now() + std::chrono::duration<double>(F4T::ConnectionPolicy::DefaultConnectAttemptWindowSeconds + .1);
					while (std::chrono::steady_clock::now() < allowed) { service(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
					for (auto& row : durable) row[0] = false;
					std::fill(durable[0].begin(), durable[0].end(), false);
					std::fill(latest[0].begin(), latest[0].end(), -1);
					for (auto& row : latest) row[0] = -1;
					retiredIds.push_back(oldId);
					peers[0].adapter.Reset();
					awaitingReplay = true;
					activate(0, true);
					rotated = !peers[0].token.empty() && peers[0].token != previousToken && peers[0].id != oldId;
					require(rotated, "resume token or transport player id did not rotate");
					auto replayed = [&] {
						for (unsigned receiver = 1; receiver < connections; ++receiver) if (!durable[receiver][0]) return false;
						if (!a_worldTokenPath.empty()) for (const auto& received : worldTimes) if (received != expectedWorldTime) return false;
						return true;
					};
					const auto replayDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
					while (!replayed() && std::chrono::steady_clock::now() < replayDeadline) { service(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
					restored = replayed() && reliable == durableBefore;
					require(restored, "durable state was not restored before client replacement state");
					service = {};
					awaitingReplay = false;
					double maxBackgroundGap = 0, minBackgroundGap = 1000000;
					for (unsigned index = 1; index < a_players; ++index) {
						maxBackgroundGap = std::max(maxBackgroundGap, peers[index].maxGapMs);
						minBackgroundGap = std::min(minBackgroundGap, peers[index].minGapMs);
					}
					require(a_players == 1 || ((movingDuringReconnect - movementBefore) >= (a_players - 1) * 90 &&
						minBackgroundGap >= 95 && maxBackgroundGap < 500), "background movement cadence outside bounded tolerance");
					cycles.push_back(Json{ { "cycle", sample }, { "rotated", rotated }, { "retired_player_id", oldId }, { "resumed_player_id", peers[0].id },
						{ "world_replayed_before_replacement", !a_worldTokenPath.empty() && worldTimes[0] == expectedWorldTime }, { "world_time", expectedWorldTime },
						{ "restored_receivers", connections - 1 }, { "restored_name", primaryName }, { "restored_before_replacement", restored },
						{ "movement_sends", movingDuringReconnect - movementBefore }, { "max_background_gap_ms", maxBackgroundGap }, { "min_background_gap_ms", a_players == 1 ? 0 : minBackgroundGap },
						{ "latency_ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cycleStart).count() } });
					primaryName = "Reconnected 0 cycle " + std::to_string(sample);
					const auto paced = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
					while (std::chrono::steady_clock::now() < paced) { drain(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
				}
				for (unsigned index = 0; index < (sample == 0 ? connections : a_players); ++index) {
					const auto messages = peers[index].adapter.AdaptOutbound(Json{
						{ "type", "transform" }, { "x", sample == 0 ? 0.0 : peers[index].x + 4.0 }, { "y", 0.0 }, { "z", 0.0 }, { "angleZ", 0.0 },
						{ "cellId", "00001111" }, { "worldspaceId", "00002222" }, { "movementType", "normal" },
						{ "equippedItems", equipment }, { "characterName", (index == 0 ? primaryName : "Movement " + std::to_string(index)) }
					}.dump());
					require(messages.size() == ((sample == 0 || (a_reconnect && index == 0 && sample >= 1 && sample <= 3)) ? 2 : 1) && messages.front().delivery == Delivery::UnreliableSequenced &&
						((sample != 0 && !(a_reconnect && index == 0 && sample >= 1 && sample <= 3)) || messages.back().delivery == Delivery::ReliableOrdered), "adapter durable suppression failed");
					for (const auto& message : messages) {
						send(peers[index], message);
						if (message.delivery == Delivery::ReliableOrdered) ++reliable; else ++transforms;
					}
				}
				const auto deadline = a_reconnect ? std::chrono::steady_clock::now() + std::chrono::milliseconds(100) :
					start + std::chrono::milliseconds(100 * (sample + 1));
				do { drain(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
				while (std::chrono::steady_clock::now() < deadline);
			}
			auto complete = [&] {
				for (unsigned receiver = 0; receiver < a_players; ++receiver)
					for (unsigned source = 0; source < a_players; ++source)
						if (receiver != source && (!durable[receiver][source] || latest[receiver][source] < 4.0 * (a_samples / 2))) return false;
				return true;
			};
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (!complete() && std::chrono::steady_clock::now() < deadline) {
				drain(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			require(complete(), "bounded peer movement/durable receipt verification failed");
			if (!a_worldTokenPath.empty()) require(WriteToken(a_worldTokenPath, peers[0].token), "world resume token write failed");
			std::cout << (a_reconnect ? "CO_RECONNECT_LOAD " : "CO_MOVEMENT_LOAD ") << Json{
				{ "world_receipts", worldReceipts }, { "final_world_time", expectedWorldTime },
				{ "players", a_players }, { "peak_connections", connections }, { "rotated", rotated },
				{ "restored_before_replacement", restored }, { "cycles", cycles }, { "movement_during_reconnect", movingDuringReconnect },
				{ "equipment_identity_scope", "concept-level/unverified synthetic engine identity" }, { "samples", a_samples }, { "interval_ms", 100 },
				{ "sent_transforms", transforms }, { "sent_durable_states", reliable }, { "peer_transform_receipts", receipts },
				{ "verified_peer_pairs", a_players * (a_players - 1) }, { "corrections", 0 },
				{ "update_phase_ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() }
			}.dump() << '\n';
			return 0;
		} catch (const std::exception& error) {
			std::cerr << error.what() << '\n';
			return 1;
		}
	}

	int Load(std::string_view a_host, std::uint16_t a_port, unsigned a_players, unsigned a_samples)
	{
		struct Clients
		{
			std::vector<co_gns_client_handle> handles;
			~Clients() { for (auto client : handles) Close(client); }
		} clients;
		for (unsigned index = 0; index < a_players; ++index) {
			if (index != 0 && index % F4T::ConnectionPolicy::DefaultMaximumConnectAttempts == 0) {
				std::this_thread::sleep_for(std::chrono::duration<double>(
					F4T::ConnectionPolicy::DefaultConnectAttemptWindowSeconds + .1));
			}
			auto client = Connect(a_host, a_port);
			if (client == nullptr) return 1;
			clients.handles.push_back(client);
			Json ready;
			if (!SendJson(client, IdentityHello("persistence-load-" + std::to_string(index))) ||
				!WaitForPacket(client, "sessionReady", ready, std::chrono::seconds(5)) ||
				ready.value("resumeToken", std::string{}).empty() ||
				!AckSessionReady(client, ready.at("resumeToken").get<std::string>())) {
				std::cerr << "Load identity handshake failed\n";
				return 1;
			}
		}
		std::array<std::uint8_t, 64 * 1024> payload{};
		const auto start = std::chrono::steady_clock::now();
		for (unsigned sample = 0; sample < a_samples; ++sample) {
			for (unsigned index = 0; index < a_players; ++index) {
				if (!SendJson(clients.handles[index], Json{
					{ "type", "playerState" },
					{ "characterName", "Load " + std::to_string(index) + " sample " + std::to_string(sample) }
				})) {
					std::cerr << "Load playerState send failed\n";
					return 1;
				}
			}
			const auto deadline = start + std::chrono::milliseconds(50 * (sample + 1));
			do {
				for (auto client : clients.handles) {
					for (unsigned drained = 0; drained < 4096; ++drained) {
						co_gns_event event{};
						const auto result = co_gns_client_poll(client, &event, payload.data(), static_cast<std::uint32_t>(payload.size()));
						if (result < 0 || event.type == CO_GNS_EVENT_DISCONNECTED || event.type == CO_GNS_EVENT_OVERSIZE_MESSAGE) {
							std::cerr << "Load connection failed\n";
							return 1;
						}
						if (result == 0) break;
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			} while (std::chrono::steady_clock::now() < deadline);
		}
		std::cout << "CO_PERSISTENCE_LOAD " << Json{
			{ "players", a_players }, { "samples", a_samples }, { "interval_ms", 50 },
			{ "sent_updates", a_players * a_samples },
			{ "update_phase_ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() }
		}.dump() << '\n';
		return 0;
	}
}

int main(int argc, char** argv)
{
	if (argc != 5 && argc != 6 && argc != 7) {
		std::cerr << "Usage: CommonwealthOnlineServerLivePersistenceGnsSmoke seed HOST PORT TOKEN_FILE STATE_FILE or resume HOST PORT TOKEN_FILE or load|movement|reconnect HOST PORT UNUSED PLAYERS SAMPLES\n";
		return 2;
	}
	std::uint16_t port = 0;
	if (!ParsePort(argv[3], port)) {
		std::cerr << "Invalid port\n";
		return 2;
	}
	const std::string_view mode{ argv[1] };
	if ((mode == "load" || mode == "movement" || mode == "reconnect" || mode == "world") && argc == 7) {
		unsigned players{}, samples{};
		auto parse = [](const char* text, unsigned& value) {
			const std::string_view input{ text };
			const auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), value);
			return error == std::errc{} && end == input.data() + input.size();
		};
		if (!parse(argv[5], players) || players < 1 || players > 16 ||
			!parse(argv[6], samples) || samples < ((mode == "reconnect" || mode == "world") ? 4u : (mode == "movement" ? 2u : 1u)) || samples > 600) {
			std::cerr << "Invalid load size\n";
			return 2;
		}
		return mode != "load" ? Movement(argv[2], port, players, samples, mode == "reconnect" || mode == "world", mode == "world" ? std::string_view{ argv[4] } : std::string_view{}) : Load(argv[2], port, players, samples);
	}
	if (mode == "seed") {
		return argc == 6 ? Seed(argv[2], port, argv[4], argv[5]) : 2;
	}
	if (argc != 5) return 2;
	if (mode == "world-resume") {
		const auto token = ReadToken(argv[4]);
		if (!token) return 1;
		auto client = Connect(argv[2], port);
		Json ready, world;
		const bool restored = client && SendJson(client, IdentityHello("movement-load-0", *token)) &&
			WaitForPacket(client, "sessionReady", ready, std::chrono::seconds(5)) &&
			!ready.value("resumeToken", "").empty() && ready.value("resumeToken", "") != *token && ready.value("profileId", "") == "movement-load-0" && ready.value("playerId", 0u) > 0 && AckSessionReady(client, ready.value("resumeToken", "")) &&
			WaitForPacket(client, "serverWorldState", world, std::chrono::seconds(5)) && world.value("timeHHmm", "") == "1203";
		Close(client);
		if (restored) std::cout << "CO_WORLD_RESTART " << Json{ { "world_time", "1203" }, { "replayed_before_client_state", true }, { "profile_id", "movement-load-0" }, { "token_rotated", true } }.dump() << '\n';
		return restored ? 0 : 1;
	}
	if (mode == "resume") {
		return Resume(argv[2], port, argv[4]);
	}
	std::cerr << "Unknown mode: " << mode << '\n';
	return 2;
}

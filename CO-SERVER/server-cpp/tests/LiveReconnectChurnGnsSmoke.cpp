#include "F4TGnsSnapshotEnvelope.h"
#include "co_gns_server_bridge.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
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

	constexpr int ResumeCycles = 5;

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

	std::string PacketCodeOrNone(const Json& a_packet)
	{
		if (!a_packet.is_object()) {
			return "<none>";
		}
		const auto found = a_packet.find("code");
		return found != a_packet.end() && found->is_string()
			? found->get<std::string>()
			: std::string{ "<none>" };
	}

	// The server admits at most F4T::ConnectionPolicy::DefaultMaximumConnectAttempts
	// (8) connects per peer per 10s window and silently drops the rest. Churn
	// testing is the one workload that runs into that ceiling honestly, so the
	// smoke paces itself below it instead of asking the server to relax.
	class ConnectPacer
	{
	public:
		static void WaitForSlot()
		{
			constexpr std::size_t AttemptsPerWindow = 7;
			constexpr auto Window = std::chrono::seconds(10);

			auto& attempts = Attempts();
			const auto now = std::chrono::steady_clock::now();
			while (!attempts.empty() && now - attempts.front() >= Window) {
				attempts.pop_front();
			}
			if (attempts.size() >= AttemptsPerWindow) {
				const auto readyAt = attempts.front() + Window;
				if (readyAt > now) {
					std::this_thread::sleep_for(readyAt - now + std::chrono::milliseconds(50));
				}
				attempts.pop_front();
			}
			attempts.push_back(std::chrono::steady_clock::now());
		}

	private:
		static std::deque<std::chrono::steady_clock::time_point>& Attempts()
		{
			static std::deque<std::chrono::steady_clock::time_point> attempts;
			return attempts;
		}
	};

	// Dropping a client handle closes the connection politely, which is the one
	// thing a crashed client never does. Sending the close into 100% fake loss
	// is what leaves the server holding a session nobody is on the other end of.
	class DroppedLinkWindow
	{
	public:
		DroppedLinkWindow()
		{
			Require(Set(100.0f), "failed to enable 100% fake packet loss");
		}

		DroppedLinkWindow(const DroppedLinkWindow&) = delete;
		DroppedLinkWindow& operator=(const DroppedLinkWindow&) = delete;

		~DroppedLinkWindow()
		{
			(void)co_gns_test_set_network_conditions(0.0f, 0.0f);
		}

		void Restore()
		{
			Require(Set(0.0f), "failed to restore normal network conditions");
		}

	private:
		static bool Set(float a_loss)
		{
			if (co_gns_test_set_network_conditions(a_loss, 0.0f) != 1) {
				return false;
			}
			float observedLoss = -1.0f;
			float observedReorder = -1.0f;
			if (co_gns_test_get_network_conditions(std::addressof(observedLoss), std::addressof(observedReorder)) != 1) {
				return false;
			}
			return std::abs(observedLoss - a_loss) < 0.001f;
		}
	};

	// A churn client owns one GNS connection at a time. Sessions are opened and
	// torn down repeatedly against a server that requires V2 identity, so the
	// hello and the resume credential are held here rather than in the ctor.
	class ChurnClient
	{
	public:
		ChurnClient(std::string_view a_host, std::uint16_t a_port, std::string a_profileId) :
			host_{ a_host },
			port_{ a_port },
			profileId_{ std::move(a_profileId) }
		{}

		ChurnClient(const ChurnClient&) = delete;
		ChurnClient& operator=(const ChurnClient&) = delete;

		~ChurnClient()
		{
			Close();
		}

		[[nodiscard]] std::uint32_t PlayerId() const noexcept
		{
			return playerId_;
		}

		[[nodiscard]] const std::string& ResumeToken() const noexcept
		{
			return resumeToken_;
		}

		void Open()
		{
			Require(handle_ == nullptr, "churn client opened while already connected");
			ConnectPacer::WaitForSlot();
			std::array<char, 512> error{};
			const std::string host{ host_ };
			if (co_gns_client_create(host.c_str(), port_, &handle_, error.data(), error.size()) == 0 || handle_ == nullptr) {
				throw std::runtime_error("GNS client create failed: " + std::string{ error.data() });
			}
			Json welcome;
			Require(ReceiveUntil([](const Json& packet) { return packet.value("type", std::string{}) == "welcome"; }, welcome, std::chrono::seconds(5)),
				"client did not receive welcome");
			playerId_ = welcome.value("playerId", 0u);
			Require(playerId_ != 0, "welcome did not contain a player id");
			sequence_ = 0;
		}

		// Sends the identity hello and returns the sessionReady frame, rotating
		// the stored resume credential. Throws if the server ends the session.
		Json Activate(const std::optional<std::string>& a_resumeToken)
		{
			Require(SendReliable(Hello(a_resumeToken)), "failed to send identity hello");
			Json packet;
			Require(ReceiveUntil([](const Json& candidate) {
				const auto type = candidate.value("type", std::string{});
				return type == "sessionReady" || type == "sessionEnded";
			}, packet, std::chrono::seconds(5)),
				"identity hello received neither sessionReady nor sessionEnded");
			Require(packet.value("type", std::string{}) == "sessionReady",
				"identity hello was rejected with code='" + PacketCodeOrNone(packet) + "'");
			Require(packet.value("profileId", std::string{}) == profileId_,
				"sessionReady carried a different profile id");
			auto rotated = packet.value("resumeToken", std::string{});
			Require(!rotated.empty(), "sessionReady did not carry a resume token");
			resumeToken_ = std::move(rotated);
			Require(SendReliable(Json{
				{ "type", "keepAlive" },
				{ "sessionReadyAck", true },
				{ "resumeTokenAck", resumeToken_ }
			}), "failed to send sessionReady acknowledgment");
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			return packet;
		}

		// Sends the identity hello expecting rejection, and returns the code.
		std::string ActivateExpectingRejection(const std::optional<std::string>& a_resumeToken)
		{
			Require(SendReliable(Hello(a_resumeToken)), "failed to send identity hello");
			Json packet;
			Require(ReceiveUntil([](const Json& candidate) {
				const auto type = candidate.value("type", std::string{});
				return type == "sessionReady" || type == "sessionEnded";
			}, packet, std::chrono::seconds(5)),
				"rejected identity hello produced no response");
			Require(packet.value("type", std::string{}) == "sessionEnded",
				"identity hello was accepted when rejection was expected");
			return PacketCodeOrNone(packet);
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

		bool SendSnapshot(const Json& a_packet)
		{
			const auto text = a_packet.dump();
			std::vector<std::uint8_t> payload(F4T::GnsSnapshotEnvelope::HeaderSize + text.size());
			if (!F4T::GnsSnapshotEnvelope::WriteHeader(std::span<std::uint8_t>{ payload }, Family::Transform, ++sequence_)) {
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

		void Close()
		{
			if (handle_ == nullptr) {
				return;
			}
			(void)co_gns_client_disconnect(handle_, 0, "reconnect churn step complete");
			std::this_thread::sleep_for(std::chrono::milliseconds(150));
			co_gns_client_destroy(handle_);
			handle_ = nullptr;
		}

		// Drops the socket without a close frame, the way a crashed or
		// network-partitioned client leaves: the server still holds the session
		// when the resume attempt arrives.
		void Abandon()
		{
			if (handle_ == nullptr) {
				return;
			}
			co_gns_client_destroy(handle_);
			handle_ = nullptr;
		}

	private:
		[[nodiscard]] Json Hello(const std::optional<std::string>& a_resumeToken) const
		{
			Json hello{
				{ "type", "hello" },
				{ "protocolVersion", 2 },
				{ "clientVersion", "reconnect-churn-smoke-1" },
				{ "falloutRuntimeVersion", "1.11.240" },
				{ "profileId", profileId_ },
				{ "contentManifestFingerprint", std::string(64, 'c') },
				{ "capabilities", Json::array({ "interest-v1", "player-state-v1", "save-attestation-v1" }) }
			};
			if (a_resumeToken) {
				hello["resumeToken"] = *a_resumeToken;
			}
			return hello;
		}

		std::string host_;
		std::uint16_t port_{};
		std::string profileId_;
		co_gns_client_handle handle_{};
		std::uint32_t playerId_{};
		std::uint32_t sequence_{};
		std::string resumeToken_;
	};

	Json Transform(double a_x, std::string_view a_movementType)
	{
		return Json{
			{ "type", "transform" },
			{ "x", a_x },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 },
			{ "cellId", "00001111" },
			{ "worldspaceId", "00002222" },
			{ "movementType", std::string{ a_movementType } }
		};
	}

	bool SawTransformFrom(const std::vector<Json>& a_packets, std::uint32_t a_playerId)
	{
		return std::any_of(a_packets.begin(), a_packets.end(), [a_playerId](const Json& packet) {
			return packet.value("type", std::string{}) == "transform" &&
				packet.value("playerId", 0u) == a_playerId;
		});
	}

	// Every reconnect must mint a fresh credential and retire the one it
	// replaced, so a client that churns repeatedly can never be resumed twice
	// from the same token.
	void RepeatedResumeCyclesRotateCredentials(std::string_view a_host, std::uint16_t a_port)
	{
		ChurnClient client{ a_host, a_port, "reconnect-churn-cycles" };
		client.Open();
		client.Activate(std::nullopt);
		const auto firstToken = client.ResumeToken();
		Require(!firstToken.empty(), "first activation did not issue a resume token");

		std::vector<std::string> retired{ firstToken };
		for (int cycle = 0; cycle < ResumeCycles; ++cycle) {
			const auto previousToken = client.ResumeToken();
			client.Close();
			client.Open();
			client.Activate(previousToken);
			Require(client.ResumeToken() != previousToken,
				"resume cycle " + std::to_string(cycle) + " did not rotate the resume token");
			Require(std::find(retired.begin(), retired.end(), client.ResumeToken()) == retired.end(),
				"resume cycle " + std::to_string(cycle) + " reissued a retired resume token");
			retired.push_back(client.ResumeToken());
		}
		client.Close();

		ChurnClient stale{ a_host, a_port, "reconnect-churn-cycles" };
		stale.Open();
		const auto code = stale.ActivateExpectingRejection(firstToken);
		Require(code == "invalid_resume",
			"a credential retired five cycles ago was not rejected as invalid_resume; received code='" + code + "'");
		stale.Close();

		std::cout << "PASS reconnect churn: " << ResumeCycles
			<< " resume cycles rotate credentials and retire every predecessor\n";
	}

	// A client that vanishes without a close frame still holds a live
	// server-side session. A proven credential must displace it immediately
	// rather than making the player wait out the transport keepalive, while an
	// unproven claim on the same profile must not.
	void ProvenResumeSupersedesTheAbandonedSession(std::string_view a_host, std::uint16_t a_port)
	{
		ChurnClient client{ a_host, a_port, "reconnect-churn-race" };
		client.Open();
		client.Activate(std::nullopt);
		const auto abandonedToken = client.ResumeToken();
		const auto abandonedPlayerId = client.PlayerId();
		{
			DroppedLinkWindow dropped;
			client.Abandon();
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
			dropped.Restore();
		}

		{
			ChurnClient impostor{ a_host, a_port, "reconnect-churn-race" };
			impostor.Open();
			const auto missing = impostor.ActivateExpectingRejection(std::nullopt);
			Require(missing == "duplicate_identity",
				"a credential-less claim on a live profile was not refused as duplicate_identity; received code='" + missing + "'");
			impostor.Close();
		}
		{
			ChurnClient impostor{ a_host, a_port, "reconnect-churn-race" };
			impostor.Open();
			const auto wrong = impostor.ActivateExpectingRejection(std::string{ "not-the-resume-token" });
			Require(wrong == "invalid_resume",
				"a wrong credential against a live profile was not refused as invalid_resume; received code='" + wrong + "'");
			impostor.Close();
		}

		ChurnClient resumed{ a_host, a_port, "reconnect-churn-race" };
		resumed.Open();
		const auto ready = resumed.Activate(abandonedToken);
		Require(resumed.ResumeToken() != abandonedToken,
			"superseding resume did not rotate the credential");
		Require(ready.value("playerId", 0u) == resumed.PlayerId(),
			"sessionReady player id did not match the resumed connection");
		Require(resumed.PlayerId() != abandonedPlayerId,
			"resumed session reused the abandoned connection's player id");
		resumed.Close();

		std::cout << "PASS reconnect churn: a proven credential supersedes the abandoned session while unproven claims do not\n";
	}

	// Run against a server configured with a short resume lifetime: once the
	// credential ages out it must be refused as resume_expired rather than
	// silently reissued.
	void ExpiredCredentialIsRefused(std::string_view a_host, std::uint16_t a_port, std::uint32_t a_lifetimeSeconds)
	{
		ChurnClient client{ a_host, a_port, "reconnect-churn-expiry" };
		client.Open();
		client.Activate(std::nullopt);
		const auto token = client.ResumeToken();
		client.Close();

		std::this_thread::sleep_for(std::chrono::seconds(a_lifetimeSeconds + 1));

		ChurnClient expired{ a_host, a_port, "reconnect-churn-expiry" };
		expired.Open();
		const auto code = expired.ActivateExpectingRejection(token);
		Require(code == "resume_expired",
			"an aged-out credential was not refused as resume_expired; received code='" + code + "'");
		expired.Close();

		std::cout << "PASS reconnect churn: an aged-out resume credential is refused as resume_expired\n";
	}

	// Relay has to follow the reconnected client: its peer must see the new
	// session's traffic, and must not keep receiving frames attributed to the
	// player id the churning client left behind.
	void ScopeSurvivesReconnectWithoutGhosts(std::string_view a_host, std::uint16_t a_port)
	{
		ChurnClient peer{ a_host, a_port, "reconnect-churn-peer" };
		peer.Open();
		peer.Activate(std::nullopt);

		ChurnClient churner{ a_host, a_port, "reconnect-churn-mover" };
		churner.Open();
		churner.Activate(std::nullopt);
		const auto priorPlayerId = churner.PlayerId();

		Require(peer.SendSnapshot(Transform(0.0, "spawn")), "failed to send peer spawn");
		Require(churner.SendSnapshot(Transform(0.0, "spawn")), "failed to send churner spawn");
		(void)peer.Collect(std::chrono::milliseconds(200));
		(void)churner.Collect(std::chrono::milliseconds(200));

		Require(churner.SendSnapshot(Transform(1.0, "normal")), "failed to send pre-reconnect movement");
		Json relayed;
		Require(peer.ReceiveUntil([priorPlayerId](const Json& packet) {
			return packet.value("type", std::string{}) == "transform" &&
				packet.value("playerId", 0u) == priorPlayerId;
		}, relayed, std::chrono::seconds(5)),
			"peer did not receive the churning client's pre-reconnect movement");

		const auto token = churner.ResumeToken();
		churner.Close();
		(void)peer.Collect(std::chrono::milliseconds(300));

		churner.Open();
		churner.Activate(token);
		const auto currentPlayerId = churner.PlayerId();
		Require(currentPlayerId != priorPlayerId, "reconnected client kept its previous player id");

		Require(churner.SendSnapshot(Transform(2.0, "spawn")), "failed to re-establish scope after reconnect");
		Require(peer.SendSnapshot(Transform(0.5, "normal")), "failed to send peer movement after reconnect");
		Require(churner.ReceiveUntil([&peer](const Json& packet) {
			return packet.value("type", std::string{}) == "transform" &&
				packet.value("playerId", 0u) == peer.PlayerId();
		}, relayed, std::chrono::seconds(5)),
			"reconnected client did not receive peer movement after re-establishing scope");

		Require(churner.SendSnapshot(Transform(3.0, "normal")), "failed to send post-reconnect movement");
		const auto observed = peer.Collect(std::chrono::milliseconds(750));
		Require(SawTransformFrom(observed, currentPlayerId),
			"peer did not receive the reconnected client's movement");
		Require(!SawTransformFrom(observed, priorPlayerId),
			"peer still received frames attributed to the abandoned player id after reconnect");

		churner.Close();
		peer.Close();

		std::cout << "PASS reconnect churn: interest scope follows the reconnected session and leaves no ghost player\n";
	}
}

int main(int argc, char** argv)
{
	if (argc != 3 && argc != 4) {
		std::cerr << "Usage: CommonwealthOnlineServerLiveReconnectChurnGnsSmoke HOST PORT [RESUME_LIFETIME_SECONDS]\n";
		return 2;
	}
	std::uint16_t port = 0;
	if (!ParsePort(argv[2], port)) {
		std::cerr << "Invalid port\n";
		return 2;
	}

	// The expiry scenario needs a server started with a matching short resume
	// lifetime, so it runs as its own invocation rather than beside scenarios
	// that assume a long-lived credential.
	if (argc == 4) {
		std::uint32_t lifetimeSeconds = 0;
		const std::string_view raw{ argv[3] };
		const auto parsed = std::from_chars(raw.data(), raw.data() + raw.size(), lifetimeSeconds);
		if (parsed.ec != std::errc{} || parsed.ptr != raw.data() + raw.size() || lifetimeSeconds < 1 || lifetimeSeconds > 60) {
			std::cerr << "Invalid resume lifetime seconds; expected 1-60\n";
			return 2;
		}
		try {
			ExpiredCredentialIsRefused(argv[1], port, lifetimeSeconds);
		} catch (const std::exception& error) {
			std::cerr << "FAIL reconnect churn: " << error.what() << '\n';
			return 1;
		}
		std::cout << "C++ GNS reconnect churn expiry smoke passed\n";
		return 0;
	}

	try {
		// Supersede runs first: it is the only scenario whose window is bounded
		// by the transport keepalive, and running it before the pacer's budget
		// is spent keeps its four connects back to back.
		ProvenResumeSupersedesTheAbandonedSession(argv[1], port);
		RepeatedResumeCyclesRotateCredentials(argv[1], port);
		ScopeSurvivesReconnectWithoutGhosts(argv[1], port);
	} catch (const std::exception& error) {
		std::cerr << "FAIL reconnect churn: " << error.what() << '\n';
		return 1;
	}

	std::cout << "C++ GNS reconnect churn smoke passed\n";
	return 0;
}

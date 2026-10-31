#include "ServerVoice.h"

#include "F4TVoiceWire.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::Delivery;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolHost;
	using CommonwealthOnline::Server::ProtocolStats;
	using CommonwealthOnline::Server::VoicePeerEntry;
	using CommonwealthOnline::Server::VoiceService;
	using CommonwealthOnline::Server::VoiceSessionState;

	constexpr std::size_t kPayloadBytes = 160;
	constexpr double kStep = 11.0;

	Json Snapshot(std::uint64_t a_revision, bool a_deafened = false, Json a_blocked = Json::array(), std::uint64_t a_profile = 0)
	{
		return Json{
			{ "type", "voiceState" },
			{ "revision", a_revision },
			{ "profile", a_profile },
			{ "rate", 48000 },
			{ "channels", 1 },
			{ "frameMs", 20 },
			{ "canSend", true },
			{ "canPlay", true },
			{ "deafened", a_deafened },
			{ "blockedPlayerIds", std::move(a_blocked) }
		};
	}

	Json With(Json a_packet, std::string_view a_key, Json a_value)
	{
		a_packet[std::string{ a_key }] = std::move(a_value);
		return a_packet;
	}

	Json Without(Json a_packet, std::string_view a_key)
	{
		a_packet.erase(std::string{ a_key });
		return a_packet;
	}

	Json Parse(const OutboundFrame& a_frame)
	{
		return Json::parse(std::string(a_frame.payload.begin(), a_frame.payload.end()));
	}

	bool IsAck(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::uint64_t a_revision)
	{
		return a_frames.size() == 1 && a_frames.front().connectionId == a_connectionId &&
			a_frames.front().delivery == Delivery::ReliableOrdered &&
			Parse(a_frames.front()).at("type") == "voiceStateAck" && Parse(a_frames.front()).at("revision") == a_revision;
	}

	bool IsNotice(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_code)
	{
		return a_frames.size() == 1 && a_frames.front().connectionId == a_connectionId &&
			Parse(a_frames.front()).at("type") == "voiceNotice" && Parse(a_frames.front()).at("code") == std::string{ a_code };
	}

	std::size_t CountRelayed(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		std::size_t count = 0;
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && frame.delivery == Delivery::UnreliableSequenced) {
				count++;
			}
		}
		return count;
	}

	bool HasTalk(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, bool a_talking)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && frame.delivery == Delivery::ReliableOrdered && Parse(frame).at("type") == "voiceTalk" &&
				Parse(frame).at("talking") == a_talking) {
				return true;
			}
		}
		return false;
	}

	struct Harness
	{
		ProtocolStats stats{};
		VoiceService service{ stats };
		VoiceSessionState state{};
		std::deque<VoiceSessionState> states;
		std::vector<VoicePeerEntry> peers;
	};

	std::vector<OutboundFrame> SendState(Harness& a_harness, const Json& a_packet, double a_now)
	{
		return a_harness.service.HandleState(7, a_harness.state, a_packet, a_now);
	}

	VoiceSessionState& AddPeer(Harness& a_harness, std::uint32_t a_connectionId, std::uint16_t a_playerId, std::string_view a_profileId, double a_x)
	{
		auto& state = a_harness.states.emplace_back();
		state.stateApplied = true;
		state.canSend = true;
		state.canPlay = true;
		F4T::Voice::VoicePeer view{
			.playerId = a_playerId,
			.active = true,
			.voiceV1 = true,
			.canPlay = true,
			.scope = F4T::InterestScope::StateScope{ "Cell", "WS", a_x, 0.0 }
		};
		a_harness.peers.push_back({ a_connectionId, view, &state, a_profileId });
		return state;
	}

	std::vector<std::uint8_t> Datagram(std::uint32_t a_sequence, bool a_burstStart = false, bool a_burstEnd = false)
	{
		F4T::Voice::VoiceHeader header;
		header.sequence = a_sequence;
		header.burstStart = a_burstStart;
		header.burstEnd = a_burstEnd;
		const std::vector<std::byte> payload(kPayloadBytes, std::byte{ 0x2A });
		std::vector<std::byte> bytes(F4T::Voice::kVoiceHeaderBytes + kPayloadBytes);
		const auto size = F4T::Voice::EncodeVoicePacket(header, payload, bytes);
		assert(size == bytes.size());
		const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
		return { data, data + size };
	}

	std::vector<OutboundFrame> SendVoice(Harness& a_harness, std::uint32_t a_connectionId, std::uint32_t a_sequence, double a_now)
	{
		return a_harness.service.HandleFrame(a_connectionId, Datagram(a_sequence), a_harness.peers, a_now);
	}

	void StaleRevisionAfterMismatchCannotRollBack()
	{
		Harness harness;
		assert(IsAck(SendState(harness, Snapshot(5), 500.0), 7, 5));
		assert(IsNotice(SendState(harness, Snapshot(6, false, Json::array(), 1), 501.0), 7, "profileMismatch"));
		assert(SendState(harness, Snapshot(5), 502.0).empty());
		assert(!harness.state.stateApplied && harness.state.revision == 6);
		assert(IsAck(SendState(harness, Snapshot(7), 503.0), 7, 7));
		assert(harness.state.stateApplied && harness.state.revision == 7);

		Harness second;
		assert(IsAck(SendState(second, Snapshot(6), 600.0), 7, 6));
		assert(IsNotice(SendState(second, Snapshot(7, false, Json::array(), 1), 601.0), 7, "profileMismatch"));
		assert(SendState(second, Snapshot(6), 602.0).empty());
		assert(IsAck(SendState(second, Snapshot(8), 603.0), 7, 8));
	}

	std::uint16_t SpeakerOf(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && frame.delivery == Delivery::UnreliableSequenced) {
				F4T::Voice::VoicePacket packet;
				assert(F4T::Voice::DecodeVoicePacket(std::as_bytes(std::span{ frame.payload }), F4T::Voice::VoiceDirection::ServerToClient, packet) == F4T::Voice::VoiceDecodeResult::Ok);
				return packet.header.speakerId;
			}
		}
		assert(false);
		return 0;
	}

	std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	Json TransformAt(double a_x)
	{
		return Json{ { "type", "transform" }, { "cellId", "100" }, { "worldspaceId", "F00D" }, { "x", a_x }, { "y", 0.0 }, { "z", 0.0 }, { "angleZ", 0.0 }, { "movementType", "spawn" } };
	}

	std::uint32_t JoinVoicePlayer(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_now, double a_x, std::string_view a_profileId, bool a_voiceCapable = true)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, 100.0);
		const Json hello = a_voiceCapable
			? Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "voice-v1" }) } }
			: Json{ { "type", "hello" }, { "protocolVersion", 2 } };
		assert(!a_host.HandleMessage(a_connectionId, EncodeJson(hello), a_now, 100.1).empty());
		a_host.BindSessionProfile(a_connectionId, a_profileId);
		(void)a_host.HandleMessage(a_connectionId, EncodeJson(TransformAt(a_x)), a_now + 0.1, 100.2);
		return Parse(welcome.front()).at("playerId").get<std::uint32_t>();
	}

	void WelcomeAdvertisesVoiceAndUnnegotiatedFramesAreDropped()
	{
		ProtocolHost host{ "Voice Test" };
		JoinVoicePlayer(host, 40, 1.0, 0.0, "profile-plain", false);
		const auto welcome = host.HandleConnected(41, 100.0);
		bool advertised = false;
		const auto packet = Parse(welcome.front());
		for (const auto& capability : packet.at("capabilities")) {
			advertised = advertised || capability == std::string{ "voice-v1" };
		}
		assert(advertised);
		assert(host.HandleMessage(40, Datagram(1), 2.0, 100.2).empty());
		assert(host.Stats().voiceDroppedUnnegotiated == 1);
		assert(host.Stats().voiceReceived == 1);
	}

	void ProtocolHostGatesVoiceAndControlsThroughHandleMessage()
	{
		ProtocolHost host{ "Voice Test" };
		const auto playerA = JoinVoicePlayer(host, 10, 1.0, 0.0, "profile-a");
		const auto playerB = JoinVoicePlayer(host, 20, 1.5, 10.0, "profile-b");
		assert(IsAck(host.HandleMessage(10, EncodeJson(Snapshot(1)), 2.0, 101.0), 10, 1));
		assert(IsAck(host.HandleMessage(20, EncodeJson(Snapshot(1)), 2.1, 101.1), 20, 1));

		const auto first = host.HandleMessage(10, Datagram(1), 3.0, 102.0);
		assert(CountRelayed(first, 20) == 1);
		assert(SpeakerOf(first, 20) == playerA);
		assert(host.Stats().voiceReceived == 1 && host.Stats().voiceAccepted == 1 && host.Stats().voiceRelayed == 1);

		assert(IsAck(host.HandleMessage(20, EncodeJson(Snapshot(2, true)), 4.0, 103.0), 20, 2));
		assert(CountRelayed(host.HandleMessage(10, Datagram(2), 5.0, 104.0), 20) == 0);
		assert(IsAck(host.HandleMessage(20, EncodeJson(Snapshot(3, false, Json::array({ playerA }))), 6.0, 105.0), 20, 3));
		assert(CountRelayed(host.HandleMessage(10, Datagram(3), 7.0, 106.0), 20) == 0);
		assert(IsAck(host.HandleMessage(20, EncodeJson(Snapshot(4)), 8.0, 107.0), 20, 4));

		assert(IsNotice(host.SetVoiceMutedByAdmin(std::nullopt, "profile-a", true, std::nullopt, 9.0).value().frames, 10, "serverMuted"));
		assert(CountRelayed(host.HandleMessage(10, Datagram(4), 10.0, 108.0), 20) == 0);
		assert(host.Stats().voiceDroppedMuted == 1);
		assert(IsNotice(host.SetVoiceMutedByAdmin(std::nullopt, "profile-a", false, std::nullopt, 11.0).value().frames, 10, "serverUnmuted"));
		assert(CountRelayed(host.HandleMessage(10, Datagram(5), 12.0, 109.0), 20) == 1);

		assert(IsNotice(host.SetVoiceMutedByAdmin(std::nullopt, "profile-a", true, std::nullopt, 13.0).value().frames, 10, "serverMuted"));
		host.HandleDisconnected(10, 14.0);
		const auto playerA2 = JoinVoicePlayer(host, 30, 15.0, 0.0, "profile-a");
		assert(playerA2 != playerA && playerB != playerA2);
		assert(CountRelayed(host.HandleMessage(30, Datagram(1), 16.0, 110.0), 20) == 0);
		assert(host.Stats().voiceDroppedNoState == 1);
		assert(IsAck(host.HandleMessage(30, EncodeJson(Snapshot(1)), 17.0, 111.0), 30, 1));
		assert(CountRelayed(host.HandleMessage(30, Datagram(2), 18.0, 112.0), 20) == 0);
		assert(host.Stats().voiceDroppedMuted == 2);
		assert(IsNotice(host.SetVoiceMutedByAdmin(std::nullopt, "profile-a", false, std::nullopt, 19.0).value().frames, 30, "serverUnmuted"));
		assert(CountRelayed(host.HandleMessage(30, Datagram(3), 20.0, 113.0), 20) == 1);
	}

	void ValidationAndRevisionRules()
	{
		Harness harness;
		double now = 100.0;
		assert(IsAck(SendState(harness, Snapshot(1), now), 7, 1));
		assert(harness.state.stateApplied && harness.state.revision == 1 && !harness.state.deafened);
		assert(harness.stats.voiceStatesApplied == 1);
		now += 1.0;
		assert(IsAck(SendState(harness, Snapshot(1), now), 7, 1));
		assert(harness.stats.voiceStatesApplied == 1);
		now += 1.0;
		assert(SendState(harness, Snapshot(1, true), now).empty());
		assert(!harness.state.deafened);
		now += 1.0;
		assert(IsAck(SendState(harness, Snapshot(2, true), now), 7, 2));
		assert(harness.state.deafened);
		now += 1.0;
		assert(SendState(harness, Snapshot(1), now).empty());
		now += 1.0;
		assert(SendState(harness, Snapshot(2, false), now).empty());
		assert(harness.state.revision == 2 && harness.state.deafened);
		assert(harness.stats.voiceStatesRejected == 3);
	}

	void MalformedSnapshotsAreRejectedWithoutAck()
	{
		Harness harness;
		double now = 100.0;
		assert(IsAck(SendState(harness, Snapshot(1), now), 7, 1));
		Json fifteen = Json::array();
		for (std::uint64_t playerId = 1; playerId <= 15; ++playerId) {
			fifteen.push_back(playerId);
		}
		Json sixteen = fifteen;
		sixteen.push_back(16);
		const std::vector<Json> malformed{
			With(Snapshot(3), "revision", 0),
			With(Snapshot(3), "revision", 9007199254740992ULL),
			With(Snapshot(3), "revision", -1),
			With(Snapshot(3), "revision", "3"),
			Without(Snapshot(3), "revision"),
			Without(Snapshot(3), "profile"),
			Without(Snapshot(3), "canSend"),
			With(Snapshot(3), "canPlay", 1),
			With(Snapshot(3), "deafened", "no"),
			With(Snapshot(3), "blockedPlayerIds", "1"),
			With(Snapshot(3), "blockedPlayerIds", Json::array({ 5, 3 })),
			With(Snapshot(3), "blockedPlayerIds", Json::array({ 3, 3 })),
			With(Snapshot(3), "blockedPlayerIds", Json::array({ 0 })),
			With(Snapshot(3), "blockedPlayerIds", Json::array({ 70000 })),
			With(Snapshot(3), "blockedPlayerIds", Json::array({ "4" })),
			With(Snapshot(3), "blockedPlayerIds", sixteen)
		};
		for (const auto& packet : malformed) {
			now += 1.0;
			assert(SendState(harness, packet, now).empty());
		}
		assert(harness.stats.voiceStatesRejected == malformed.size());
		assert(harness.state.revision == 1 && harness.state.blockedCount == 0);
		now += 1.0;
		assert(IsAck(SendState(harness, With(Snapshot(3), "blockedPlayerIds", fifteen), now), 7, 3));
		assert(harness.state.blockedCount == 15);
	}

	void ControlBucketDropWithoutAck()
	{
		Harness harness;
		assert(IsAck(SendState(harness, Snapshot(1), 200.0), 7, 1));
		assert(IsAck(SendState(harness, Snapshot(2), 200.0), 7, 2));
		assert(SendState(harness, Snapshot(3), 200.0).empty());
		assert(harness.stats.voiceStatesLimited == 1);
		assert(harness.state.revision == 2);
		assert(IsAck(SendState(harness, Snapshot(3), 201.0), 7, 3));
	}

	void ProfileMismatchDisablesWithNoticeLimit()
	{
		Harness harness;
		assert(IsAck(SendState(harness, Snapshot(1), 300.0), 7, 1));
		assert(IsNotice(SendState(harness, Snapshot(2, false, Json::array(), 1), 301.0), 7, "profileMismatch"));
		assert(!harness.state.stateApplied && !harness.state.canSend && !harness.state.canPlay);
		assert(SendState(harness, Snapshot(3, false, Json::array(), 1), 302.0).empty());
		assert(SendState(harness, Snapshot(4, false, Json::array(), 1), 310.9).empty());
		assert(IsNotice(SendState(harness, Snapshot(5, false, Json::array(), 1), 311.0), 7, "profileMismatch"));
		assert(IsAck(SendState(harness, Snapshot(6), 312.0), 7, 6));
		assert(harness.state.stateApplied);
	}

	void StaleProfileMismatchCannotDisable()
	{
		Harness harness;
		assert(IsAck(SendState(harness, Snapshot(5), 400.0), 7, 5));
		assert(SendState(harness, Snapshot(4, false, Json::array(), 1), 401.0).empty());
		assert(harness.state.stateApplied && harness.state.canSend && harness.state.revision == 5);
	}

	void EachProfileFieldMismatchDisablesVoice()
	{
		const std::vector<Json> mismatched{
			With(Snapshot(1), "rate", 44100),
			With(Snapshot(1), "channels", 2),
			With(Snapshot(1), "frameMs", 10)
		};
		for (const auto& packet : mismatched) {
			Harness harness;
			assert(IsNotice(SendState(harness, packet, 300.0), 7, "profileMismatch"));
			assert(!harness.state.stateApplied && !harness.state.canSend && !harness.state.canPlay);
			assert(harness.state.revision == 1);
		}
	}

	void ServerMuteDropsRelayAndNotifiesSender()
	{
		Harness harness;
		AddPeer(harness, 1, 1, "profile-a", 0.0);
		AddPeer(harness, 2, 2, "profile-b", 10.0);
		double now = 500.0;
		assert(CountRelayed(SendVoice(harness, 1, 1, now), 2) == 1);
		now += kStep;
		assert(IsNotice(harness.service.SetServerMute(harness.peers, "profile-a", "profile-a", true, std::nullopt, now), 1, "serverMuted"));
		now += 1.0;
		assert(SendVoice(harness, 1, 2, now).empty());
		assert(harness.stats.voiceDroppedMuted == 1);
		now += kStep;
		assert(IsNotice(harness.service.SetServerMute(harness.peers, "profile-a", "profile-a", false, std::nullopt, now), 1, "serverUnmuted"));
		assert(CountRelayed(SendVoice(harness, 1, 2, now), 2) == 1);
		now += kStep;
		assert(IsNotice(harness.service.SetServerMute(harness.peers, "profile-a", "profile-a", true, 5.0, now), 1, "serverMuted"));
		now += 1.0;
		assert(SendVoice(harness, 1, 3, now).empty());
		now += 5.0;
		assert(CountRelayed(SendVoice(harness, 1, 3, now), 2) == 1);
	}

	void ReconnectNeedsNewStateAndKeepsProfileMute()
	{
		Harness harness;
		AddPeer(harness, 1, 1, "profile-a", 0.0);
		AddPeer(harness, 2, 2, "profile-b", 10.0);
		double now = 600.0;
		assert(harness.service.SetServerMute(harness.peers, "profile-a", "profile-a", true, std::nullopt, now).size() == 1);
		assert(harness.service.SetServerMute(harness.peers, "profile-offline", "profile-offline", true, std::nullopt, now).empty());
		now += kStep;
		VoiceSessionState fresh;
		harness.peers[0].connectionId = 3;
		harness.peers[0].state = &fresh;
		assert(SendVoice(harness, 3, 1, now).empty());
		assert(harness.stats.voiceDroppedNoState == 1);
		now += 1.0;
		assert(IsAck(harness.service.HandleState(3, fresh, Snapshot(1), now), 3, 1));
		now += 1.0;
		const auto muted = harness.stats.voiceDroppedMuted;
		assert(SendVoice(harness, 3, 1, now).empty());
		assert(harness.stats.voiceDroppedMuted == muted + 1);
		AddPeer(harness, 4, 4, "profile-offline", 0.0);
		now += 1.0;
		assert(SendVoice(harness, 4, 1, now).empty());
		assert(harness.stats.voiceDroppedMuted == muted + 2);
		now += kStep;
		assert(IsNotice(harness.service.SetServerMute(harness.peers, "profile-a", "profile-a", false, std::nullopt, now), 3, "serverUnmuted"));
		assert(CountRelayed(SendVoice(harness, 3, 1, now), 2) == 1);
	}

	void AdminResolvesPlayerIdToProfileMute()
	{
		ProtocolHost host{ "Voice Test" };
		const auto playerA = JoinVoicePlayer(host, 10, 1.0, 0.0, "profile-a");
		JoinVoicePlayer(host, 20, 1.5, 10.0, "profile-b");
		const auto byPlayer = host.SetVoiceMutedByAdmin(playerA, "", true, std::nullopt, 9.0);
		assert(byPlayer && byPlayer->profileId == "profile-a");
		assert(IsNotice(byPlayer->frames, 10, "serverMuted"));
		assert(!host.SetVoiceMutedByAdmin(9999, "", true, std::nullopt, 9.5));
		const auto byProfile = host.SetVoiceMutedByAdmin(std::nullopt, "profile-offline", true, 5.0, 10.0);
		assert(byProfile && byProfile->profileId == "profile-offline" && byProfile->frames.empty());
		assert(!host.SetVoiceMutedByAdmin(std::nullopt, "", true, std::nullopt, 10.0));
		const auto unmuted = host.SetVoiceMutedByAdmin(playerA, "", false, std::nullopt, 11.0);
		assert(unmuted && IsNotice(unmuted->frames, 10, "serverUnmuted"));
	}

	void SteamKeyedMuteFollowsIdentityAcrossProfileChange()
	{
		Harness harness;
		AddPeer(harness, 1, 1, "profile-a", 0.0);
		AddPeer(harness, 2, 2, "profile-b", 10.0);
		harness.peers[0].steamId = "76561198000000001";
		double now = 700.0;
		assert(CountRelayed(SendVoice(harness, 1, 1, now), 2) == 1);
		now += kStep;
		assert(IsNotice(harness.service.SetServerMute(harness.peers, "76561198000000001", "profile-a", true, std::nullopt, now), 1, "serverMuted"));
		now += 1.0;
		assert(SendVoice(harness, 1, 2, now).empty());
		harness.peers[0].profileId = "profile-renamed";
		now += 1.0;
		assert(SendVoice(harness, 1, 3, now).empty());
		assert(harness.stats.voiceDroppedMuted == 2);
		now += kStep;
		assert(IsNotice(harness.service.SetServerMute(harness.peers, "76561198000000001", "profile-renamed", false, std::nullopt, now), 1, "serverUnmuted"));
		assert(CountRelayed(SendVoice(harness, 1, 4, now), 2) == 1);
	}

	void ProfileKeyedMuteStillAppliesToSteamVerifiedPlayer()
	{
		Harness harness;
		AddPeer(harness, 1, 1, "profile-a", 0.0);
		AddPeer(harness, 2, 2, "profile-b", 10.0);
		harness.peers[0].steamId = "76561198000000001";
		double now = 720.0;
		assert(harness.service.SetServerMute(harness.peers, "profile-a", "profile-a", true, std::nullopt, now).size() == 1);
		now += kStep;
		assert(SendVoice(harness, 1, 1, now).empty());
		assert(harness.stats.voiceDroppedMuted == 1);
	}

	void AdminMuteKeysOnVerifiedSteamIdAcrossReconnect()
	{
		ProtocolHost host{ "Voice Test" };
		const auto playerA = JoinVoicePlayer(host, 10, 1.0, 0.0, "profile-a");
		JoinVoicePlayer(host, 20, 1.5, 10.0, "profile-b");
		host.BindSessionSteamId(10, "76561198000000001");
		assert(IsAck(host.HandleMessage(10, EncodeJson(Snapshot(1)), 2.0, 101.0), 10, 1));
		assert(IsAck(host.HandleMessage(20, EncodeJson(Snapshot(1)), 2.1, 101.1), 20, 1));
		const auto muted = host.SetVoiceMutedByAdmin(playerA, "", true, std::nullopt, 9.0);
		assert(muted && muted->profileId == "profile-a" && IsNotice(muted->frames, 10, "serverMuted"));
		assert(CountRelayed(host.HandleMessage(10, Datagram(1), 10.0, 108.0), 20) == 0);
		assert(host.Stats().voiceDroppedMuted == 1);
		host.HandleDisconnected(10, 11.0);

		const auto playerRenamed = JoinVoicePlayer(host, 30, 12.0, 0.0, "profile-renamed");
		host.BindSessionSteamId(30, "76561198000000001");
		assert(IsAck(host.HandleMessage(30, EncodeJson(Snapshot(1)), 13.0, 113.0), 30, 1));
		assert(CountRelayed(host.HandleMessage(30, Datagram(1), 14.0, 114.0), 20) == 0);
		assert(host.Stats().voiceDroppedMuted == 2);
		const auto unmuted = host.SetVoiceMutedByAdmin(playerRenamed, "", false, std::nullopt, 15.0);
		assert(unmuted && IsNotice(unmuted->frames, 30, "serverUnmuted"));
		assert(CountRelayed(host.HandleMessage(30, Datagram(2), 16.0, 116.0), 20) == 1);
	}

	void AdminProfileMuteClearsAfterSteamIdBinds()
	{
		ProtocolHost host{ "Voice Test" };
		const auto offline = host.SetVoiceMutedByAdmin(std::nullopt, "profile-a", true, std::nullopt, 8.0);
		assert(offline && offline->frames.empty());
		JoinVoicePlayer(host, 10, 1.0, 0.0, "profile-a");
		JoinVoicePlayer(host, 20, 1.5, 10.0, "profile-b");
		host.BindSessionSteamId(10, "76561198000000001");
		assert(IsAck(host.HandleMessage(10, EncodeJson(Snapshot(1)), 2.0, 101.0), 10, 1));
		assert(IsAck(host.HandleMessage(20, EncodeJson(Snapshot(1)), 2.1, 101.1), 20, 1));
		assert(CountRelayed(host.HandleMessage(10, Datagram(1), 10.0, 108.0), 20) == 0);
		assert(host.Stats().voiceDroppedMuted == 1);
		const auto unmuted = host.SetVoiceMutedByAdmin(std::nullopt, "profile-a", false, std::nullopt, 11.0);
		assert(unmuted && IsNotice(unmuted->frames, 10, "serverUnmuted"));
		assert(CountRelayed(host.HandleMessage(10, Datagram(2), 12.0, 110.0), 20) == 1);
	}

	void TalkEndReachesListenerThatMovedOutOfRange()
	{
		Harness harness;
		AddPeer(harness, 1, 1, "profile-a", 0.0);
		AddPeer(harness, 2, 2, "profile-b", 100.0);
		harness.peers.back().peer.canPlay = false;
		assert(HasTalk(harness.service.HandleFrame(1, Datagram(1, true), harness.peers, 1.0), 2, true));
		harness.peers.back().peer.scope = F4T::InterestScope::StateScope{ "Cell", "WS", 1500.0, 0.0 };
		assert(HasTalk(harness.service.Poll(harness.peers, 1.5), 2, false));
		assert(harness.service.Poll(harness.peers, 2.0).empty());
	}

	void BothBurstFlagsEmitAtMostOneTalkFramePerPacket()
	{
		Harness harness;
		AddPeer(harness, 1, 1, "profile-a", 0.0);
		AddPeer(harness, 2, 2, "profile-b", 100.0);
		harness.peers.back().peer.canPlay = false;
		std::size_t talk = 0;
		for (std::uint32_t sequence = 1; sequence <= 20; ++sequence) {
			const auto frames = harness.service.HandleFrame(1, Datagram(sequence, true, true), harness.peers, 1.0 + sequence * 0.05);
			assert(frames.size() <= 1);
			talk += frames.size();
		}
		assert(talk == 1);
	}

	void TalkerDisconnectEndsTalkIndicator()
	{
		ProtocolHost host{ "Voice Test" };
		JoinVoicePlayer(host, 10, 1.0, 0.0, "profile-a");
		JoinVoicePlayer(host, 20, 1.5, 10.0, "profile-b");
		assert(IsAck(host.HandleMessage(10, EncodeJson(Snapshot(1)), 2.0, 101.0), 10, 1));
		assert(IsAck(host.HandleMessage(20, EncodeJson(With(Snapshot(1), "canPlay", false)), 2.1, 101.1), 20, 1));
		assert(HasTalk(host.HandleMessage(10, Datagram(1, true), 3.0, 102.0), 20, true));
		assert(HasTalk(host.HandleDisconnected(10, 3.5), 20, false));
	}
}

int main()
{
	ValidationAndRevisionRules();
	MalformedSnapshotsAreRejectedWithoutAck();
	ControlBucketDropWithoutAck();
	ProfileMismatchDisablesWithNoticeLimit();
	EachProfileFieldMismatchDisablesVoice();
	StaleProfileMismatchCannotDisable();
	ServerMuteDropsRelayAndNotifiesSender();
	ReconnectNeedsNewStateAndKeepsProfileMute();
	StaleRevisionAfterMismatchCannotRollBack();
	WelcomeAdvertisesVoiceAndUnnegotiatedFramesAreDropped();
	ProtocolHostGatesVoiceAndControlsThroughHandleMessage();
	TalkerDisconnectEndsTalkIndicator();
	AdminResolvesPlayerIdToProfileMute();
	SteamKeyedMuteFollowsIdentityAcrossProfileChange();
	ProfileKeyedMuteStillAppliesToSteamVerifiedPlayer();
	AdminMuteKeysOnVerifiedSteamIdAcrossReconnect();
	AdminProfileMuteClearsAfterSteamIdBinds();
	TalkEndReachesListenerThatMovedOutOfRange();
	BothBurstFlagsEmitAtMostOneTalkFramePerPacket();
	return 0;
}

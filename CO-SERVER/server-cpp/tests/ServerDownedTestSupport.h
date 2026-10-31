#pragma once

#include "ServerProtocol.h"
#include "ServerConfiguration.h"

#include "F4TGnsSnapshotEnvelope.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ServerDownedTestSupport
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolHost;

	inline std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	inline std::vector<std::uint8_t> EncodeSnapshot(const Json& a_packet, std::uint32_t a_sequence)
	{
		const auto json = a_packet.dump();
		std::vector<std::uint8_t> output(F4T::GnsSnapshotEnvelope::HeaderSize + json.size());
		assert(F4T::GnsSnapshotEnvelope::WriteHeader(
			std::span<std::uint8_t>{ output }, F4T::GnsSnapshotEnvelope::Family::Transform, a_sequence));
		std::copy(json.begin(), json.end(), output.begin() + static_cast<std::ptrdiff_t>(F4T::GnsSnapshotEnvelope::HeaderSize));
		return output;
	}

	inline Json DecodePacket(const OutboundFrame& a_frame)
	{
		F4T::GnsSnapshotEnvelope::Header header{};
		const auto decode = F4T::GnsSnapshotEnvelope::Decode(a_frame.payload, header);
		std::span<const std::uint8_t> payload{ a_frame.payload };
		if (decode == F4T::GnsSnapshotEnvelope::DecodeResult::Valid) {
			payload = payload.subspan(F4T::GnsSnapshotEnvelope::HeaderSize);
		}
		return Json::parse(std::string{
			reinterpret_cast<const char*>(payload.data()),
			payload.size()
		});
	}

	inline std::vector<Json> FramesOfType(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_type)
	{
		std::vector<Json> packets;
		for (const auto& frame : a_frames) {
			if (frame.connectionId != a_connectionId || frame.payload.empty()) continue;
			auto packet = DecodePacket(frame);
			if (packet.value("type", std::string{}) == a_type) packets.push_back(std::move(packet));
		}
		return packets;
	}

	inline std::size_t CountOfType(const std::vector<OutboundFrame>& a_frames, std::string_view a_type)
	{
		std::size_t count = 0;
		for (const auto& frame : a_frames) {
			if (frame.payload.empty()) continue;
			if (DecodePacket(frame).value("type", std::string{}) == a_type) count++;
		}
		return count;
	}

	inline std::uint32_t PlayerIdOf(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId != a_connectionId || frame.payload.empty()) continue;
			const auto packet = DecodePacket(frame);
			if (packet.value("type", std::string{}) == "sessionReady") {
				return packet.at("playerId").get<std::uint32_t>();
			}
		}
		assert(false);
		return 0;
	}

	inline std::vector<OutboundFrame> JoinFrames(ProtocolHost& a_host, std::uint32_t a_connection, const char* a_profileId)
	{
		(void)a_host.HandleConnected(a_connection, 100.0);
		a_host.BindSessionProfile(a_connection, a_profileId);
		return a_host.HandleMessage(a_connection, EncodeJson(Json{
			{ "type", "hello" },
			{ "protocolVersion", 2u },
			{ "capabilities", Json::array() } }), 1.0, 100.0);
	}

	inline std::uint32_t Join(ProtocolHost& a_host, std::uint32_t a_connection, const char* a_profileId)
	{
		const auto frames = JoinFrames(a_host, a_connection, a_profileId);
		assert(!frames.empty());
		return PlayerIdOf(frames, a_connection);
	}

	inline std::uint32_t transformSequence{ 1 };

	inline void Move(
		ProtocolHost& a_host,
		std::uint32_t a_connection,
		double a_monotonic,
		double a_x,
		const char* a_movementType = "spawn",
		const char* a_cellId = "100",
		const char* a_worldspaceId = "F00D")
	{
		(void)a_host.HandleMessage(a_connection, EncodeSnapshot(Json{
			{ "type", "transform" },
			{ "cellId", a_cellId },
			{ "worldspaceId", a_worldspaceId },
			{ "x", a_x },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 },
			{ "movementType", a_movementType } }, transformSequence++), a_monotonic, 100.0);
	}

	inline Json Vitals(bool a_isDowned, bool a_isDead, std::int32_t a_healthPercent)
	{
		return Json{
			{ "healthPercent", a_healthPercent },
			{ "isDowned", a_isDowned },
			{ "isDead", a_isDead }
		};
	}

	inline std::vector<OutboundFrame> SendVitals(
		ProtocolHost& a_host,
		std::uint32_t a_connection,
		double a_monotonic,
		bool a_isDowned,
		bool a_isDead,
		std::int32_t a_healthPercent)
	{
		return a_host.HandleMessage(a_connection, EncodeJson(Json{
			{ "type", "playerState" },
			{ "vitals", Vitals(a_isDowned, a_isDead, a_healthPercent) } }), a_monotonic, 100.0);
	}

	inline std::vector<OutboundFrame> SendRevive(ProtocolHost& a_host, std::uint32_t a_connection, std::uint32_t a_target, double a_monotonic)
	{
		return a_host.HandleMessage(a_connection, EncodeJson(Json{
			{ "type", "reviveRequest" },
			{ "target", a_target } }), a_monotonic, 100.0);
	}

	inline std::vector<OutboundFrame> SendReviveClaim(
		ProtocolHost& a_host,
		std::uint32_t a_connection,
		std::uint32_t a_target,
		std::uint32_t a_stimpaks,
		double a_monotonic)
	{
		return a_host.HandleMessage(a_connection, EncodeJson(Json{
			{ "type", "reviveRequest" },
			{ "target", a_target },
			{ "stimpaks", a_stimpaks } }), a_monotonic, 100.0);
	}

	inline std::vector<OutboundFrame> SendReviveCancel(
		ProtocolHost& a_host,
		std::uint32_t a_connection,
		std::uint32_t a_target,
		const char* a_reason,
		double a_monotonic)
	{
		return a_host.HandleMessage(a_connection, EncodeJson(Json{
			{ "type", "reviveCancel" },
			{ "target", a_target },
			{ "reason", a_reason } }), a_monotonic, 100.0);
	}

	inline std::vector<OutboundFrame> SendCheckpoint(
		ProtocolHost& a_host,
		std::uint32_t a_connection,
		bool a_interior,
		std::uint32_t a_space,
		double a_monotonic)
	{
		return a_host.HandleMessage(a_connection, EncodeJson(Json{
			{ "type", "checkpoint" },
			{ "interior", a_interior },
			{ "space", a_space },
			{ "x", 0.0 },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "rotZ", 0.0 } }), a_monotonic, 100.0);
	}

	// Constraint: tests pinning the clock, range gate or grace revive instantly.
	inline void UseInstantRevive(ProtocolHost& a_host)
	{
		F4T::DownedState::Config config;
		config.reviveChannelSeconds = 0.0;
		a_host.SetDownedConfig(config);
	}

	inline std::vector<OutboundFrame> SendRespawn(
		ProtocolHost& a_host,
		std::uint32_t a_connection,
		const char* a_point,
		std::optional<std::uint32_t> a_friendId,
		double a_monotonic)
	{
		Json request{
			{ "type", "respawnRequest" },
			{ "point", a_point }
		};
		if (a_friendId) request["friendId"] = *a_friendId;
		return a_host.HandleMessage(a_connection, EncodeJson(request), a_monotonic, 100.0);
	}
}


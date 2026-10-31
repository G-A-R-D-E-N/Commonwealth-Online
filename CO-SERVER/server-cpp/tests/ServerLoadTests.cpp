#include "ServerProtocol.h"

#include "F4TGnsSnapshotEnvelope.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::OutboundFrame;
	using F4T::GnsSnapshotEnvelope::DecodeResult;
	using F4T::GnsSnapshotEnvelope::Family;

	std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	std::vector<std::uint8_t> EncodeSnapshot(const Json& a_packet, std::uint32_t a_sequence)
	{
		const auto text = a_packet.dump();
		std::vector<std::uint8_t> output(F4T::GnsSnapshotEnvelope::HeaderSize + text.size());
		assert(F4T::GnsSnapshotEnvelope::WriteHeader(std::span<std::uint8_t>{ output }, Family::Transform, a_sequence));
		std::copy(text.begin(), text.end(), output.begin() + static_cast<std::ptrdiff_t>(F4T::GnsSnapshotEnvelope::HeaderSize));
		return output;
	}

	Json DecodePacket(const OutboundFrame& a_frame)
	{
		F4T::GnsSnapshotEnvelope::Header header{};
		const auto decode = F4T::GnsSnapshotEnvelope::Decode(a_frame.payload, header);
		std::span<const std::uint8_t> payload{ a_frame.payload };
		if (decode == DecodeResult::Valid) {
			payload = payload.subspan(F4T::GnsSnapshotEnvelope::HeaderSize);
		} else {
			assert(decode == DecodeResult::NotEnvelope);
		}
		return Json::parse(std::string{
			reinterpret_cast<const char*>(payload.data()),
			payload.size()
		});
	}

	std::vector<const OutboundFrame*> FramesOfType(
		const std::vector<OutboundFrame>& a_frames,
		std::string_view a_type)
	{
		std::vector<const OutboundFrame*> matches;
		for (const auto& frame : a_frames) {
			if (!frame.payload.empty() && DecodePacket(frame).value("type", std::string{}) == a_type) {
				matches.push_back(&frame);
			}
		}
		return matches;
	}

	Json Spawn(std::size_t a_scope, double a_x)
	{
		char cell[9]{};
		char world[9]{};
		std::snprintf(cell, sizeof(cell), "%08X", static_cast<unsigned int>(0x1000 + a_scope));
		std::snprintf(world, sizeof(world), "%08X", static_cast<unsigned int>(0x2000 + a_scope));
		return Json{
			{ "type", "transform" },
			{ "x", a_x }, { "y", 0.0 }, { "z", 0.0 }, { "angleZ", 0.0 },
			{ "cellId", cell }, { "worldspaceId", world }, { "movementType", "spawn" }
		};
	}

	void RunLoad(std::size_t a_count)
	{
		using CommonwealthOnline::Server::ProtocolHost;
		assert(a_count >= 4 && a_count % 4 == 0);

		ProtocolHost host{ "Load Test" };
		std::vector<std::uint32_t> connections;
		std::unordered_map<std::uint32_t, std::uint32_t> playerByConnection;
		connections.reserve(a_count);
		std::set<std::uint32_t> uniquePlayers;

		for (std::size_t index = 0; index < a_count; ++index) {
			const auto connectionId = static_cast<std::uint32_t>(1000 + index);
			connections.push_back(connectionId);
			const auto welcome = host.HandleConnected(connectionId, 1000.0 + static_cast<double>(index) * 0.01);
			assert(welcome.size() == 1);
			const auto playerId = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
			assert(playerId != 0);
			assert(uniquePlayers.insert(playerId).second);
			playerByConnection[connectionId] = playerId;
			const auto ready = host.HandleMessage(
				connectionId,
				EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }),
				1.0 + static_cast<double>(index) * 0.001,
				1001.0 + static_cast<double>(index) * 0.01);
			assert(!ready.empty());
		}
		assert(host.ActiveCount() == a_count);
		assert(uniquePlayers.size() == a_count);

		const auto perScope = a_count / 4;
		std::array<std::vector<std::uint32_t>, 4> scopeConnections;
		std::array<std::uint32_t, 4> authorityPlayers{};
		std::array<std::uint32_t, 4> authorityEpochs{};

		for (std::size_t index = 0; index < a_count; ++index) {
			const auto scope = index / perScope;
			const auto connectionId = connections[index];
			scopeConnections[scope].push_back(connectionId);
			const auto frames = host.HandleMessage(
				connectionId,
				EncodeSnapshot(Spawn(scope, static_cast<double>(index % perScope) * 10.0), 1),
				2.0 + static_cast<double>(index) * 0.01,
				1002.0 + static_cast<double>(index) * 0.01);

			for (const auto* frame : FramesOfType(frames, "npcAuthority")) {
				const auto packet = DecodePacket(*frame);
				if (packet.value("authorityCellId", std::string{}) == Spawn(scope, 0.0).at("cellId").get<std::string>() &&
					packet.value("authorityWorldspaceId", std::string{}) == Spawn(scope, 0.0).at("worldspaceId").get<std::string>()) {
					authorityPlayers[scope] = packet.value("authorityPlayerId", 0u);
					authorityEpochs[scope] = packet.value("authorityEpoch", 0u);
				}
			}
		}

		for (std::size_t scope = 0; scope < 4; ++scope) {
			const auto ownerConnection = scopeConnections[scope].front();
			assert(authorityPlayers[scope] == playerByConnection.at(ownerConnection));
			assert(authorityEpochs[scope] == 1);
		}

		for (std::size_t scope = 0; scope < 4; ++scope) {
			const auto sender = scopeConnections[scope].front();
			auto movement = Spawn(scope, 25.0);
			movement["movementType"] = "normal";
			const auto frames = host.HandleMessage(
				sender,
				EncodeSnapshot(movement, 2),
				4.0 + static_cast<double>(scope),
				1004.0 + static_cast<double>(scope));
			const auto transforms = FramesOfType(frames, "transform");
			assert(transforms.size() == perScope - 1);
			for (const auto* frame : transforms) {
				assert(std::find(scopeConnections[scope].begin(), scopeConnections[scope].end(), frame->connectionId) != scopeConnections[scope].end());
				assert(frame->connectionId != sender);
				const auto packet = DecodePacket(*frame);
				assert(packet.at("playerId") == playerByConnection.at(sender));
			}
		}

		for (std::size_t scope = 0; scope < 4; ++scope) {
			const auto oldOwner = scopeConnections[scope].front();
			const auto newOwner = scopeConnections[scope][1];
			const auto frames = host.HandleDisconnected(oldOwner, 1010.0 + static_cast<double>(scope));
			bool sawHandoff = false;
			for (const auto* frame : FramesOfType(frames, "npcAuthority")) {
				const auto packet = DecodePacket(*frame);
				if (packet.value("authorityPlayerId", 0u) == playerByConnection.at(newOwner) &&
					packet.value("authorityEpoch", 0u) == 2 &&
					packet.value("authorityCellId", std::string{}) == Spawn(scope, 0.0).at("cellId").get<std::string>()) {
					sawHandoff = true;
					break;
				}
			}
			assert(sawHandoff);
		}
		assert(host.ActiveCount() == a_count - 4);
	}
}

int main()
{
	RunLoad(16);
	RunLoad(32);
	RunLoad(64);
	return 0;
}

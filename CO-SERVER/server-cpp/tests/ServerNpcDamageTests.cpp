#include "ServerProtocol.h"

#include "F4TGnsSnapshotEnvelope.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::Delivery;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolHost;
	using F4T::GnsSnapshotEnvelope::DecodeResult;
	using F4T::GnsSnapshotEnvelope::Family;

	std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	std::vector<std::uint8_t> EncodeSnapshot(const Json& a_packet, Family a_family, std::uint32_t a_sequence)
	{
		const auto json = a_packet.dump();
		std::vector<std::uint8_t> output(F4T::GnsSnapshotEnvelope::HeaderSize + json.size());
		assert(F4T::GnsSnapshotEnvelope::WriteHeader(std::span<std::uint8_t>{ output }, a_family, a_sequence));
		std::copy(json.begin(), json.end(), output.begin() + static_cast<std::ptrdiff_t>(F4T::GnsSnapshotEnvelope::HeaderSize));
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

	const OutboundFrame* FindFrame(
		const std::vector<OutboundFrame>& a_frames,
		std::uint32_t a_connectionId,
		std::string_view a_type)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && !frame.payload.empty() &&
				DecodePacket(frame).value("type", std::string{}) == a_type) {
				return &frame;
			}
		}
		return nullptr;
	}

	Json Transform(double a_x, std::string_view a_cellId = "CAFE", std::string_view a_movementType = "spawn")
	{
		return Json{
			{ "type", "transform" },
			{ "cellId", std::string{ a_cellId } },
			{ "worldspaceId", "F00D" },
			{ "x", a_x },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 },
			{ "movementType", std::string{ a_movementType } }
		};
	}

	Json NpcStates(std::uint32_t a_authorityEpoch, std::uint32_t a_firstNpcId, std::size_t a_count, double a_x, bool a_isDead = false)
	{
		Json npcs = Json::array();
		for (std::size_t index = 0; index < a_count; ++index) {
			npcs.push_back(Json{
				{ "npcId", a_firstNpcId + static_cast<std::uint32_t>(index) },
				{ "baseFormId", "5678" },
				{ "sourceFormId", "1234" },
				{ "cellId", "CAFE" },
				{ "worldspaceId", "F00D" },
				{ "x", a_x }, { "y", 0.0 }, { "z", 0.0 }, { "angleZ", 0.0 },
				{ "isDead", a_isDead },
				{ "health", 100.0 }, { "maxHealth", 100.0 }
			});
		}
		return Json{
			{ "type", "npcState" },
			{ "authorityEpoch", a_authorityEpoch },
			{ "authorityCellId", "CAFE" },
			{ "authorityWorldspaceId", "F00D" },
			{ "fullReplace", true },
			{ "npcs", npcs }
		};
	}

	Json NpcDamage(
		std::uint32_t a_targetPlayerId,
		std::uint32_t a_npcId,
		std::uint32_t a_sequence,
		double a_damage,
		std::uint32_t a_authorityEpoch,
		std::string_view a_sourceFormId = "1234")
	{
		return Json{
			{ "type", "npcDamage" },
			{ "targetPlayerId", a_targetPlayerId },
			{ "npcId", a_npcId },
			{ "cellId", "CAFE" },
			{ "worldspaceId", "F00D" },
			{ "sourceFormId", std::string{ a_sourceFormId } },
			{ "sequence", a_sequence },
			{ "authorityEpoch", a_authorityEpoch },
			{ "damage", a_damage },
			{ "weaponFormId", "1234" }
		};
	}

	struct Arena
	{
		std::uint32_t authorityPlayerId{};
		std::uint32_t targetPlayerId{};
		std::uint32_t epoch{};
	};

	Arena SetUpArena(ProtocolHost& a_host)
	{
		Arena arena;
		const auto welcomeA = a_host.HandleConnected(10, 100.0);
		arena.authorityPlayerId = DecodePacket(welcomeA.front()).at("playerId").get<std::uint32_t>();
		assert(FindFrame(a_host.HandleMessage(10, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 1.0, 100.1), 10, "sessionReady") != nullptr);

		const auto welcomeB = a_host.HandleConnected(20, 100.2);
		arena.targetPlayerId = DecodePacket(welcomeB.front()).at("playerId").get<std::uint32_t>();
		assert(FindFrame(a_host.HandleMessage(20, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 1.1, 100.3), 20, "sessionReady") != nullptr);

		const auto spawnA = a_host.HandleMessage(10, EncodeSnapshot(Transform(0.0), Family::Transform, 1), 2.0, 101.0);
		const auto* authorityFrame = FindFrame(spawnA, 10, "npcAuthority");
		assert(authorityFrame != nullptr);
		arena.epoch = DecodePacket(*authorityFrame).at("authorityEpoch").get<std::uint32_t>();

		a_host.HandleMessage(20, EncodeSnapshot(Transform(20.0), Family::Transform, 1), 2.1, 101.1);
		a_host.HandleMessage(10, EncodeSnapshot(NpcStates(arena.epoch, 42, 1, 20.0), Family::NpcState, 1), 2.5, 101.5);
		return arena;
	}

	void AssertRejected(ProtocolHost& a_host, std::uint32_t a_connectionId, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		const auto before = a_host.Stats();
		assert(a_host.HandleMessage(a_connectionId, EncodeJson(a_packet), a_monotonicSeconds, a_unixTimeSeconds).empty());
		assert(a_host.Stats().npcDamageRejected == before.npcDamageRejected + 1);
	}
}

int main()
{
	{
		ProtocolHost host{ "NPC Damage Route" };
		const auto arena = SetUpArena(host);
		const auto frames = host.HandleMessage(10, EncodeJson(NpcDamage(arena.targetPlayerId, 42, 1, 25.0, arena.epoch)), 3.0, 102.0);
		assert(frames.size() == 1);
		const auto* routed = FindFrame(frames, 20, "npcDamage");
		assert(routed != nullptr);
		assert(routed->delivery == Delivery::ReliableOrdered);
		const auto relay = DecodePacket(*routed);
		assert(relay.at("targetPlayerId").get<std::uint32_t>() == arena.targetPlayerId);
		assert(relay.at("npcId") == 42);
		assert(relay.at("cellId") == "0000CAFE");
		assert(relay.at("worldspaceId") == "0000F00D");
		assert(relay.at("sourceFormId") == "00001234");
		assert(relay.at("sequence") == 1);
		assert(relay.at("damage") == 25.0);
		assert(relay.at("weaponFormId") == "00001234");
		assert(relay.at("authorityPlayerId").get<std::uint32_t>() == arena.authorityPlayerId);
		assert(relay.at("serverTime") == 102.0);
		assert(!relay.contains("authorityEpoch"));
		assert(host.Stats().npcDamageReceived == 1);
		assert(host.Stats().npcDamageRouted == 1);
		assert(host.Stats().npcDamageRejected == 0);
	}

	{
		ProtocolHost host{ "NPC Damage Authority" };
		const auto arena = SetUpArena(host);
		AssertRejected(host, 20, NpcDamage(arena.authorityPlayerId, 42, 1, 5.0, arena.epoch), 3.0, 102.0);
	}

	{
		ProtocolHost host{ "NPC Damage Epoch" };
		const auto arena = SetUpArena(host);
		AssertRejected(host, 10, NpcDamage(arena.targetPlayerId, 42, 1, 5.0, arena.epoch + 99), 3.0, 102.0);
	}

	{
		ProtocolHost host{ "NPC Damage Sequence" };
		const auto arena = SetUpArena(host);
		assert(FindFrame(host.HandleMessage(10, EncodeJson(NpcDamage(arena.targetPlayerId, 42, 5, 5.0, arena.epoch)), 3.0, 102.0), 20, "npcDamage") != nullptr);
		AssertRejected(host, 10, NpcDamage(arena.targetPlayerId, 42, 5, 5.0, arena.epoch), 3.1, 102.1);
		AssertRejected(host, 10, NpcDamage(arena.targetPlayerId, 42, 4, 5.0, arena.epoch), 3.2, 102.2);
	}

	{
		ProtocolHost host{ "NPC Damage Unknown" };
		const auto arena = SetUpArena(host);
		AssertRejected(host, 10, NpcDamage(arena.targetPlayerId, 999, 1, 5.0, arena.epoch), 3.0, 102.0);
	}

	{
		ProtocolHost host{ "NPC Damage Source" };
		const auto arena = SetUpArena(host);
		AssertRejected(host, 10, NpcDamage(arena.targetPlayerId, 42, 1, 5.0, arena.epoch, "9999"), 3.0, 102.0);
	}

	{
		ProtocolHost host{ "NPC Damage Dead" };
		const auto arena = SetUpArena(host);
		host.HandleMessage(10, EncodeSnapshot(NpcStates(arena.epoch, 42, 1, 20.0, true), Family::NpcState, 2), 2.6, 101.6);
		AssertRejected(host, 10, NpcDamage(arena.targetPlayerId, 42, 1, 5.0, arena.epoch), 3.0, 102.0);
	}

	{
		ProtocolHost host{ "NPC Damage Stale Snapshot" };
		const auto arena = SetUpArena(host);
		host.HandleMessage(20, EncodeSnapshot(Transform(20.0), Family::Transform, 2), 7.55, 107.55);
		AssertRejected(host, 10, NpcDamage(arena.targetPlayerId, 42, 1, 5.0, arena.epoch), 7.6, 107.6);
	}

	{
		ProtocolHost host{ "NPC Damage Stale Target" };
		const auto arena = SetUpArena(host);
		host.HandleMessage(10, EncodeSnapshot(NpcStates(arena.epoch, 42, 1, 20.0), Family::NpcState, 2), 7.0, 107.0);
		AssertRejected(host, 10, NpcDamage(arena.targetPlayerId, 42, 1, 5.0, arena.epoch), 7.2, 107.2);
	}

	{
		ProtocolHost host{ "NPC Damage Self" };
		const auto arena = SetUpArena(host);
		AssertRejected(host, 10, NpcDamage(arena.authorityPlayerId, 42, 1, 5.0, arena.epoch), 3.0, 102.0);
	}

	{
		ProtocolHost host{ "NPC Damage Offline" };
		const auto arena = SetUpArena(host);
		host.HandleDisconnected(20, 2.9);
		AssertRejected(host, 10, NpcDamage(arena.targetPlayerId, 42, 1, 5.0, arena.epoch), 3.0, 102.0);
	}

	{
		ProtocolHost host{ "NPC Damage Cell" };
		const auto arena = SetUpArena(host);
		host.HandleMessage(20, EncodeSnapshot(Transform(20.0, "BEEF", "teleport"), Family::Transform, 2), 2.9, 102.9);
		AssertRejected(host, 10, NpcDamage(arena.targetPlayerId, 42, 1, 5.0, arena.epoch), 3.0, 102.0);
	}

	{
		ProtocolHost host{ "NPC Damage Range" };
		const auto arena = SetUpArena(host);
		host.HandleMessage(20, EncodeSnapshot(Transform(100000.0, "CAFE", "teleport"), Family::Transform, 2), 2.9, 102.9);
		AssertRejected(host, 10, NpcDamage(arena.targetPlayerId, 42, 1, 5.0, arena.epoch), 3.0, 102.0);
	}

	{
		ProtocolHost host{ "NPC Damage Per Target Rate" };
		const auto arena = SetUpArena(host);
		for (std::uint32_t sequence = 1; sequence <= 5; ++sequence) {
			const auto frames = host.HandleMessage(10, EncodeJson(NpcDamage(arena.targetPlayerId, 42, sequence, 1.0, arena.epoch)), 3.0 + 0.01 * sequence, 102.0);
			assert(FindFrame(frames, 20, "npcDamage") != nullptr);
		}
		AssertRejected(host, 10, NpcDamage(arena.targetPlayerId, 42, 6, 1.0, arena.epoch), 3.07, 102.07);
		assert(FindFrame(host.HandleMessage(10, EncodeJson(NpcDamage(arena.targetPlayerId, 42, 6, 1.0, arena.epoch)), 4.2, 103.2), 20, "npcDamage") != nullptr);
	}

	{
		ProtocolHost host{ "NPC Damage Per Session Rate" };
		const auto arena = SetUpArena(host);
		host.HandleMessage(10, EncodeSnapshot(NpcStates(arena.epoch, 100, 31, 20.0), Family::NpcState, 2), 2.6, 101.6);
		for (std::uint32_t index = 0; index < 30; ++index) {
			const auto frames = host.HandleMessage(10, EncodeJson(NpcDamage(arena.targetPlayerId, 100 + index, index + 1, 1.0, arena.epoch)), 3.0 + 0.01 * index, 102.0);
			assert(FindFrame(frames, 20, "npcDamage") != nullptr);
		}
		AssertRejected(host, 10, NpcDamage(arena.targetPlayerId, 130, 31, 1.0, arena.epoch), 3.4, 102.4);
	}

	{
		ProtocolHost host{ "NPC Damage Bound" };
		const auto arena = SetUpArena(host);
		AssertRejected(host, 10, NpcDamage(arena.targetPlayerId, 42, 1, 1000.5, arena.epoch), 3.0, 102.0);
		assert(FindFrame(host.HandleMessage(10, EncodeJson(NpcDamage(arena.targetPlayerId, 42, 1, 1000.0, arena.epoch)), 3.01, 102.01), 20, "npcDamage") != nullptr);
	}

	return 0;
}

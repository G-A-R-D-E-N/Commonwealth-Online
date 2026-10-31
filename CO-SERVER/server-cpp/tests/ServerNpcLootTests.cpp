// source:CO-BETA/docs/protocol/packets.md
#include "ServerProtocol.h"

#include "F4TGnsSnapshotEnvelope.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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

	Json Transform(double a_x, std::string_view a_cellId = "CAFE", std::string_view a_movementType = "spawn", std::string_view a_worldspaceId = "F00D")
	{
		return Json{
			{ "type", "transform" },
			{ "cellId", std::string{ a_cellId } },
			{ "worldspaceId", std::string{ a_worldspaceId } },
			{ "x", a_x },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 },
			{ "movementType", std::string{ a_movementType } }
		};
	}

	Json NpcStates(std::uint32_t a_authorityEpoch, std::uint32_t a_npcId, double a_x, bool a_isDead)
	{
		return Json{
			{ "type", "npcState" },
			{ "authorityEpoch", a_authorityEpoch },
			{ "authorityCellId", "CAFE" },
			{ "authorityWorldspaceId", "F00D" },
			{ "fullReplace", true },
			{ "npcs", Json::array({ Json{
				{ "npcId", a_npcId },
				{ "baseFormId", "5678" },
				{ "sourceFormId", "1234" },
				{ "cellId", "CAFE" },
				{ "worldspaceId", "F00D" },
				{ "x", a_x }, { "y", 0.0 }, { "z", 0.0 }, { "angleZ", 0.0 },
				{ "isDead", a_isDead },
				{ "health", a_isDead ? 0.0 : 100.0 }, { "maxHealth", 100.0 }
			} }) }
		};
	}

	Json NpcLoot(std::uint32_t a_authorityEpoch, std::uint32_t a_revision, Json a_items = Json::array({ Json{ { "formId", "0001F669" }, { "count", 3 } } }))
	{
		return Json{
			{ "type", "npcLoot" },
			{ "npcId", 42 },
			{ "cellId", "CAFE" },
			{ "worldspaceId", "F00D" },
			{ "sourceFormId", "1234" },
			{ "authorityEpoch", a_authorityEpoch },
			{ "revision", a_revision },
			{ "items", std::move(a_items) }
		};
	}

	Json NpcLootTake(std::string_view a_operationId, std::uint32_t a_authorityEpoch, std::uint32_t a_count = 3)
	{
		return Json{
			{ "type", "npcLootTake" },
			{ "operationId", std::string{ a_operationId } },
			{ "npcId", 42 },
			{ "cellId", "CAFE" },
			{ "worldspaceId", "F00D" },
			{ "sourceFormId", "1234" },
			{ "authorityEpoch", a_authorityEpoch },
			{ "formId", "0001F669" },
			{ "count", a_count }
		};
	}

	Json NpcLootResult(std::string_view a_operationId, std::string_view a_status, std::uint32_t a_revision, std::uint32_t a_takerPlayerId, std::uint32_t a_count = 3)
	{
		return Json{
			{ "type", "npcLootResult" },
			{ "operationId", std::string{ a_operationId } },
			{ "status", std::string{ a_status } },
			{ "formId", "0001F669" },
			{ "count", a_count },
			{ "revision", a_revision },
			{ "takerPlayerId", a_takerPlayerId }
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
		a_host.HandleMessage(10, EncodeSnapshot(NpcStates(arena.epoch, 42, 20.0, false), Family::NpcState, 1), 2.5, 101.5);
		return arena;
	}

	void MarkDead(ProtocolHost& a_host, const Arena& a_arena)
	{
		a_host.HandleMessage(10, EncodeSnapshot(NpcStates(a_arena.epoch, 42, 20.0, true), Family::NpcState, 2), 2.6, 101.6);
	}

	void AssertLootRejected(ProtocolHost& a_host, std::uint32_t a_connectionId, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		const auto before = a_host.Stats();
		assert(a_host.HandleMessage(a_connectionId, EncodeJson(a_packet), a_monotonicSeconds, a_unixTimeSeconds).empty());
		assert(a_host.Stats().npcLootRejected == before.npcLootRejected + 1);
	}

	void AssertTakeRejected(ProtocolHost& a_host, std::uint32_t a_connectionId, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		const auto before = a_host.Stats();
		assert(a_host.HandleMessage(a_connectionId, EncodeJson(a_packet), a_monotonicSeconds, a_unixTimeSeconds).empty());
		assert(a_host.Stats().npcLootTakeRejected == before.npcLootTakeRejected + 1);
	}

	void AssertResultRejected(ProtocolHost& a_host, std::uint32_t a_connectionId, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		const auto before = a_host.Stats();
		assert(a_host.HandleMessage(a_connectionId, EncodeJson(a_packet), a_monotonicSeconds, a_unixTimeSeconds).empty());
		assert(a_host.Stats().npcLootResultRejected == before.npcLootResultRejected + 1);
	}

	std::uint32_t JoinPlayer(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		assert(FindFrame(a_host.HandleMessage(a_connectionId, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), a_monotonicSeconds, a_unixTimeSeconds + 0.1), a_connectionId, "sessionReady") != nullptr);
		return DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
	}
}

int main()
{
	{
		ProtocolHost host{ "NPC Loot Route" };
		const auto arena = SetUpArena(host);
		MarkDead(host, arena);
		const auto frames = host.HandleMessage(10, EncodeJson(NpcLoot(arena.epoch, 1)), 3.0, 102.0);
		assert(frames.size() == 1);
		const auto* routed = FindFrame(frames, 20, "npcLoot");
		assert(routed != nullptr);
		assert(routed->delivery == Delivery::ReliableOrdered);
		const auto relay = DecodePacket(*routed);
		assert(relay.at("playerId").get<std::uint32_t>() == arena.authorityPlayerId);
		assert(relay.at("serverTime") == 102.0);
		assert(relay.at("npcId") == 42);
		assert(relay.at("cellId") == "0000CAFE");
		assert(relay.at("worldspaceId") == "0000F00D");
		assert(relay.at("sourceFormId") == "00001234");
		assert(relay.at("revision") == 1);
		assert(relay.at("items").size() == 1);
		assert(relay.at("items")[0] == (Json{ { "formId", "0001F669" }, { "count", 3 } }));
		assert(relay.at("authorityEpoch") == arena.epoch);
		assert(host.Stats().npcLootReceived == 1);
		assert(host.Stats().npcLootRouted == 1);
		assert(host.Stats().npcLootRejected == 0);
	}

	{
		ProtocolHost host{ "NPC Loot Late Joiner" };
		const auto arena = SetUpArena(host);
		MarkDead(host, arena);
		host.HandleMessage(10, EncodeJson(NpcLoot(arena.epoch, 1)), 3.0, 102.0);
		JoinPlayer(host, 30, 3.2, 102.2);
		const auto frames = host.HandleMessage(30, EncodeSnapshot(Transform(20.0), Family::Transform, 1), 3.5, 102.5);
		const auto* cached = FindFrame(frames, 30, "npcLoot");
		assert(cached != nullptr);
		const auto relay = DecodePacket(*cached);
		assert(relay.at("playerId").get<std::uint32_t>() == arena.authorityPlayerId);
		assert(relay.at("serverTime") == 102.5);
		assert(relay.at("items")[0] == (Json{ { "formId", "0001F669" }, { "count", 3 } }));
	}

	{
		ProtocolHost host{ "NPC Loot Authority Only" };
		const auto arena = SetUpArena(host);
		MarkDead(host, arena);
		AssertLootRejected(host, 20, NpcLoot(arena.epoch, 1), 3.0, 102.0);
	}

	{
		ProtocolHost host{ "NPC Loot Alive" };
		const auto arena = SetUpArena(host);
		AssertLootRejected(host, 10, NpcLoot(arena.epoch, 1), 3.0, 102.0);
	}

	{
		ProtocolHost host{ "NPC Loot Revision" };
		const auto arena = SetUpArena(host);
		MarkDead(host, arena);
		assert(FindFrame(host.HandleMessage(10, EncodeJson(NpcLoot(arena.epoch, 5)), 3.0, 102.0), 20, "npcLoot") != nullptr);
		AssertLootRejected(host, 10, NpcLoot(arena.epoch, 5), 3.1, 102.1);
		AssertLootRejected(host, 10, NpcLoot(arena.epoch, 4), 3.2, 102.2);
		AssertLootRejected(host, 10, NpcLoot(arena.epoch, 0), 3.3, 102.3);
		assert(FindFrame(host.HandleMessage(10, EncodeJson(NpcLoot(arena.epoch, 6)), 3.4, 102.4), 20, "npcLoot") != nullptr);
	}

	{
		ProtocolHost host{ "NPC Loot Epoch" };
		const auto arena = SetUpArena(host);
		MarkDead(host, arena);
		AssertLootRejected(host, 10, NpcLoot(arena.epoch + 1, 1), 3.0, 102.0);
	}

	{
		ProtocolHost host{ "NPC Loot Byte Cap" };
		const auto arena = SetUpArena(host);
		MarkDead(host, arena);
		Json items = Json::array();
		for (std::uint32_t index = 0; index < 64; ++index) {
			char formId[9]{};
			std::snprintf(formId, sizeof(formId), "0001F0%02X", index);
			items.push_back(Json{ { "formId", formId }, { "count", 1 } });
		}
		const auto encoded = EncodeJson(NpcLoot(arena.epoch, 1, items));
		assert(encoded.size() > 2 * 1024);
		assert(FindFrame(host.HandleMessage(10, encoded, 3.0, 102.0), 20, "npcLoot") != nullptr);
	}

	{
		ProtocolHost host{ "NPC Loot Take Route" };
		const auto arena = SetUpArena(host);
		MarkDead(host, arena);
		const auto frames = host.HandleMessage(20, EncodeJson(NpcLootTake("op-1", arena.epoch)), 3.0, 102.0);
		assert(frames.size() == 1);
		const auto* routed = FindFrame(frames, 10, "npcLootTake");
		assert(routed != nullptr);
		assert(routed->delivery == Delivery::ReliableOrdered);
		const auto relay = DecodePacket(*routed);
		assert(relay.at("playerId").get<std::uint32_t>() == arena.targetPlayerId);
		assert(relay.at("serverTime") == 102.0);
		assert(relay.at("operationId") == "op-1");
		assert(relay.at("formId") == "0001F669");
		assert(relay.at("count") == 3);
		assert(host.Stats().npcLootTakeReceived == 1);
		assert(host.Stats().npcLootTakeRouted == 1);
		assert(host.Stats().npcLootTakeRejected == 0);
	}

	{
		ProtocolHost host{ "NPC Loot Take Far" };
		const auto arena = SetUpArena(host);
		MarkDead(host, arena);
		host.HandleMessage(20, EncodeSnapshot(Transform(800.0, "CAFE", "teleport"), Family::Transform, 2), 2.9, 102.9);
		AssertTakeRejected(host, 20, NpcLootTake("op-1", arena.epoch), 3.0, 102.0);
	}

	{
		ProtocolHost host{ "NPC Loot Take Authority" };
		const auto arena = SetUpArena(host);
		MarkDead(host, arena);
		AssertTakeRejected(host, 10, NpcLootTake("op-1", arena.epoch), 3.0, 102.0);
	}

	{
		ProtocolHost host{ "NPC Loot Take Duplicate" };
		const auto arena = SetUpArena(host);
		MarkDead(host, arena);
		assert(FindFrame(host.HandleMessage(20, EncodeJson(NpcLootTake("op-1", arena.epoch)), 3.0, 102.0), 10, "npcLootTake") != nullptr);
		AssertTakeRejected(host, 20, NpcLootTake("op-1", arena.epoch), 3.1, 102.1);
	}

	{
		ProtocolHost host{ "NPC Loot Take Alive" };
		const auto arena = SetUpArena(host);
		AssertTakeRejected(host, 20, NpcLootTake("op-1", arena.epoch), 3.0, 102.0);
	}

	{
		ProtocolHost host{ "NPC Loot Take Offline Other Cell" };
		const auto arena = SetUpArena(host);
		MarkDead(host, arena);
		host.HandleMessage(20, EncodeSnapshot(Transform(20.0, "BEEF", "teleport"), Family::Transform, 2), 2.9, 102.9);
		host.HandleDisconnected(10, 2.95);
		AssertTakeRejected(host, 20, NpcLootTake("op-1", arena.epoch), 3.0, 102.0);
	}

	{
		ProtocolHost host{ "NPC Loot Take Authority Gone Same Cell" };
		const auto arena = SetUpArena(host);
		MarkDead(host, arena);
		host.HandleDisconnected(10, 2.95);
		AssertTakeRejected(host, 20, NpcLootTake("op-1", arena.epoch), 3.0, 102.0);
	}

	{
		ProtocolHost host{ "NPC Loot Result From Taker" };
		const auto arena = SetUpArena(host);
		MarkDead(host, arena);
		host.HandleMessage(20, EncodeJson(NpcLootTake("op-1", arena.epoch)), 3.0, 102.0);
		AssertResultRejected(host, 20, NpcLootResult("op-1", "accepted", 1, arena.targetPlayerId), 3.1, 102.1);
	}

	{
		ProtocolHost host{ "NPC Loot Authority Handoff" };
		const auto arena = SetUpArena(host);
		MarkDead(host, arena);
		assert(FindFrame(host.HandleMessage(10, EncodeJson(NpcLoot(arena.epoch, 1)), 3.0, 102.0), 20, "npcLoot") != nullptr);
		const auto handoff = host.HandleDisconnected(10, 3.1);
		const auto* authority = FindFrame(handoff, 20, "npcAuthority");
		assert(authority != nullptr);
		const auto newEpoch = DecodePacket(*authority).at("authorityEpoch").get<std::uint32_t>();
		assert(newEpoch != arena.epoch);
		host.HandleMessage(20, EncodeSnapshot(NpcStates(newEpoch, 42, 20.0, true), Family::NpcState, 1), 3.3, 102.3);
		JoinPlayer(host, 40, 3.4, 102.4);
		assert(FindFrame(host.HandleMessage(40, EncodeSnapshot(Transform(20.0), Family::Transform, 1), 3.5, 102.5), 40, "npcLoot") == nullptr);
		const auto frames = host.HandleMessage(20, EncodeJson(NpcLoot(newEpoch, 1)), 3.6, 102.6);
		const auto* routed = FindFrame(frames, 40, "npcLoot");
		assert(routed != nullptr);
		assert(DecodePacket(*routed).at("authorityEpoch").get<std::uint32_t>() == newEpoch);
		assert(host.Stats().npcLootRejected == 0);
	}

	{
		ProtocolHost host{ "NPC Loot Out Of Interest" };
		const auto arena = SetUpArena(host);
		MarkDead(host, arena);
		JoinPlayer(host, 50, 3.2, 102.2);
		host.HandleMessage(50, EncodeSnapshot(Transform(20.0, "BEEF", "teleport", "BEEF"), Family::Transform, 1), 3.3, 102.3);
		const auto frames = host.HandleMessage(10, EncodeJson(NpcLoot(arena.epoch, 1)), 3.4, 102.4);
		assert(FindFrame(frames, 50, "npcLoot") == nullptr);
		JoinPlayer(host, 60, 3.5, 102.5);
		assert(FindFrame(host.HandleMessage(60, EncodeSnapshot(Transform(20.0, "BEEF", "teleport", "BEEF"), Family::Transform, 1), 3.6, 102.6), 60, "npcLoot") == nullptr);
	}

	{
		ProtocolHost host{ "NPC Loot Result Rejected Forward" };
		const auto arena = SetUpArena(host);
		MarkDead(host, arena);
		host.HandleMessage(20, EncodeJson(NpcLootTake("op-1", arena.epoch)), 3.0, 102.0);
		const auto frames = host.HandleMessage(10, EncodeJson(NpcLootResult("op-1", "rejected", 0, arena.targetPlayerId)), 3.1, 102.1);
		const auto* forwarded = FindFrame(frames, 20, "npcLootResult");
		assert(forwarded != nullptr);
		assert(DecodePacket(*forwarded).at("status") == "rejected");
	}

	{
		ProtocolHost host{ "NPC Loot Result Taker Gone" };
		const auto arena = SetUpArena(host);
		MarkDead(host, arena);
		host.HandleMessage(20, EncodeJson(NpcLootTake("op-1", arena.epoch)), 3.0, 102.0);
		host.HandleDisconnected(20, 3.05);
		AssertResultRejected(host, 10, NpcLootResult("op-1", "accepted", 1, arena.targetPlayerId), 3.1, 102.1);
	}

	{
		ProtocolHost host{ "NPC Loot Result Two Takers" };
		const auto arena = SetUpArena(host);
		MarkDead(host, arena);
		host.HandleMessage(20, EncodeJson(NpcLootTake("op-1", arena.epoch)), 3.0, 102.0);
		const auto takerC = JoinPlayer(host, 30, 3.05, 102.05);
		host.HandleMessage(30, EncodeSnapshot(Transform(20.0), Family::Transform, 1), 3.06, 102.06);
		assert(FindFrame(host.HandleMessage(30, EncodeJson(NpcLootTake("op-1", arena.epoch)), 3.1, 102.1), 10, "npcLootTake") != nullptr);
		AssertResultRejected(host, 10, NpcLootResult("op-1", "accepted", 1, 999), 3.2, 102.2);
		Json wrongForm = NpcLootResult("op-1", "accepted", 1, takerC);
		wrongForm["formId"] = "0001F66A";
		AssertResultRejected(host, 10, wrongForm, 3.3, 102.3);
		const auto toC = host.HandleMessage(10, EncodeJson(NpcLootResult("op-1", "accepted", 1, takerC)), 3.4, 102.4);
		assert(FindFrame(toC, 30, "npcLootResult") != nullptr);
		assert(FindFrame(toC, 20, "npcLootResult") == nullptr);
		assert(FindFrame(host.HandleMessage(10, EncodeJson(NpcLootResult("op-1", "accepted", 1, arena.targetPlayerId)), 3.5, 102.5), 20, "npcLootResult") != nullptr);
	}

	{
		ProtocolHost host{ "NPC Loot Take Epoch" };
		const auto arena = SetUpArena(host);
		MarkDead(host, arena);
		const auto takerC = JoinPlayer(host, 30, 2.7, 101.7);
		host.HandleMessage(30, EncodeSnapshot(Transform(20.0), Family::Transform, 1), 2.8, 101.9);
		assert(FindFrame(host.HandleMessage(30, EncodeJson(NpcLootTake("op-1", arena.epoch)), 3.0, 102.0), 10, "npcLootTake") != nullptr);
		host.HandleMessage(20, EncodeSnapshot(Transform(20.0, "BEEF", "teleport", "BEEF"), Family::Transform, 3), 2.95, 101.95);
		host.HandleMessage(10, EncodeSnapshot(Transform(0.0, "BEEF", "teleport", "BEEF"), Family::Transform, 2), 4.0, 102.9);
		host.HandleMessage(30, EncodeSnapshot(Transform(20.0, "BEEF", "teleport", "BEEF"), Family::Transform, 2), 4.1, 103.0);
		const auto returned = host.HandleMessage(10, EncodeSnapshot(Transform(0.0, "CAFE", "teleport"), Family::Transform, 3), 5.1, 103.9);
		const auto* authority = FindFrame(returned, 10, "npcAuthority");
		assert(authority != nullptr);
		assert(DecodePacket(*authority).at("authorityEpoch").get<std::uint32_t>() > arena.epoch);
		AssertResultRejected(host, 10, NpcLootResult("op-1", "accepted", 1, takerC), 5.2, 104.0);
	}

	return 0;
}

#include "ServerProtocol.h"

#include "F4TGnsSnapshotEnvelope.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Concept-level fixture: every form id below is a synthetic wire value, not engine data.
namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::Delivery;
	using CommonwealthOnline::Server::OutboundFrame;
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

	// The attack the most recent playerState opened; an npcHit names the one it came
	// from and, unless told otherwise, that is "the one I just sent".
	std::uint32_t g_currentAttackSequence = 0;

	Json RangedPlayerState(std::uint32_t a_actionSequence, std::string_view a_weapon = "1234")
	{
		g_currentAttackSequence = a_actionSequence;
		return Json{
			{ "type", "playerState" },
			{ "equippedItems", Json::array({ Json{ { "slot", "rightHand" }, { "formId", std::string{ a_weapon } } } }) },
			{ "actionEvents", Json::array({ Json{
				{ "sequence", a_actionSequence },
				{ "type", 3 },
				{ "eventName", "fireSingle" }
			} }) }
		};
	}

	Json MeleePlayerState(std::uint32_t a_actionSequence, std::string_view a_weapon = "1234")
	{
		g_currentAttackSequence = a_actionSequence;
		return Json{
			{ "type", "playerState" },
			{ "equippedItems", Json::array({ Json{ { "slot", "rightHand" }, { "formId", std::string{ a_weapon } } } }) },
			{ "actionEvents", Json::array({ Json{
				{ "sequence", a_actionSequence },
				{ "type", 1 },
				{ "eventName", "meleeattackStart" }
			} }) }
		};
	}

	Json Transform(double a_x)
	{
		return Json{
			{ "type", "transform" },
			{ "cellId", "CAFE" },
			{ "worldspaceId", "F00D" },
			{ "x", a_x },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 },
			{ "movementType", "spawn" }
		};
	}

	Json NpcStateSnapshot(
		std::uint32_t a_authorityEpoch,
		std::uint32_t a_npcId,
		double a_x,
		double a_health,
		bool a_isDead = false,
		double a_maxHealth = -1.0,
		std::string_view a_npcCellId = "CAFE")
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
				{ "cellId", std::string{ a_npcCellId } },
				{ "worldspaceId", "F00D" },
				{ "x", a_x }, { "y", 0.0 }, { "z", 0.0 }, { "angleZ", 0.0 },
				{ "isDead", a_isDead },
				{ "health", a_health }, { "maxHealth", a_maxHealth < 0.0 ? a_health : a_maxHealth }
			} }) }
		};
	}

	// The authority epoch the NPC snapshot was published under; hits must name it.
	std::uint32_t g_authorityEpoch = 0;

	Json NpcHit(
		std::uint32_t a_npcId,
		std::uint32_t a_sequence,
		double a_damage,
		std::string_view a_weapon = "1234",
		std::uint32_t a_attackSequence = 0,
		std::string_view a_sourceFormId = "1234",
		std::uint32_t a_authorityEpoch = 0)
	{
		Json hit{
			{ "type", "npcHit" },
			{ "npcId", a_npcId },
			{ "cellId", "CAFE" },
			{ "worldspaceId", "F00D" },
			{ "sequence", a_sequence },
			{ "attackSequence", a_attackSequence == 0 ? g_currentAttackSequence : a_attackSequence },
			{ "damage", a_damage },
			{ "sourceFormId", std::string{ a_sourceFormId } },
			{ "authorityEpoch", a_authorityEpoch == 0 ? g_authorityEpoch : a_authorityEpoch }
		};
		if (!a_weapon.empty()) {
			hit["weaponFormId"] = std::string{ a_weapon };
		}
		return hit;
	}
}

int main()
{
	using CommonwealthOnline::Server::ProtocolHost;

	ProtocolHost host{ "NPC Combat Test" };

	// Player A (connection 10) connects first, so it takes the lower player id and
	// becomes the authority for the shared scope. Player B (connection 20) is the
	// non-authority attacker whose damage must reach A.
	const auto welcomeA = host.HandleConnected(10, 100.0);
	const auto playerA = DecodePacket(welcomeA.front()).at("playerId").get<std::uint32_t>();
	assert(FindFrame(host.HandleMessage(10, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 1.0, 100.1), 10, "sessionReady") != nullptr);

	const auto welcomeB = host.HandleConnected(20, 100.2);
	const auto playerB = DecodePacket(welcomeB.front()).at("playerId").get<std::uint32_t>();
	assert(FindFrame(host.HandleMessage(20, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 1.1, 100.3), 20, "sessionReady") != nullptr);

	// A spawns and is handed authority for {CAFE, F00D}; capture the epoch so its
	// npcState is accepted.
	const auto spawnAFrames = host.HandleMessage(10, EncodeSnapshot(Transform(0.0), Family::Transform, 1), 2.0, 101.0);
	const auto* authorityFrame = FindFrame(spawnAFrames, 10, "npcAuthority");
	assert(authorityFrame != nullptr);
	const auto authority = DecodePacket(*authorityFrame);
	assert(authority.at("authorityPlayerId") == playerA);
	const auto authorityEpoch = authority.at("authorityEpoch").get<std::uint32_t>();
	g_authorityEpoch = authorityEpoch;

	// B spawns nearby in the same scope; authority stays with A.
	host.HandleMessage(20, EncodeSnapshot(Transform(20.0), Family::Transform, 1), 2.1, 101.1);

	// A, as authority, publishes an NPC (id 42) at B's position with full health.
	const auto beforeAccepted = host.Stats();
	host.HandleMessage(10, EncodeSnapshot(NpcStateSnapshot(authorityEpoch, 42, 20.0, 100.0), Family::NpcState, 1), 2.5, 101.5);
	assert(host.Stats().npcStateRejected == beforeAccepted.npcStateRejected);

	// A validated ranged action on B authorizes its hit on the NPC, which routes to
	// the authority (A, connection 10) - not to any player-target channel.
	host.HandleMessage(20, EncodeJson(RangedPlayerState(10)), 3.0, 102.0);
	const auto hitFrames = host.HandleMessage(20, EncodeJson(NpcHit(42, 1, 25.0)), 3.1, 102.1);
	const auto* routed = FindFrame(hitFrames, 10, "npcHit");
	assert(routed != nullptr);
	assert(routed->delivery == Delivery::ReliableOrdered);
	const auto routedHit = DecodePacket(*routed);
	assert(routedHit.at("playerId") == playerB);
	assert(routedHit.at("npcId") == 42);
	assert(routedHit.at("damage") == 25.0);
	assert(routedHit.at("weaponFormId") == "00001234");
	// The attacker-local correlation is consumed at the server, not forwarded.
	assert(!routedHit.contains("attackSequence"));
	assert(host.Stats().npcHitsRouted == 1);

	// A replayed sequence is not newer, so it is rejected and routes nothing.
	const auto beforeDuplicate = host.Stats();
	assert(host.HandleMessage(20, EncodeJson(NpcHit(42, 1, 1.0)), 3.11, 102.11).empty());
	assert(host.Stats().npcHitsRejected == beforeDuplicate.npcHitsRejected + 1);

	// The authority damaging its own NPC needs no packet: a self-routed hit is rejected.
	host.HandleMessage(10, EncodeJson(RangedPlayerState(50)), 3.2, 102.2);
	const auto beforeSelf = host.Stats();
	assert(host.HandleMessage(10, EncodeJson(NpcHit(42, 100, 5.0)), 3.21, 102.21).empty());
	assert(host.Stats().npcHitsRejected == beforeSelf.npcHitsRejected + 1);

	// A hit on an NPC that is not in the authority's snapshot is rejected.
	host.HandleMessage(20, EncodeJson(RangedPlayerState(11)), 3.3, 102.3);
	const auto beforeUnknown = host.Stats();
	assert(host.HandleMessage(20, EncodeJson(NpcHit(999, 2, 5.0)), 3.31, 102.31).empty());
	assert(host.Stats().npcHitsRejected == beforeUnknown.npcHitsRejected + 1);

	// Out of reach: move B far from the NPC and the geometry check rejects the hit.
	Json farB = Transform(100000.0);
	farB["movementType"] = "teleport";
	host.HandleMessage(20, EncodeSnapshot(farB, Family::Transform, 2), 3.4, 102.4);
	host.HandleMessage(20, EncodeJson(RangedPlayerState(12)), 3.41, 102.41);
	const auto beforeRange = host.Stats();
	assert(host.HandleMessage(20, EncodeJson(NpcHit(42, 3, 5.0)), 3.42, 102.42).empty());
	assert(host.Stats().npcHitsRejected == beforeRange.npcHitsRejected + 1);

	// Bring B back next to the NPC for the identity/freshness checks below. Spaced a
	// full second past the range-test teleport so the transition budget accepts it.
	Json nearB = Transform(20.0);
	nearB["movementType"] = "teleport";
	host.HandleMessage(20, EncodeSnapshot(nearB, Family::Transform, 3), 4.5, 104.5);

	// Wrong equipped weapon: a ranged claim must match B's equipped right hand.
	host.HandleMessage(20, EncodeJson(RangedPlayerState(20)), 4.51, 104.51);
	const auto beforeWeapon = host.Stats();
	assert(host.HandleMessage(20, EncodeJson(NpcHit(42, 4, 5.0, "9999")), 4.52, 104.52).empty());
	assert(host.Stats().npcHitsRejected == beforeWeapon.npcHitsRejected + 1);

	// Naming an attack that was never recorded is not authorization.
	host.HandleMessage(20, EncodeJson(RangedPlayerState(21)), 4.53, 104.53);
	const auto beforeAttack = host.Stats();
	assert(host.HandleMessage(20, EncodeJson(NpcHit(42, 4, 5.0, "1234", 999)), 4.54, 104.54).empty());
	assert(host.Stats().npcHitsRejected == beforeAttack.npcHitsRejected + 1);

	// Right npcId, wrong actor: sourceFormId must match the snapshot's for that id.
	host.HandleMessage(20, EncodeJson(RangedPlayerState(22)), 4.55, 104.55);
	const auto beforeSource = host.Stats();
	assert(host.HandleMessage(20, EncodeJson(NpcHit(42, 4, 5.0, "1234", 0, "9999")), 4.56, 104.56).empty());
	assert(host.Stats().npcHitsRejected == beforeSource.npcHitsRejected + 1);

	// A stale authority epoch (a handoff after capture) is rejected.
	host.HandleMessage(20, EncodeJson(RangedPlayerState(23)), 4.57, 104.57);
	const auto beforeEpoch = host.Stats();
	assert(host.HandleMessage(20, EncodeJson(NpcHit(42, 4, 5.0, "1234", 0, "1234", authorityEpoch + 99)), 4.58, 104.58).empty());
	assert(host.Stats().npcHitsRejected == beforeEpoch.npcHitsRejected + 1);

	// None of those spent the sequence, so a fully valid hit still routes.
	host.HandleMessage(20, EncodeJson(RangedPlayerState(24)), 4.59, 104.59);
	assert(FindFrame(host.HandleMessage(20, EncodeJson(NpcHit(42, 4, 5.0)), 4.60, 104.60), 10, "npcHit") != nullptr);

	// Exact-action deque: an older attack stays valid after a newer one is recorded,
	// so naming the earlier ranged action still authorizes its hit (the #57 regression).
	host.HandleMessage(20, EncodeJson(RangedPlayerState(40)), 4.61, 104.61);
	host.HandleMessage(20, EncodeJson(RangedPlayerState(41)), 4.62, 104.62);
	assert(FindFrame(host.HandleMessage(20, EncodeJson(NpcHit(42, 6, 5.0, "1234", 40)), 4.63, 104.63), 10, "npcHit") != nullptr);

	// Stale NPC snapshot: with the authority silent, its last position stops
	// authorizing hits once it ages out, even though B stays fresh and in range.
	host.HandleMessage(20, EncodeSnapshot(Transform(20.0), Family::Transform, 4), 7.9, 107.4);
	host.HandleMessage(20, EncodeJson(RangedPlayerState(42)), 7.91, 107.41);
	const auto beforeStale = host.Stats();
	assert(host.HandleMessage(20, EncodeJson(NpcHit(42, 7, 5.0)), 7.92, 107.42).empty());
	assert(host.Stats().npcHitsRejected == beforeStale.npcHitsRejected + 1);

	// A hit on an NPC the authority reports dead is rejected. Refresh the snapshot with
	// the NPC marked dead so this is a corpse rejection, not a staleness one.
	host.HandleMessage(10, EncodeSnapshot(NpcStateSnapshot(authorityEpoch, 42, 20.0, 100.0, true), Family::NpcState, 2), 8.0, 108.0);
	host.HandleMessage(20, EncodeJson(RangedPlayerState(43)), 8.01, 108.01);
	const auto beforeCorpse = host.Stats();
	assert(host.HandleMessage(20, EncodeJson(NpcHit(42, 8, 5.0)), 8.02, 108.02).empty());
	assert(host.Stats().npcHitsRejected == beforeCorpse.npcHitsRejected + 1);

	host.HandleMessage(10, EncodeSnapshot(NpcStateSnapshot(authorityEpoch, 42, 20.0, 0.0, false, 100.0), Family::NpcState, 3), 8.1, 108.1);
	host.HandleMessage(20, EncodeJson(RangedPlayerState(44)), 8.11, 108.11);
	const auto beforeZeroHealth = host.Stats();
	assert(host.HandleMessage(20, EncodeJson(NpcHit(42, 9, 5.0)), 8.12, 108.12).empty());
	assert(host.Stats().npcHitsRejected == beforeZeroHealth.npcHitsRejected + 1);

	host.HandleMessage(10, EncodeSnapshot(NpcStateSnapshot(authorityEpoch, 42, 20.0, 100.0), Family::NpcState, 4), 8.2, 108.2);
	host.HandleMessage(20, EncodeJson(RangedPlayerState(45)), 8.21, 108.21);
	const auto beforeOverkill = host.Stats();
	assert(host.HandleMessage(20, EncodeJson(NpcHit(42, 9, 401.0)), 8.22, 108.22).empty());
	assert(host.Stats().npcHitsRejected == beforeOverkill.npcHitsRejected + 1);
	host.HandleMessage(20, EncodeJson(RangedPlayerState(46)), 8.23, 108.23);
	const auto* boundedHit = FindFrame(host.HandleMessage(20, EncodeJson(NpcHit(42, 9, 400.0)), 8.24, 108.24), 10, "npcHit");
	assert(boundedHit != nullptr);
	const auto beforeNeighbour = host.Stats();
	host.HandleMessage(10, EncodeSnapshot(NpcStateSnapshot(authorityEpoch, 42, 20.0, 100.0, false, -1.0, "BEEF"), Family::NpcState, 5), 8.25, 108.25);
	assert(host.Stats().npcStateRejected == beforeNeighbour.npcStateRejected + 1);

	{
		ProtocolHost stickyHost{ "Sticky NPC Combat" };

		const auto lowWelcome = stickyHost.HandleConnected(100, 500.0);
		const auto lowId = DecodePacket(lowWelcome.front()).at("playerId").get<std::uint32_t>();
		stickyHost.HandleMessage(100, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 500.01, 5000.01);

		const auto midWelcome = stickyHost.HandleConnected(200, 500.1);
		const auto midId = DecodePacket(midWelcome.front()).at("playerId").get<std::uint32_t>();
		stickyHost.HandleMessage(200, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 500.11, 5000.11);

		const auto highWelcome = stickyHost.HandleConnected(300, 500.2);
		const auto highId = DecodePacket(highWelcome.front()).at("playerId").get<std::uint32_t>();
		stickyHost.HandleMessage(300, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 500.21, 5000.21);

		assert(lowId < midId && midId < highId);

		const auto highEnter = stickyHost.HandleMessage(300, EncodeSnapshot(Transform(30.0), Family::Transform, 1), 501.0, 5001.0);
		const auto* highAuthority = FindFrame(highEnter, 300, "npcAuthority");
		assert(highAuthority != nullptr);
		assert(DecodePacket(*highAuthority).at("authorityPlayerId").get<std::uint32_t>() == highId);
		assert(DecodePacket(*highAuthority).at("authorityEpoch").get<std::uint32_t>() == 1);

		stickyHost.HandleMessage(300, EncodeSnapshot(NpcStateSnapshot(1, 42, 20.0, 100.0), Family::NpcState, 1), 501.1, 5001.1);
		stickyHost.HandleMessage(200, EncodeSnapshot(Transform(22.0), Family::Transform, 1), 501.2, 5001.2);

		const auto lowEnter = stickyHost.HandleMessage(100, EncodeSnapshot(Transform(25.0), Family::Transform, 1), 501.3, 5001.3);
		const auto* stickyAnnounce = FindFrame(lowEnter, 100, "npcAuthority");
		assert(stickyAnnounce != nullptr);
		assert(DecodePacket(*stickyAnnounce).at("authorityPlayerId").get<std::uint32_t>() == highId);
		assert(DecodePacket(*stickyAnnounce).at("authorityEpoch").get<std::uint32_t>() == 1);
		const auto* survivingNpcState = FindFrame(lowEnter, 100, "npcState");
		assert(survivingNpcState != nullptr);
		assert(DecodePacket(*survivingNpcState).at("authorityEpoch").get<std::uint32_t>() == 1);

		stickyHost.HandleMessage(100, EncodeJson(MeleePlayerState(1)), 501.4, 5001.4);
		const auto beforeStickyHit = stickyHost.Stats();
		const auto stickyHitFrames = stickyHost.HandleMessage(100, EncodeJson(NpcHit(42, 1, 5.0, "1234", 0, "1234", 1)), 501.5, 5001.5);
		const auto* routedToIncumbent = FindFrame(stickyHitFrames, 300, "npcHit");
		assert(routedToIncumbent != nullptr);
		assert(routedToIncumbent->delivery == Delivery::ReliableOrdered);
		const auto stickyRouted = DecodePacket(*routedToIncumbent);
		assert(stickyRouted.at("playerId").get<std::uint32_t>() == lowId);
		assert(stickyRouted.at("authorityEpoch").get<std::uint32_t>() == 1);
		assert(FindFrame(stickyHitFrames, 100, "npcHit") == nullptr);
		assert(stickyHost.Stats().npcHitsRouted == beforeStickyHit.npcHitsRouted + 1);

		const auto beforeBudget = stickyHost.Stats();
		assert(stickyHost.HandleMessage(100, EncodeJson(NpcHit(42, 2, 5.0, "1234", 1, "1234", 1)), 501.55, 5001.55).empty());
		assert(stickyHost.Stats().npcHitsRejected == beforeBudget.npcHitsRejected + 1);

		const auto disconnect = stickyHost.HandleDisconnected(300, 5002.0);
		const auto* transfer = FindFrame(disconnect, 100, "npcAuthority");
		assert(transfer != nullptr);
		assert(DecodePacket(*transfer).at("authorityPlayerId").get<std::uint32_t>() == lowId);
		assert(DecodePacket(*transfer).at("authorityEpoch").get<std::uint32_t>() == 2);

		stickyHost.HandleMessage(200, EncodeSnapshot(Transform(22.0), Family::Transform, 2), 502.1, 5002.1);
		stickyHost.HandleMessage(200, EncodeJson(RangedPlayerState(10)), 502.11, 5002.11);
		const auto beforeClearedCache = stickyHost.Stats();
		assert(stickyHost.HandleMessage(200, EncodeJson(NpcHit(42, 1, 5.0, "1234", 0, "1234", 2)), 502.12, 5002.12).empty());
		assert(stickyHost.Stats().npcHitsRejected == beforeClearedCache.npcHitsRejected + 1);

		stickyHost.HandleMessage(100, EncodeSnapshot(NpcStateSnapshot(2, 7, 20.0, 100.0), Family::NpcState, 1), 502.2, 5002.2);

		stickyHost.HandleMessage(200, EncodeSnapshot(Transform(22.0), Family::Transform, 3), 502.3, 5002.3);
		stickyHost.HandleMessage(200, EncodeJson(RangedPlayerState(11)), 502.31, 5002.31);
		const auto beforeStaleEpoch = stickyHost.Stats();
		assert(stickyHost.HandleMessage(200, EncodeJson(NpcHit(7, 2, 5.0, "1234", 0, "1234", 1)), 502.32, 5002.32).empty());
		assert(stickyHost.Stats().npcHitsRejected == beforeStaleEpoch.npcHitsRejected + 1);

		stickyHost.HandleMessage(200, EncodeSnapshot(Transform(22.0), Family::Transform, 4), 502.4, 5002.4);
		stickyHost.HandleMessage(200, EncodeJson(RangedPlayerState(12)), 502.41, 5002.41);
		const auto newOwnerHit = stickyHost.HandleMessage(200, EncodeJson(NpcHit(7, 3, 5.0, "1234", 0, "1234", 2)), 502.42, 5002.42);
		const auto* routedToNewOwner = FindFrame(newOwnerHit, 100, "npcHit");
		assert(routedToNewOwner != nullptr);
		assert(DecodePacket(*routedToNewOwner).at("playerId").get<std::uint32_t>() == midId);
		assert(DecodePacket(*routedToNewOwner).at("authorityEpoch").get<std::uint32_t>() == 2);
	}

	return 0;
}

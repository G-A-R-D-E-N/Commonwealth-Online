#include "ServerProtocol.h"

#include "F4TGnsProtocolAdapter.h"
#include "F4TGnsSnapshotEnvelope.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// TODO-verify: concept-level wire checks, engine facts unverified; source:docs/code-notes/co-core.md.

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolHost;
	using F4T::GnsProtocolAdapter::Delivery;
	using F4T::GnsProtocolAdapter::OutboundMessage;
	using F4T::GnsProtocolAdapter::Session;
	using F4T::GnsSnapshotEnvelope::DecodeResult;
	using F4T::GnsSnapshotEnvelope::Family;

	struct Peer
	{
		std::uint32_t connectionId{};
		std::uint32_t playerId{};
		std::uint32_t transformSequence{};
		std::uint32_t npcSequence{};
		Session session;
		std::vector<Json> inbound;
	};

	struct Wire
	{
		ProtocolHost host;
		Peer a;
		Peer b;
		double monotonic{ 1.0 };
		double unixTime{ 1000.0 };

		explicit Wire(std::string a_name) :
			host(std::move(a_name), "in-process adapter contract"),
			a(100),
			b(200)
		{}
	};

	std::vector<std::uint8_t> EncodeSnapshot(const Json& a_packet, Family a_family, std::uint32_t a_sequence)
	{
		const auto json = a_packet.dump();
		std::vector<std::uint8_t> output(F4T::GnsSnapshotEnvelope::HeaderSize + json.size());
		assert(F4T::GnsSnapshotEnvelope::WriteHeader(std::span<std::uint8_t>{ output }, a_family, a_sequence));
		std::copy(json.begin(), json.end(), output.begin() + static_cast<std::ptrdiff_t>(F4T::GnsSnapshotEnvelope::HeaderSize));
		return output;
	}

	std::string FrameText(const OutboundFrame& a_frame)
	{
		F4T::GnsSnapshotEnvelope::Header header{};
		const auto decode = F4T::GnsSnapshotEnvelope::Decode(a_frame.payload, header);
		std::span<const std::uint8_t> payload{ a_frame.payload };
		if (decode == DecodeResult::Valid) {
			payload = payload.subspan(F4T::GnsSnapshotEnvelope::HeaderSize);
		} else {
			assert(decode == DecodeResult::NotEnvelope);
		}
		return std::string{ reinterpret_cast<const char*>(payload.data()), payload.size() };
	}

	Json ParseFrame(const OutboundFrame& a_frame)
	{
		return Json::parse(FrameText(a_frame));
	}

	bool IsHandshake(std::string_view a_type)
	{
		return a_type == "welcome" || a_type == "sessionReady" || a_type == "sessionEnded" || a_type == "sessionState";
	}

	void Advance(Wire& a_wire)
	{
		a_wire.monotonic += 0.01;
		a_wire.unixTime += 0.01;
	}

	Peer& PeerFor(Wire& a_wire, std::uint32_t a_connectionId)
	{
		if (a_connectionId == a_wire.a.connectionId) {
			return a_wire.a;
		}
		assert(a_connectionId == a_wire.b.connectionId);
		return a_wire.b;
	}

	std::vector<std::uint8_t> ToServerPayload(Peer& a_peer, const OutboundMessage& a_message)
	{
		if (a_message.delivery != Delivery::UnreliableSequenced) {
			return { a_message.payload.begin(), a_message.payload.end() };
		}
		const auto packet = Json::parse(a_message.payload);
		const auto type = packet.value("type", std::string{});
		const auto family = type == "npcState" ? Family::NpcState : Family::Transform;
		auto& sequence = type == "npcState" ? a_peer.npcSequence : a_peer.transformSequence;
		sequence = F4T::GnsSnapshotEnvelope::NextSequence(sequence);
		return EncodeSnapshot(packet, family, sequence);
	}

	void Route(Wire& a_wire, const std::vector<OutboundFrame>& a_frames);

	void Deliver(Wire& a_wire, Peer& a_peer, const std::string& a_text)
	{
		const auto result = a_peer.session.AdaptInbound(a_text);
		if (result.protocolViolation || result.localHandshakeFailure) {
			std::cerr << result.error << '\n';
			assert(false);
		}
		for (const auto& legacy : result.legacyMessages) {
			a_peer.inbound.push_back(Json::parse(legacy));
		}
		for (const auto& control : result.controlMessages) {
			const auto frames = a_wire.host.HandleMessage(
				a_peer.connectionId,
				ToServerPayload(a_peer, control),
				a_wire.monotonic,
				a_wire.unixTime);
			Advance(a_wire);
			Route(a_wire, frames);
		}
	}

	void Route(Wire& a_wire, const std::vector<OutboundFrame>& a_frames)
	{
		std::vector<OutboundFrame> deferred;
		for (const auto& frame : a_frames) {
			auto& peer = PeerFor(a_wire, frame.connectionId);
			const auto type = ParseFrame(frame).value("type", std::string{});
			if (!peer.session.IsReady() && !IsHandshake(type)) {
				deferred.push_back(frame);
				continue;
			}
			Deliver(a_wire, peer, FrameText(frame));
		}
		for (const auto& frame : deferred) {
			Deliver(a_wire, PeerFor(a_wire, frame.connectionId), FrameText(frame));
		}
	}

	std::vector<OutboundFrame> Push(Wire& a_wire, Peer& a_peer, const OutboundMessage& a_message)
	{
		const auto frames = a_wire.host.HandleMessage(
			a_peer.connectionId,
			ToServerPayload(a_peer, a_message),
			a_wire.monotonic,
			a_wire.unixTime);
		Advance(a_wire);
		Route(a_wire, frames);
		return frames;
	}

	void Handshake(Wire& a_wire, Peer& a_peer)
	{
		const auto welcome = a_wire.host.HandleConnected(a_peer.connectionId, a_wire.unixTime);
		Advance(a_wire);
		assert(welcome.size() == 1);
		a_peer.playerId = ParseFrame(welcome[0]).at("playerId").get<std::uint32_t>();
		assert(a_peer.playerId != 0);
		Route(a_wire, welcome);
		assert(a_peer.session.IsReady());
	}

	Json Spawn(double a_x, const Json& a_extra = Json::object())
	{
		Json packet{
			{ "type", "transform" },
			{ "cellId", "CAFE" },
			{ "worldspaceId", "F00D" },
			{ "x", a_x },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "angleZ", 25.0 },
			{ "movementType", "spawn" }
		};
		for (const auto& [key, value] : a_extra.items()) {
			packet[key] = value;
		}
		return packet;
	}

	Json Armed(std::string_view a_formId)
	{
		return Json::array({ Json{ { "slot", "rightHand" }, { "formId", std::string{ a_formId } } } });
	}

	Json Action(std::uint32_t a_sequence, std::uint32_t a_type, const char* a_eventName)
	{
		return Json::array({ Json{
			{ "sequence", a_sequence },
			{ "type", a_type },
			{ "eventName", a_eventName }
		} });
	}

	Json Move(double a_x, std::uint32_t a_actionSequence, std::uint32_t a_actionType, const char* a_eventName, std::string_view a_formId)
	{
		Json packet{
			{ "type", "transform" },
			{ "cellId", "CAFE" },
			{ "worldspaceId", "F00D" },
			{ "x", a_x },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "angleZ", 25.0 },
			{ "movementType", "normal" },
			{ "characterName", "Adapter Pilot" },
			{ "equippedItems", Armed(a_formId) }
		};
		if (a_actionSequence != 0) {
			packet["actionEvents"] = Action(a_actionSequence, a_actionType, a_eventName);
		}
		return packet;
	}

	const OutboundFrame* FindFrame(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_type)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && ParseFrame(frame).value("type", std::string{}) == a_type) {
				return &frame;
			}
		}
		return nullptr;
	}

	int CountType(const Peer& a_peer, std::string_view a_type)
	{
		int count = 0;
		for (const auto& packet : a_peer.inbound) {
			if (packet.value("type", std::string{}) == a_type) {
				++count;
			}
		}
		return count;
	}

	std::vector<std::uint8_t> EncodeRaw(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	void AssertFactionOk(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_op)
	{
		const auto* result = FindFrame(a_frames, a_connectionId, "factionResult");
		assert(result != nullptr);
		assert(ParseFrame(*result).at("op") == std::string{ a_op });
		assert(ParseFrame(*result).at("ok") == true);
	}

	void FormFaction(Wire& a_wire)
	{
		const auto created = a_wire.host.HandleMessage(
			a_wire.a.connectionId, EncodeRaw(Json{ { "type", "factionCreate" }, { "name", "Adapter Crew" } }), a_wire.monotonic, a_wire.unixTime);
		Advance(a_wire);
		AssertFactionOk(created, a_wire.a.connectionId, "create");
		const auto invited = a_wire.host.HandleMessage(
			a_wire.a.connectionId, EncodeRaw(Json{ { "type", "factionInvite" }, { "targetPlayerId", a_wire.b.playerId } }), a_wire.monotonic, a_wire.unixTime);
		Advance(a_wire);
		AssertFactionOk(invited, a_wire.a.connectionId, "invite");
		const auto* received = FindFrame(invited, a_wire.b.connectionId, "factionInviteReceived");
		assert(received != nullptr);
		const auto inviteId = ParseFrame(*received).at("inviteId").get<std::string>();
		const auto responded = a_wire.host.HandleMessage(
			a_wire.b.connectionId,
			EncodeRaw(Json{ { "type", "factionInviteResponse" }, { "inviteId", inviteId }, { "accept", true } }),
			a_wire.monotonic,
			a_wire.unixTime);
		Advance(a_wire);
		AssertFactionOk(responded, a_wire.b.connectionId, "inviteResponse");
	}
}

int main()
{
	Wire wire{ "Adapter Wire Server" };
	// Constraint: adapter peers must bind profiles and share a faction so names relay.
	assert(wire.host.EnableSocial(nullptr));
	assert(wire.host.EnableFactions(nullptr));
	wire.host.SetFriendlyFire(true, true);
	Handshake(wire, wire.a);
	Handshake(wire, wire.b);
	assert(wire.a.playerId != wire.b.playerId);
	wire.host.BindSessionProfile(wire.a.connectionId, "adapter-a");
	wire.host.BindSessionProfile(wire.b.connectionId, "adapter-b");
	FormFaction(wire);

	const auto spawned = wire.a.session.AdaptOutbound(Spawn(0.0, Json{
		{ "characterName", "Adapter Pilot" },
		{ "equippedItems", Armed("1234") },
		{ "actionEvents", Action(1, 1, "meleeattackStart") }
	}).dump());
	assert(spawned.size() == 2);
	assert(spawned[0].delivery == Delivery::UnreliableSequenced);
	assert(spawned[1].delivery == Delivery::ReliableOrdered);
	const auto wireTransform = Json::parse(spawned[0].payload);
	assert(!wireTransform.contains("characterName"));
	assert(!wireTransform.contains("equippedItems"));
	assert(!wireTransform.contains("actionEvents"));
	const auto playerState = Json::parse(spawned[1].payload);
	assert(playerState.at("type") == "playerState");
	assert(playerState.at("characterName") == "Adapter Pilot");
	assert(playerState.at("actionEvents").at(0).at("sequence") == 1);
	assert(playerState.at("actionEvents").at(0).at("type") == 1);

	Push(wire, wire.a, spawned[0]);
	Push(wire, wire.a, spawned[1]);
	const auto authorityA = wire.a.session.GetNpcAuthority("0000CAFE", "0000F00D");
	assert(authorityA.has_value());
	assert(authorityA->playerId == wire.a.playerId);
	assert(authorityA->epoch == 1);

	const auto spawnedB = wire.b.session.AdaptOutbound(Spawn(100.0).dump());
	assert(!spawnedB.empty());
	for (const auto& message : spawnedB) {
		Push(wire, wire.b, message);
	}
	const auto authorityB = wire.b.session.GetNpcAuthority("0000CAFE", "0000F00D");
	assert(authorityB.has_value());
	assert(authorityB->playerId == wire.a.playerId);
	assert(authorityB->epoch == 1);

	bool mergedPilot = false;
	for (const auto& packet : wire.b.inbound) {
		if (packet.value("type", std::string{}) == "transform" && packet.value("characterName", std::string{}) == "Adapter Pilot") {
			mergedPilot = true;
			assert(packet.at("equippedItems").at(0).at("slot") == "rightHand");
			assert(packet.at("playerId") == wire.a.playerId);
		}
	}
	assert(mergedPilot);

	const auto beforeCorrection = wire.host.Stats();
	const auto impossible = wire.a.session.AdaptOutbound(Json{
		{ "type", "transform" },
		{ "cellId", "CAFE" },
		{ "worldspaceId", "F00D" },
		{ "x", 10000.0 },
		{ "y", 0.0 },
		{ "z", 0.0 },
		{ "angleZ", 25.0 },
		{ "movementType", "normal" },
		{ "characterName", "Adapter Pilot" },
		{ "equippedItems", Armed("1234") }
	}.dump());
	assert(impossible.size() == 1);
	const auto correctionFrames = Push(wire, wire.a, impossible[0]);
	const auto rejectedSequence = wire.a.transformSequence;
	assert(rejectedSequence == 2);
	const auto* correctionFrame = FindFrame(correctionFrames, wire.a.connectionId, "positionCorrection");
	assert(correctionFrame != nullptr);
	const auto correction = ParseFrame(*correctionFrame);
	assert(correction.at("x") == 0.0);
	assert(correction.at("reason").get<std::string>().find("exceeded server envelope") != std::string::npos);
	assert(wire.host.Stats().movementRejected == beforeCorrection.movementRejected + 1);
	assert(wire.host.Stats().movementCorrectionsSent == beforeCorrection.movementCorrectionsSent + 1);
	bool sawCorrection = false;
	for (const auto& packet : wire.a.inbound) {
		if (packet.value("type", std::string{}) == "positionCorrection") {
			sawCorrection = true;
			assert(packet.at("x") == 0.0);
		}
	}
	assert(sawCorrection);

	const auto corrected = wire.a.session.AdaptOutbound(Move(1.0, 0, 0, "fireSingle", "1234").dump());
	assert(corrected.size() == 1);
	const auto acceptedMove = Push(wire, wire.a, corrected[0]);
	assert(FindFrame(acceptedMove, wire.b.connectionId, "transform") != nullptr);

	const Json npcState{
		{ "type", "npcState" },
		{ "npcs", Json::array({ Json{
			{ "sourceFormId", "1234" },
			{ "cellId", "0000CAFE" },
			{ "worldspaceId", "0000F00D" },
			{ "x", 15.0 },
			{ "y", 25.0 },
			{ "z", 35.0 },
			{ "angleZ", 45.0 }
		} }) }
	};
	const auto npcOutbound = wire.a.session.AdaptOutbound(npcState.dump());
	assert(npcOutbound.size() == 1);
	assert(npcOutbound[0].delivery == Delivery::UnreliableSequenced);
	const auto beforeNpc = wire.host.Stats();
	const auto npcFrames = Push(wire, wire.a, npcOutbound[0]);
	assert(wire.host.Stats().npcAuthorityRejected == beforeNpc.npcAuthorityRejected);
	assert(FindFrame(npcFrames, wire.b.connectionId, "npcState") != nullptr);
	assert(CountType(wire.b, "npcState") >= 1);

	const auto hitsBefore = wire.host.Stats().combatHitsRouted;
	const auto melee = wire.a.session.AdaptOutbound(Json{
		{ "type", "combatHit" },
		{ "targetPlayerId", wire.b.playerId },
		{ "sequence", 1 },
		{ "attackSequence", 1 },
		{ "damage", 25.0 },
		{ "weaponFormId", "1234" }
	}.dump());
	assert(melee.size() == 1);
	const auto meleeFrames = Push(wire, wire.a, melee[0]);
	const auto* meleeHit = FindFrame(meleeFrames, wire.b.connectionId, "combatHit");
	assert(meleeHit != nullptr);
	assert(!ParseFrame(*meleeHit).contains("attackSequence"));
	assert(ParseFrame(*meleeHit).at("weaponFormId") == "00001234");

	const auto rangedState = wire.a.session.AdaptOutbound(Move(1.0, 2, 3, "fireSingle", "1234").dump());
	assert(rangedState.size() == 2);
	for (const auto& message : rangedState) {
		Push(wire, wire.a, message);
	}
	const auto ranged = wire.a.session.AdaptOutbound(Json{
		{ "type", "combatHit" },
		{ "targetPlayerId", wire.b.playerId },
		{ "sequence", 2 },
		{ "attackSequence", 2 },
		{ "damage", 25.0 },
		{ "weaponFormId", "1234" }
	}.dump());
	const auto rangedFrames = Push(wire, wire.a, ranged[0]);
	const auto* rangedHit = FindFrame(rangedFrames, wire.b.connectionId, "combatHit");
	assert(rangedHit != nullptr);
	assert(ParseFrame(*rangedHit).at("weaponFormId") == "00001234");

	const auto unarmedState = wire.a.session.AdaptOutbound(Move(1.0, 3, 2, "meleeattackStart", "").dump());
	assert(unarmedState.size() == 2);
	for (const auto& message : unarmedState) {
		Push(wire, wire.a, message);
	}
	const auto unarmed = wire.a.session.AdaptOutbound(Json{
		{ "type", "combatHit" },
		{ "targetPlayerId", wire.b.playerId },
		{ "sequence", 3 },
		{ "attackSequence", 3 },
		{ "damage", 5.0 }
	}.dump());
	const auto unarmedFrames = Push(wire, wire.a, unarmed[0]);
	assert(FindFrame(unarmedFrames, wire.b.connectionId, "combatHit") != nullptr);
	assert(wire.host.Stats().combatHitsRouted == hitsBefore + 3);

	std::cout
		<< "adapter server contract: correction x=" << correction.at("x").get<double>()
		<< " sequence=" << rejectedSequence
		<< " playerState=" << playerState.at("characterName").get<std::string>()
		<< " melee=1 ranged=1 unarmed=1\n";
	return 0;
}
